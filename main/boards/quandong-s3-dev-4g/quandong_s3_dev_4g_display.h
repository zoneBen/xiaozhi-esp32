#ifndef _QUANDONG_S3_DEV_4G_DISPLAY_H_
#define _QUANDONG_S3_DEV_4G_DISPLAY_H_

#include "cat_face_anim.h"
#include "cat_face_lvgl.h"
#include "cat_settings_ui.h"
#include "deepseek_balance_ui.h"
#include "deepseek_client.h"

#include "display/lcd_display.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// quandong-s3-dev-4g 的显示层：在 SpiLcdDisplay 基础上
//   1) 让状态栏左上角的网络图标（WiFi / 4G 信号）可点；
//   2) 提供一个通用模态确认弹窗；
//   3) 用程序化矢量猫脸取代位图表情 —— 屏幕本身就是那张脸。
//
// 这里只有「UI 能力」，不含任何网络知识 —— 点击后做什么由板子注入的回调决定。
class QuandongS3Dev4GDisplay : public SpiLcdDisplay {
public:
    QuandongS3Dev4GDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y, bool mirror_x,
                           bool mirror_y, bool swap_xy);
    ~QuandongS3Dev4GDisplay() override;

    virtual void SetupUI() override;

    // 情绪入口。先走基类（位图 emoji / 字体图标兜底），认得这个名字才覆盖成猫脸；
    // 认不得就把画布藏起来，让基类那张图正常显示。
    virtual void SetEmotion(const char* emotion) override;

    // 预览图期间要暂停猫脸，否则两块内容会叠在一起
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;

    // 网络图标被点击时的回调。SetupUI() 之前或之后设置都可以
    void SetNetworkIconTappedHandler(std::function<void()> handler);

    // 弹一个模态确认框：点 ✓ 执行 on_confirm，点 ✗ 关闭。
    // message 只允许用内置字体内已有的文案（见 README 的字体字集说明）
    void ShowConfirmDialog(const char* message, std::function<void()> on_confirm);

    // 板子注入的网络 / 重启能力。设置页和余额页只做 UI，
    // 清配网与余额请求都从这里回到板级实现。
    struct UiServices {
        std::function<bool(const std::string& token, deepseek::BalanceInfo& info,
                           std::string& error)>
            fetch_balance;
        std::function<void()> clear_provisioning;
        std::function<void()> on_token_changed;
    };
    void SetUiServices(UiServices services);

    // 供板卡 MCP 工具调用
    void ShowBalancePage();

    virtual void SetTheme(Theme* theme) override;

private:
    static void OnTapAreaClicked(lv_event_t* event);
    static void OnConfirmClicked(lv_event_t* event);
    static void OnCancelClicked(lv_event_t* event);
    static void OnSettingsButtonClicked(lv_event_t* event);

    void CreateNetworkTapArea();
    void CreateSettingsButton();
    void CloseConfirmDialog();

    void ShowSettingsPage();
    void ShowSettingsPageLocked();
    void HideSettingsPageLocked();
    void ShowBalancePageLocked();
    void HideBalancePageLocked();
    void SetOverlayActiveLocked(bool active);
    CatSettingsUi::Callbacks MakeSettingsCallbacks();
    DeepSeekBalanceUi::Callbacks MakeBalanceCallbacks();

#if LV_USE_VECTOR_GRAPHIC
    void CreateCatFace();
    void DestroyCatFace();
    void DrawCatFrame();
    static void OnCatTimer(lv_timer_t* timer);
#endif

    lv_obj_t* tap_area_ = nullptr;  // 左上角网络图标的透明触摸热区
    lv_obj_t* dialog_ = nullptr;    // 非空表示确认框已打开
    lv_obj_t* settings_button_ = nullptr;
    lv_obj_t* settings_icon_ = nullptr;
    lv_obj_t* settings_tap_area_ = nullptr;
    std::unique_ptr<CatSettingsUi> settings_ui_;
    std::unique_ptr<DeepSeekBalanceUi> balance_ui_;
    UiServices ui_services_;
    bool overlay_active_ = false;
    std::function<void()> on_network_icon_tapped_;
    std::function<void()> on_confirm_;

#if LV_USE_VECTOR_GRAPHIC
    lv_obj_t* cat_canvas_ = nullptr;
    // 画布像素由本类持有：lv_draw_buf_init() 建出来的 buf 不带 ALLOCATED 标志，
    // lv_draw_buf_destroy() 既不会释放像素也不会释放结构体，两头都得自己收
    lv_draw_buf_t* cat_draw_buf_ = nullptr;
    void* cat_pixels_ = nullptr;
    lv_timer_t* cat_timer_ = nullptr;
    cat_face::Animator cat_animator_;
    // 上一帧的时间戳，用来算 dt；0 表示「没有上一帧」，下一帧按 dt=0 处理
    int64_t cat_last_frame_us_ = 0;

    // 渲染耗时分段统计，每 STATS_FRAMES 帧汇总打一条日志，用来核对帧预算
    uint32_t cat_frame_count_ = 0;
    int64_t cat_frame_us_sum_ = 0;
    int64_t cat_frame_us_max_ = 0;
#endif
};

#endif  // _QUANDONG_S3_DEV_4G_DISPLAY_H_
