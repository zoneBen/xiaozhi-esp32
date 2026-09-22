#include "quandong_s3_dev_4g_display.h"

#include "lvgl_theme.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <material_symbols.h>

#define TAG "QuandongS3Dev4GDisplay"

namespace {

// 图标本体只有 20px 高，而它所在的 top_bar_ 自身高度也只有 ~28px。
// LVGL 的命中测试只会下探到「父对象 coords 之内」的子对象，所以对 label 调
// lv_obj_set_ext_click_area() 扩出来的热区会被父对象裁掉，按不准。
// 这里改用一块贴在屏幕左上角的透明覆盖层做热区：父对象是整个屏幕，尺寸不受 top_bar_ 限制。
constexpr int kTapAreaWidth = 72;
constexpr int kTapAreaHeight = 56;

// 弹窗尺寸（屏幕 320x240，按钮留够手指可点的面积）
constexpr int kDialogWidth = 264;
constexpr int kButtonWidth = 96;
constexpr int kButtonHeight = 44;

// 按钮配色刻意不取主题色。主题里 border_color 和 text_color 可能同时是黑的，
// 那样「取消按钮的底色」和「✗ 的字色」会撞成同一个颜色，图标直接看不见。
// 这里两个按钮统一用饱和深底 + 白色图标：任何主题下都有对比度，
// 同时绿 / 灰也符合「确认 / 取消」的语义。
constexpr uint32_t kConfirmBgColor = 0x2E7D32;  // Material Green 800
constexpr uint32_t kCancelBgColor = 0x616161;   // Material Grey 700

#if LV_USE_VECTOR_GRAPHIC
// 猫脸画布边长，与 cat_face::kCanvasSize 同值：几何层按 1:1 的 168x168
// 逻辑空间作画，所以画布尺寸必须与它一致，否则整张脸会错位或缩水。
constexpr int kCatCanvasSize = cat_face::kCanvasSize;
// 30fps。与仓库里 LvglGif 的刷新周期一致。
constexpr uint32_t kCatFramePeriodMs = 33;
// 线宽。几何层默认值就是 3.0f，这里显式写出来是为了让它可见可调；
// 颜色不写死，每帧从主题的 text_color 取，换主题就跟着换。
constexpr float kCatLineWidth = 3.0f;
// 单帧 dt 的上限。浅睡期间 LVGL 整体停摆（sleep_timer.cc 里的 lvgl_port_stop），
// 唤醒后第一帧算出来的间隔可能是好几秒，不钳住姿态会直接跳过去。
constexpr int64_t kCatMaxStepUs = 100000;  // 100ms
// 每这么多帧汇总一条渲染耗时日志（约 10 秒一条），用来核对 33ms 的帧预算
constexpr uint32_t kCatStatsFrames = 300;

// ---------------------------------------------------------------- 渲染后端
// 几何层（cat_face.h）只认 cat_face::PathSink，两种实现都放在 cat_face_lvgl.*：
//   kPrimitive —— 用 LVGL 自己的线段 / 三角形图元画，整帧不碰 thorvg。默认。
//   kVector    —— 走 LVGL 矢量路径再交给 thorvg 光栅化，也就是原来那版。
// 默认选 kPrimitive 的原因：kVector 每帧都要新建一次 thorvg 的 sw canvas、画完
// 立刻拆掉，而 thorvg 的 C++ 对象走 IDF 堆、它内部被替换过的 malloc/free 又走
// LVGL 的池，两套分配器在同一条释放路径上相遇 —— 开机第一次画脸就会炸在
// heap_caps_free → find_containing_heap 上（串口逐条路径定位到了第 7 条路径的
// 拆解阶段）。kPrimitive 全程只用 LVGL 的分配器，绕开这个组合。
// 代价只有一处：曲线按 0.5px 容差折线化后再画，其余是精确等价。
enum class CatRenderBackend { kPrimitive, kVector };
constexpr CatRenderBackend kCatBackend = CatRenderBackend::kPrimitive;
#endif

}  // namespace

