#ifndef _QUANDONG_S3_DEV_4G_DISPLAY_H_
#define _QUANDONG_S3_DEV_4G_DISPLAY_H_

#include "display/lcd_display.h"

#include <functional>

// quandong-s3-dev-4g 的显示层：在 SpiLcdDisplay 基础上
//   1) 让状态栏左上角的网络图标（WiFi / 4G 信号）可点；
//   2) 提供一个通用模态确认弹窗。
//
// 这里只有「UI 能力」，不含任何网络知识 —— 点击后做什么由板子注入的回调决定。
class QuandongS3Dev4GDisplay : public SpiLcdDisplay {
public:
    QuandongS3Dev4GDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y, bool mirror_x,
                           bool mirror_y, bool swap_xy);
    ~QuandongS3Dev4GDisplay() override;

    virtual void SetupUI() override;

    // 网络图标被点击时的回调。SetupUI() 之前或之后设置都可以
    void SetNetworkIconTappedHandler(std::function<void()> handler);

    // 弹一个模态确认框：点 ✓ 执行 on_confirm，点 ✗ 关闭。
    // message 只允许用内置字体内已有的文案（见 README 的字体字集说明）
    void ShowConfirmDialog(const char* message, std::function<void()> on_confirm);

private:
    static void OnTapAreaClicked(lv_event_t* event);
    static void OnConfirmClicked(lv_event_t* event);
    static void OnCancelClicked(lv_event_t* event);

    void CreateNetworkTapArea();
    void CloseConfirmDialog();

    lv_obj_t* tap_area_ = nullptr;  // 左上角网络图标的透明触摸热区
    lv_obj_t* dialog_ = nullptr;    // 非空表示确认框已打开
    std::function<void()> on_network_icon_tapped_;
    std::function<void()> on_confirm_;
};

#endif  // _QUANDONG_S3_DEV_4G_DISPLAY_H_
