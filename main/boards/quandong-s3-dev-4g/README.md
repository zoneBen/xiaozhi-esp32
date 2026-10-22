# quandong-s3-dev-4g

在 [quandong-s3-dev](../quandong-s3-dev/) 基础上的 4G 变体：硬件完全相同（ESP32-S3N16R8 + ILI9341 SPI 屏 + ES8311 音频 + 板载音频功放），额外在 **GPIO43 / GPIO44** 上接了一颗 **ML307R** 4G 模组，支持在 WiFi 和 4G 之间切换。

基础板子保持纯 WiFi 不变，两者板子身份（OTA 上报的 `type`）各自独立。

## 接线

| ESP32-S3 | 方向 | ML307R |
| -------- | ---- | ------ |
| GPIO43 | ESP TX → | 模组 RX |
| GPIO44 | ESP RX ← | 模组 TX |

模组挂在 **UART1** 上（`78/esp-ml307` 组件固定使用 UART1），不占用 GPIO43/44 之外的引脚。本板未接 DTR，`ML307_DTR_PIN` 定义为 `GPIO_NUM_NC`。

> **关于 GPIO43/44 与控制台**：这两个脚是 ESP32-S3 上 UART0 控制台的 IOMUX 默认引脚。模组驱动会把它们改由 GPIO 矩阵路由，从而摘掉该引脚的 IOMUX 外设功能 —— 副作用是**控制台日志不再从 GPIO43 输出**（这正好避免日志乱码灌进模组），日志请从**原生 USB 口**查看。启动瞬间 ROM bootloader 仍可能在 GPIO43 上输出一小段数据，模组会忽略非 AT 字节，属正常现象。

## 网络切换

网络类型存在 NVS（`network` 命名空间，`type` 键：`0` = WiFi，`1` = 4G），首次开机默认 **WiFi**。

**按键操作（BOOT，GPIO0）：**

| 操作 | 状态 | 行为 |
| ---- | ---- | ---- |
| 单击 | 启动中且当前为 WiFi | 进入 WiFi 配网 |
| 单击 | 其他 | 切换对话状态 |
| 双击 | 启动中或配网中 | 在 WiFi ↔ 4G 之间切换（提示后自动重启） |

> 单击有约 180ms 延迟，这是注册了双击回调后 `espressif/button` 用来区分单击/双击的正常行为。

> **双击在 4G 模式下拿不到机会**：它只在 `kDeviceStateStarting` / `kDeviceStateWifiConfiguring`
> 两个状态下生效，而 4G 模式开机后直接进入 4G 联网流程，没有这个窗口 —— 一旦切到 4G 就再也回不去
> WiFi 了。**触摸切换就是为补这个口子加的**，见下一节。

**触摸操作（点左上角网络图标）：**

状态栏左上角的 WiFi / 4G 信号图标在触摸屏上是可点的（热区 72×56，比图标本体大得多）：

1. 点图标 → 弹出「切换到 4G...」/「切换到 Wi-Fi...」确认框（屏下方变暗 + 居中卡片）
2. 点 ✓ → 保存网络类型到 NVS，提示后自动重启；点 ✗ → 关闭，设备继续运行
3. 点遮罩空白处不做任何事（保持弹窗打开，避免误触关掉）

触摸切换在**除固件升级外的任何状态下**都可用（升级过程中点击会被忽略，避免打断固件写入），
所以 4G 模式下不依赖开机窗口也能切回 WiFi。BOOT 单击 / 双击的行为保持原样，是触摸失灵时的兜底。

## 设置页与 DeepSeek 余额

右上角有一个齿轮图标（透明触摸热区 40×40，和左上角网络图标同样处理，避免被
`status_bar_` 抢走点击）。点开后是全屏设置页：

| 项目 | 行为 |
| ---- | ---- |
| DeepSeek Token | 用屏幕键盘输入（`sk-...`，只保留 `a-z`、`0-9`、`-`、`_`），存入 NVS `deepseek` 命名空间的 `token` 键。Token 只用于请求余额接口，不会出现在日志里 |
| 清除配网并重启 | 清空 `SsidManager` 保存的 Wi-Fi、把 `network/type` 强制为 `0`（WiFi）后重启。重启时没有已保存 SSID，会自动进入配网 AP |
| 清除 DeepSeek Token | 删除本机保存的 Token（未设置时该项置灰、不可点） |

余额页通过 MCP function call `self.deepseek.balance` 打开，描述里包含
「查看余额」「查看大模型余额」「查看deepseek信息」「DeepSeek还有多少钱」等触发说法。
页面打开后立即请求一次 `https://api.deepseek.com/user/balance`，之后每 **60 秒** 刷新。
顶部大号显示「今日消耗」（最醒目），下方三栏「赠送 / 充值 / 总额」分别显示对应余额，右上角
状态胶囊显示可用/余额不足；点左上角返回箭头回到猫脸。页面隐藏后停止轮询，不会在后台持续
走流量。HTTP 请求跑在独立任务里（8KB 栈），不阻塞 LVGL 和音频任务。

顶部「今日消耗」按 **本次开机第一次成功请求到的总余额 − 当前总余额** 计算。设备每次联网成功
后（包括开机联网、断线重连）都会在后台请求一次余额并写入基准，**即使没有打开余额页**；如果
后台请求失败，余额页首次成功请求会兜底建立基准。设置页保存/清除 Token 时会重置基准并重新
尝试后台请求。

基准只存在 RAM 里，关掉页面再打开仍然保留，重启后重置。期间充值/赠金导致余额变多时结果为
负数，带负号显示。

Token 未配置或被接口判定为无效时，余额页会显示对应提示和「去设置」按钮，直接跳到设置页的
Token 输入面板。

