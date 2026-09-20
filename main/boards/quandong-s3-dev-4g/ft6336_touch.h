#ifndef _FT6336_TOUCH_H_
#define _FT6336_TOUCH_H_

#include "i2c_device.h"

#include <driver/gpio.h>
#include <lvgl.h>

// FT6336G 电容触摸屏驱动，并把触摸注册成 LVGL 的指针输入设备。
// 触摸与 ES8311 共用 I2C0（i2c_master 驱动自身串行化同总线访问，无需额外互斥）。
//
// 坐标映射的校准点只有一个：ft6336_touch.cc 里的 MapRawToScreen()。
class Ft6336Touch : public I2cDevice {
public:
    Ft6336Touch(i2c_master_bus_handle_t i2c_bus, uint8_t addr, gpio_num_t rst_pin);

    // 复位并探测触摸芯片。返回 false 表示触摸屏不存在，调用方应跳过 RegisterLvglIndev()
    bool Init();

    // 注册为 LVGL 指针输入设备。需在显示对象就绪（lvgl_port_add_disp 已执行）之后调用，
    // 且调用时必须持有 LVGL 锁。Init() 未成功时本函数直接返回
    void RegisterLvglIndev();

private:
    // LVGL 读回调：由 LVGL 任务在已持锁状态下调用，内部不得再加锁或长时间阻塞
    static void ReadCallback(lv_indev_t* indev, lv_indev_data_t* data);

    // 读一帧触摸数据并刷新缓存的屏幕坐标，返回本帧是否处于按下状态
    bool Poll();

    gpio_num_t rst_pin_;
    bool initialized_ = false;
    bool pressed_ = false;
    int screen_x_ = 0;
    int screen_y_ = 0;
    bool logged_press_ = false;  // 只在「按下」那一刻打一条坐标日志，避免刷屏
};

#endif  // _FT6336_TOUCH_H_