QuandongS3Dev4GDisplay::QuandongS3Dev4GDisplay(esp_lcd_panel_io_handle_t panel_io,
                                               esp_lcd_panel_handle_t panel, int width, int height,
                                               int offset_x, int offset_y, bool mirror_x,
                                               bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy)
#if LV_USE_VECTOR_GRAPHIC
      ,
      // 用真实熵做种子：每次开机猫的微表情都不一样
      cat_animator_(esp_random())
#endif
{
}

QuandongS3Dev4GDisplay::~QuandongS3Dev4GDisplay() {
#if LV_USE_VECTOR_GRAPHIC
    DestroyCatFace();
#endif
    // 弹窗挂在 lv_layer_top() 上，不在基类析构负责的 label 之列，这里自己收掉
    if (dialog_ != nullptr) {
        lv_obj_delete(dialog_);
        dialog_ = nullptr;
    }
}

void QuandongS3Dev4GDisplay::SetupUI() {
    // 基类自己会加锁，并创建 top_bar_ / network_label_ 等对象；
    // 它内部有重复调用保护，所以这里不需要再判一次 IsSetupUICalled()
    SpiLcdDisplay::SetupUI();

    DisplayLockGuard lock(this);
    if (!lock) {
        ESP_LOGE(TAG, "加锁失败，网络图标热区未创建");
        return;
    }
    CreateNetworkTapArea();
#if LV_USE_VECTOR_GRAPHIC
    CreateCatFace();
#endif
}

