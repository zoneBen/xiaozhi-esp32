# cat_face_preview

把 `quandong-s3-dev-4g` 的程序化猫脸渲染成 SVG，在浏览器里改脸，不用为了看一眼效果就重新烧板子。

## 为什么需要它

猫脸是**用函数现画**的矢量图形（源码在 `main/boards/quandong-s3-dev-4g/cat_face*.cc`），
不是位图资源。它的观感只能靠「画出来看」来调，而「编译 -> 烧录 -> 触发情绪 -> 拍照」
一轮要几分钟。这个目录把那轮压缩到几百毫秒。

关键点：**这里的几何代码不是副本**。本目录只编译板目录下的 `cat_face.*` / `cat_face_anim.*`，
一份源码两端共用，所以预览里看到的形状、比例、动画曲线和设备上跑的完全一致。

## 依赖

只需要一个支持 C++20 的主机编译器。**不需要 LVGL，不需要 ESP-IDF，不需要交叉工具链**——
几何层刻意不引用这两者中的任何头文件，就是为了让这一层能在 PC 上直接编。
本机验证过的是 TDM-GCC 10.3.0。

## 构建与运行

```bash
# Windows
build.bat

# 或者手动（任意平台）
g++ -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter \
    -I../../main/boards/quandong-s3-dev-4g \
    -o preview main.cc \
    ../../main/boards/quandong-s3-dev-4g/{cat_face,cat_face_shapes,cat_face_marks,cat_face_table,cat_face_anim}.cc
```

三种用法：

| 命令 | 作用 |
| ---- | ---- |
| `preview.exe --selftest` | 跑不变量自检，失败返回非 0。**改完几何先跑这个** |
| `preview.exe [输出.html]` | 写预览页（默认 `preview.html`），浏览器打开逐个校对 |
| `preview.exe --svg <目录> [边长px]` | 每个情绪导一张独立 `.svg`（默认 512px），便于并排比对或做视觉回归 |

`preview.html` 里是 21 个情绪的静态姿态网格，便于逐个校对；另外挑了几个情绪做成动画版，
用来看弹入、眨眼、漂浮符号的观感对不对。

> **截图小技巧**：需要自动截图时，`chrome --headless --screenshot=out.png preview.html`
> 的输出目录别选在仓库里 —— Chrome 会因为权限直接写不出来。用系统临时目录，例如
> `%TEMP%\catface\`。加 `--window-size=1400,2600` 能一次拍全整页。

## 自检在查什么

`--selftest` 覆盖的是**只有代码能保证**的部分，观感好不好它管不了：

- **每个情绪都画得出东西**：路径数在 `3..120` 之间（太少说明有分支没发射，太多说明有循环失控）。
- **坐标合法**：没有 NaN/Inf，且整体没有大幅跑出画布（特征符号和胡须允许出界一点）。
- **随机化不会改变情绪**：对每个情绪跑 64 个种子 × 40 个 tick，断言**眼型、嘴型、表情符号、
  脸的大小**全程不变。这是"按情绪微调而不是纯随机"这条要求的机器可验证形式 ——
  随机的只能是相位和幅度，不能是"这是什么表情"。
- **名字唯一且闭合**：21 个名字不重复；`ParseEmotion()` 对未知名字、`nullptr`、空串都要拒绝，
  否则板子会把不认识的情绪画成一张错的脸，而不是回落到位图。

## 改脸的时候

- **每个情绪只有一处可调**：`cat_face_table.cc` 里的 `EmotionSpec{name, base, motion}`。
  `base` 决定"像不像这个情绪"，**永不随机**；`motion` 决定"动起来是什么感觉"。
- **几何常量集中在 `cat_face_internal.h`**（归一化到脸的半径，改一个数整张脸跟着缩）。
- **`Pose` 的默认值和 `FaceMetrics` 的默认值必须一致**（分别在 `cat_face.h` 和
  `cat_face_internal.h`）。`Draw()` 是从 `Pose` 造 `FaceMetrics`，所以暂时只有 `Pose`
  在生效，但两处不一致会在有人直接 `FaceMetrics{}` 时变成一个很难查的布局事故。
- 改完跑 `--selftest`，再 `preview.exe`，肉眼过一遍 21 张脸。

## 设备侧对应的文件

预览只管几何。真正上屏的是这几处，改几何之外的东西时要一起看：

| 文件 | 作用 |
| ---- | ---- |
| `cat_face_lvgl.h` / `.cc` | `PathSink` 的 LVGL 矢量实现（ThorVG 软件光栅化） |
| `quandong_s3_dev_4g_display.cc` | 画布、定时器、情绪分发、`SetEmotion` / `SetPreviewImage` 覆写 |
| `config.json` | `sdkconfig_append` 里开 `LV_USE_VECTOR_GRAPHIC` / `LV_USE_THORVG` / `LV_USE_THORVG_INTERNAL` |

几个会静默出错的耦合点：

- `cat_face::kCanvasSize`（168）必须等于 `quandong_s3_dev_4g_display.cc` 里的 `kCatCanvasSize`。
  两个值不一致不会报错，只会让整张脸错位或缩水。
- 画布图层**必须是 ARGB8888**。换成别的格式，`lv_draw_sw_vector` 会为每一次绘制分配
  一整块同尺寸的临时 ARGB8888 缓冲、清零再混回去 —— 在 168×168 上就是每帧 ~110KB 的反复折腾。
- 线色每帧从主题的 `text_color()` 现取，所以换主题不需要额外的钩子；线宽是
  `quandong_s3_dev_4g_display.cc` 里的 `kCatLineWidth`。
- 预览不参与固件构建：它不在 `main/boards/` 下，板级源文件那个非递归 glob 扫不到这里，
  而且 `main.cc` 带 `main()`，本来也不能进固件。
