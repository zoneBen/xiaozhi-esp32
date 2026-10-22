#ifndef QUANDONG_S3_DEV_4G_CAT_SETTINGS_UI_H_
#define QUANDONG_S3_DEV_4G_CAT_SETTINGS_UI_H_

#include "cat_ui_theme.h"

#include <functional>
#include <string>
#include <vector>

// 齿轮按钮打开的全屏设置页。这里只放 UI 和 Token 的本地读写；
// 清配网、重启这类板级动作由 Callbacks 交回板子执行。
class CatSettingsUi {
public:
    struct Callbacks {
        std::function<void()> on_close;               // 返回猫脸
        std::function<void()> on_clear_provisioning;  // 清 SSID + 切回 WiFi + 重启
        std::function<void()> on_token_changed;       // 重置余额基准并尝试后台取一次
        // 复用主界面已有的模态确认框（message, on_confirm）
        std::function<void(const char*, std::function<void()>)> show_confirm;
    };

    CatSettingsUi(Display* display, Callbacks callbacks);
    ~CatSettingsUi();

    CatSettingsUi(const CatSettingsUi&) = delete;
    CatSettingsUi& operator=(const CatSettingsUi&) = delete;

    void Show();
    void Hide();
    void ShowTokenPage();
    bool IsVisible() const;
    void RefreshTheme();

private:
    struct Row {
        lv_obj_t* row = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* icon_bg = nullptr;
        lv_obj_t* title = nullptr;
        lv_obj_t* value = nullptr;
        lv_obj_t* chevron = nullptr;
    };

    void EnsureBuilt();
    void BuildSettingsPanel();
    void BuildTokenPanel();
    void BuildToast();

    Row CreateRow(lv_obj_t* parent, int y, const char* icon, const char* title, bool danger);
    lv_obj_t* CreateHeaderButton(lv_obj_t* parent, const char* icon, bool accent, lv_align_t align,
                                 int x, int y);

    void ShowSettingsPanel();
    void UpdateTokenStatus();
    void ShowToast(const char* text);
    void SaveTokenFromEditor();
    void ToggleTokenVisibility();
    void ApplyPalette();

    static void OnBackToFace(lv_event_t* event);
    static void OnOpenTokenPage(lv_event_t* event);
    static void OnClearProvisioning(lv_event_t* event);
    static void OnClearToken(lv_event_t* event);
    static void OnTokenBack(lv_event_t* event);
    static void OnTokenSave(lv_event_t* event);
    static void OnTokenEye(lv_event_t* event);
    static void OnKeyboardReady(lv_event_t* event);
    static void OnKeyboardCancel(lv_event_t* event);
    static void OnToastTimer(lv_timer_t* timer);

    Display* display_ = nullptr;
    Callbacks callbacks_;
    CatUiPalette palette_;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* settings_panel_ = nullptr;
    lv_obj_t* token_panel_ = nullptr;
    lv_obj_t* footer_ = nullptr;

    Row token_row_;
    Row provisioning_row_;
    Row clear_token_row_;

    lv_obj_t* token_edit_ = nullptr;
    lv_obj_t* token_error_ = nullptr;
    lv_obj_t* eye_button_ = nullptr;
    lv_obj_t* eye_icon_ = nullptr;
    lv_obj_t* keyboard_ = nullptr;

    lv_obj_t* toast_ = nullptr;
    lv_obj_t* toast_label_ = nullptr;
    lv_timer_t* toast_timer_ = nullptr;

    std::vector<lv_obj_t*> cards_;
    std::vector<lv_obj_t*> primary_labels_;
    std::vector<lv_obj_t*> muted_labels_;
    std::vector<lv_obj_t*> accent_labels_;
    std::vector<lv_obj_t*> danger_labels_;
    std::vector<lv_obj_t*> accent_soft_bgs_;
    std::vector<lv_obj_t*> danger_soft_bgs_;
    std::vector<lv_obj_t*> accent_buttons_;
};

#endif  // QUANDONG_S3_DEV_4G_CAT_SETTINGS_UI_H_