void QuandongS3Dev4GDisplay::SetEmotion(const char* emotion) {
#if LV_USE_VECTOR_GRAPHIC
    cat_face::Emotion parsed = cat_face::Emotion::kNeutral;
    const bool handled_here =
        (emotion != nullptr) && cat_face::ParseEmotion(emotion, parsed) && (cat_canvas_ != nullptr);

    if (handled_here) {
        DisplayLockGuard lock(this);
        if (!lock) {
            ESP_LOGE(TAG, "加锁失败，表情未切换");
            return;
        }
        // 基类那条路上的 GIF 控制器要停掉，否则它会继续按帧去更新一个看不见的 image
        if (gif_controller_) {
            gif_controller_->Stop();
            gif_controller_.reset();
        }
        if (emoji_label_ != nullptr) {
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_image_ != nullptr) {
            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        }
        cat_animator_.SetEmotion(parsed);
        // 时间基准清零，下一帧从 dt=0 起算，不会把上一个情绪的停留时长补进这一帧
        cat_last_frame_us_ = 0;
        lv_obj_remove_flag(cat_canvas_, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGD(TAG, "猫脸表情: %s", cat_face::EmotionName(parsed));
        return;
    }
#endif

    // 名字不是猫脸认得的那 21 个，或者画布压根没建起来（内存不足 / 矢量开关关掉了）：
    // 整体交回基类，让位图 / GIF / 字体图标三档兜底，再把自己藏起来
    LcdDisplay::SetEmotion(emotion);
#if LV_USE_VECTOR_GRAPHIC
    if (cat_canvas_ != nullptr) {
        DisplayLockGuard lock(this);
        lv_obj_add_flag(cat_canvas_, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

void QuandongS3Dev4GDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    // 基类在预览期间会把 emoji_box_ 整体隐藏，画布作为它的子对象自然也跟着不见，
    // 所以这里只需要把定时器一并停掉，省下被遮住那几帧的绘制开销。
    // preview_image_ 为空说明 SetupUI() 还没跑，基类会直接返回、什么都不做，
    // 这时也不能去动定时器的状态，否则会停在暂停态再也恢复不了。
    const bool preview_requested = (image != nullptr) && (preview_image_ != nullptr);

    LcdDisplay::SetPreviewImage(std::move(image));

#if LV_USE_VECTOR_GRAPHIC
    DisplayLockGuard lock(this);
    // 进出预览都把时间基准清零，恢复时不会把整个预览时长当成一帧的 dt 补进去
    cat_last_frame_us_ = 0;
    if (cat_timer_ != nullptr) {
        if (preview_requested) {
            lv_timer_pause(cat_timer_);
        } else {
            lv_timer_resume(cat_timer_);
        }
    }
#endif
}

void QuandongS3Dev4GDisplay::CreateNetworkTapArea() {
    if (tap_area_ != nullptr) {
        return;
    }

    // 全透明覆盖层，只承担触摸热区，不改变任何视觉
    tap_area_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(tap_area_, kTapAreaWidth, kTapAreaHeight);
    lv_obj_align(tap_area_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(tap_area_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap_area_, 0, 0);
    lv_obj_set_style_radius(tap_area_, 0, 0);
    lv_obj_set_style_pad_all(tap_area_, 0, 0);
    lv_obj_set_style_shadow_width(tap_area_, 0, 0);
    lv_obj_set_scrollbar_mode(tap_area_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(tap_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tap_area_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tap_area_, OnTapAreaClicked, LV_EVENT_CLICKED, this);

    // 必须排在 top_bar_ / status_bar_ 之后，否则会被它们盖住收不到点击
    lv_obj_move_foreground(tap_area_);
    ESP_LOGI(TAG, "网络图标热区已创建 (%dx%d)", kTapAreaWidth, kTapAreaHeight);
}

void QuandongS3Dev4GDisplay::SetNetworkIconTappedHandler(std::function<void()> handler) {
    on_network_icon_tapped_ = std::move(handler);
}

void QuandongS3Dev4GDisplay::ShowConfirmDialog(const char* message,
                                               std::function<void()> on_confirm) {
    DisplayLockGuard lock(this);
    if (!lock) {
        ESP_LOGE(TAG, "加锁失败，确认弹窗未创建");
        return;
    }

    // 幂等：重复点击图标时先关掉旧弹窗，避免叠出多个
    CloseConfirmDialog();

    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    if (lvgl_theme == nullptr || lvgl_theme->text_font() == nullptr ||
        lvgl_theme->icon_font() == nullptr) {
        ESP_LOGE(TAG, "主题未就绪，确认弹窗未创建");
        return;
    }
    const lv_font_t* text_font = lvgl_theme->text_font()->font();
    const lv_font_t* icon_font = lvgl_theme->icon_font()->font();
    const lv_color_t text_color = lvgl_theme->text_color();

    // 注意：lv_layer_top() 不是 screen 的子对象，继承不到 SetupUI() 设在 screen 上的
    // 中文字体（默认字体 montserrat 没有中文字形），所以下面每个对象都显式指定字体
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_40, 0);
    lv_obj_set_style_text_font(overlay, text_font, 0);
    lv_obj_set_style_text_color(overlay, text_color, 0);
    lv_obj_set_scrollbar_mode(overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    // overlay 自带 CLICKABLE，点遮罩不会穿透到下面的图标（真正的模态）。
    // 遮罩上不挂事件：点空白处不做任何事，比误触直接关掉更稳妥

    lv_obj_t* card = lv_obj_create(overlay);
    lv_obj_set_width(card, kDialogWidth);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(card, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_color(card, lvgl_theme->border_color(), 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* text = lv_label_create(card);
    lv_label_set_text(text, message);
    lv_obj_set_style_text_font(text, text_font, 0);
    lv_obj_set_style_text_color(text, text_color, 0);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(text, LV_PCT(100));
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);

    lv_obj_t* buttons = lv_obj_create(card);
    lv_obj_set_size(buttons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(buttons, 0, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_column(buttons, 16, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    // 取消：灰底 + 白 ✗
    lv_obj_t* cancel = lv_button_create(buttons);
    lv_obj_set_size(cancel, kButtonWidth, kButtonHeight);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(kCancelBgColor), 0);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(cancel, OnCancelClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* cancel_icon = lv_label_create(cancel);
    lv_label_set_text(cancel_icon, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_font(cancel_icon, icon_font, 0);
    lv_obj_set_style_text_color(cancel_icon, lv_color_white(), 0);
    lv_obj_center(cancel_icon);

    // 确认：绿底 + 白 ✓
    lv_obj_t* confirm = lv_button_create(buttons);
    lv_obj_set_size(confirm, kButtonWidth, kButtonHeight);
    lv_obj_set_style_bg_color(confirm, lv_color_hex(kConfirmBgColor), 0);
    lv_obj_set_style_bg_opa(confirm, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(confirm, OnConfirmClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* confirm_icon = lv_label_create(confirm);
    lv_label_set_text(confirm_icon, MATERIAL_SYMBOLS_CHECK);
    lv_obj_set_style_text_font(confirm_icon, icon_font, 0);
    lv_obj_set_style_text_color(confirm_icon, lv_color_white(), 0);
    lv_obj_center(confirm_icon);

    dialog_ = overlay;
    on_confirm_ = std::move(on_confirm);
    ESP_LOGI(TAG, "确认弹窗已打开: %s", message);
}

void QuandongS3Dev4GDisplay::CloseConfirmDialog() {
    if (dialog_ == nullptr) {
        return;
    }
    // 本函数可能正处在 dialog_ 子孙对象的事件分发过程中，直接删对象不安全，
    // 用 async 版把删除推迟到下一次 lv_timer_handler
    lv_obj_delete_async(dialog_);
    dialog_ = nullptr;
    on_confirm_ = nullptr;
}

void QuandongS3Dev4GDisplay::OnTapAreaClicked(lv_event_t* event) {
    auto* self = static_cast<QuandongS3Dev4GDisplay*>(lv_event_get_user_data(event));
    if (self != nullptr && self->on_network_icon_tapped_) {
        self->on_network_icon_tapped_();
    }
}

void QuandongS3Dev4GDisplay::OnCancelClicked(lv_event_t* event) {
    auto* self = static_cast<QuandongS3Dev4GDisplay*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->CloseConfirmDialog();
    }
}

void QuandongS3Dev4GDisplay::OnConfirmClicked(lv_event_t* event) {
    auto* self = static_cast<QuandongS3Dev4GDisplay*>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }
    // 先取出回调再关弹窗（CloseConfirmDialog 会清空 on_confirm_）
    auto on_confirm = self->on_confirm_;
    self->CloseConfirmDialog();
    if (on_confirm) {
        on_confirm();
    }
}

#if LV_USE_VECTOR_GRAPHIC

void QuandongS3Dev4GDisplay::CreateCatFace() {
    if (cat_canvas_ != nullptr || emoji_box_ == nullptr) {
        return;
    }

    const uint32_t stride =
        lv_draw_buf_width_to_stride(kCatCanvasSize, LV_COLOR_FORMAT_ARGB8888);
    const size_t bytes = static_cast<size_t>(stride) * kCatCanvasSize;

    // 像素得从堆上要，不能走 lv_draw_buf_create()：CONFIG_LV_USE_BUILTIN_MALLOC=y
    // 时 lv_malloc 走的是 LVGL 自己的 TLSF 池（CONFIG_LV_MEM_SIZE_KILOBYTES=1024），
    // 把这块 110KB 塞进去会直接挤掉 UI 自己的内存。
    cat_pixels_ = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (cat_pixels_ == nullptr) {
        ESP_LOGW(TAG, "PSRAM 分配 %u 字节失败，猫脸画布回落到内部 RAM", (unsigned)bytes);
        cat_pixels_ = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (cat_pixels_ == nullptr) {
        ESP_LOGE(TAG, "猫脸画布像素分配失败（%u 字节），表情回落到位图", (unsigned)bytes);
        return;
    }

    cat_draw_buf_ = static_cast<lv_draw_buf_t*>(lv_malloc(sizeof(lv_draw_buf_t)));
    if (cat_draw_buf_ == nullptr) {
        ESP_LOGE(TAG, "猫脸 draw buf 结构体分配失败，表情回落到位图");
        heap_caps_free(cat_pixels_);
        cat_pixels_ = nullptr;
        return;
    }
    // lv_draw_buf_init() 会把 header.flags 清成 0，也就是说 lv_draw_buf_destroy()
    // 既不会释放像素也不会释放结构体 —— 所以这里不调 destroy，两头都由本类自己收
    if (lv_draw_buf_init(cat_draw_buf_, kCatCanvasSize, kCatCanvasSize, LV_COLOR_FORMAT_ARGB8888,
                         stride, cat_pixels_, bytes) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "猫脸 draw buf 初始化失败，表情回落到位图");
        lv_free(cat_draw_buf_);
        cat_draw_buf_ = nullptr;
        heap_caps_free(cat_pixels_);
        cat_pixels_ = nullptr;
        return;
    }

    // 画布挂在 emoji_box_ 下面：基类在预览图期间整体隐藏 emoji_box_，猫脸就跟着走，
    // 不需要另外写一套显隐逻辑。层格式必须是 ARGB8888（见 cat_face_lvgl.h 的说明）。
    cat_canvas_ = lv_canvas_create(emoji_box_);
    lv_canvas_set_draw_buf(cat_canvas_, cat_draw_buf_);
    lv_obj_set_size(cat_canvas_, kCatCanvasSize, kCatCanvasSize);
    lv_obj_center(cat_canvas_);
    // 画布自己画满整块，任何背景 / 边框 / 内边距都会盖住或挤偏猫脸
    lv_obj_set_style_bg_opa(cat_canvas_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cat_canvas_, 0, 0);
    lv_obj_set_style_pad_all(cat_canvas_, 0, 0);
    lv_obj_set_style_radius(cat_canvas_, 0, 0);
    lv_obj_set_style_shadow_width(cat_canvas_, 0, 0);

    // 开机的机器人 logo 由猫脸接管：屏幕本身就是那张脸，不该躺着一个机器人图标
    if (emoji_label_ != nullptr) {
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_image_ != nullptr) {
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }

    cat_animator_.SetEmotion(cat_face::Emotion::kNeutral);

    cat_timer_ = lv_timer_create(OnCatTimer, kCatFramePeriodMs, this);
    if (cat_timer_ == nullptr) {
        ESP_LOGE(TAG, "猫脸定时器创建失败，表情回落到位图");
        DestroyCatFace();
        return;
    }

    // heap_caps 给的是未初始化内存，先立刻铺一帧上去，别让屏幕上出现一屏垃圾像素
    DrawCatFrame();
    ESP_LOGI(TAG, "猫脸画布已就绪 (%dx%d, %u 字节, %d fps)", kCatCanvasSize, kCatCanvasSize,
             (unsigned)bytes, 1000 / static_cast<int>(kCatFramePeriodMs));
}

void QuandongS3Dev4GDisplay::DestroyCatFace() {
    DisplayLockGuard lock(this);

    // 顺序不能反：先停定时器保证不会再有回调进来，再删画布让 lv_canvas 不再引用像素，
    // 最后才释放像素。反过来做的话，正在渲染的那一帧会读到已经还回去的内存。
    if (cat_timer_ != nullptr) {
        lv_timer_delete(cat_timer_);
        cat_timer_ = nullptr;
    }
    if (cat_canvas_ != nullptr) {
        lv_obj_delete(cat_canvas_);
        cat_canvas_ = nullptr;
    }
    if (cat_draw_buf_ != nullptr) {
        // flags 为 0，lv_draw_buf_destroy() 什么都不会释放，所以自己收结构体
        lv_free(cat_draw_buf_);
        cat_draw_buf_ = nullptr;
    }
    if (cat_pixels_ != nullptr) {
        heap_caps_free(cat_pixels_);
        cat_pixels_ = nullptr;
    }
    cat_last_frame_us_ = 0;
}

void QuandongS3Dev4GDisplay::DrawCatFrame() {
    if (cat_canvas_ == nullptr || cat_draw_buf_ == nullptr) {
        return;
    }
    if (lv_obj_has_flag(cat_canvas_, LV_OBJ_FLAG_HIDDEN)) {
        // 当前情绪不是猫脸认得的那 21 个，画布被藏起来了，没必要白画
        cat_last_frame_us_ = 0;
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    // 第一帧没有参考点，dt 记 0；浅睡唤醒后的间隔可能有好几秒，上限钳到 100ms
    int64_t dt_us = (cat_last_frame_us_ != 0) ? (now_us - cat_last_frame_us_) : 0;
    cat_last_frame_us_ = now_us;
    if (dt_us < 0) {
        dt_us = 0;
    } else if (dt_us > kCatMaxStepUs) {
        dt_us = kCatMaxStepUs;
    }

    // 每帧都取一次主题线色，换主题就跟着换，不需要额外覆写 SetTheme()
    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    if (lvgl_theme != nullptr) {
        const lv_color_t ink = lvgl_theme->text_color();
        cat_animator_.SetInk(cat_face::RgbColor(ink.red, ink.green, ink.blue), kCatLineWidth);
    }
    cat_animator_.Tick(static_cast<uint32_t>(dt_us / 1000));

    const int64_t start_us = esp_timer_get_time();

    // 每帧都得先擦干净：猫脸并没有铺满整块画布，不擦的话上一帧的特征会留成残影
    lv_canvas_fill_bg(cat_canvas_, lv_color_black(), LV_OPA_TRANSP);

    lv_layer_t layer;
    lv_canvas_init_layer(cat_canvas_, &layer);
    {
        if (kCatBackend == CatRenderBackend::kVector) {
            LvglPathSink sink(&layer);
            cat_face::Draw(cat_animator_.pose(), sink);
            // lv_draw_vector() 内部立刻 dispatch，flush 要在离开作用域前做掉
            sink.Flush();
        } else {
            LvglPrimitiveSink sink(&layer);
            cat_face::Draw(cat_animator_.pose(), sink);
        }
    }
    lv_canvas_finish_layer(cat_canvas_, &layer);

    const int64_t cost_us = esp_timer_get_time() - start_us;
    cat_frame_us_sum_ += cost_us;
    if (cost_us > cat_frame_us_max_) {
        cat_frame_us_max_ = cost_us;
    }
    if (++cat_frame_count_ >= kCatStatsFrames) {
        ESP_LOGI(TAG, "猫脸渲染 %u 帧: 平均 %lld us, 峰值 %lld us (周期 %u us)", cat_frame_count_,
                 cat_frame_us_sum_ / cat_frame_count_, cat_frame_us_max_,
                 kCatFramePeriodMs * 1000);
        cat_frame_count_ = 0;
        cat_frame_us_sum_ = 0;
        cat_frame_us_max_ = 0;
    }
}

void QuandongS3Dev4GDisplay::OnCatTimer(lv_timer_t* timer) {
    auto* self = static_cast<QuandongS3Dev4GDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    // 本回调由 lv_timer_handler 调起，那条路径上 LVGL 锁已经由端口任务持着
    // （esp_lvgl_port 的 task 是 lock -> lv_timer_handler -> unlock），
    // 这里再取一次就是重入同一个递归锁，仓库里的 LvglGif 也是这么做的
    self->DrawCatFrame();
}

#endif  // LV_USE_VECTOR_GRAPHIC
