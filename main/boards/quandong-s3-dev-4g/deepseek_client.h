#ifndef QUANDONG_S3_DEV_4G_DEEPSEEK_CLIENT_H_
#define QUANDONG_S3_DEV_4G_DEEPSEEK_CLIENT_H_

#include <cstdint>
#include <string>
#include <vector>

// DeepSeek 余额查询。只被 quandong-s3-dev-4g 使用：
//   * Token 存在 NVS 的 "deepseek" 命名空间，key 为 "token"；
//   * 查询接口是 https://api.deepseek.com/user/balance，Wi-Fi 和 4G
//     两条链路都通过 Board::GetNetwork()->CreateHttp() 发出去。
namespace deepseek {

struct BalanceEntry {
    std::string currency;         // CNY / USD
    std::string total_balance;    // 赠金 + 充值
    std::string granted_balance;  // 未过期赠金
    std::string topped_up_balance;
};

struct BalanceInfo {
    bool is_available = false;
    std::vector<BalanceEntry> entries;
};

// 空字符串表示还没配置过。
std::string LoadToken();

// 去掉首尾空白和误粘贴进来的 "Bearer " 前缀后写入 NVS。
// 失败时 error 是可直接显示在界面上的中文文案。
bool SaveToken(const std::string& token, std::string& error);

// 用写入空串的方式清除，确保 Settings 的析构会提交 NVS。
void ClearToken();

// 同步请求，最长阻塞约 15 秒；调用方必须放在独立任务里，不能占着 LVGL 或主任务。
// 内部串行化，后台开机请求和余额页的请求不会同时占用同一个 HTTP 连接号。
bool FetchBalance(const std::string& token, BalanceInfo& info, std::string& error);

// 解析接口返回的金额字符串（如 "110.00"）。
bool ParseAmount(const std::string& text, double& value);

// 今日消耗的基准总额，连同写入时的 Unix 时间戳一起持久化到 NVS。
// 开机后台任务根据 8 小时有效期决定沿用还是重新获取；运行中不更新。
struct BaselineInfo {
    double total = 0.0;
    std::string currency;
    int64_t timestamp = 0;
};

bool GetBaselineInfo(BaselineInfo& info);
bool GetBaseline(double& total, std::string& currency);
void SetBaselineIfUnset(double total, const std::string& currency);
void SetBaseline(double total, const std::string& currency, int64_t timestamp);
// 检测到充值（余额变多）时，用当前总额覆盖基准并刷新时间戳。
void SetBaselineNow(double total, const std::string& currency);
void ResetBaseline();

// 请求一次余额并把总余额写入基准。overwrite=false 时仅在无基准时写入。
bool FetchAndStoreBaseline(const std::string& token, int64_t timestamp, std::string& error,
                           bool overwrite);

}  // namespace deepseek

#endif  // QUANDONG_S3_DEV_4G_DEEPSEEK_CLIENT_H_