> **构建注意**：屏幕键盘依赖 `CONFIG_LV_USE_KEYBOARD=y`，已写进本目录的
> `config.json` 的 `sdkconfig_append`。请用 `python scripts/build.py quandong-s3-dev-4g`
> 构建（脚本会 reconfigure 并应用该选项）；直接拿旧 `sdkconfig` 跑 `idf.py build`
> 会因 `lv_keyboard` API 未启用而编译失败。
>
> 新页面的中文文案都在 `78__xiaozhi-fonts` 的 `common` 字符集里。正常构建会把
> `font_noto_sans_common_20_4.bin` 打进 assets 分区；只单独更新 app 分区时，如果设备上
> 的 assets 仍是更老的字体，设置页/余额页可能出现缺字，需要同时烧录 assets 分区。

## 触摸屏

电容触摸屏是 **FT6336G**，挂在 **I2C0** 上（与 ES8311 音频 codec 同一条总线，`i2c_master`
驱动自身串行化同总线访问，无需额外互斥）。

| ESP32-S3 | 方向 | FT6336G |
| -------- | ---- | ------- |
| GPIO16 | SDA | SDA |
| GPIO15 | SCL | SCL |
| GPIO18 | 输出 | RST（低 10ms → 高 100ms） |
| GPIO17 | 输入 | INT（**本方案未使用**，走 LVGL 默认的轮询模式） |

I2C 地址 `0x38`。驱动为板子本地实现（`ft6336_touch.h/.cc`），未用 `esp_lcd_touch_ft5x06` 组件 ——
后者读触摸点数寄存器时没做 `& 0x0F` 掩码，会被 FT6336 的高 4 位事件标志带偏。

触摸屏不存在时（探测 `0xA3` 芯片 ID 失败）只打一条警告日志，不注册 LVGL 输入设备，
界面与之前完全一致，BOOT 双击切换照常可用。

### 触摸坐标校准

屏幕按 **320×240 横屏**使用（`DISPLAY_SWAP_XY=true` / `DISPLAY_MIRROR_Y=true`），
而 FT6336 上报的是**面板原生 240×320 竖屏坐标**；LVGL 不会替驱动旋转输入坐标，
所以 `ft6336_touch.cc` 里的 `MapRawToScreen()` 做了一次 swap / mirror。

**坐标点不准时只改这一个函数的两行**，四个候选（注释里也列了）：

| 候选 | 映射 | 关系 |
| ---- | ---- | ---- |
| A | `x = raw_y; y = raw_x;` | 与 D 相差 180° |
| B | `x = raw_y; y = DISPLAY_HEIGHT - 1 - raw_x;` | 与 C 相差 180° |
| C（当前实测采用） | `x = DISPLAY_WIDTH - 1 - raw_y; y = raw_x;` | 与 B 相差 180° |
| D | `x = DISPLAY_WIDTH - 1 - raw_y; y = DISPLAY_HEIGHT - 1 - raw_x;` | 与 A 相差 180° |

校准方法：从**原生 USB 口**看日志，手指依次点屏幕**左上角**和**右下角**，每次按下会打一条

```
I (12345) Ft6336Touch: raw=(x,y) -> (x,y)  [面板 240x320]
```

点左上角时换算结果应接近 `(0,0)`，点右下角应接近 `(319,239)`。按现象判断：

| 现象 | 结论 |
| ---- | ---- |
| 坐标与手指位置中心对称（摸右下打中左上） | 换成相差 180° 的那个候选（A↔D、B↔C） |
| 只有左右反（摸左边报右边） | 换掉 X 方向的镜像 |
| 只有上下反 | 换掉 Y 方向的镜像 |
| 左右上下量级错乱（x 出现 >239 的竖屏量级） | swap 方向反了，需重新推导 |

本板实测为「**摸右下角才能打中左上角**」，即默认候选 B 处于 180° 反向，已改为 C。

### 弹窗按钮为什么是 ✓ / ✗ 而不是「确定 / 取消」

内置字体的字集是按 App 现有静态字符串生成的（`78__xiaozhi-fonts/charsets/basic.json`）：
`切` `换` `到` `定` 都在，但 **`确` `取` `消` 不在**，写成中文会渲染成空白方块。
`common.json` 里虽然有，但那是运行时下载的资源包字体，不能假定设备上一定存在。
所以弹窗正文复用现成的 `Lang::Strings::SWITCH_TO_4G_NETWORK` / `SWITCH_TO_WIFI_NETWORK`，
按钮用图标字体的 `MATERIAL_SYMBOLS_CHECK` / `MATERIAL_SYMBOLS_CLOSE`（均已确认在内置
`font_material_symbols_20_4` 中），**不需要改动任何字体资源**。

按钮配色用的是**固定色**（确认 = 绿底白 ✓，取消 = 灰底白 ✗），不取主题色 ——
主题里 `border_color` 与 `text_color` 可能同时是黑的，若拿一个当底、另一个当图标色，
两者会撞成同一个颜色导致图标看不见。

## 编译烧录

```bash
python scripts/build.py quandong-s3-dev-4g
```

或使用仓库根目录的一键脚本：

```bat
build.bat quandong-s3-dev-4g
```

menuconfig 中的位置：`Xiaozhi Assistant -> Board Type -> Quandong ESP32-S3 Development Board 4G (全动)`。

## 依赖

`78/esp-ml307` 已在 `main/idf_component.yml` 全局声明，`boards/common/{dual_network_board,ml307_board}.cc` 也已在 `main/CMakeLists.txt` 的 `SOURCES` 中，本板无需额外依赖配置。
