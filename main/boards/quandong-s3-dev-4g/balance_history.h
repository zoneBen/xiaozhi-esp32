#ifndef QUANDONG_S3_DEV_4G_BALANCE_HISTORY_H_
#define QUANDONG_S3_DEV_4G_BALANCE_HISTORY_H_

#include "deepseek_client.h"

#include <cstdint>
#include <string>
#include <vector>

// 每日余额账单。每条记录对应本地日期（YYYYMMDD），保存当天第一次取到的
// 总额 / 赠送额（计算“今日消耗”的基准）以及最近一次取到的值。
//
// 数据存在 NVS 的 "deepseek" 命名空间：
//   * key = "d" + YYYYMMDD，value = 六个逗号分隔字段；
//   * key = "hist_idx"，保存最近 35 个日期，用于清理旧记录。
//
// 当天记录由余额页或后台采样任务更新；正常刷新只改 RAM，由后台任务每
// 10 分钟调用 Flush() 落盘一次。新的一天第一次取值、检测到充值因而重置
// 基准时会立即落盘，避免掉电把这部分关键数据丢掉。
namespace balance_history {

struct DailyRecord {
    std::string date;        // 本地日期 YYYYMMDD
    std::string currency;    // CNY / USD
    double start_total = 0.0;    // 当天第一次取到的总额
    double start_granted = 0.0;  // 当天第一次取到的赠送额
    double last_total = 0.0;     // 最近一次取到的总额
    double last_granted = 0.0;   // 最近一次取到的赠送额
    int64_t updated_at = 0;      // 最近一次更新的 Unix 时间戳
    bool valid = false;

    // 今日消耗 = 当天基准总额 - 最近总额。充值会重置基准，所以不会为负。
    double Consumption() const;
};

// 用一次成功的余额请求更新“今天”的记录。
// timestamp 无效（系统时间还没同步）时返回 false，不写任何数据。
// 同一天重启会沿用当天早上第一次的基准；当天不存在记录时以本次数据建立基准。
bool Update(const deepseek::BalanceEntry& entry, int64_t timestamp);

// 把 RAM 中尚未落盘的当天记录写入 NVS。没有改动时是空操作。
bool Flush();

// 读取今天已落盘（或 RAM 中更新过）的记录。当天还没有数据时返回 false。
bool GetToday(DailyRecord& record);

// 返回最近 days 天的记录，按日期从旧到新排列，定长 days。
// 没有数据的日期也会占一个位置，valid=false，方便折线图留空。
std::vector<DailyRecord> GetRecent(int days);

// 清除全部账单历史。只在 Token / 账号变化时调用。
void ResetAll();

// 距离当天记录最近一次更新经过的秒数。没有记录或时间无效时返回一个很大的值。
int64_t SecondsSinceUpdate();

}  // namespace balance_history

#endif  // QUANDONG_S3_DEV_4G_BALANCE_HISTORY_H_
