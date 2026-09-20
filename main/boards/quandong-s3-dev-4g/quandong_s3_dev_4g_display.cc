#include "quandong_s3_dev_4g_display.h"

#include "lvgl_theme.h"

#include <esp_log.h>
#include <material_symbols.h>

#define TAG "QuandongS3Dev4GDisplay"

namespace {

// 图标本体只有 20px 高，而它所在的 top_bar_ 自身高度也只有 ~28px。
// LVGL 的命中测试只会下探到「父对象 coords 之内」的子对象，所以对 label 调
// lv_obj_set_ext_click_area() 扩出来的热区会被父对象裁掉，按不准。
// 这里改用一块贴在屏幕左上角的透明覆盖层做热区：父对象是整个屏幕，尺寸不受 top_bar_ 限制。
constexpr int kTapAreaWidth = 72;
constexpr int kTapAreaHeight = 56;

// 弹窗尺寸（屏幕 320x240，按钮留够手指可点的面积）
constexpr int kDialogWidth = 264;
constexpr int kButtonWidth = 96;
constexpr int kButtonHeight = 44;

// 按钮配色刻意不取主题色。主题里 border_color 和 text_color 可能同时是黑的，
// 那样「取消按钮的底色」和「✗ 的字色」会撞成同一个颜色，图标直接看不见。
// 这里两个按钮统一用饱和深底 + 白色图标：任何主题下都有对比度，
// 同时绿 / 灰也符合「确认 / 取消」的语义。
constexpr uint32_t kConfirmBgColor = 0x2E7D32;  // Material Green 800
constexpr uint32_t kCancelBgColor = 0x616161;   // Material Grey 700

}  // namespace

