#ifndef QUANDONG_S3_DEV_4G_DEEPSEEK_BILL_UI_H_
#define QUANDONG_S3_DEV_4G_DEEPSEEK_BILL_UI_H_

#include "balance_history.h"
#include "cat_ui_theme.h"

#include <functional>
#include <string>
#include <vector>

// 全屏余额账单页：最近 7 天 / 30 天的消耗与余额变化折线图。
// 数据来自 balance_history 的每日记录，只在打开页面时读取 NVS，
// 不发起任何网络请求。
class DeepSeekBillUi {
public:
    struct Callbacks {
        std::function<void()> on_close;  // 返回今日消耗页（或在无来源页时回猫脸）
    };

    DeepSeekBillUi(Display* display, Callbacks callbacks);
    ~DeepSeekBillUi();

    DeepSeekBillUi(const DeepSeekBillUi&) = delete;
    DeepSeekBillUi& operator=(const DeepSeekBillUi&) = delete;

    void Show();
    void Hide();
    bool IsVisible() const;
    void RefreshTheme();

private:
    enum class Range { kWeek, kMonth };

    void EnsureBuilt();
    void BuildHeader();
    void BuildCards();
    void BuildEmptyState();
    void ApplyPalette();
    void UpdateRangeButtons();
    void Reload();
    void UpdateCharts();
    void UpdateConsumptionChart();
    void UpdateBalanceChart();
    void SetRange(Range range);

    void CreateChartCard(int y, const char* title, lv_obj_t** title_out, lv_obj_t** summary_out,
                         lv_obj_t** start_date_out, lv_obj_t** end_date_out, lv_obj_t** chart_out,
                         lv_chart_series_t** series_out);

    static void OnWeekClicked(lv_event_t* event);
    static void OnMonthClicked(lv_event_t* event);
    static void OnBackClicked(lv_event_t* event);

    Display* display_ = nullptr;
    Callbacks callbacks_;
    CatUiPalette palette_;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* back_icon_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* week_button_ = nullptr;
    lv_obj_t* month_button_ = nullptr;
    lv_obj_t* week_label_ = nullptr;
    lv_obj_t* month_label_ = nullptr;

    lv_obj_t* consumption_card_ = nullptr;
    lv_obj_t* consumption_title_ = nullptr;
    lv_obj_t* consumption_total_ = nullptr;
    lv_obj_t* consumption_start_date_ = nullptr;
    lv_obj_t* consumption_end_date_ = nullptr;
    lv_obj_t* consumption_chart_ = nullptr;
    lv_chart_series_t* consumption_series_ = nullptr;

    lv_obj_t* balance_card_ = nullptr;
    lv_obj_t* balance_title_ = nullptr;
    lv_obj_t* balance_latest_ = nullptr;
    lv_obj_t* balance_start_date_ = nullptr;
    lv_obj_t* balance_end_date_ = nullptr;
    lv_obj_t* balance_chart_ = nullptr;
    lv_chart_series_t* balance_series_ = nullptr;

    lv_obj_t* empty_box_ = nullptr;
    lv_obj_t* empty_icon_ = nullptr;
    lv_obj_t* empty_title_ = nullptr;
    lv_obj_t* empty_hint_ = nullptr;

    Range range_ = Range::kWeek;
    std::vector<balance_history::DailyRecord> records_;
    std::string currency_;
    bool has_data_ = false;

    std::vector<lv_obj_t*> cards_;
    std::vector<lv_obj_t*> primary_labels_;
    std::vector<lv_obj_t*> muted_labels_;
};

#endif  // QUANDONG_S3_DEV_4G_DEEPSEEK_BILL_UI_H_
