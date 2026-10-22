#include "deepseek_balance_ui.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <material_symbols.h>

#include <cmath>
#include <cstdio>
#include <utility>

#define TAG "DeepSeekBalanceUi"

LV_FONT_DECLARE(font_noto_sans_basic_14_1);
LV_FONT_DECLARE(font_noto_sans_basic_30_4);

namespace {

constexpr int kMargin = 12;
constexpr int kHeroY = 44;
constexpr int kHeroHeight = 96;
constexpr int kTileY = 146;
constexpr int kTileHeight = 58;
constexpr int kTileGap = 7;
constexpr int64_t kRefreshIntervalUs = 60LL * 1000 * 1000;
constexpr uint32_t kUiTickMs = 200;

std::string CurrencySymbol(const std::string& currency) {
    if (currency == "USD") {
        return "$";
    }
    if (currency == "CNY") {
        return "\xC2\xA5";
    }
    return currency.empty() ? std::string() : currency + " ";
}

std::string Money(const std::string& amount, const std::string& currency) {
    if (amount.empty()) {
        return "--";
    }
    return CurrencySymbol(currency) + amount;
}

// 今日消耗 = 开机后第一次成功取到的总额 - 当前总额。
// 正数是消耗，负数说明期间充值/赠金导致余额变多。
std::string SignedMoney(double value, const std::string& currency) {
    char number[32];
    std::snprintf(number, sizeof(number), "%.2f", std::fabs(value));
    const std::string sign = value < -0.005 ? "-" : "";
    return sign + CurrencySymbol(currency) + number;
}

lv_obj_t* CreateCard(lv_obj_t* parent, int x, int y, int width, int height) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_shadow_offset_y(card, 3, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    return card;
}

lv_obj_t* CreateHeaderButton(lv_obj_t* parent, const char* icon, const lv_font_t* icon_font,
                             lv_align_t align, int x, int y) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, 32, 32);
    lv_obj_align(button, align, x, y);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_width(button, 6, 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_20, 0);
    lv_obj_set_style_shadow_offset_y(button, 2, 0);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_font(label, icon_font, 0);
    lv_obj_center(label);
    return button;
}

}  // namespace

DeepSeekBalanceUi::DeepSeekBalanceUi(Display* display, Callbacks callbacks)
    : display_(display), callbacks_(std::move(callbacks)) {
    EnsureBuilt();
}

