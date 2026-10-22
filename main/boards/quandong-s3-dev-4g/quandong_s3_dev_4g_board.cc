#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/es8311_audio_codec.h"
#include "config.h"
#include "deepseek_client.h"
#include "display/lcd_display.h"
#include "dual_network_board.h"
#include "esp_lcd_ili9341.h"
#include "ft6336_touch.h"
#include "mcp_server.h"
#include "quandong_s3_dev_4g_display.h"
#include "settings.h"

#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ssid_manager.h>
#include <wifi_station.h>

#include <atomic>
#include <memory>

#define TAG "QuandongS3Dev4GBoard"

// ILI9341 厂家自定义初始化序列
static const ili9341_lcd_init_cmd_t vendor_specific_init[] = {
    {0xCF, (uint8_t[]){0x00, 0xC1, 0x30}, 3, 0},
    {0xED, (uint8_t[]){0x64, 0x03, 0x12, 0x81}, 4, 0},
    {0xE8, (uint8_t[]){0x85, 0x00, 0x78}, 3, 0},
    {0xCB, (uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
    {0xF7, (uint8_t[]){0x20}, 1, 0},
    {0xEA, (uint8_t[]){0x00, 0x00}, 2, 0},
    {0xC0, (uint8_t[]){0x13}, 1, 0},
    {0xC1, (uint8_t[]){0x13}, 1, 0},
    {0xC5, (uint8_t[]){0x22, 0x35}, 2, 0},
    {0xC7, (uint8_t[]){0xBD}, 1, 0},
    {0, (uint8_t[]){0}, 0x11, 0},
    {0x36, (uint8_t[]){0x08}, 1, 0},
    {0xB6, (uint8_t[]){0x0A, 0xA2}, 2, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0xF6, (uint8_t[]){0x01, 0x30}, 2, 0},
    {0xB1, (uint8_t[]){0x00, 0x1B}, 2, 0},
    {0xF2, (uint8_t[]){0x00}, 1, 0},
    {0x26, (uint8_t[]){0x01}, 1, 0},
    {0xE0,
     (uint8_t[]){0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A,
                 0x00},
     15, 0},
    {0xE1,
     (uint8_t[]){0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34,
                 0x0F},
     15, 0},
    {0, (uint8_t[]){0}, 0x11, 0},
    {0, (uint8_t[]){0}, 0x29, 0},
};

class QuandongS3Dev4GBoard : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    QuandongS3Dev4GDisplay* display_ = nullptr;
    std::unique_ptr<Ft6336Touch> touch_;  // 触摸屏不存在时保持 nullptr
    NetworkEventCallback app_network_callback_;
    std::atomic<bool> baseline_fetch_running_{false};

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    // 板上音频功放使能引脚，需置为低电平
    void InitializeAudioPaEnable() {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << AUDIO_PA_ENABLE_PIN);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level(AUDIO_PA_ENABLE_PIN, 0);
    }

    void InitializeButtons() {
        // 单击：仅 WiFi 模式下处于启动阶段时进入配网，否则切换对话状态
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            // 必须先判网络类型：4G 模式下 current_board_ 是 Ml307Board 而非 WifiBoard，
            // 直接 static_cast 再调用 EnterWifiConfigMode() 会访问不存在的成员
            if (GetNetworkType() == NetworkType::WIFI &&
                app.GetDeviceState() == kDeviceStateStarting) {
                auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                wifi_board.EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        // 双击：启动中或配网中双击，在 WiFi <-> 4G 之间切换（内部保存到 NVS 后重启）
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting ||
                app.GetDeviceState() == kDeviceStateWifiConfiguring) {
                SwitchNetworkType();
            }
        });
    }

    static const char* NetworkTypeName(NetworkType type) {
        return type == NetworkType::ML307 ? "4G" : "Wi-Fi";
    }

    // 把工具参数归一化成 NetworkType。AI 未必严格按 schema 传 "wifi"/"4g"，
    // 常见同义词一并接受。返回 false 表示参数无法识别
    static bool ParseNetworkType(const std::string& text, NetworkType& type) {
        if (text == "wifi" || text == "wi-fi" || text == "wlan") {
            type = NetworkType::WIFI;
            return true;
        }
        if (text == "4g" || text == "cellular" || text == "lte" || text == "ml307") {
            type = NetworkType::ML307;
            return true;
        }
        return false;
    }

    // 通过对话切换网络。工具回调跑在 Application 主任务上（McpServer::DoToolCall 用
    // Schedule 派发），所以这里只做判定和安排，真正等回复 / 重启都交给独立任务
    ToolResult HandleSetNetworkType(const PropertyList& properties) {
        const std::string& requested = properties["type"].value<std::string>();
        NetworkType target;
        if (!ParseNetworkType(requested, target)) {
            return std::unexpected("Invalid network type: " + requested +
                                   ", expected `wifi` or `4g`");
        }

        // 已经在目标网络上就别重启了，白白断一次线
        if (GetNetworkType() == target) {
            return std::string("Already using ") + NetworkTypeName(target) + ", nothing to switch.";
        }

        // 升级过程中重启会打断固件写入，和触摸切换保持一致
        if (Application::GetInstance().GetDeviceState() == kDeviceStateUpgrading) {
            return std::unexpected("Firmware upgrade in progress, cannot switch network now");
        }

        ScheduleNetworkSwitch();
        ESP_LOGI(TAG, "AI 请求切换到 %s", NetworkTypeName(target));
        return std::string("Switching to ") + NetworkTypeName(target) +
               ", the device will reboot in a few seconds.";
    }

    void ScheduleNetworkSwitch() {
        xTaskCreate(
            [](void* arg) {
                static_cast<QuandongS3Dev4GBoard*>(arg)->WaitForReplyThenSwitch();
                vTaskDelete(nullptr);
            },
            "net_switch", 4096, this, 3, nullptr);
    }

    // SwitchNetworkType() 内部是「提示 + 延时 1s + 重启」，但 AI 的语音回复（"正在切换到 4G"）
    // 是在工具返回之后才由服务端下发的 —— 立刻重启会把这句话掐掉，所以等它播完再切。
    // 只能轮询设备状态：协议层没有「回复播完」的回调，而 kDeviceStateSpeaking 正是播放中。
    void WaitForReplyThenSwitch() {
        auto& app = Application::GetInstance();
        constexpr int kPollMs = 100;
        constexpr int kMinWaitMs = 4000;   // 工具返回到 TTS 出声有秒级延迟，先无条件等一段
        constexpr int kMaxWaitMs = 15000;  // 兜底：回复异常时不能把设备一直挂着

        int waited = 0;
        bool seen_speaking = false;
        while (waited < kMaxWaitMs) {
            const bool speaking = (app.GetDeviceState() == kDeviceStateSpeaking);
            seen_speaking = seen_speaking || speaking;
            // 见过 speaking 且已经结束 → 回复播完；一次都没见过 → 至少等到 kMinWaitMs
            if (seen_speaking ? !speaking : waited >= kMinWaitMs) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(kPollMs));
            waited += kPollMs;
        }

        if (!seen_speaking) {
            ESP_LOGW(TAG, "等待 %dms 未见语音回复，直接切换网络", waited);
        }
        SwitchNetworkType();
    }

    // 自定义 MCP 工具必须注册在板子的 InitializeTools() 里（见 mcp_server.cc 的说明）
    void InitializeTools() {
        McpServer::GetInstance().AddTool(
            "self.network.set_type",
            "Switch the device's network connection between Wi-Fi and 4G (cellular).\n"
            "Only call this when the user explicitly asks to change the network. The device "
            "saves the choice and then reboots to apply it, so the audio channel will be closed "
            "shortly after this tool returns.\n"
            "If you do not know which network is currently in use, call "
            "`self.get_device_status` first and look at `network.type` (`wifi` or `cellular`).\n"
            "Args:\n"
            "  `type`: `wifi` or `4g`.",
            PropertyList({Property("type", kPropertyTypeString)}),
            [this](const PropertyList& properties) -> ToolResult {
                return HandleSetNetworkType(properties);
            });

        McpServer::GetInstance().AddTool(
            "self.deepseek.balance",
            "Open the DeepSeek account balance page on the device screen and keep it refreshed "
            "once per minute.\n"
            "Call this whenever the user asks about DeepSeek balance, credit, remaining money, "
            "or DeepSeek account information. Typical user phrases include: "
            "\"查看余额\", \"查看大模型余额\", \"查看deepseek信息\", \"DeepSeek还有多少钱\", "
            "\"查一下大模型余额\", \"check my DeepSeek balance\".\n"
            "The page stays on screen until the user taps the back arrow. If the API token has "
            "not been configured, the page shows a shortcut to the token settings; tell the user "
            "to open the gear icon > DeepSeek Token.",
            PropertyList(), [this](const PropertyList&) -> ToolResult {
                if (display_ == nullptr) {
                    return std::unexpected("Display is not ready");
                }
                display_->ShowBalancePage();
                return std::string(
                    "The DeepSeek balance page is now open on the screen. It refreshes every 60 "
                    "seconds, and the user can tap the back arrow to return to the cat face. If "
                    "the page reports a missing token, ask the user to open Settings (gear icon) "
                    "and set DeepSeek Token first.");
            });
    }

    // 开机/重连后各尝试一次，把“今日消耗”的基准总额写进 RAM。
    // 已有基准或没有 Token 时直接返回；后台任务自己拿锁串行化 HTTP 请求。
    void StartBalanceBaselineFetch() {
        double total = 0.0;
        std::string currency;
        if (deepseek::GetBaseline(total, currency)) {
            return;
        }
        if (deepseek::LoadToken().empty()) {
            return;
        }
        bool expected = false;
        if (!baseline_fetch_running_.compare_exchange_strong(expected, true)) {
            return;
        }
        if (xTaskCreate(BaselineTaskEntry, "ds_baseline", 8192, this, 2, nullptr) != pdPASS) {
            baseline_fetch_running_.store(false);
            ESP_LOGW(TAG, "创建余额基准任务失败");
        }
    }

    void RunBalanceBaselineFetch() {
        const std::string token = deepseek::LoadToken();
        if (!token.empty()) {
            std::string error;
            if (deepseek::FetchAndStoreBaseline(token, error)) {
                ESP_LOGI(TAG, "余额基准后台获取完成");
            } else {
                ESP_LOGW(TAG, "余额基准后台获取失败: %s", error.c_str());
            }
        }
        baseline_fetch_running_.store(false);
    }

    static void BaselineTaskEntry(void* arg) {
        auto* self = static_cast<QuandongS3Dev4GBoard*>(arg);
        self->RunBalanceBaselineFetch();
        vTaskDelete(nullptr);
    }

    // 设置页 / 余额页只做 UI，清配网和余额查询在板子里落地。
    // clear_provisioning 从 LVGL 事件回调里调起，所以只做 Schedule，
    // 让主任务去清 NVS 和重启，别把 LVGL 任务卡在 Reboot() 的 1 秒延时里。
    void InitializeUiServices() {
        if (display_ == nullptr) {
            return;
        }
        display_->SetUiServices({
            .fetch_balance =
                [](const std::string& token, deepseek::BalanceInfo& info, std::string& error) {
                    return deepseek::FetchBalance(token, info, error);
                },
            .clear_provisioning =
                []() {
                    ESP_LOGI(TAG, "清除配网设置并重启");
                    Application::GetInstance().Schedule([]() {
                        SsidManager::GetInstance().Clear();
                        {
                            Settings settings("network", true);
                            settings.SetInt("type", 0);
                        }
                        Application::GetInstance().Reboot();
                    });
                },
            .on_token_changed =
                [this]() {
                    deepseek::ResetBaseline();
                    StartBalanceBaselineFetch();
                },
        });
    }

    // 触摸屏初始化：复位 + 探测芯片。失败只打日志，不影响其它功能
    void InitializeFt6336Touch() {
        auto touch = std::make_unique<Ft6336Touch>(codec_i2c_bus_, TOUCH_I2C_ADDR, TOUCH_RST_PIN);
        if (!touch->Init()) {
            ESP_LOGW(TAG, "未找到 FT6336 触摸屏，本次启动不带触摸（BOOT 双击仍可切换网络）");
            return;
        }
        touch_ = std::move(touch);
    }

    void RegisterFt6336Indev() {
        if (touch_ == nullptr || display_ == nullptr) {
            return;
        }
        // lv_indev_create() 会改动 LVGL 内部结构，必须持锁调用
        DisplayLockGuard lock(display_);
        if (!lock) {
            ESP_LOGE(TAG, "加锁失败，触摸屏未注册");
            return;
        }
        touch_->RegisterLvglIndev();
    }

    // 触摸屏承担两个交互：左上角网络图标切换 WiFi / 4G，右上角齿轮打开设置页。
    // 业务逻辑都留在板子里，显示层只提供 UI 能力。
    void InitializeNetworkSwitchByTouch() {
        display_->SetNetworkIconTappedHandler([this]() {
            auto& app = Application::GetInstance();
            // 升级过程中重启会打断固件写入，这里直接忽略点击
            if (app.GetDeviceState() == kDeviceStateUpgrading) {
                ESP_LOGW(TAG, "固件升级中，忽略网络切换请求");
                return;
            }
            const bool to_4g = (GetNetworkType() == NetworkType::WIFI);
            display_->ShowConfirmDialog(
                to_4g ? Lang::Strings::SWITCH_TO_4G_NETWORK : Lang::Strings::SWITCH_TO_WIFI_NETWORK,
                // SwitchNetworkType() 内部会提示 + 延时 1s + 重启，不能占着 LVGL 任务跑，
                // 丢回主任务执行（否则那句提示根本来不及绘制出来）
                [this]() {
                    Application::GetInstance().Schedule([this]() { SwitchNetworkType(); });
                });
        });
    }

    void InitializeIli9341Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        const ili9341_vendor_config_t vendor_config = {
            .init_cmds = &vendor_specific_init[0],
            .init_cmds_size = sizeof(vendor_specific_init) / sizeof(ili9341_lcd_init_cmd_t),
        };

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.flags.reset_active_high = 1;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void*)&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new QuandongS3Dev4GDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                              DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                              DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

public:
    // 末位参数为默认网络类型：0 = WiFi 优先，1 = 4G 优先
    QuandongS3Dev4GBoard()
        : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, ML307_DTR_PIN, 0),
          boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeAudioPaEnable();
        // 触摸必须先于显示初始化：I2C 总线已就绪，且要把触摸设备注册进 LVGL
        InitializeFt6336Touch();
        InitializeIli9341Display();
        InitializeUiServices();
        RegisterFt6336Indev();
        InitializeNetworkSwitchByTouch();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->SetBrightness(100);
    }

    // Application 只注册一个网络事件回调；这里包一层，在每次联网成功后
    // 顺便尝试记录一次余额基准，再把事件原样转给 Application。
    virtual void SetNetworkEventCallback(NetworkEventCallback callback) override {
        app_network_callback_ = std::move(callback);
        DualNetworkBoard::SetNetworkEventCallback(
            [this](NetworkEvent event, const std::string& data) {
                if (event == NetworkEvent::Connected) {
                    StartBalanceBaselineFetch();
                }
                if (app_network_callback_) {
                    app_network_callback_(event, data);
                }
            });
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, true, true);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(QuandongS3Dev4GBoard);
