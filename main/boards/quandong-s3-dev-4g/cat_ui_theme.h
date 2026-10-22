#ifndef QUANDONG_S3_DEV_4G_CAT_UI_THEME_H_
#define QUANDONG_S3_DEV_4G_CAT_UI_THEME_H_

#include "display.h"
#include "lvgl_theme.h"

#include <lvgl.h>

// 设置页 / 余额页共用的一套颜色和字体解析。两个页面都挂在 lv_layer_top() 上，
// 拿不到 screen 上继承下来的中文字体，所以每次显示前都从当前主题重新取一次。
struct CatUiPalette {
    lv_color_t background;
    lv_color_t background_grad;
    lv_color_t card;
    lv_color_t card_border;
    lv_color_t text;
    lv_color_t muted;
    lv_color_t accent;
    lv_color_t accent_soft;
    lv_color_t success;
    lv_color_t danger;
    lv_color_t warning;
    lv_color_t key_bg;
    lv_color_t key_pressed;
    bool dark = false;
};

inline CatUiPalette CatUiResolvePalette(Display* display) {
    const auto* theme =
        display != nullptr ? static_cast<const LvglTheme*>(display->GetTheme()) : nullptr;
    const bool dark = theme != nullptr && theme->name() == "dark";
    if (dark) {
        return {
            .background = lv_color_hex(0x0B1020),
            .background_grad = lv_color_hex(0x121A2B),
            .card = lv_color_hex(0x151C2C),
            .card_border = lv_color_hex(0x263147),
            .text = lv_color_hex(0xF2F4F7),
            .muted = lv_color_hex(0x98A2B3),
            .accent = lv_color_hex(0x6C8CFF),
            .accent_soft = lv_color_hex(0x1E2B4D),
            .success = lv_color_hex(0x32D583),
            .danger = lv_color_hex(0xF97066),
            .warning = lv_color_hex(0xFDB022),
            .key_bg = lv_color_hex(0x1B2436),
            .key_pressed = lv_color_hex(0x2B3A5A),
            .dark = true,
        };
    }
    return {
        .background = lv_color_hex(0xF4F6FB),
        .background_grad = lv_color_hex(0xE9EEF7),
        .card = lv_color_hex(0xFFFFFF),
        .card_border = lv_color_hex(0xE4E7EC),
        .text = lv_color_hex(0x101828),
        .muted = lv_color_hex(0x667085),
        .accent = lv_color_hex(0x4D6BFE),
        .accent_soft = lv_color_hex(0xE8EDFF),
        .success = lv_color_hex(0x12B76A),
        .danger = lv_color_hex(0xD92D20),
        .warning = lv_color_hex(0xF79009),
        .key_bg = lv_color_hex(0xFFFFFF),
        .key_pressed = lv_color_hex(0xDCE3F5),
        .dark = false,
    };
}

inline const lv_font_t* CatUiTextFont(Display* display) {
    const auto* theme =
        display != nullptr ? static_cast<const LvglTheme*>(display->GetTheme()) : nullptr;
    if (theme != nullptr && theme->text_font() != nullptr &&
        theme->text_font()->font() != nullptr) {
        return theme->text_font()->font();
    }
    return &lv_font_montserrat_14;
}

inline const lv_font_t* CatUiIconFont(Display* display) {
    const auto* theme =
        display != nullptr ? static_cast<const LvglTheme*>(display->GetTheme()) : nullptr;
    if (theme != nullptr && theme->icon_font() != nullptr &&
        theme->icon_font()->font() != nullptr) {
        return theme->icon_font()->font();
    }
    return &lv_font_montserrat_14;
}

// 全屏页面根对象：透明、无边框、无滚动，铺满屏幕。
inline lv_obj_t* CatUiCreatePageRoot() {
    lv_obj_t* root = lv_obj_create(lv_layer_top());
    lv_obj_set_size(root, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_shadow_width(root, 0, 0);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    return root;
}

#endif  // QUANDONG_S3_DEV_4G_CAT_UI_THEME_H_