DeepSeekBalanceUi::~DeepSeekBalanceUi() {
    stop_.store(true);
    visible_.store(false);
    // 先等任务退出，不能持着 LVGL 锁等：HTTP 超时是 15s，持锁会把整个界面冻住。
    for (int i = 0; i < 300 && worker_running_.load(); ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (worker_running_.load()) {
        ESP_LOGE(TAG, "深度求索余额任务未在 30 秒内退出");
    }

    DisplayLockGuard lock(display_);
    if (ui_timer_ != nullptr) {
        lv_timer_delete(ui_timer_);
        ui_timer_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

void DeepSeekBalanceUi::EnsureBuilt() {
    if (root_ != nullptr) {
        return;
    }
    palette_ = CatUiResolvePalette(display_);
    root_ = CatUiCreatePageRoot();
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(root_, CatUiTextFont(display_), 0);

    BuildHeader();
    BuildHero();
    BuildTiles();
    BuildFooter();
    ApplyPalette();
    ApplyIdleState();

    ui_timer_ = lv_timer_create(OnUiTimer, kUiTickMs, this);
    if (ui_timer_ != nullptr) {
        lv_timer_pause(ui_timer_);
    }
}

void DeepSeekBalanceUi::BuildHeader() {
    const lv_font_t* icon_font = CatUiIconFont(display_);
    lv_obj_t* back = CreateHeaderButton(root_, MATERIAL_SYMBOLS_ARROW_BACK, icon_font,
                                        LV_ALIGN_TOP_LEFT, kMargin, 6);
    lv_obj_add_event_cb(back, OnBackClicked, LV_EVENT_CLICKED, this);
    back_icon_ = lv_obj_get_child(back, 0);
    cards_.push_back(back);
    primary_labels_.push_back(back_icon_);

    title_ = lv_label_create(root_);
    lv_label_set_text(title_, "天才陨落倒计时");
    lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 10);
    primary_labels_.push_back(title_);

    lv_obj_t* refresh = CreateHeaderButton(root_, MATERIAL_SYMBOLS_REFRESH, icon_font,
                                           LV_ALIGN_TOP_RIGHT, -kMargin, 6);
    lv_obj_add_event_cb(refresh, OnRefreshClicked, LV_EVENT_CLICKED, this);
    refresh_icon_ = lv_obj_get_child(refresh, 0);
    cards_.push_back(refresh);
    primary_labels_.push_back(refresh_icon_);
}

void DeepSeekBalanceUi::BuildHero() {
    hero_ = lv_obj_create(root_);
    lv_obj_set_pos(hero_, kMargin, kHeroY);
    lv_obj_set_size(hero_, LV_HOR_RES - 2 * kMargin, kHeroHeight);
    lv_obj_set_style_radius(hero_, 16, 0);
    lv_obj_set_style_clip_corner(hero_, true, 0);
    lv_obj_set_style_border_width(hero_, 0, 0);
    lv_obj_set_style_pad_all(hero_, 0, 0);
    lv_obj_set_style_bg_color(hero_, lv_color_hex(0x1D3BFF), 0);
    lv_obj_set_style_bg_grad_color(hero_, lv_color_hex(0x5B7BFF), 0);
    lv_obj_set_style_bg_grad_dir(hero_, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_shadow_width(hero_, 18, 0);
    lv_obj_set_style_shadow_opa(hero_, LV_OPA_20, 0);
    lv_obj_set_style_shadow_offset_y(hero_, 8, 0);
    lv_obj_set_style_shadow_color(hero_, lv_color_hex(0x1D3BFF), 0);
    lv_obj_set_scrollbar_mode(hero_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(hero_, LV_OBJ_FLAG_SCROLLABLE);

    // 右侧两枚半透明圆：被圆角卡片裁切后形成弧线，给纯色卡面一点层次。
    lv_obj_t* ring = lv_obj_create(hero_);
    lv_obj_set_size(ring, 104, 104);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_10, 0);
    lv_obj_set_style_border_width(ring, 0, 0);
    lv_obj_set_style_shadow_width(ring, 0, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ring, LV_ALIGN_RIGHT_MID, 42, -8);

    lv_obj_t* small_ring = lv_obj_create(hero_);
    lv_obj_set_size(small_ring, 54, 54);
    lv_obj_set_style_radius(small_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(small_ring, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(small_ring, LV_OPA_10, 0);
    lv_obj_set_style_border_width(small_ring, 0, 0);
    lv_obj_set_style_shadow_width(small_ring, 0, 0);
    lv_obj_clear_flag(small_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(small_ring, LV_ALIGN_RIGHT_MID, 18, 38);

    caption_ = lv_label_create(hero_);
    lv_label_set_text(caption_, "今日消耗");
    lv_obj_set_style_text_color(caption_, lv_color_hex(0xDCE5FF), 0);
    lv_obj_set_pos(caption_, 14, 10);

    status_pill_ = lv_obj_create(hero_);
    lv_obj_set_size(status_pill_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(status_pill_, LV_ALIGN_TOP_RIGHT, -12, 6);
    lv_obj_set_style_radius(status_pill_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(status_pill_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(status_pill_, LV_OPA_20, 0);
    lv_obj_set_style_border_width(status_pill_, 0, 0);
    lv_obj_set_style_pad_left(status_pill_, 8, 0);
    lv_obj_set_style_pad_right(status_pill_, 9, 0);
    lv_obj_set_style_pad_top(status_pill_, 3, 0);
    lv_obj_set_style_pad_bottom(status_pill_, 3, 0);
    lv_obj_set_style_pad_column(status_pill_, 4, 0);
    lv_obj_set_style_shadow_width(status_pill_, 0, 0);
    lv_obj_set_flex_flow(status_pill_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_pill_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(status_pill_, LV_OBJ_FLAG_SCROLLABLE);

    status_icon_ = lv_label_create(status_pill_);
    lv_label_set_text(status_icon_, MATERIAL_SYMBOLS_PROGRESS_ACTIVITY);
    lv_obj_set_style_text_font(status_icon_, CatUiIconFont(display_), 0);
    lv_obj_set_style_text_color(status_icon_, lv_color_white(), 0);

    status_text_ = lv_label_create(status_pill_);
    lv_label_set_text(status_text_, "获取中");
    lv_obj_set_style_text_color(status_text_, lv_color_white(), 0);

    amount_row_ = lv_obj_create(hero_);
    lv_obj_set_size(amount_row_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(amount_row_, 14, 44);
    lv_obj_set_style_bg_opa(amount_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(amount_row_, 0, 0);
    lv_obj_set_style_pad_all(amount_row_, 0, 0);
    lv_obj_set_style_pad_column(amount_row_, 6, 0);
    lv_obj_set_style_shadow_width(amount_row_, 0, 0);
    lv_obj_set_flex_flow(amount_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(amount_row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_clear_flag(amount_row_, LV_OBJ_FLAG_SCROLLABLE);

    amount_ = lv_label_create(amount_row_);
    lv_label_set_text(amount_, "--");
    lv_obj_set_style_text_font(amount_, &font_noto_sans_basic_30_4, 0);
    lv_obj_set_style_text_color(amount_, lv_color_white(), 0);
    lv_obj_set_width(amount_, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(amount_, 210, 0);
    lv_label_set_long_mode(amount_, LV_LABEL_LONG_MODE_DOTS);

    currency_ = lv_label_create(amount_row_);
    lv_label_set_text(currency_, "");
    lv_obj_set_style_text_font(currency_, &font_noto_sans_basic_14_1, 0);
    lv_obj_set_style_text_color(currency_, lv_color_hex(0xDCE5FF), 0);
    lv_obj_set_style_pad_bottom(currency_, 3, 0);
}

void DeepSeekBalanceUi::BuildTiles() {
    const int tile_width = (LV_HOR_RES - 2 * kMargin - 2 * kTileGap) / 3;

    auto create_stat = [&](int index, const char* title, lv_obj_t** value_out) {
        lv_obj_t* card = CreateCard(root_, kMargin + index * (tile_width + kTileGap), kTileY,
                                    tile_width, kTileHeight);
        cards_.push_back(card);

        lv_obj_t* title_label = lv_label_create(card);
        lv_label_set_text(title_label, title);
        lv_obj_set_width(title_label, LV_PCT(100));
        lv_obj_set_pos(title_label, 0, 4);
        lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
        muted_labels_.push_back(title_label);

        lv_obj_t* value = lv_label_create(card);
        lv_label_set_text(value, "--");
        lv_obj_set_style_text_font(value, &font_noto_sans_basic_14_1, 0);
        lv_obj_set_width(value, LV_PCT(100));
        lv_obj_set_pos(value, 0, 32);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(value, LV_LABEL_LONG_MODE_DOTS);
        *value_out = value;
    };

    create_stat(0, "赠送", &granted_value_);
    create_stat(1, "充值", &topped_value_);
    create_stat(2, "总额", &total_value_);
}

void DeepSeekBalanceUi::BuildFooter() {
    footer_ = lv_label_create(root_);
    lv_label_set_text(footer_, "正在获取余额...");
    lv_obj_set_width(footer_, LV_HOR_RES - 2 * kMargin);
    lv_obj_set_pos(footer_, kMargin, LV_VER_RES - 30);
    lv_label_set_long_mode(footer_, LV_LABEL_LONG_MODE_DOTS);
    muted_labels_.push_back(footer_);

    settings_link_ = lv_button_create(root_);
    lv_obj_set_size(settings_link_, 76, 28);
    lv_obj_align(settings_link_, LV_ALIGN_BOTTOM_RIGHT, -kMargin, -6);
    lv_obj_set_style_radius(settings_link_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(settings_link_, 0, 0);
    lv_obj_set_style_shadow_width(settings_link_, 0, 0);
    lv_obj_add_flag(settings_link_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(settings_link_, OnSettingsLinkClicked, LV_EVENT_CLICKED, this);

    lv_obj_t* label = lv_label_create(settings_link_);
    lv_label_set_text(label, "去设置");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
}

void DeepSeekBalanceUi::Show() {
    EnsureBuilt();
    ApplyPalette();
    if (!has_data_) {
        ApplyIdleState();
    } else {
        current_state_ = State::kLoading;
        ApplyStatus(State::kLoading, is_available_);
        lv_label_set_text(footer_, "正在刷新...");
        lv_obj_set_style_text_color(footer_, palette_.muted, 0);
        SetTokenLinkVisible(false);
    }

    visible_.store(true);
    refresh_requested_.store(true);
    StartWorker();
    if (ui_timer_ != nullptr) {
        lv_timer_resume(ui_timer_);
    }
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
}

void DeepSeekBalanceUi::Hide() {
    if (root_ == nullptr) {
        return;
    }
    visible_.store(false);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    if (ui_timer_ != nullptr) {
        lv_timer_pause(ui_timer_);
    }
}

bool DeepSeekBalanceUi::IsVisible() const {
    return root_ != nullptr && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void DeepSeekBalanceUi::RefreshTheme() {
    if (root_ == nullptr) {
        return;
    }
    ApplyPalette();
    ApplyStatus(current_state_, is_available_);
    lv_obj_set_style_text_color(
        footer_, current_state_ == State::kError ? palette_.warning : palette_.muted, 0);
    if (current_state_ == State::kOk) {
        UpdateAgeText();
    }
}

void DeepSeekBalanceUi::SetTokenLinkVisible(bool visible) {
    if (footer_ == nullptr || settings_link_ == nullptr) {
        return;
    }
    if (visible) {
        lv_obj_add_flag(footer_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(settings_link_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(footer_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(settings_link_, LV_OBJ_FLAG_HIDDEN);
    }
}

void DeepSeekBalanceUi::UpdateConsumptionAmount() {
    if (amount_ == nullptr) {
        return;
    }
    if (has_data_ && has_consumption_) {
        lv_label_set_text(amount_, SignedMoney(today_consumption_, consumption_currency_).c_str());
    } else {
        lv_label_set_text(amount_, "--");
    }
}

void DeepSeekBalanceUi::ApplyPalette() {
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
    for (lv_obj_t* label : primary_labels_) {
        lv_obj_set_style_text_color(label, palette_.text, 0);
    }
    for (lv_obj_t* label : muted_labels_) {
        lv_obj_set_style_text_color(label, palette_.muted, 0);
    }

    lv_obj_set_style_text_font(amount_, &font_noto_sans_basic_30_4, 0);
    lv_obj_set_style_text_color(amount_, lv_color_white(), 0);
    lv_obj_set_style_text_font(currency_, &font_noto_sans_basic_14_1, 0);
    lv_obj_set_style_text_color(currency_, lv_color_hex(0xDCE5FF), 0);
    lv_obj_set_style_text_color(caption_, lv_color_hex(0xDCE5FF), 0);
    lv_obj_set_style_text_color(status_text_, lv_color_white(), 0);
    lv_obj_set_style_text_font(status_icon_, CatUiIconFont(display_), 0);
    lv_obj_set_style_text_font(granted_value_, &font_noto_sans_basic_14_1, 0);
    lv_obj_set_style_text_color(granted_value_, palette_.warning, 0);
    lv_obj_set_style_text_font(topped_value_, &font_noto_sans_basic_14_1, 0);
    lv_obj_set_style_text_color(topped_value_, palette_.accent, 0);
    lv_obj_set_style_text_font(total_value_, &font_noto_sans_basic_14_1, 0);
    lv_obj_set_style_text_color(total_value_, palette_.text, 0);
    lv_obj_set_style_bg_color(settings_link_, palette_.accent, 0);
    lv_obj_set_style_bg_opa(settings_link_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(settings_link_, lv_color_darken(palette_.accent, 20),
                              LV_STATE_PRESSED);
    lv_obj_set_style_text_font(footer_, CatUiTextFont(display_), 0);
    lv_obj_set_style_text_color(footer_, palette_.muted, 0);
    UpdateConsumptionAmount();
}

void DeepSeekBalanceUi::ApplyIdleState() {
    current_state_ = State::kIdle;
    has_data_ = false;
    is_available_ = false;
    needs_token_ = false;
    last_success_us_ = 0;
    last_age_text_.clear();

    lv_label_set_text(amount_, "--");
    lv_label_set_text(currency_, "");
    lv_label_set_text(granted_value_, "--");
    lv_label_set_text(topped_value_, "--");
    lv_label_set_text(total_value_, "--");
    ApplyStatus(State::kIdle, false);

    lv_label_set_text(footer_, "正在获取余额...");
    lv_obj_set_style_text_color(footer_, palette_.muted, 0);
    SetTokenLinkVisible(false);
    UpdateConsumptionAmount();
}

void DeepSeekBalanceUi::ApplyUpdate(const Update& update) {
    if (update.state == State::kLoading) {
        current_state_ = State::kLoading;
        ApplyStatus(State::kLoading, is_available_);
        lv_label_set_text(footer_, has_data_ ? "正在刷新..." : "正在获取余额...");
        lv_obj_set_style_text_color(footer_, palette_.muted, 0);
        SetTokenLinkVisible(false);
        UpdateConsumptionAmount();
        return;
    }

    if (update.state == State::kOk) {
        const deepseek::BalanceEntry& entry = update.info.entries.front();
        has_data_ = true;
        is_available_ = update.info.is_available;
        needs_token_ = false;
        current_state_ = State::kOk;
        last_success_us_ = update.fetched_at_us;
        last_age_text_.clear();

        // 基准正常由开机后台请求写入；如果后台还没成功，这里首次成功请求兜底。
        double current_total = 0.0;
        if (deepseek::ParseAmount(entry.total_balance, current_total)) {
            deepseek::SetBaselineIfUnset(current_total, entry.currency);
            double baseline_total = 0.0;
            std::string baseline_currency;
            if (deepseek::GetBaseline(baseline_total, baseline_currency) &&
                baseline_currency == entry.currency) {
                today_consumption_ = baseline_total - current_total;
                has_consumption_ = true;
                consumption_currency_ = entry.currency;
            } else {
                // 币种变了：基准切到当前币种，从 0 开始。
                deepseek::ResetBaseline();
                deepseek::SetBaselineIfUnset(current_total, entry.currency);
                today_consumption_ = 0.0;
                has_consumption_ = true;
                consumption_currency_ = entry.currency;
            }
        } else {
            has_consumption_ = false;
        }

        lv_label_set_text(currency_, has_consumption_ ? entry.currency.c_str() : "");
        lv_label_set_text(granted_value_, Money(entry.granted_balance, entry.currency).c_str());
        lv_label_set_text(topped_value_, Money(entry.topped_up_balance, entry.currency).c_str());
        lv_label_set_text(total_value_, Money(entry.total_balance, entry.currency).c_str());
        ApplyStatus(State::kOk, is_available_);
        lv_obj_set_style_text_color(footer_, palette_.muted, 0);
        SetTokenLinkVisible(false);
        UpdateConsumptionAmount();
        UpdateAgeText();
        return;
    }

    // kError / kIdle：有旧数据就保留，只把状态和底部文案切成失败态。
    current_state_ = State::kError;
    needs_token_ = update.needs_token;
    if (!has_data_) {
        lv_label_set_text(amount_, "--");
        lv_label_set_text(currency_, "");
        lv_label_set_text(granted_value_, "--");
        lv_label_set_text(topped_value_, "--");
        lv_label_set_text(total_value_, "--");
        ApplyStatus(State::kError, false);
    } else {
        ApplyStatus(State::kError, is_available_);
    }

    UpdateConsumptionAmount();
    if (update.needs_token) {
        SetTokenLinkVisible(true);
    } else {
        lv_label_set_text(footer_, "稍后重试");
        lv_obj_set_style_text_color(footer_, palette_.warning, 0);
        SetTokenLinkVisible(false);
    }
}

void DeepSeekBalanceUi::ApplyStatus(State state, bool available) {
    if (status_icon_ == nullptr || status_text_ == nullptr) {
        return;
    }

    lv_color_t icon_color = lv_color_white();
    const char* icon = MATERIAL_SYMBOLS_PROGRESS_ACTIVITY;
    const char* text = "获取中";
    switch (state) {
        case State::kOk:
            if (available) {
                icon = MATERIAL_SYMBOLS_CHECK_CIRCLE;
                text = "可用";
                icon_color = lv_color_hex(0x7CF7B0);
            } else {
                icon = MATERIAL_SYMBOLS_WARNING;
                text = "余额不足";
                icon_color = lv_color_hex(0xFFD1CC);
            }
            break;
        case State::kError:
            icon = MATERIAL_SYMBOLS_WARNING;
            text = needs_token_ ? "需要设置" : (has_data_ ? "更新失败" : "获取失败");
            icon_color = lv_color_hex(0xFFD1CC);
            break;
        case State::kIdle:
        case State::kLoading:
        default:
            break;
    }

    lv_label_set_text(status_icon_, icon);
    lv_obj_set_style_text_color(status_icon_, icon_color, 0);
    lv_label_set_text(status_text_, text);
}

void DeepSeekBalanceUi::UpdateAgeText() {
    if (current_state_ != State::kOk || last_success_us_ == 0 || footer_ == nullptr) {
        return;
    }
    const std::string age = FormatAge(esp_timer_get_time() - last_success_us_);
    if (age == last_age_text_) {
        return;
    }
    last_age_text_ = age;
    lv_label_set_text(footer_, age.c_str());
    lv_obj_set_style_text_color(footer_, palette_.muted, 0);
    SetTokenLinkVisible(false);
}

std::string DeepSeekBalanceUi::FormatAge(int64_t age_us) {
    const int64_t seconds = age_us / 1000000;
    if (seconds < 5) {
        return "刚刚";
    }
    if (seconds < 60) {
        return std::to_string(seconds) + "秒前";
    }
    const int64_t minutes = seconds / 60;
    if (minutes < 60) {
        return std::to_string(minutes) + "分钟前";
    }
    const int64_t hours = minutes / 60;
    return std::to_string(hours) + "小时前";
}

void DeepSeekBalanceUi::StartWorker() {
    bool expected = false;
    if (!worker_running_.compare_exchange_strong(expected, true)) {
        return;
    }
    if (xTaskCreate(WorkerEntry, "ds_balance", 8192, this, 2, nullptr) != pdPASS) {
        worker_running_.store(false);
        deepseek::BalanceInfo info;
        PublishResult(false, info, "无法启动查询任务", false);
        ESP_LOGE(TAG, "创建余额查询任务失败");
    }
}

void DeepSeekBalanceUi::WorkerEntry(void* arg) {
    static_cast<DeepSeekBalanceUi*>(arg)->WorkerLoop();
}

void DeepSeekBalanceUi::WorkerLoop() {
    int64_t next_fetch_us = 0;
    while (!stop_.load()) {
        if (!visible_.load()) {
            next_fetch_us = 0;
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        const int64_t now = esp_timer_get_time();
        const bool forced = refresh_requested_.exchange(false);
        if (forced || next_fetch_us == 0 || now >= next_fetch_us) {
            const int64_t fetch_started_us = esp_timer_get_time();
            PublishLoading();

            const std::string token =
                callbacks_.load_token ? callbacks_.load_token() : std::string();
            deepseek::BalanceInfo info;
            std::string error;
            bool ok = false;
            if (token.empty()) {
                error = "未设置 Token";
            } else if (callbacks_.fetch) {
                ok = callbacks_.fetch(token, info, error);
            } else {
                error = "余额服务未配置";
            }
            // Token 不存在或接口判定它无效时，都需要把用户引到设置页。
            const bool needs_token = !ok && error.find("Token") != std::string::npos;
            PublishResult(ok, info, error, needs_token);

            // 以开始时间计时，这样一次慢请求不会把下一轮的间隔整体推后。
            next_fetch_us = fetch_started_us + kRefreshIntervalUs;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    worker_running_.store(false);
    vTaskDelete(nullptr);
}

void DeepSeekBalanceUi::PublishLoading() {
    std::lock_guard<std::mutex> lock(update_mutex_);
    pending_.revision++;
    pending_.state = State::kLoading;
    pending_.error.clear();
    pending_.needs_token = false;
    has_pending_ = true;
}

void DeepSeekBalanceUi::PublishResult(bool ok, const deepseek::BalanceInfo& info,
                                      const std::string& error, bool needs_token) {
    std::lock_guard<std::mutex> lock(update_mutex_);
    const bool valid = ok && !info.entries.empty();
    pending_.revision++;
    pending_.state = valid ? State::kOk : State::kError;
    pending_.info = info;
    pending_.error = valid ? error : (error.empty() ? "未返回余额信息" : error);
    pending_.needs_token = needs_token;
    pending_.fetched_at_us = esp_timer_get_time();
    has_pending_ = true;
}

void DeepSeekBalanceUi::OnUiTimer(lv_timer_t* timer) {
    auto* self = static_cast<DeepSeekBalanceUi*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }

    Update update;
    bool has_update = false;
    {
        std::lock_guard<std::mutex> lock(self->update_mutex_);
        if (self->has_pending_ && self->pending_.revision != self->applied_revision_) {
            update = self->pending_;
            self->applied_revision_ = update.revision;
            self->has_pending_ = false;
            has_update = true;
        }
    }
    if (has_update) {
        self->ApplyUpdate(update);
    }
    self->UpdateAgeText();
}

void DeepSeekBalanceUi::OnBackClicked(lv_event_t* event) {
    auto* self = static_cast<DeepSeekBalanceUi*>(lv_event_get_user_data(event));
    if (self != nullptr && self->callbacks_.on_close) {
        self->callbacks_.on_close();
    }
}

void DeepSeekBalanceUi::OnRefreshClicked(lv_event_t* event) {
    auto* self = static_cast<DeepSeekBalanceUi*>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }
    self->refresh_requested_.store(true);
    self->StartWorker();
    self->current_state_ = State::kLoading;
    self->ApplyStatus(State::kLoading, self->is_available_);
    lv_label_set_text(self->footer_, self->has_data_ ? "正在刷新..." : "正在获取余额...");
    lv_obj_set_style_text_color(self->footer_, self->palette_.muted, 0);
    self->SetTokenLinkVisible(false);
}

void DeepSeekBalanceUi::OnSettingsLinkClicked(lv_event_t* event) {
    auto* self = static_cast<DeepSeekBalanceUi*>(lv_event_get_user_data(event));
    if (self != nullptr && self->callbacks_.on_open_settings) {
        self->callbacks_.on_open_settings();
    }
}
