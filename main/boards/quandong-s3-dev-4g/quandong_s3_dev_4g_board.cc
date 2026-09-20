#include "dual_network_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "esp_lcd_ili9341.h"
#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "config.h"
#include "ft6336_touch.h"
#include "quandong_s3_dev_4g_display.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>

#include <memory>

#define TAG "QuandongS3Dev4GBoard"

// ILI9341 厂家自定义初始化序列
static const ili9341_lcd_init_cmd_t vendor_specific_init[] = {
    {0xCF, (uint8_t []){0x00, 0xC1, 0x30}, 3, 0},
    {0xED, (uint8_t []){0x64, 0x03, 0x12, 0x81}, 4, 0},
    {0xE8, (uint8_t []){0x85, 0x00, 0x78}, 3, 0},
    {0xCB, (uint8_t []){0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
    {0xF7, (uint8_t []){0x20}, 1, 0},
    {0xEA, (uint8_t []){0x00, 0x00}, 2, 0},
    {0xC0, (uint8_t []){0x13}, 1, 0},
    {0xC1, (uint8_t []){0x13}, 1, 0},
    {0xC5, (uint8_t []){0x22, 0x35}, 2, 0},
    {0xC7, (uint8_t []){0xBD}, 1, 0},
    {0, (uint8_t []){0}, 0x11, 0},
    {0x36, (uint8_t []){0x08}, 1, 0},
    {0xB6, (uint8_t []){0x0A, 0xA2}, 2, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0xF6, (uint8_t []){0x01, 0x30}, 2, 0},
    {0xB1, (uint8_t []){0x00, 0x1B}, 2, 0},
    {0xF2, (uint8_t []){0x00}, 1, 0},
    {0x26, (uint8_t []){0x01}, 1, 0},
    {0xE0, (uint8_t []){0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00}, 15, 0},
    {0xE1, (uint8_t []){0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F}, 15, 0},
    {0, (uint8_t []){0}, 0x11, 0},
    {0, (uint8_t []){0}, 0x29, 0},
};

class QuandongS3Dev4GBoard : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    QuandongS3Dev4GDisplay* display_ = nullptr;
    std::unique_ptr<Ft6336Touch> touch_;  // 触摸屏不存在时保持 nullptr

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    // 板上音频功放使能引脚，需置为低电平
    void InitializeAudioPaEnable() {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << AUDIO_PA_ENABLE_PIN);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level(AUDIO_PA_ENABLE_PIN, 0);
    }

    void InitializeButtons() {
        // 单击：仅 WiFi 模式下处于启动阶段时进入配网，否则切换对话状态
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            // 必须先判网络类型：4G 模式下 current_board_ 是 Ml307Board 而非 WifiBoard，
            // 直接 static_cast 再调用 EnterWifiConfigMode() 会访问不存在的成员
            if (GetNetworkType() == NetworkType::WIFI &&
                app.GetDeviceState() == kDeviceStateStarting) {
                auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                wifi_board.EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        // 双击：启动中或配网中双击，在 WiFi <-> 4G 之间切换（内部保存到 NVS 后重启）
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting ||
                app.GetDeviceState() == kDeviceStateWifiConfiguring) {
                SwitchNetworkType();
            }
        });
    }

    // 触摸屏初始化：复位 + 探测芯片。失败只打日志，不影响其它功能
    void InitializeFt6336Touch() {
        auto touch = std::make_unique<Ft6336Touch>(codec_i2c_bus_, TOUCH_I2C_ADDR, TOUCH_RST_PIN);
        if (!touch->Init()) {
            ESP_LOGW(TAG, "未找到 FT6336 触摸屏，本次启动不带触摸（BOOT 双击仍可切换网络）");
            return;
        }
        touch_ = std::move(touch);
    }

    void RegisterFt6336Indev() {
        if (touch_ == nullptr || display_ == nullptr) {
            return;
        }
        // lv_indev_create() 会改动 LVGL 内部结构，必须持锁调用
        DisplayLockGuard lock(display_);
        if (!lock) {
            ESP_LOGE(TAG, "加锁失败，触摸屏未注册");
            return;
        }
        touch_->RegisterLvglIndev();
    }

    // 触摸屏只承担「点左上角网络图标切换 WiFi / 4G」这一个交互，业务逻辑都留在板子里，
    // 显示层只提供「图标可点 + 弹确认框」这两个 UI 能力
    void InitializeNetworkSwitchByTouch() {
        display_->SetNetworkIconTappedHandler([this]() {
            auto& app = Application::GetInstance();
            // 升级过程中重启会打断固件写入，这里直接忽略点击
            if (app.GetDeviceState() == kDeviceStateUpgrading) {
                ESP_LOGW(TAG, "固件升级中，忽略网络切换请求");
                return;
            }
            const bool to_4g = (GetNetworkType() == NetworkType::WIFI);
            display_->ShowConfirmDialog(
                to_4g ? Lang::Strings::SWITCH_TO_4G_NETWORK : Lang::Strings::SWITCH_TO_WIFI_NETWORK,
                // SwitchNetworkType() 内部会提示 + 延时 1s + 重启，不能占着 LVGL 任务跑，
                // 丢回主任务执行（否则那句提示根本来不及绘制出来）
                [this]() { Application::GetInstance().Schedule([this]() { SwitchNetworkType(); }); });
        });
    }

    void InitializeIli9341Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        const ili9341_vendor_config_t vendor_config = {
            .init_cmds = &vendor_specific_init[0],
            .init_cmds_size = sizeof(vendor_specific_init) / sizeof(ili9341_lcd_init_cmd_t),
        };

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.flags.reset_active_high = 1;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void *)&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new QuandongS3Dev4GDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

public:
    // 末位参数为默认网络类型：0 = WiFi 优先，1 = 4G 优先
    QuandongS3Dev4GBoard() : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, ML307_DTR_PIN, 0),
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeAudioPaEnable();
        // 触摸必须先于显示初始化：I2C 总线已就绪，且要把触摸设备注册进 LVGL
        InitializeFt6336Touch();
        InitializeIli9341Display();
        RegisterFt6336Indev();
        InitializeNetworkSwitchByTouch();
        InitializeButtons();
        GetBacklight()->SetBrightness(100);
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            codec_i2c_bus_,
            I2C_NUM_0,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            true,
            true);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(QuandongS3Dev4GBoard);
