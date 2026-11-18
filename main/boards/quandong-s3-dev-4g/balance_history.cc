#include "balance_history.h"

#include "settings.h"

#include <esp_log.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <mutex>
#include <utility>

#define TAG "BalanceHistory"

namespace balance_history {
namespace {

constexpr const char* kNamespace = "deepseek";
constexpr const char* kIndexKey = "hist_idx";
constexpr char kRecordPrefix = 'd';
constexpr int kMaxRecords = 35;
constexpr int kMaxRecentDays = 31;
constexpr int64_t kMinValidTimestamp = 1735689600;  // 2025-01-01 UTC
constexpr double kTopUpEpsilon = 0.005;
// 余额接口的正常数量级远小于此；上限同时防止异常数据撑爆序列化缓冲区。
constexpr double kMaxAmount = 1e9;
constexpr int64_t kUnknownAge = std::numeric_limits<int64_t>::max() / 4;

std::mutex g_mutex;
bool g_loaded = false;
bool g_dirty = false;
DailyRecord g_today;

bool ParseTimestamp(const std::string& text, int64_t& value) {
    if (text.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || parsed <= 0) {
        return false;
    }
    value = static_cast<int64_t>(parsed);
    return true;
}

bool IsValidDateKey(const std::string& date) {
    if (date.size() != 8) {
        return false;
    }
    for (char ch : date) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return std::atoi(date.substr(0, 4).c_str()) >= 2025;
}

std::string FormatLocalDate(int64_t timestamp) {
    if (timestamp <= 0) {
        return {};
    }
    const time_t value = static_cast<time_t>(timestamp);
    struct tm local = {};
    if (localtime_r(&value, &local) == nullptr) {
        return {};
    }
    const int year = local.tm_year + 1900;
    if (year < 2000 || year > 2100) {
        return {};
    }
    char date[32];
    std::snprintf(date, sizeof(date), "%04d%02d%02d", year, local.tm_mon + 1, local.tm_mday);
    return date;
}

std::string DateKey(int64_t timestamp) {
    if (timestamp < kMinValidTimestamp) {
        return {};
    }
    const std::string date = FormatLocalDate(timestamp);
    // 账单只从 2025 年开始记录；更早的日期仍可格式化出来给折线图当空槽位。
    return date < "20250101" ? std::string() : date;
}

std::string RecordKey(const std::string& date) {
    return std::string(1, kRecordPrefix) + date;
}

std::string SafeCurrency(const std::string& currency) {
    std::string result;
    result.reserve(std::min<size_t>(currency.size(), 8));
    for (char ch : currency) {
        if (result.size() >= 8) {
            break;
        }
        // 值格式用逗号分隔，币种里不能出现分隔符。
        result.push_back(ch == ',' ? '_' : ch);
    }
    return result;
}

void SplitCsv(const std::string& text, std::vector<std::string>& out) {
    out.clear();
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t end = text.find(',', begin);
        const size_t length = (end == std::string::npos) ? text.size() - begin : end - begin;
        if (length > 0) {
            out.emplace_back(text.substr(begin, length));
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
}

// 账单记录的最后一个字段是币种，允许为空。索引用的 SplitCsv 会跳过空字段，
// 这里必须保留空字段，否则空币种的记录会被误判为损坏。
bool SplitRecordFields(const std::string& text, std::vector<std::string>& out) {
    out.clear();
    size_t begin = 0;
    for (int i = 0; i < 5; ++i) {
        const size_t end = text.find(',', begin);
        if (end == std::string::npos) {
            out.clear();
            return false;
        }
        out.emplace_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    out.emplace_back(text.substr(begin));
    return true;
}

std::string JoinCsv(const std::vector<std::string>& values) {
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) {
            result.push_back(',');
        }
        result += value;
    }
    return result;
}

std::string SerializeRecord(const DailyRecord& record) {
    char text[192];
    std::snprintf(text, sizeof(text), "%.2f,%.2f,%.2f,%.2f,%lld,%s", record.start_total,
                  record.start_granted, record.last_total, record.last_granted,
                  static_cast<long long>(record.updated_at), SafeCurrency(record.currency).c_str());
    return text;
}

bool ParseRecord(const std::string& text, const std::string& date, DailyRecord& record) {
    std::vector<std::string> parts;
    if (!SplitRecordFields(text, parts) || parts.size() != 6) {
        return false;
    }

    double start_total = 0.0;
    double start_granted = 0.0;
    double last_total = 0.0;
    double last_granted = 0.0;
    int64_t updated_at = 0;
    if (!deepseek::ParseAmount(parts[0], start_total) ||
        !deepseek::ParseAmount(parts[1], start_granted) ||
        !deepseek::ParseAmount(parts[2], last_total) ||
        !deepseek::ParseAmount(parts[3], last_granted) ||
        !ParseTimestamp(parts[4], updated_at)) {
        return false;
    }
    if (!std::isfinite(start_total) || !std::isfinite(start_granted) ||
        !std::isfinite(last_total) || !std::isfinite(last_granted) || start_total < 0.0 ||
        start_granted < 0.0 || last_total < 0.0 || last_granted < 0.0 ||
        start_total > kMaxAmount || start_granted > kMaxAmount || last_total > kMaxAmount ||
        last_granted > kMaxAmount || updated_at < kMinValidTimestamp) {
        return false;
    }

    record.date = date;
    record.currency = SafeCurrency(parts[5]);
    record.start_total = start_total;
    record.start_granted = start_granted;
    record.last_total = last_total;
    record.last_granted = last_granted;
    record.updated_at = updated_at;
    record.valid = true;
    return true;
}

bool LoadRecord(const std::string& date, DailyRecord& record) {
    record = {};
    record.date = date;
    if (!IsValidDateKey(date)) {
        return false;
    }
    Settings settings(kNamespace, false);
    const std::string text = settings.GetString(RecordKey(date));
    if (text.empty()) {
        return false;
    }
    return ParseRecord(text, date, record);
}

// 把记录写入 NVS，并维护日期索引。索引只保留最近 kMaxRecords 天，
// 被挤出去的记录会一起删掉，避免 NVS 随使用时间无限增长。
void StoreRecord(const DailyRecord& record) {
    Settings settings(kNamespace, true);
    const std::string old_index_text = settings.GetString(kIndexKey);

    std::vector<std::string> old_index;
    SplitCsv(old_index_text, old_index);

    std::vector<std::string> new_index;
    new_index.push_back(record.date);
    for (const auto& date : old_index) {
        if (date == record.date || !IsValidDateKey(date)) {
            continue;
        }
        if (std::find(new_index.begin(), new_index.end(), date) == new_index.end()) {
            new_index.push_back(date);
        }
    }
    // 日期是 YYYYMMDD，按字符串倒序就是时间从新到旧。这样即使补写的是
    // 昨天的记录，也总是清理最旧的那些 key。
    std::sort(new_index.begin(), new_index.end(),
              [](const std::string& lhs, const std::string& rhs) { return lhs > rhs; });

    std::vector<std::string> removed;
    while (new_index.size() > static_cast<size_t>(kMaxRecords)) {
        removed.push_back(new_index.back());
        new_index.pop_back();
    }
    for (const auto& date : removed) {
        settings.EraseKey(RecordKey(date));
        ESP_LOGI(TAG, "清理过期账单: %s", date.c_str());
    }

    const std::string new_index_text = JoinCsv(new_index);
    if (new_index_text != old_index_text) {
        settings.SetString(kIndexKey, new_index_text);
    }
    settings.SetString(RecordKey(record.date), SerializeRecord(record));
}

// 把当前 RAM 记录切换成指定日期。调用方必须持有 g_mutex。
bool LoadTodayLocked(const std::string& date, bool& created) {
    created = false;
    if (g_loaded && g_today.valid && g_dirty) {
        StoreRecord(g_today);
        g_dirty = false;
    }

    DailyRecord loaded;
    if (LoadRecord(date, loaded)) {
        g_today = loaded;
        g_loaded = true;
        return true;
    }

    g_today = {};
    g_today.date = date;
    g_loaded = true;
    created = true;
    return false;
}

}  // namespace

double DailyRecord::Consumption() const {
    if (!valid) {
        return 0.0;
    }
    const double value = start_total - last_total;
    return value > 0.0 ? value : 0.0;
}

bool Update(const deepseek::BalanceEntry& entry, int64_t timestamp) {
    const std::string date = DateKey(timestamp);
    if (date.empty()) {
        return false;
    }

    double total = 0.0;
    if (!deepseek::ParseAmount(entry.total_balance, total) || !std::isfinite(total) ||
        total < 0.0 || total > kMaxAmount) {
        return false;
    }
    double granted = 0.0;
    if (!deepseek::ParseAmount(entry.granted_balance, granted) || !std::isfinite(granted) ||
        granted < 0.0 || granted > kMaxAmount) {
        granted = 0.0;
    }
    const std::string currency = SafeCurrency(entry.currency);

    std::lock_guard<std::mutex> lock(g_mutex);
    bool created = false;
    if (!g_loaded || !g_today.valid || g_today.date != date) {
        LoadTodayLocked(date, created);
    }

    bool repaired = false;
    if (created) {
        g_today.currency = currency;
        g_today.start_total = total;
        g_today.start_granted = granted;
    } else {
        // 记录的时间戳必须落在它所属的日期内。来自未来、或日期对不上（时钟被
        // 改过 / 数据损坏）时，以本次数据重新建立当天基准，避免今日消耗异常。
        if (g_today.updated_at > timestamp + 6 * 3600 ||
            FormatLocalDate(g_today.updated_at) != date) {
            ESP_LOGW(TAG, "账单日期 %s 的时间戳异常，按本次数据修复", date.c_str());
            g_today.start_total = total;
            g_today.start_granted = granted;
            g_today.currency = currency;
            repaired = true;
        }

        if (g_today.currency.empty()) {
            g_today.currency = currency;
        }

        if (!repaired && !currency.empty() && g_today.currency != currency) {
            // 币种变了：旧基准不再有意义，从当前余额重新开始。
            g_today.currency = currency;
            g_today.start_total = total;
            g_today.start_granted = granted;
            repaired = true;
        } else if (!repaired && total > g_today.start_total + kTopUpEpsilon) {
            // 余额变多说明充值/赠金到账：更新基准并把今日消耗清零。
            g_today.start_total = total;
            g_today.start_granted = granted;
            repaired = true;
        }
    }

    g_today.date = date;
    g_today.last_total = total;
    g_today.last_granted = granted;
    g_today.updated_at = timestamp;
    g_today.valid = true;
    g_dirty = true;

    // 当天的第一笔数据、充值和异常修复都属于关键数据，立即落盘；
    // 常规的每分钟变化由后台任务每 10 分钟统一 Flush。
    if (created || repaired) {
        StoreRecord(g_today);
        g_dirty = false;
    }

    if (created) {
        ESP_LOGI(TAG, "建立今日账单: %s 余额 %.2f 赠送 %.2f", date.c_str(), total, granted);
    } else if (repaired) {
        ESP_LOGI(TAG, "今日账单基准已重置: %s 余额 %.2f 赠送 %.2f", date.c_str(), total, granted);
    }
    return true;
}

bool Flush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_loaded || !g_today.valid || !g_dirty) {
        return true;
    }
    StoreRecord(g_today);
    g_dirty = false;
    return true;
}

