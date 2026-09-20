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
