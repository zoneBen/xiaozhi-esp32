#include "ft6336_touch.h"

#include "config.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Ft6336Touch"

namespace {

// FT6336 寄存器
constexpr uint8_t kRegTouchPoints = 0x02;  // 低 4 位 = 触摸点数，高 4 位 = 事件标志
constexpr uint8_t kRegChipId = 0xA3;
constexpr size_t kTouchPointBytes = 6;  // 点数(1) + X(2) + Y(2) + 保留(1)
constexpr int kI2cTimeoutMs = 100;

// 面板原生分辨率：竖屏 240x320
constexpr int kPanelWidth = 240;
constexpr int kPanelHeight = 320;

// --------------------------------------------------------------- 坐标校准入口
// 本板屏幕按 320x240 横屏使用（config.h: DISPLAY_SWAP_XY=true / DISPLAY_MIRROR_Y=true），
// 而 FT6336 上报的是面板原生 240x320 坐标。LVGL 不会替我们把输入坐标旋转过去
// （lv_indev 不做显示旋转变换），所以必须在这里自己做一次 swap / mirror。
//
// 位置对不上时只改下面两行。四个候选（raw_x ∈ [0,239]、raw_y ∈ [0,319]）：
//   A: x = raw_y;                      y = raw_x;
//   B: x = raw_y;                      y = DISPLAY_HEIGHT - 1 - raw_x;   // 与 C 相差 180°
//   C: x = DISPLAY_WIDTH  - 1 - raw_y; y = raw_x;                       // <- 当前默认
//   D: x = DISPLAY_WIDTH  - 1 - raw_y; y = DISPLAY_HEIGHT - 1 - raw_x;  // 与 A 相差 180°
//
// 相邻两个候选之间只差一个轴的镜像；相差 180° 的两对是 A↔D 与 B↔C。
//
// 校准方法（详见 README.md）：手指依次点屏幕左上角、右下角，把串口日志里的
// "raw=(x,y) -> (x,y)" 抄下来：
//   - 换算后的坐标与手指位置中心对称（摸右下打中左上）-> 换成相差 180° 的那个候选
//   - 只有左右反（摸左边报右边）-> 换掉 X 方向的镜像
//   - 只有上下反 -> 换掉 Y 方向的镜像
//   - 左右上下互换的量级错乱 -> swap 方向反了，得重新推导
// 本板实测为「摸右下打中左上」，即 B 处于 180° 反向，故采用 C。
void MapRawToScreen(int raw_x, int raw_y, int* x, int* y) {
    *x = DISPLAY_WIDTH - 1 - raw_y;
    *y = raw_x;
}

// 触摸点偶尔会落在映射后的边界外，夹到屏幕内，避免点不到任何对象
int Clamp(int value, int max_inclusive) {
    if (value < 0) {
        return 0;
    }
    return value > max_inclusive ? max_inclusive : value;
}

}  // namespace

Ft6336Touch::Ft6336Touch(i2c_master_bus_handle_t i2c_bus, uint8_t addr, gpio_num_t rst_pin)
    : I2cDevice(i2c_bus, addr), rst_pin_(rst_pin) {}

bool Ft6336Touch::Init() {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << rst_pin_);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // 复位时序：拉低 10ms -> 拉高后等 100ms 让芯片就绪
    gpio_set_level(rst_pin_, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rst_pin_, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 注意：这里不能用基类的 ReadReg()/ReadRegs()，它们内部是 ESP_ERROR_CHECK，
    // 触摸屏缺失（NACK）或总线争用超时时会直接 abort 整个固件。
    // 直接用 esp_err_t 返回值判断，读失败只表示「这一帧没有触摸」
    uint8_t reg = kRegChipId;
    uint8_t chip_id = 0;
    esp_err_t err = i2c_master_transmit_receive(i2c_device_, &reg, 1, &chip_id, 1, kI2cTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "触摸芯片无响应: %s (addr=0x%02X, SDA=%d, SCL=%d)", esp_err_to_name(err),
                 device_address_, TOUCH_I2C_SDA_PIN, TOUCH_I2C_SCL_PIN);
        return false;
    }

    ESP_LOGI(TAG, "FT6336 芯片 ID: 0x%02X", chip_id);
    initialized_ = true;
    return true;
}

void Ft6336Touch::RegisterLvglIndev() {
    if (!initialized_) {
        ESP_LOGW(TAG, "触摸未初始化成功，跳过 LVGL 输入设备注册");
        return;
    }

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, ReadCallback);
    lv_indev_set_user_data(indev, this);
    // 只接了轮询，未用 INT 引脚，因此走 LVGL 默认的 LV_INDEV_MODE_TIMER：
    // 由 lv_timer_handler() 按刷新周期调用读回调
    lv_indev_set_display(indev, lv_display_get_default());
    ESP_LOGI(TAG, "触摸已注册为 LVGL 指针输入设备");
}

bool Ft6336Touch::Poll() {
    if (!initialized_) {
        return false;
    }

    uint8_t buffer[kTouchPointBytes] = {};
    uint8_t reg = kRegTouchPoints;
    esp_err_t err =
        i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, kTouchPointBytes, kI2cTimeoutMs);
    if (err != ESP_OK) {
        // 偶发读失败按抬起处理，不要在这里 abort 整个设备
        return false;
    }

    // 与参考实现逐字节一致：高 4 位是事件标志，必须屏蔽
    if ((buffer[0] & 0x0F) == 0) {
        return false;
    }

    int raw_x = ((buffer[1] & 0x0F) << 8) | buffer[2];
    int raw_y = ((buffer[3] & 0x0F) << 8) | buffer[4];

    int x = 0;
    int y = 0;
    MapRawToScreen(raw_x, raw_y, &x, &y);

    if (!logged_press_) {
        // 每次「抬起 -> 按下」只打一条，用于校准坐标映射
        ESP_LOGI(TAG, "raw=(%d,%d) -> (%d,%d)  [面板 %dx%d]", raw_x, raw_y, x, y, kPanelWidth,
                 kPanelHeight);
    }

    screen_x_ = Clamp(x, DISPLAY_WIDTH - 1);
    screen_y_ = Clamp(y, DISPLAY_HEIGHT - 1);
    return true;
}

void Ft6336Touch::ReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* self = static_cast<Ft6336Touch*>(lv_indev_get_user_data(indev));
    if (self == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // 本回调运行在 LVGL 任务里，且调用时 LVGL 锁已被 esp_lvgl_port 持有：
    // 这里不能再加锁，也不能做阻塞很久的操作
    self->pressed_ = self->Poll();
    self->logged_press_ = self->pressed_;

    // 抬起时也要上报最后的位置，否则手指在边缘抬起时会点到别的对象
    data->point.x = self->screen_x_;
    data->point.y = self->screen_y_;
    data->state = self->pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
