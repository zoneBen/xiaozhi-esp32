#include "deepseek_bill_ui.h"

#include <material_symbols.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>

LV_FONT_DECLARE(font_noto_sans_basic_14_1);

namespace {

constexpr int kMargin = 12;
constexpr int kCardWidth = 296;
constexpr int kCardHeight = 90;
constexpr int kCardGap = 6;
constexpr int kConsumptionCardY = 42;
constexpr int kBalanceCardY = kConsumptionCardY + kCardHeight + kCardGap;  // 138
constexpr int kChartWidth = 280;
constexpr int kChartHeight = 38;
constexpr int kRangeButtonWidth = 54;
constexpr int kRangeButtonHeight = 28;

std::string CurrencySymbol(const std::string& currency) {
    if (currency == "USD") {
        return "$";
    }
    if (currency == "CNY") {
        return "\xC2\xA5";
    }
    return currency.empty() ? std::string() : currency + " ";
}

std::string Money(double value, const std::string& currency) {
    char number[32];
    std::snprintf(number, sizeof(number), "%.2f", value);
    return CurrencySymbol(currency) + number;
}

std::string ShortDate(const std::string& date) {
    if (date.size() != 8) {
        return date;
    }
    return date.substr(4, 2) + "-" + date.substr(6, 2);
}

int32_t ToCents(double value) {
    if (!std::isfinite(value) || value <= 0.0) {
        return 0;
    }
    const double cents = value * 100.0;
    // 给坐标轴留出 15% 余量和最小 padding，避免极端金额把 int32 撑溢出。
    constexpr int32_t kMaxPoint = std::numeric_limits<int32_t>::max() / 4;
    if (cents >= static_cast<double>(kMaxPoint)) {
        return kMaxPoint;
    }
    return static_cast<int32_t>(std::llround(cents));
}

lv_obj_t* CreateHeaderButton(lv_obj_t* parent, const char* icon, const lv_font_t* icon_font) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, 32, 32);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, kMargin, 4);
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