bool GetToday(DailyRecord& record) {
    const std::string date = DateKey(static_cast<int64_t>(std::time(nullptr)));
    if (date.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_loaded && g_today.valid && g_today.date == date) {
        record = g_today;
        return true;
    }

    bool created = false;
    LoadTodayLocked(date, created);
    if (!g_today.valid) {
        return false;
    }
    record = g_today;
    return true;
}

std::vector<DailyRecord> GetRecent(int days) {
    std::vector<DailyRecord> result;
    if (days <= 0) {
        return result;
    }
    days = std::min(days, kMaxRecentDays);

    const time_t now = std::time(nullptr);
    if (DateKey(static_cast<int64_t>(now)).empty()) {
        return result;
    }

    struct tm local = {};
    if (localtime_r(&now, &local) == nullptr) {
        return result;
    }
    // 用当天中午做锚点，按 24 小时回退，跨夏令时时也不会跳过/重复日期。
    local.tm_hour = 12;
    local.tm_min = 0;
    local.tm_sec = 0;
    const time_t anchor = std::mktime(&local);
    if (anchor == static_cast<time_t>(-1)) {
        return result;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    result.reserve(static_cast<size_t>(days));
    for (int offset = days - 1; offset >= 0; --offset) {
        const int64_t timestamp = static_cast<int64_t>(anchor) - offset * 86400;
        const std::string date = FormatLocalDate(timestamp);
        if (date.empty()) {
            continue;
        }

        DailyRecord record;
        record.date = date;
        if (g_loaded && g_today.valid && g_today.date == date) {
            record = g_today;
        } else {
            LoadRecord(date, record);
            record.date = date;
        }
        result.push_back(std::move(record));
    }
    return result;
}

void ResetAll() {
    std::lock_guard<std::mutex> lock(g_mutex);
    Settings settings(kNamespace, true);
    const std::string index_text = settings.GetString(kIndexKey);

    std::vector<std::string> index;
    SplitCsv(index_text, index);
    for (const auto& date : index) {
        if (IsValidDateKey(date)) {
            settings.EraseKey(RecordKey(date));
        }
    }
    // 旧版本用 base_* 保存 8 小时基准；换成每日账单后一并清掉。
    settings.EraseKey("base_total");
    settings.EraseKey("base_cur");
    settings.EraseKey("base_time");
    // EraseKey 本身不置 dirty，写一次空索引把上面的删除一起提交。
    settings.SetString(kIndexKey, "");

    g_loaded = false;
    g_dirty = false;
    g_today = {};
    ESP_LOGI(TAG, "余额账单历史已清除");
}

int64_t SecondsSinceUpdate() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_loaded || !g_today.valid || g_today.updated_at <= 0) {
        return kUnknownAge;
    }
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    const std::string today = DateKey(now);
    if (today.empty() || g_today.date != today) {
        return kUnknownAge;
    }
    if (g_today.updated_at > now) {
        // 记录来自未来（校时/时钟回拨）：不能当作“刚刚更新”，否则后台任务会
        // 一直跳过采样，直到时钟追上这个时间戳。
        return kUnknownAge;
    }
    return now - g_today.updated_at;
}

}  // namespace balance_history