QuandongS3Dev4GDisplay::QuandongS3Dev4GDisplay(esp_lcd_panel_io_handle_t panel_io,
                                               esp_lcd_panel_handle_t panel, int width, int height,
                                               int offset_x, int offset_y, bool mirror_x,
                                               bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

QuandongS3Dev4GDisplay::~QuandongS3Dev4GDisplay() {
    // 弹窗挂在 lv_layer_top() 上，不在基类析构负责的 label 之列，这里自己收掉
    if (dialog_ != nullptr) {
        lv_obj_delete(dialog_);
        dialog_ = nullptr;
    }
}

void QuandongS3Dev4GDisplay::SetupUI() {
    // 基类自己会加锁，并创建 top_bar_ / network_label_ 等对象；
    // 它内部有重复调用保护，所以这里不需要再判一次 IsSetupUICalled()
    SpiLcdDisplay::SetupUI();

    DisplayLockGuard lock(this);
    if (!lock) {
        ESP_LOGE(TAG, "加锁失败，网络图标热区未创建");
        return;
    }
    CreateNetworkTapArea();
}

void QuandongS3Dev4GDisplay::CreateNetworkTapArea() {
    if (tap_area_ != nullptr) {
        return;
    }

    // 全透明覆盖层，只承担触摸热区，不改变任何视觉
    tap_area_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(tap_area_, kTapAreaWidth, kTapAreaHeight);
    lv_obj_align(tap_area_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(tap_area_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap_area_, 0, 0);
    lv_obj_set_style_radius(tap_area_, 0, 0);
    lv_obj_set_style_pad_all(tap_area_, 0, 0);
    lv_obj_set_style_shadow_width(tap_area_, 0, 0);
    lv_obj_set_scrollbar_mode(tap_area_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(tap_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tap_area_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tap_area_, OnTapAreaClicked, LV_EVENT_CLICKED, this);

    // 必须排在 top_bar_ / status_bar_ 之后，否则会被它们盖住收不到点击
    lv_obj_move_foreground(tap_area_);
    ESP_LOGI(TAG, "网络图标热区已创建 (%dx%d)", kTapAreaWidth, kTapAreaHeight);
}

void QuandongS3Dev4GDisplay::SetNetworkIconTappedHandler(std::function<void()> handler) {
    on_network_icon_tapped_ = std::move(handler);
}

void QuandongS3Dev4GDisplay::ShowConfirmDialog(const char* message,
                                               std::function<void()> on_confirm) {
    DisplayLockGuard lock(this);
    if (!lock) {
        ESP_LOGE(TAG, "加锁失败，确认弹窗未创建");
        return;
    }

    // 幂等：重复点击图标时先关掉旧弹窗，避免叠出多个
    CloseConfirmDialog();

    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    if (lvgl_theme == nullptr || lvgl_theme->text_font() == nullptr ||
        lvgl_theme->icon_font() == nullptr) {
        ESP_LOGE(TAG, "主题未就绪，确认弹窗未创建");
        return;
    }
    const lv_font_t* text_font = lvgl_theme->text_font()->font();
    const lv_font_t* icon_font = lvgl_theme->icon_font()->font();
    const lv_color_t text_color = lvgl_theme->text_color();

    // 注意：lv_layer_top() 不是 screen 的子对象，继承不到 SetupUI() 设在 screen 上的
    // 中文字体（默认字体 montserrat 没有中文字形），所以下面每个对象都显式指定字体
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_40, 0);
    lv_obj_set_style_text_font(overlay, text_font, 0);
    lv_obj_set_style_text_color(overlay, text_color, 0);
    lv_obj_set_scrollbar_mode(overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    // overlay 自带 CLICKABLE，点遮罩不会穿透到下面的图标（真正的模态）。
    // 遮罩上不挂事件：点空白处不做任何事，比误触直接关掉更稳妥

    lv_obj_t* card = lv_obj_create(overlay);
    lv_obj_set_width(card, kDialogWidth);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(card, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_color(card, lvgl_theme->border_color(), 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* text = lv_label_create(card);
    lv_label_set_text(text, message);
    lv_obj_set_style_text_font(text, text_font, 0);
    lv_obj_set_style_text_color(text, text_color, 0);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(text, LV_PCT(100));
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);

    lv_obj_t* buttons = lv_obj_create(card);
    lv_obj_set_size(buttons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(buttons, 0, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_column(buttons, 16, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    // 取消：灰底 + 白 ✗
    lv_obj_t* cancel = lv_button_create(buttons);
    lv_obj_set_size(cancel, kButtonWidth, kButtonHeight);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(kCancelBgColor), 0);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(cancel, OnCancelClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* cancel_icon = lv_label_create(cancel);
    lv_label_set_text(cancel_icon, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_font(cancel_icon, icon_font, 0);
    lv_obj_set_style_text_color(cancel_icon, lv_color_white(), 0);
    lv_obj_center(cancel_icon);

    // 确认：绿底 + 白 ✓
    lv_obj_t* confirm = lv_button_create(buttons);
    lv_obj_set_size(confirm, kButtonWidth, kButtonHeight);
    lv_obj_set_style_bg_color(confirm, lv_color_hex(kConfirmBgColor), 0);
    lv_obj_set_style_bg_opa(confirm, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(confirm, OnConfirmClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* confirm_icon = lv_label_create(confirm);
    lv_label_set_text(confirm_icon, MATERIAL_SYMBOLS_CHECK);
    lv_obj_set_style_text_font(confirm_icon, icon_font, 0);
    lv_obj_set_style_text_color(confirm_icon, lv_color_white(), 0);
    lv_obj_center(confirm_icon);

    dialog_ = overlay;
    on_confirm_ = std::move(on_confirm);
    ESP_LOGI(TAG, "确认弹窗已打开: %s", message);
}

void QuandongS3Dev4GDisplay::CloseConfirmDialog() {
    if (dialog_ == nullptr) {
        return;
    }
    // 本函数可能正处在 dialog_ 子孙对象的事件分发过程中，直接删对象不安全，
    // 用 async 版把删除推迟到下一次 lv_timer_handler
    lv_obj_delete_async(dialog_);
    dialog_ = nullptr;
    on_confirm_ = nullptr;
}

void QuandongS3Dev4GDisplay::OnTapAreaClicked(lv_event_t* event) {
    auto* self = static_cast<QuandongS3Dev4GDisplay*>(lv_event_get_user_data(event));
    if (self != nullptr && self->on_network_icon_tapped_) {
        self->on_network_icon_tapped_();
    }
}

void QuandongS3Dev4GDisplay::OnCancelClicked(lv_event_t* event) {
    auto* self = static_cast<QuandongS3Dev4GDisplay*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->CloseConfirmDialog();
    }
}

void QuandongS3Dev4GDisplay::OnConfirmClicked(lv_event_t* event) {
    auto* self = static_cast<QuandongS3Dev4GDisplay*>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }
    // 先取出回调再关弹窗（CloseConfirmDialog 会清空 on_confirm_）
    auto on_confirm = self->on_confirm_;
    self->CloseConfirmDialog();
    if (on_confirm) {
        on_confirm();
    }
}
