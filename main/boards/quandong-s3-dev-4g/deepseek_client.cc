#include "deepseek_client.h"

#include "board.h"
#include "settings.h"

#include <esp_log.h>
#include <cJSON.h>
#include <http.h>

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <mutex>

#define TAG "DeepSeek"

namespace deepseek {
namespace {

constexpr const char* kBalanceUrl = "https://api.deepseek.com/user/balance";
// 和仓库里其它联网工具一样用连接号 3；Wi-Fi / ML307 两条链路都支持。
constexpr int kHttpConnectId = 3;
constexpr int kHttpTimeoutMs = 15000;
constexpr size_t kMaxTokenLength = 128;
constexpr const char* kTokenNamespace = "deepseek";
constexpr const char* kTokenKey = "token";

std::string Trim(const std::string& value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool StartsWithBearer(const std::string& value) {
    static constexpr char kPrefix[] = "bearer ";
    if (value.size() < sizeof(kPrefix) - 1) {
        return false;
    }
    for (size_t i = 0; i < sizeof(kPrefix) - 1; ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) != kPrefix[i]) {
            return false;
        }
    }
    return true;
}

std::string NormalizeToken(const std::string& raw) {
    std::string token = Trim(raw);
    if (StartsWithBearer(token)) {
        token = Trim(token.substr(7));
    }
    return token;
}

bool IsPrintableAscii(const std::string& token) {
    for (unsigned char ch : token) {
        if (ch < 0x21 || ch > 0x7E) {
            return false;
        }
    }
    return true;
}

std::string GetStringField(const cJSON* object, const char* key) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return {};
    }
    return item->valuestring;
}

// 今日消耗基准：只存在 RAM，重启后由开机后的后台请求重新建立。
std::mutex g_baseline_mutex;
bool g_has_baseline = false;
double g_baseline_total = 0.0;
std::string g_baseline_currency;

// 后台开机请求和余额页可能同时发起请求，串行化避免抢同一个 HTTP 连接号。
std::mutex g_fetch_mutex;

}  // namespace

bool ParseAmount(const std::string& text, double& value) {
    if (text.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool GetBaseline(double& total, std::string& currency) {
    std::lock_guard<std::mutex> lock(g_baseline_mutex);
    if (!g_has_baseline) {
        return false;
    }
    total = g_baseline_total;
    currency = g_baseline_currency;
    return true;
}

void SetBaselineIfUnset(double total, const std::string& currency) {
    std::lock_guard<std::mutex> lock(g_baseline_mutex);
    if (g_has_baseline) {
        return;
    }
    g_baseline_total = total;
    g_baseline_currency = currency;
    g_has_baseline = true;
    ESP_LOGI(TAG, "今日消耗基准已记录: %.2f %s", total, currency.c_str());
}

void ResetBaseline() {
    std::lock_guard<std::mutex> lock(g_baseline_mutex);
    g_has_baseline = false;
    g_baseline_total = 0.0;
    g_baseline_currency.clear();
}

std::string LoadToken() {
    Settings settings(kTokenNamespace, false);
    return NormalizeToken(settings.GetString(kTokenKey));
}

bool SaveToken(const std::string& token, std::string& error) {
    const std::string normalized = NormalizeToken(token);
    if (normalized.empty()) {
        error = "Token 不能为空";
        return false;
    }
    if (normalized.size() > kMaxTokenLength) {
        error = "Token 过长";
        return false;
    }
    if (!IsPrintableAscii(normalized)) {
        error = "Token 只能包含可见 ASCII 字符";
        return false;
    }

    Settings settings(kTokenNamespace, true);
    settings.SetString(kTokenKey, normalized);
    ESP_LOGI(TAG, "DeepSeek Token 已保存 (%u 字符)", static_cast<unsigned>(normalized.size()));
    return true;
}

void ClearToken() {
    Settings settings(kTokenNamespace, true);
    // 写空串而不是 EraseKey：Settings::SetString 会置 dirty_，析构时一定会提交 NVS。
    settings.SetString(kTokenKey, "");
    ESP_LOGI(TAG, "DeepSeek Token 已清除");
}

bool FetchBalance(const std::string& token, BalanceInfo& info, std::string& error) {
    std::lock_guard<std::mutex> lock(g_fetch_mutex);
    info = {};
    if (token.empty()) {
        error = "未设置 Token";
        return false;
    }
    if (!IsPrintableAscii(token)) {
        error = "Token 格式无效";
        return false;
    }

    auto* network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        error = "网络未就绪";
        return false;
    }

    auto http = network->CreateHttp(kHttpConnectId);
    if (http == nullptr) {
        error = "无法创建网络连接";
        return false;
    }

    http->SetTimeout(kHttpTimeoutMs);
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Authorization", "Bearer " + token);

    if (auto opened = http->Open("GET", kBalanceUrl); !opened) {
        ESP_LOGW(TAG, "打开余额接口失败: %s", opened.error().ToString().c_str());
        error = "网络连接失败";
        http->Close();
        return false;
    }

    auto status = http->GetStatusCode();
    if (!status) {
        ESP_LOGW(TAG, "读取 HTTP 状态失败: %s", status.error().ToString().c_str());
        error = "读取请求状态失败";
        http->Close();
        return false;
    }

    const std::string body = http->ReadAll();
    http->Close();

    if (*status != 200) {
        ESP_LOGW(TAG, "余额接口返回 HTTP %d: %s", *status, body.c_str());
        if (*status == 401) {
            error = "Token 无效或已过期";
        } else {
            error = "请求失败 (HTTP " + std::to_string(*status) + ")";
        }
        return false;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "余额接口返回的不是合法 JSON");
        error = "返回数据格式错误";
        return false;
    }

    const cJSON* available = cJSON_GetObjectItemCaseSensitive(root, "is_available");
    info.is_available = cJSON_IsTrue(available);

    const cJSON* balances = cJSON_GetObjectItemCaseSensitive(root, "balance_infos");
    if (!cJSON_IsArray(balances)) {
        cJSON_Delete(root);
        error = "返回数据缺少余额信息";
        return false;
    }

    const int count = cJSON_GetArraySize(balances);
    for (int i = 0; i < count; ++i) {
        const cJSON* item = cJSON_GetArrayItem(balances, i);
        if (!cJSON_IsObject(item)) {
            continue;
        }
        BalanceEntry entry;
        entry.currency = GetStringField(item, "currency");
        entry.total_balance = GetStringField(item, "total_balance");
        entry.granted_balance = GetStringField(item, "granted_balance");
        entry.topped_up_balance = GetStringField(item, "topped_up_balance");
        if (entry.currency.empty() && entry.total_balance.empty()) {
            continue;
        }
        info.entries.push_back(std::move(entry));
    }
    cJSON_Delete(root);

    if (info.entries.empty()) {
        error = "未返回余额信息";
        return false;
    }

    ESP_LOGI(TAG, "余额查询成功: available=%d, entries=%u", info.is_available,
             static_cast<unsigned>(info.entries.size()));
    return true;
}

bool FetchAndStoreBaseline(const std::string& token, std::string& error) {
    BalanceInfo info;
    if (!FetchBalance(token, info, error)) {
        return false;
    }
    if (info.entries.empty()) {
        error = "未返回余额信息";
        return false;
    }
    const BalanceEntry& entry = info.entries.front();
    double total = 0.0;
    if (!ParseAmount(entry.total_balance, total)) {
        error = "总余额格式错误";
        return false;
    }
    SetBaselineIfUnset(total, entry.currency);
    return true;
}

}  // namespace deepseek
