#ifndef QUANDONG_S3_DEV_4G_DEEPSEEK_BALANCE_UI_H_
#define QUANDONG_S3_DEV_4G_DEEPSEEK_BALANCE_UI_H_

#include "cat_ui_theme.h"
#include "deepseek_client.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// 全屏余额页。HTTP 请求跑在独立任务里，LVGL 只通过一个 lv_timer 取结果，
// 所以网络慢 / 断网都不会卡住触摸和猫脸刷新。
class DeepSeekBalanceUi {
public:
    struct Callbacks {
        std::function<std::string()> load_token;
        std::function<bool(const std::string& token, deepseek::BalanceInfo& info,
                           std::string& error)>
            fetch;
        std::function<void()> on_close;          // 返回猫脸
        std::function<void()> on_open_settings;  // Token 未配置时的“去设置”
    };

    DeepSeekBalanceUi(Display* display, Callbacks callbacks);
    ~DeepSeekBalanceUi();

    DeepSeekBalanceUi(const DeepSeekBalanceUi&) = delete;
    DeepSeekBalanceUi& operator=(const DeepSeekBalanceUi&) = delete;

    void Show();
    void Hide();
    bool IsVisible() const;
    void RefreshTheme();

private:
    enum class State { kIdle, kLoading, kOk, kError };

    struct Update {
        uint32_t revision = 0;
        State state = State::kIdle;
        deepseek::BalanceInfo info;
        std::string error;
        bool needs_token = false;
        int64_t fetched_at_us = 0;
    };

    void EnsureBuilt();
    void BuildHeader();
    void BuildHero();
    void BuildTiles();
    void BuildFooter();
    void ApplyPalette();
    void ApplyUpdate(const Update& update);
    void ApplyIdleState();
    void ApplyStatus(State state, bool available);
    void UpdateAgeText();
    void UpdateConsumptionAmount();
    void SetTokenLinkVisible(bool visible);

    void StartWorker();
    void WorkerLoop();
    void PublishLoading();
    void PublishResult(bool ok, const deepseek::BalanceInfo& info, const std::string& error,
                       bool needs_token);
    static void WorkerEntry(void* arg);
    static void OnUiTimer(lv_timer_t* timer);

    static void OnBackClicked(lv_event_t* event);
    static void OnRefreshClicked(lv_event_t* event);
    static void OnSettingsLinkClicked(lv_event_t* event);

    static std::string FormatAge(int64_t age_us);

    Display* display_ = nullptr;
    Callbacks callbacks_;
    CatUiPalette palette_;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* back_icon_ = nullptr;
    lv_obj_t* refresh_icon_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* hero_ = nullptr;
    lv_obj_t* caption_ = nullptr;
    lv_obj_t* status_pill_ = nullptr;
    lv_obj_t* status_icon_ = nullptr;
    lv_obj_t* status_text_ = nullptr;
    lv_obj_t* amount_ = nullptr;
    lv_obj_t* currency_ = nullptr;
    lv_obj_t* amount_row_ = nullptr;

    lv_obj_t* granted_value_ = nullptr;
    lv_obj_t* topped_value_ = nullptr;
    lv_obj_t* total_value_ = nullptr;

    lv_obj_t* footer_ = nullptr;
    lv_obj_t* settings_link_ = nullptr;
    lv_timer_t* ui_timer_ = nullptr;

    std::atomic<bool> visible_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> refresh_requested_{false};
    std::atomic<bool> worker_running_{false};

    std::mutex update_mutex_;
    Update pending_;
    bool has_pending_ = false;
    uint32_t applied_revision_ = 0;

    State current_state_ = State::kIdle;
    bool has_data_ = false;
    bool is_available_ = false;
    bool needs_token_ = false;
    int64_t last_success_us_ = 0;
    std::string last_age_text_;

    // “今日消耗”的基准由 deepseek_client 的进程级存储维护，
    // 开机后台请求先写入；余额页首次成功请求作为兜底。
    bool has_consumption_ = false;
    double today_consumption_ = 0.0;
    std::string consumption_currency_;

    std::vector<lv_obj_t*> cards_;
    std::vector<lv_obj_t*> primary_labels_;
    std::vector<lv_obj_t*> muted_labels_;
};

#endif  // QUANDONG_S3_DEV_4G_DEEPSEEK_BALANCE_UI_H_
