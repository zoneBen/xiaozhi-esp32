#include "cat_settings_ui.h"

#include "deepseek_client.h"

#include <material_symbols.h>

#include <utility>

LV_FONT_DECLARE(font_noto_sans_basic_14_1);

namespace {

constexpr int kMargin = 12;
constexpr int kRowHeight = 48;
constexpr int kRowGap = 8;
constexpr int kTokenPageKeyboardHeight = 138;

// 只保留 Token 实际会用到的字符：小写十六进制 + "sk-" 前缀。
// 省掉模式切换键，输入时不用在字母/数字两套键盘之间来回切。
const char* const kTokenKeyMap[] = {"1",          "2",
                                    "3",          "4",
                                    "5",          "6",
                                    "7",          "8",
                                    "9",          "0",
                                    "\n",         "q",
                                    "w",          "e",
                                    "r",          "t",
                                    "y",          "u",
                                    "i",          "o",
                                    "p",          "\n",
                                    "a",          "s",
                                    "d",          "f",
                                    "g",          "h",
                                    "j",          "k",
                                    "l",          "-",
                                    "\n",         LV_SYMBOL_BACKSPACE,
                                    "z",          "x",
                                    "c",          "v",
                                    "b",          "n",
                                    "m",          "_",
                                    LV_SYMBOL_OK, ""};

// C++ 不允许 int 隐式转 enum，这里显式转一次；低位就是键宽。
constexpr lv_buttonmatrix_ctrl_t KeyWidth(int units) {
    return static_cast<lv_buttonmatrix_ctrl_t>(units);
}
constexpr lv_buttonmatrix_ctrl_t ControlKey() {
    return static_cast<lv_buttonmatrix_ctrl_t>(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 1);
}

// lv_part_t 和 lv_state_t 是两套枚举，C++20 下直接 | 会被当成弃用写法。
constexpr lv_style_selector_t PartState(lv_part_t part, lv_state_t state) {
    return static_cast<lv_style_selector_t>(part) | static_cast<lv_style_selector_t>(state);
}

const lv_buttonmatrix_ctrl_t kTokenKeyCtrl[] = {
    KeyWidth(1), KeyWidth(1), KeyWidth(1),  KeyWidth(1), KeyWidth(1),  KeyWidth(1), KeyWidth(1),
    KeyWidth(1), KeyWidth(1), KeyWidth(1),  KeyWidth(1), KeyWidth(1),  KeyWidth(1), KeyWidth(1),
    KeyWidth(1), KeyWidth(1), KeyWidth(1),  KeyWidth(1), KeyWidth(1),  KeyWidth(1), KeyWidth(1),
    KeyWidth(1), KeyWidth(1), KeyWidth(1),  KeyWidth(1), KeyWidth(1),  KeyWidth(1), KeyWidth(1),
    KeyWidth(1), KeyWidth(1), ControlKey(), KeyWidth(1), KeyWidth(1),  KeyWidth(1), KeyWidth(1),
    KeyWidth(1), KeyWidth(1), KeyWidth(1),  KeyWidth(1), ControlKey(),
};

lv_obj_t* CreateTransparentPanel(lv_obj_t* parent) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

}  // namespace

CatSettingsUi::CatSettingsUi(Display* display, Callbacks callbacks)
    : display_(display), callbacks_(std::move(callbacks)) {
    EnsureBuilt();
}