void StyleChart(lv_obj_t* chart) {
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);

    lv_obj_set_style_width(chart, 4, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart, 4, LV_PART_INDICATOR);
    lv_obj_set_style_radius(chart, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

DeepSeekBillUi::DeepSeekBillUi(Display* display, Callbacks callbacks)
    : display_(display), callbacks_(std::move(callbacks)) {
    EnsureBuilt();
}

DeepSeekBillUi::~DeepSeekBillUi() {
    DisplayLockGuard lock(display_);
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

void DeepSeekBillUi::EnsureBuilt() {
    if (root_ != nullptr) {
        return;
    }
    palette_ = CatUiResolvePalette(display_);
    root_ = CatUiCreatePageRoot();
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(root_, CatUiTextFont(display_), 0);

    BuildHeader();
    BuildCards();
    BuildEmptyState();
    ApplyPalette();
    Reload();
}

void DeepSeekBillUi::BuildHeader() {
    const lv_font_t* icon_font = CatUiIconFont(display_);
    lv_obj_t* back = CreateHeaderButton(root_, MATERIAL_SYMBOLS_ARROW_BACK, icon_font);
    lv_obj_add_event_cb(back, OnBackClicked, LV_EVENT_CLICKED, this);
    back_icon_ = lv_obj_get_child(back, 0);
    cards_.push_back(back);
    primary_labels_.push_back(back_icon_);

    title_ = lv_label_create(root_);
    lv_label_set_text(title_, "账单");
    lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 6);
    primary_labels_.push_back(title_);

    week_button_ = lv_button_create(root_);
    lv_obj_set_size(week_button_, kRangeButtonWidth, kRangeButtonHeight);
    lv_obj_align(week_button_, LV_ALIGN_TOP_RIGHT, -12 - kRangeButtonWidth - 4, 4);
    lv_obj_set_style_radius(week_button_, kRangeButtonHeight / 2, 0);
    lv_obj_set_style_border_width(week_button_, 1, 0);
    lv_obj_set_style_shadow_width(week_button_, 0, 0);
    lv_obj_set_style_pad_all(week_button_, 0, 0);
    lv_obj_add_event_cb(week_button_, OnWeekClicked, LV_EVENT_CLICKED, this);
    week_label_ = lv_label_create(week_button_);
    lv_label_set_text(week_label_, "7天");
    lv_obj_center(week_label_);

    month_button_ = lv_button_create(root_);
    lv_obj_set_size(month_button_, kRangeButtonWidth, kRangeButtonHeight);
    lv_obj_align(month_button_, LV_ALIGN_TOP_RIGHT, -12, 4);
    lv_obj_set_style_radius(month_button_, kRangeButtonHeight / 2, 0);
    lv_obj_set_style_border_width(month_button_, 1, 0);
    lv_obj_set_style_shadow_width(month_button_, 0, 0);
    lv_obj_set_style_pad_all(month_button_, 0, 0);
    lv_obj_add_event_cb(month_button_, OnMonthClicked, LV_EVENT_CLICKED, this);
    month_label_ = lv_label_create(month_button_);
    lv_label_set_text(month_label_, "30天");
    lv_obj_center(month_label_);
}

void DeepSeekBillUi::CreateChartCard(int y, const char* title, lv_obj_t** title_out,
                                     lv_obj_t** summary_out, lv_obj_t** start_date_out,
                                     lv_obj_t** end_date_out, lv_obj_t** chart_out,
                                     lv_chart_series_t** series_out) {
    lv_obj_t* card = lv_obj_create(root_);
    lv_obj_set_pos(card, kMargin, y);
    lv_obj_set_size(card, kCardWidth, kCardHeight);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_shadow_offset_y(card, 3, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    cards_.push_back(card);

    lv_obj_t* title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, 10, 1);
    *title_out = title_label;

    lv_obj_t* summary = lv_label_create(card);
    lv_label_set_text(summary, "--");
    lv_obj_set_style_text_font(summary, &font_noto_sans_basic_14_1, 0);
    lv_obj_align(summary, LV_ALIGN_TOP_RIGHT, -10, 7);
    lv_obj_set_style_text_align(summary, LV_TEXT_ALIGN_RIGHT, 0);
    *summary_out = summary;

    lv_obj_t* chart = lv_chart_create(card);
    lv_obj_set_pos(chart, 8, 28);
    lv_obj_set_size(chart, kChartWidth, kChartHeight);
    StyleChart(chart);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 7);
    lv_chart_set_div_line_count(chart, 2, 0);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    *chart_out = chart;
    *series_out = lv_chart_add_series(chart, lv_color_white(), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t* start_date = lv_label_create(card);
    lv_label_set_text(start_date, "--");
    lv_obj_set_style_text_font(start_date, &font_noto_sans_basic_14_1, 0);
    lv_obj_set_pos(start_date, 10, 68);
    *start_date_out = start_date;

    lv_obj_t* end_date = lv_label_create(card);
    lv_label_set_text(end_date, "--");
    lv_obj_set_style_text_font(end_date, &font_noto_sans_basic_14_1, 0);
    lv_obj_align(end_date, LV_ALIGN_TOP_RIGHT, -10, 68);
    lv_obj_set_style_text_align(end_date, LV_TEXT_ALIGN_RIGHT, 0);
    *end_date_out = end_date;

    muted_labels_.push_back(start_date);
    muted_labels_.push_back(end_date);
}

void DeepSeekBillUi::BuildCards() {
    CreateChartCard(kConsumptionCardY, "消耗", &consumption_title_, &consumption_total_,
                    &consumption_start_date_, &consumption_end_date_, &consumption_chart_,
                    &consumption_series_);
    consumption_card_ = cards_.back();

    CreateChartCard(kBalanceCardY, "余额", &balance_title_, &balance_latest_,
                    &balance_start_date_, &balance_end_date_, &balance_chart_,
                    &balance_series_);
    balance_card_ = cards_.back();
}

void DeepSeekBillUi::BuildEmptyState() {
    empty_box_ = lv_obj_create(root_);
    lv_obj_set_size(empty_box_, kCardWidth, 150);
    lv_obj_align(empty_box_, LV_ALIGN_CENTER, 0, 16);
    lv_obj_set_style_bg_opa(empty_box_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(empty_box_, 0, 0);
    lv_obj_set_style_pad_all(empty_box_, 0, 0);
    lv_obj_set_style_shadow_width(empty_box_, 0, 0);
    lv_obj_remove_flag(empty_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(empty_box_, LV_OBJ_FLAG_CLICKABLE);

    empty_icon_ = lv_label_create(empty_box_);
    lv_label_set_text(empty_icon_, MATERIAL_SYMBOLS_CALENDAR_MONTH);
    lv_obj_set_style_text_font(empty_icon_, CatUiIconFont(display_), 0);
    lv_obj_align(empty_icon_, LV_ALIGN_TOP_MID, 0, 8);
    muted_labels_.push_back(empty_icon_);

    empty_title_ = lv_label_create(empty_box_);
    lv_label_set_text(empty_title_, "暂无账单数据");
    lv_obj_set_style_text_align(empty_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(empty_title_, 260);
    lv_obj_align(empty_title_, LV_ALIGN_TOP_MID, 0, 44);
    primary_labels_.push_back(empty_title_);

    empty_hint_ = lv_label_create(empty_box_);
    lv_label_set_text(empty_hint_, "配置 Token 后，后台每 10 分钟记录一次余额");
    lv_obj_set_style_text_align(empty_hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(empty_hint_, 260);
    lv_label_set_long_mode(empty_hint_, LV_LABEL_LONG_WRAP);
    lv_obj_align(empty_hint_, LV_ALIGN_TOP_MID, 0, 78);
    muted_labels_.push_back(empty_hint_);
}

void DeepSeekBillUi::Show() {
    EnsureBuilt();
    ApplyPalette();
    Reload();
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
}

void DeepSeekBillUi::Hide() {
    if (root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool DeepSeekBillUi::IsVisible() const {
    return root_ != nullptr && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void DeepSeekBillUi::RefreshTheme() {
    if (root_ == nullptr) {
        return;
    }
    ApplyPalette();
    Reload();
}

void DeepSeekBillUi::ApplyPalette() {
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

    lv_obj_set_style_text_color(consumption_title_, palette_.accent, 0);
    lv_obj_set_style_text_color(balance_title_, palette_.success, 0);
    lv_obj_set_style_text_color(consumption_total_, palette_.text, 0);
    lv_obj_set_style_text_color(balance_latest_, palette_.text, 0);

    auto style_chart = [this](lv_obj_t* chart, lv_chart_series_t* series, lv_color_t color) {
        if (chart == nullptr || series == nullptr) {
            return;
        }
        lv_obj_set_style_line_color(chart, palette_.card_border, LV_PART_MAIN);
        lv_obj_set_style_bg_color(chart, color, LV_PART_INDICATOR);
        lv_chart_set_series_color(chart, series, color);
        lv_chart_refresh(chart);
    };
    style_chart(consumption_chart_, consumption_series_, palette_.accent);
    style_chart(balance_chart_, balance_series_, palette_.success);

    lv_obj_set_style_text_font(week_label_, CatUiTextFont(display_), 0);
    lv_obj_set_style_text_font(month_label_, CatUiTextFont(display_), 0);
    UpdateRangeButtons();
}

void DeepSeekBillUi::UpdateRangeButtons() {
    auto style = [this](lv_obj_t* button, lv_obj_t* label, bool selected) {
        if (button == nullptr || label == nullptr) {
            return;
        }
        lv_obj_set_style_bg_color(button, selected ? palette_.accent : palette_.card, 0);
        lv_obj_set_style_border_color(button, selected ? palette_.accent : palette_.card_border,
                                      0);
        lv_obj_set_style_bg_color(
            button, selected ? lv_color_darken(palette_.accent, 15) : palette_.key_pressed,
            LV_STATE_PRESSED);
        lv_obj_set_style_text_color(label, selected ? lv_color_white() : palette_.muted, 0);
    };
    style(week_button_, week_label_, range_ == Range::kWeek);
    style(month_button_, month_label_, range_ == Range::kMonth);
}

void DeepSeekBillUi::SetRange(Range range) {
    // 再次点击当前范围也重新读取一次，相当于一个不占位置的刷新按钮。
    range_ = range;
    Reload();
}

void DeepSeekBillUi::Reload() {
    const int days = range_ == Range::kWeek ? 7 : 30;
    records_ = balance_history::GetRecent(days);

    has_data_ = false;
    currency_.clear();
    for (const auto& record : records_) {
        if (record.valid) {
            has_data_ = true;
            if (!record.currency.empty()) {
                currency_ = record.currency;
            }
        }
    }

    if (!has_data_) {
        // 只隐藏两张图表卡，返回按钮 / 标题 / 范围切换必须一直可用。
        if (consumption_card_ != nullptr) {
            lv_obj_add_flag(consumption_card_, LV_OBJ_FLAG_HIDDEN);
        }
        if (balance_card_ != nullptr) {
            lv_obj_add_flag(balance_card_, LV_OBJ_FLAG_HIDDEN);
        }
        if (empty_box_ != nullptr) {
            lv_obj_remove_flag(empty_box_, LV_OBJ_FLAG_HIDDEN);
        }
        UpdateRangeButtons();
        return;
    }

    if (consumption_card_ != nullptr) {
        lv_obj_remove_flag(consumption_card_, LV_OBJ_FLAG_HIDDEN);
    }
    if (balance_card_ != nullptr) {
        lv_obj_remove_flag(balance_card_, LV_OBJ_FLAG_HIDDEN);
    }
    if (empty_box_ != nullptr) {
        lv_obj_add_flag(empty_box_, LV_OBJ_FLAG_HIDDEN);
    }
    UpdateCharts();
    UpdateRangeButtons();
}

void DeepSeekBillUi::UpdateCharts() {
    UpdateConsumptionChart();
    UpdateBalanceChart();

    const std::string first = records_.empty() ? std::string() : ShortDate(records_.front().date);
    const std::string last = records_.empty() ? std::string() : ShortDate(records_.back().date);
    lv_label_set_text(consumption_start_date_, first.c_str());
    lv_label_set_text(consumption_end_date_, last.c_str());
    lv_label_set_text(balance_start_date_, first.c_str());
    lv_label_set_text(balance_end_date_, last.c_str());
}

void DeepSeekBillUi::UpdateConsumptionChart() {
    if (consumption_chart_ == nullptr || consumption_series_ == nullptr) {
        return;
    }

    std::vector<int32_t> values;
    values.reserve(records_.size());
    int32_t max_cents = 0;
    double total = 0.0;
    bool has_any = false;
    for (const auto& record : records_) {
        if (!record.valid) {
            values.push_back(LV_CHART_POINT_NONE);
            continue;
        }
        const double consumption = record.Consumption();
        const int32_t cents = ToCents(consumption);
        values.push_back(cents);
        max_cents = std::max(max_cents, cents);
        total += consumption;
        has_any = true;
    }

    // LVGL 的折线图按 point_cnt - 1 计算 x 坐标，少于两个点会除零。
    if (values.size() < 2) {
        return;
    }
    lv_chart_set_point_count(consumption_chart_, values.size());
    const int32_t axis_max =
        std::max<int32_t>(100, static_cast<int32_t>(std::ceil(max_cents * 1.15)));
    lv_chart_set_axis_range(consumption_chart_, LV_CHART_AXIS_PRIMARY_Y, 0, axis_max);
    lv_chart_set_series_values(consumption_chart_, consumption_series_, values.data(),
                               values.size());
    lv_chart_refresh(consumption_chart_);

    lv_label_set_text(consumption_total_, has_any ? Money(total, currency_).c_str() : "--");
}

void DeepSeekBillUi::UpdateBalanceChart() {
    if (balance_chart_ == nullptr || balance_series_ == nullptr) {
        return;
    }

    std::vector<int32_t> values;
    values.reserve(records_.size());
    int32_t min_cents = std::numeric_limits<int32_t>::max();
    int32_t max_cents = std::numeric_limits<int32_t>::min();
    double latest = 0.0;
    bool has_any = false;
    for (const auto& record : records_) {
        if (!record.valid) {
            values.push_back(LV_CHART_POINT_NONE);
            continue;
        }
        const int32_t cents = ToCents(record.last_total);
        values.push_back(cents);
        min_cents = std::min(min_cents, cents);
        max_cents = std::max(max_cents, cents);
        latest = record.last_total;
        has_any = true;
    }

    if (values.size() < 2 || !has_any) {
        return;
    }
    lv_chart_set_point_count(balance_chart_, values.size());

    if (max_cents == min_cents) {
        const int32_t pad = 100;
        min_cents = std::max<int32_t>(0, min_cents - pad);
        max_cents += pad;
    } else {
        const int32_t pad = std::max<int32_t>(50, (max_cents - min_cents) / 10);
        min_cents = std::max<int32_t>(0, min_cents - pad);
        max_cents += pad;
    }
    if (max_cents <= min_cents) {
        max_cents = min_cents + 1;
    }
    lv_chart_set_axis_range(balance_chart_, LV_CHART_AXIS_PRIMARY_Y, min_cents, max_cents);
    lv_chart_set_series_values(balance_chart_, balance_series_, values.data(), values.size());
    lv_chart_refresh(balance_chart_);

    lv_label_set_text(balance_latest_, Money(latest, currency_).c_str());
}

void DeepSeekBillUi::OnWeekClicked(lv_event_t* event) {
    auto* self = static_cast<DeepSeekBillUi*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->SetRange(Range::kWeek);
    }
}

void DeepSeekBillUi::OnMonthClicked(lv_event_t* event) {
    auto* self = static_cast<DeepSeekBillUi*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->SetRange(Range::kMonth);
    }
}

void DeepSeekBillUi::OnBackClicked(lv_event_t* event) {
    auto* self = static_cast<DeepSeekBillUi*>(lv_event_get_user_data(event));
    if (self != nullptr && self->callbacks_.on_close) {
        self->callbacks_.on_close();
    }
}