CatSettingsUi::~CatSettingsUi() {
    DisplayLockGuard lock(display_);
    if (toast_timer_ != nullptr) {
        lv_timer_delete(toast_timer_);
        toast_timer_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

void CatSettingsUi::EnsureBuilt() {
    if (root_ != nullptr) {
        return;
    }

    palette_ = CatUiResolvePalette(display_);
    root_ = CatUiCreatePageRoot();
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(root_, CatUiTextFont(display_), 0);

    BuildSettingsPanel();
    BuildTokenPanel();
    BuildToast();
    ApplyPalette();
}

void CatSettingsUi::BuildSettingsPanel() {
    settings_panel_ = CreateTransparentPanel(root_);

    lv_obj_t* back = CreateHeaderButton(settings_panel_, MATERIAL_SYMBOLS_ARROW_BACK, false,
                                        LV_ALIGN_TOP_LEFT, kMargin, 6);
    lv_obj_add_event_cb(back, OnBackToFace, LV_EVENT_CLICKED, this);

    lv_obj_t* title = lv_label_create(settings_panel_);
    lv_label_set_text(title, "设置");
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    primary_labels_.push_back(title);

    token_row_ = CreateRow(settings_panel_, 44, MATERIAL_SYMBOLS_KEY, "DeepSeek Token", false);
    lv_obj_add_event_cb(token_row_.row, OnOpenTokenPage, LV_EVENT_CLICKED, this);

    provisioning_row_ = CreateRow(settings_panel_, 44 + kRowHeight + kRowGap,
                                  MATERIAL_SYMBOLS_WIFI_OFF, "清除配网并重启", true);
    lv_obj_add_event_cb(provisioning_row_.row, OnClearProvisioning, LV_EVENT_CLICKED, this);

    clear_token_row_ = CreateRow(settings_panel_, 44 + 2 * (kRowHeight + kRowGap),
                                 MATERIAL_SYMBOLS_DELETE, "清除 DeepSeek Token", true);
    lv_obj_add_event_cb(clear_token_row_.row, OnClearToken, LV_EVENT_CLICKED, this);

    footer_ = lv_label_create(settings_panel_);
    lv_label_set_text(footer_, "XiaoZhi · quandong-s3-dev-4g");
    lv_obj_set_style_text_font(footer_, &font_noto_sans_basic_14_1, 0);
    lv_obj_align(footer_, LV_ALIGN_BOTTOM_MID, 0, -2);
    muted_labels_.push_back(footer_);
}

CatSettingsUi::Row CatSettingsUi::CreateRow(lv_obj_t* parent, int y, const char* icon,
                                            const char* title, bool danger) {
    Row row;
    row.row = lv_obj_create(parent);
    lv_obj_set_size(row.row, LV_HOR_RES - 2 * kMargin, kRowHeight);
    lv_obj_set_pos(row.row, kMargin, y);
    lv_obj_set_style_radius(row.row, 14, 0);
    lv_obj_set_style_border_width(row.row, 1, 0);
    lv_obj_set_style_pad_left(row.row, 10, 0);
    lv_obj_set_style_pad_right(row.row, 10, 0);
    lv_obj_set_style_pad_top(row.row, 0, 0);
    lv_obj_set_style_pad_bottom(row.row, 0, 0);
    lv_obj_set_style_shadow_width(row.row, 10, 0);
    lv_obj_set_style_shadow_opa(row.row, LV_OPA_10, 0);
    lv_obj_set_style_shadow_offset_y(row.row, 3, 0);
    lv_obj_set_flex_flow(row.row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row.row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(row.row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(row.row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row.row, LV_OBJ_FLAG_CLICKABLE);
    cards_.push_back(row.row);

    row.icon_bg = lv_obj_create(row.row);
    lv_obj_set_size(row.icon_bg, 28, 28);
    lv_obj_set_style_radius(row.icon_bg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(row.icon_bg, 0, 0);
    lv_obj_set_style_pad_all(row.icon_bg, 0, 0);
    lv_obj_set_style_shadow_width(row.icon_bg, 0, 0);
    lv_obj_clear_flag(row.icon_bg, LV_OBJ_FLAG_SCROLLABLE);
    // 图标底座和右侧值容器只负责显示；不接收点击，让整行都能触发 row 的事件。
    lv_obj_remove_flag(row.icon_bg, LV_OBJ_FLAG_CLICKABLE);
    (danger ? danger_soft_bgs_ : accent_soft_bgs_).push_back(row.icon_bg);

    row.icon = lv_label_create(row.icon_bg);
    lv_label_set_text(row.icon, icon);
    lv_obj_set_style_text_font(row.icon, CatUiIconFont(display_), 0);
    lv_obj_center(row.icon);
    (danger ? danger_labels_ : accent_labels_).push_back(row.icon);

    row.title = lv_label_create(row.row);
    lv_label_set_text(row.title, title);
    lv_obj_set_flex_grow(row.title, 1);
    primary_labels_.push_back(row.title);
    if (danger) {
        danger_labels_.push_back(row.title);
    }

    lv_obj_t* right = lv_obj_create(row.row);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_set_style_pad_column(right, 4, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_CLICKABLE);

    row.value = lv_label_create(right);
    lv_label_set_text(row.value, "");
    // 颜色由 UpdateTokenStatus() 按“已设置 / 未设置”单独控制

    row.chevron = lv_label_create(right);
    lv_label_set_text(row.chevron, MATERIAL_SYMBOLS_KEYBOARD_ARROW_RIGHT);
    lv_obj_set_style_text_font(row.chevron, CatUiIconFont(display_), 0);
    muted_labels_.push_back(row.chevron);

    return row;
}

lv_obj_t* CatSettingsUi::CreateHeaderButton(lv_obj_t* parent, const char* icon, bool accent,
                                            lv_align_t align, int x, int y) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, 32, 32);
    lv_obj_align(button, align, x, y);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(button, 6, 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_20, 0);
    lv_obj_set_style_shadow_offset_y(button, 2, 0);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_font(label, CatUiIconFont(display_), 0);
    lv_obj_center(label);

    if (accent) {
        accent_buttons_.push_back(button);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    } else {
        cards_.push_back(button);
        primary_labels_.push_back(label);
    }
    return button;
}

void CatSettingsUi::BuildTokenPanel() {
    token_panel_ = CreateTransparentPanel(root_);
    lv_obj_add_flag(token_panel_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* back = CreateHeaderButton(token_panel_, MATERIAL_SYMBOLS_ARROW_BACK, false,
                                        LV_ALIGN_TOP_LEFT, kMargin, 4);
    lv_obj_add_event_cb(back, OnTokenBack, LV_EVENT_CLICKED, this);

    lv_obj_t* title = lv_label_create(token_panel_);
    lv_label_set_text(title, "DeepSeek Token");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);
    primary_labels_.push_back(title);

    lv_obj_t* save = CreateHeaderButton(token_panel_, MATERIAL_SYMBOLS_CHECK, true,
                                        LV_ALIGN_TOP_RIGHT, -kMargin, 4);
    lv_obj_add_event_cb(save, OnTokenSave, LV_EVENT_CLICKED, this);

    token_edit_ = lv_textarea_create(token_panel_);
    lv_obj_set_size(token_edit_, 260, 36);
    lv_obj_set_pos(token_edit_, kMargin, 36);
    lv_obj_set_style_radius(token_edit_, 10, 0);
    lv_obj_set_style_border_width(token_edit_, 1, 0);
    lv_obj_set_style_pad_left(token_edit_, 10, 0);
    lv_obj_set_style_pad_right(token_edit_, 10, 0);
    lv_obj_set_style_pad_top(token_edit_, 4, 0);
    lv_obj_set_style_pad_bottom(token_edit_, 4, 0);
    lv_textarea_set_one_line(token_edit_, true);
    lv_textarea_set_max_length(token_edit_, 128);
    lv_textarea_set_password_mode(token_edit_, true);
    // 默认遮罩是 U+2022，basic 字体里没有；用 ASCII 星号保证没有 assets 字体时也能显示。
    lv_textarea_set_password_bullet(token_edit_, "*");
    lv_textarea_set_placeholder_text(token_edit_, "sk-...");

    eye_button_ = lv_button_create(token_panel_);
    lv_obj_set_size(eye_button_, 28, 28);
    lv_obj_set_pos(eye_button_, 280, 40);
    lv_obj_set_style_radius(eye_button_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(eye_button_, 1, 0);
    lv_obj_set_style_shadow_width(eye_button_, 0, 0);
    lv_obj_add_event_cb(eye_button_, OnTokenEye, LV_EVENT_CLICKED, this);
    cards_.push_back(eye_button_);

    eye_icon_ = lv_label_create(eye_button_);
    lv_label_set_text(eye_icon_, MATERIAL_SYMBOLS_EYEGLASSES);
    lv_obj_set_style_text_font(eye_icon_, CatUiIconFont(display_), 0);
    lv_obj_center(eye_icon_);
    muted_labels_.push_back(eye_icon_);

    // 没有单独一行提示：这行平时显示“仅保存在本机”，保存失败时原地变成错误文案，
    // 省下来的高度留给键盘，按键更容易点中。
    token_error_ = lv_label_create(token_panel_);
    lv_label_set_text(token_error_, "");
    lv_obj_set_width(token_error_, LV_HOR_RES - 2 * kMargin);
    lv_obj_set_pos(token_error_, kMargin, 72);
    lv_label_set_long_mode(token_error_, LV_LABEL_LONG_MODE_DOTS);

    keyboard_ = lv_keyboard_create(token_panel_);
    lv_obj_set_size(keyboard_, LV_HOR_RES - 8, kTokenPageKeyboardHeight);
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_opa(keyboard_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keyboard_, 0, 0);
    lv_obj_set_style_pad_all(keyboard_, 4, 0);
    lv_obj_set_style_pad_row(keyboard_, 4, 0);
    lv_obj_set_style_pad_column(keyboard_, 4, 0);
    lv_keyboard_set_textarea(keyboard_, token_edit_);
    lv_keyboard_set_popovers(keyboard_, false);
    lv_keyboard_set_map(keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER, kTokenKeyMap, kTokenKeyCtrl);
    lv_keyboard_set_mode(keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(keyboard_, OnKeyboardReady, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, OnKeyboardCancel, LV_EVENT_CANCEL, this);
}

void CatSettingsUi::BuildToast() {
    toast_ = lv_obj_create(root_);
    lv_obj_set_size(toast_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -38);
    lv_obj_set_style_radius(toast_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(toast_, 0, 0);
    lv_obj_set_style_pad_left(toast_, 14, 0);
    lv_obj_set_style_pad_right(toast_, 14, 0);
    lv_obj_set_style_pad_top(toast_, 7, 0);
    lv_obj_set_style_pad_bottom(toast_, 7, 0);
    lv_obj_set_style_shadow_width(toast_, 12, 0);
    lv_obj_set_style_shadow_opa(toast_, LV_OPA_20, 0);
    lv_obj_set_style_shadow_offset_y(toast_, 4, 0);
    lv_obj_set_scrollbar_mode(toast_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(toast_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);

    toast_label_ = lv_label_create(toast_);
    lv_label_set_text(toast_label_, "");
    lv_obj_center(toast_label_);

    toast_timer_ = lv_timer_create(OnToastTimer, 2200, this);
    if (toast_timer_ != nullptr) {
        lv_timer_pause(toast_timer_);
    }
}

void CatSettingsUi::Show() {
    EnsureBuilt();
    ApplyPalette();
    UpdateTokenStatus();
    ShowSettingsPanel();
    if (toast_ != nullptr) {
        lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    }
    if (toast_timer_ != nullptr) {
        lv_timer_pause(toast_timer_);
    }
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
}

void CatSettingsUi::ShowTokenPage() {
    EnsureBuilt();
    ApplyPalette();
    UpdateTokenStatus();

    lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(token_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);

    lv_label_set_text(token_error_, "仅保存在本机，用于查询余额");
    lv_obj_set_style_text_color(token_error_, palette_.muted, 0);
    lv_textarea_set_text(token_edit_, deepseek::LoadToken().c_str());
    lv_textarea_set_password_mode(token_edit_, true);
    lv_obj_set_style_text_color(eye_icon_, palette_.muted, 0);
    lv_textarea_set_cursor_pos(token_edit_, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_add_state(token_edit_, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(keyboard_, token_edit_);
    lv_obj_move_foreground(keyboard_);
}

void CatSettingsUi::ShowSettingsPanel() {
    if (token_panel_ != nullptr) {
        lv_obj_add_flag(token_panel_, LV_OBJ_FLAG_HIDDEN);
    }
    if (settings_panel_ != nullptr) {
        lv_obj_remove_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    }
    if (token_edit_ != nullptr) {
        lv_obj_remove_state(token_edit_, LV_STATE_FOCUSED);
    }
}

void CatSettingsUi::Hide() {
    if (root_ == nullptr) {
        return;
    }
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    if (toast_timer_ != nullptr) {
        lv_timer_pause(toast_timer_);
    }
}

bool CatSettingsUi::IsVisible() const {
    return root_ != nullptr && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void CatSettingsUi::UpdateTokenStatus() {
    if (token_row_.value == nullptr || clear_token_row_.row == nullptr) {
        return;
    }
    const bool configured = !deepseek::LoadToken().empty();
    lv_label_set_text(token_row_.value, configured ? "已设置" : "未设置");
    lv_obj_set_style_text_color(token_row_.value, configured ? palette_.success : palette_.muted,
                                0);
    lv_obj_set_style_opa(clear_token_row_.row, configured ? LV_OPA_COVER : LV_OPA_40, 0);
    if (configured) {
        lv_obj_add_flag(clear_token_row_.row, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_remove_flag(clear_token_row_.row, LV_OBJ_FLAG_CLICKABLE);
    }
}

void CatSettingsUi::ShowToast(const char* text) {
    if (toast_ == nullptr || toast_label_ == nullptr) {
        return;
    }
    lv_label_set_text(toast_label_, text);
    lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_);
    if (toast_timer_ != nullptr) {
        lv_timer_reset(toast_timer_);
        lv_timer_resume(toast_timer_);
    }
}

void CatSettingsUi::SaveTokenFromEditor() {
    const char* text = lv_textarea_get_text(token_edit_);
    std::string error;
    if (!deepseek::SaveToken(text != nullptr ? text : "", error)) {
        lv_label_set_text(token_error_, error.c_str());
        lv_obj_set_style_text_color(token_error_, palette_.danger, 0);
        return;
    }
    lv_label_set_text(token_error_, "仅保存在本机，用于查询余额");
    lv_obj_set_style_text_color(token_error_, palette_.muted, 0);
    ShowSettingsPanel();
    UpdateTokenStatus();
    ShowToast("Token 已保存");
    if (callbacks_.on_token_changed) {
        callbacks_.on_token_changed();
    }
}

void CatSettingsUi::ToggleTokenVisibility() {
    const bool masked = lv_textarea_get_password_mode(token_edit_);
    lv_textarea_set_password_mode(token_edit_, !masked);
    lv_obj_set_style_text_color(eye_icon_, masked ? palette_.accent : palette_.muted, 0);
}

void CatSettingsUi::ApplyPalette() {
    if (root_ == nullptr) {
        return;
    }
    palette_ = CatUiResolvePalette(display_);

    lv_obj_set_style_bg_color(root_, palette_.background, 0);
    lv_obj_set_style_bg_grad_color(root_, palette_.background_grad, 0);
    lv_obj_set_style_bg_grad_dir(root_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(root_, CatUiTextFont(display_), 0);
    lv_obj_set_style_text_color(root_, palette_.text, 0);

    for (lv_obj_t* card : cards_) {
        lv_obj_set_style_bg_color(card, palette_.card, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, palette_.card_border, 0);
        lv_obj_set_style_shadow_color(card, lv_color_black(), 0);
        lv_obj_set_style_bg_color(card, palette_.key_pressed, LV_STATE_PRESSED);
    }
    for (lv_obj_t* button : accent_buttons_) {
        lv_obj_set_style_bg_color(button, palette_.accent, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_shadow_color(button, palette_.accent, 0);
        lv_obj_set_style_bg_color(button, lv_color_darken(palette_.accent, 20), LV_STATE_PRESSED);
    }
    for (lv_obj_t* label : primary_labels_) {
        lv_obj_set_style_text_color(label, palette_.text, 0);
    }
    for (lv_obj_t* label : muted_labels_) {
        lv_obj_set_style_text_color(label, palette_.muted, 0);
    }
    for (lv_obj_t* label : accent_labels_) {
        lv_obj_set_style_text_color(label, palette_.accent, 0);
    }
    for (lv_obj_t* label : danger_labels_) {
        lv_obj_set_style_text_color(label, palette_.danger, 0);
    }
    for (lv_obj_t* bg : accent_soft_bgs_) {
        lv_obj_set_style_bg_color(bg, palette_.accent_soft, 0);
        lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    }
    for (lv_obj_t* bg : danger_soft_bgs_) {
        lv_obj_set_style_bg_color(bg, palette_.danger, 0);
        lv_obj_set_style_bg_opa(bg, LV_OPA_10, 0);
    }

    if (token_edit_ != nullptr) {
        lv_obj_set_style_bg_color(token_edit_, palette_.card, 0);
        lv_obj_set_style_bg_opa(token_edit_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(token_edit_, palette_.card_border, 0);
        lv_obj_set_style_border_color(token_edit_, palette_.accent, LV_STATE_FOCUSED);
        lv_obj_set_style_text_color(token_edit_, palette_.text, 0);
        lv_obj_set_style_text_color(token_edit_, palette_.muted, LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_text_font(token_edit_, CatUiTextFont(display_), 0);
    }
    if (eye_button_ != nullptr) {
        lv_obj_set_style_border_color(eye_button_, palette_.card_border, 0);
    }
    if (eye_icon_ != nullptr && token_edit_ != nullptr) {
        lv_obj_set_style_text_color(
            eye_icon_,
            lv_textarea_get_password_mode(token_edit_) ? palette_.muted : palette_.accent, 0);
    }
    if (keyboard_ != nullptr) {
        lv_obj_set_style_bg_color(keyboard_, palette_.key_bg, LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(keyboard_, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(keyboard_, palette_.key_pressed,
                                  PartState(LV_PART_ITEMS, LV_STATE_PRESSED));
        lv_obj_set_style_text_color(keyboard_, palette_.text, LV_PART_ITEMS);
        lv_obj_set_style_text_font(keyboard_, &lv_font_montserrat_14, LV_PART_ITEMS);
        lv_obj_set_style_border_width(keyboard_, 1, LV_PART_ITEMS);
        lv_obj_set_style_border_color(keyboard_, palette_.card_border, LV_PART_ITEMS);
        lv_obj_set_style_radius(keyboard_, 6, LV_PART_ITEMS);
        lv_obj_set_style_shadow_width(keyboard_, 0, LV_PART_ITEMS);
    }
    if (toast_ != nullptr) {
        lv_obj_set_style_bg_color(toast_, palette_.text, 0);
        lv_obj_set_style_bg_opa(toast_, LV_OPA_90, 0);
        lv_obj_set_style_shadow_color(toast_, lv_color_black(), 0);
    }
    if (toast_label_ != nullptr) {
        lv_obj_set_style_text_color(toast_label_, palette_.background, 0);
        lv_obj_set_style_text_font(toast_label_, CatUiTextFont(display_), 0);
    }
}

void CatSettingsUi::RefreshTheme() {
    if (root_ == nullptr) {
        return;
    }
    ApplyPalette();
    UpdateTokenStatus();
}

void CatSettingsUi::OnBackToFace(lv_event_t* event) {
    auto* self = static_cast<CatSettingsUi*>(lv_event_get_user_data(event));
    if (self != nullptr && self->callbacks_.on_close) {
        self->callbacks_.on_close();
    }
}

void CatSettingsUi::OnOpenTokenPage(lv_event_t* event) {
    auto* self = static_cast<CatSettingsUi*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->ShowTokenPage();
    }
}

void CatSettingsUi::OnClearProvisioning(lv_event_t* event) {
    auto* self = static_cast<CatSettingsUi*>(lv_event_get_user_data(event));
    if (self == nullptr || !self->callbacks_.show_confirm) {
        return;
    }
    self->callbacks_.show_confirm("清除配网并重启？\n将删除已保存的 Wi-Fi，并在重启后进入配网页",
                                  [self]() {
                                      self->ShowToast("配网已清除，正在重启...");
                                      if (self->callbacks_.on_clear_provisioning) {
                                          self->callbacks_.on_clear_provisioning();
                                      }
                                  });
}

void CatSettingsUi::OnClearToken(lv_event_t* event) {
    auto* self = static_cast<CatSettingsUi*>(lv_event_get_user_data(event));
    if (self == nullptr || !self->callbacks_.show_confirm) {
        return;
    }
    if (deepseek::LoadToken().empty()) {
        return;
    }
    self->callbacks_.show_confirm("清除 DeepSeek Token？\n删除后需重新输入才能查询余额", [self]() {
        deepseek::ClearToken();
        if (self->callbacks_.on_token_changed) {
            self->callbacks_.on_token_changed();
        }
        self->UpdateTokenStatus();
        self->ShowToast("DeepSeek Token 已清除");
    });
}

void CatSettingsUi::OnTokenBack(lv_event_t* event) {
    auto* self = static_cast<CatSettingsUi*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->ShowSettingsPanel();
        self->UpdateTokenStatus();
    }
}

void CatSettingsUi::OnTokenSave(lv_event_t* event) {
    auto* self = static_cast<CatSettingsUi*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->SaveTokenFromEditor();
    }
}

void CatSettingsUi::OnTokenEye(lv_event_t* event) {
    auto* self = static_cast<CatSettingsUi*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->ToggleTokenVisibility();
    }
}

void CatSettingsUi::OnKeyboardReady(lv_event_t* event) {
    auto* self = static_cast<CatSettingsUi*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->SaveTokenFromEditor();
    }
}

void CatSettingsUi::OnKeyboardCancel(lv_event_t* event) {
    auto* self = static_cast<CatSettingsUi*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->ShowSettingsPanel();
        self->UpdateTokenStatus();
    }
}

void CatSettingsUi::OnToastTimer(lv_timer_t* timer) {
    auto* self = static_cast<CatSettingsUi*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->toast_ == nullptr) {
        return;
    }
    lv_obj_add_flag(self->toast_, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
}
