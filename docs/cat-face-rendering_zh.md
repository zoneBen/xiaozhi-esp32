# 猫脸矢量渲染（quandong-s3-dev-4g）

本文档记录 `quandong-s3-dev-4g` 板子上程序化猫脸表情的渲染架构，以及「开机第一次画脸
就崩」这次问题的排查过程与最终采用的绕过方案。

## 1. 总览

猫脸不是位图资源，而是用函数现画的矢量图形：21 个情绪，每个情绪由椭圆、贝塞尔曲线、
水滴、四角星、心形、音符等图元组合而成，在 168×168 的逻辑空间里作画。

代码分成三层，边界是 `cat_face::PathSink` 这个纯接口：

| 层 | 文件 | 职责 |
| --- | --- | --- |
| 几何层 | `main/boards/quandong-s3-dev-4g/` 下的 `cat_face.h`、`cat_face.cc`、`cat_face_shapes.*`、`cat_face_marks.cc`、`cat_face_table.cc`、`cat_face_anim.*` | 姿态、动画、形状，只调用 `PathSink` 接口。**刻意不引用 LVGL 与 ESP-IDF 的任何头文件** |
| 设备后端 | 同目录 `cat_face_lvgl.h`、`cat_face_lvgl.cc` | `PathSink` 的两个实现：`LvglPrimitiveSink`（LVGL 图元，默认）与 `LvglPathSink`（LVGL 矢量路径 → ThorVG） |
| 主机预览 | `tools/cat_face_preview/` | 把同一份几何层代码编成 PC 程序，输出 SVG / HTML 预览，不用为了看一眼效果就烧板子 |

几何层不依赖任何板端头文件，所以同一份源码在 PC 上可以直接编译（见
`tools/cat_face_preview/README.md`）。预览里看到的形状、比例、动画曲线，和跑到设备上的
是同一份代码，不是副本。

板端参数（`quandong_s3_dev_4g_display.cc`）：

- 画布 168×168、`LV_COLOR_FORMAT_ARGB8888`，像素放 PSRAM（约 110 KB）
- 30 fps（`kCatFramePeriodMs = 33`）
- 每 300 帧打一条平均 / 峰值渲染耗时日志，用来核对 33 ms 的帧预算
- 线色每帧从主题 `text_color` 取，换主题跟着换

## 2. 症状

开机第一次画脸就崩，必现。崩在 `CreateCatFace()` 末尾那次 `DrawCatFrame()` 里，
串口回溯停在：

```
heap_caps_free
  → find_containing_heap
```

主任务（Core 0）。

## 3. 定位：只用串口

现场没有调试器，只有串口。做法是把几何层喂给后端的路径**一条一条放行**：在 `PathSink`
外面套一层计数代理，每执行完一条 `EndPath()` 就打印序号，再用一个常量控制「只放行前
N 条」。改一个数字、编译、烧录、看串口停在哪一号。

结果收敛到第 7 条路径的窗口 `[6, 7)`，而且是它的**拆解阶段**——路径本身已经画完，
崩在这个 thorvg sw canvas 被销毁的时候。

堆侧的旁证：

- 受害者是一次普通的 `free()`，不是 LVGL 的 `lv_free`
- `registered_heaps` 链表头被一个**小的整数值**覆盖
- free heap 与各 region 的空闲计数**没有变化** → 一次性的野写，不是泄漏或溢出累积

这部分诊断代码已经全部删除。要复现，照上面那句「计数代理 + 常量放行」重写一遍即可。

## 4. 根因分析：两套分配器在同一条释放路径上相遇

下面的机制与观察到的现象高度吻合。没有调试器，无法逐字节证实，但它解释了全部现象，
也足以决定该往哪个方向绕：

- **LVGL 侧**：`CONFIG_LV_USE_BUILTIN_MALLOC=y`，`lv_malloc` / `lv_free` 走 LVGL
  自己的 TLSF 池（`CONFIG_LV_MEM_SIZE_KILOBYTES=1024`），另外 `lcd_display.cc` 还
  额外往池里挂了一块 2560 KB 的 PSRAM。
- **ThorVG 侧**：它被 vendored 进 LVGL，内部一部分 `malloc` / `free` 被替换成了
  `lv_malloc` / `lv_free`；但它自己的 C++ 对象走 `new` / `delete`，落在 IDF 堆上。

于是一条释放路径上同时存在两套分配器。一旦某个**来自 IDF 堆的指针**被送进 `lv_free`，
TLSF 会把该指针前面的若干字节当成块头来解析——里面是什么就是什么——然后按一个由垃圾
推导出来的地址去写 free list / 标志位，写坏 `registered_heaps` 附近的状态。下一次普通
`free()` 走到 `find_containing_heap` 时就炸了。

只有矢量后端能踩到，是因为 `LvglPathSink` 每帧都要**新建一次** thorvg 的 sw canvas、
画完**立刻拆掉**，创建 / 销毁的频率和对象数量都最高。

## 5. 方案：图元后端

结论是绕开这个组合，而不是继续往下挖——继续挖的收益只是给上游提一个我们无法验证的补丁。
新增 `LvglPrimitiveSink`：整帧只用 LVGL 自己的图元（`lv_draw_line` / `lv_draw_triangle`），
**不创建任何 thorvg canvas**，全程只经过 LVGL 的分配器。几何层一行没动。

映射规则是**精确等价**，不是「看起来差不多」：

### 5.1 描边 → `lv_draw_line`

几何层里所有描边都是圆头（round cap）+ 圆角连接（round join）。LVGL 的
`round_start` / `round_end` 会在端点画一个直径 `dsc->width` 的实心圆盘，而线体本身在
两端是平切的。所以：

| 顶点位置 | 处理 |
| --- | --- |
| 轮廓内顶点 | 两侧都开 `round_*` → 两个圆盘叠在同一点上，正好是圆角连接要画的那个圆盘 |
| 开放轮廓的两个端点 | 内侧开 `round_*`、外侧不开 → 圆头 + 平尾 |

也就是说圆角连接是**复现**出来的，不是近似。脸上的描边都是不透明的，同一个圆盘画两遍
和画一遍没有区别。线宽只能整像素，浮点宽度四舍五入到最近像素。

### 5.2 填充 → 绕质心的三角扇

LVGL 这个版本没有多边形填充图元。做法是求折线轮廓的质心，再 `(质心, p[i], p[i+1])`
逐边连三角。对**质心星形**的轮廓这是精确的，而几何层发出的填充图元——椭圆、水滴、
四角星、心形、音符头、面罩——全部满足。自相交轮廓会画错，但几何层不发这种轮廓。

填充一律按闭合处理（与 SVG 语义一致），不要求路径显式 `Close()`。

### 5.3 曲线折线化

`deviation` 取曲线中点到端点弦的距离：二次曲线是 `0.5·|c − mid(p0,p1)|`，
三次曲线是 `0.75·|c1+c2−p0−p1|`。段数 `n = ceil(sqrt(deviation / kFlatnessPx))`，
钳在 `[1, 24]`，`kFlatnessPx = 0.5f`（像素）。n 段折线的误差按 `deviation / n²` 衰减，
所以 0.5 px 容差下，一个 90 px 宽的眼睛每段圆弧只要 7 个点左右。

最长的路径是整只眼睛的椭圆（四段三次曲线），约 30 个点；缓冲上限 `kMaxPoints = 96`
是兜底，正常到不了。

### 5.4 多轮廓路径

`MoveTo` 出现在路径中间时（例如怒标一个路径发四条手臂），先把已经收集的轮廓
`EndPath()` 结掉，再起新轮廓，而不是把两段接起来。

> **include 顺序的坑**：图元描述符要在 `.cc` 里按路径 include —— `draw/lv_draw.h`
> → `draw/lv_draw_line.h` / `draw/lv_draw_triangle.h`。`lvgl.h` 只带出
> `lv_draw_vector.h`，而 `lv_draw_line.h` 用到了 `lv_draw_dsc_base_t` 却没有自己
> include 它（LVGL 内部总是经 `lv_draw_private.h` 到达），所以 `lv_draw.h` 必须排最前。

## 6. 切换后端

`quandong_s3_dev_4g_display.cc` 顶部：

```cpp
enum class CatRenderBackend { kPrimitive, kVector };
constexpr CatRenderBackend kCatBackend = CatRenderBackend::kPrimitive;
```

改成 `kVector` 就回到原来那版（ThorVG）。两个后端一直并列保留，方便 A/B 比对，改这一个
常量不需要动别的地方。

> 只有 `LV_USE_VECTOR_GRAPHIC=y` 时这两个后端才会被编译（`kVector` 依赖 LVGL 的矢量
> API）。所以即使板子只用图元后端，这个开关也必须保持打开。

## 7. 已知代价

| 项 | 说明 |
| --- | --- |
| 曲线折线化 | 最大偏差 0.5 px。面板是 16 位色，看不出来 |
| 半透明填充的接缝 | LVGL 的线段掩码带抗锯齿，三角扇相邻边各盖一半，会在半透明填充内部留下极淡的径向接缝。不透明填充没有这个问题，脸上的填充基本都不透明。曾考虑让相邻三角在角度上重叠，但那样半透明填充会被叠深，放弃 |
| 线宽整像素 | 浮点宽度四舍五入。脸上的线宽是 3.0，正好 |
| 栈占用 | `pts_[96]` 占 768 字节，落在 LVGL 任务（默认 7168）的栈帧里 |
| 绘制任务数 | 每帧约 200–400 个 draw task，开销已包含在上面那条渲染耗时日志里 |

## 8. 构建配置

`main/boards/quandong-s3-dev-4g/config.json` 的 `sdkconfig_append`：

```
CONFIG_LV_USE_VECTOR_GRAPHIC=y
CONFIG_LV_USE_THORVG=y
CONFIG_LV_USE_THORVG_INTERNAL=y
```

`main/CMakeLists.txt` 里给 vendored 的 LVGL 组件单独放宽一个警告：

```cmake
if(CONFIG_LV_USE_THORVG)
    idf_component_get_property(lvgl_thorvg_lib lvgl__lvgl COMPONENT_LIB)
    if(lvgl_thorvg_lib)
        target_compile_options(${lvgl_thorvg_lib} PRIVATE -Wno-type-limits)
    endif()
endif()
```

原因：ThorVG 里有个 `encoded[3] < 0` 的符号位测试，Xtensa 上 `char` 是无符号的，这个
比较是死代码，而 ESP-IDF 默认 `-Wextra` + `-Werror` 会因此报 `-Wtype-limits`。只对这一个
警告、只对这一个组件放宽，自己的代码照旧。

画布必须是 `LV_COLOR_FORMAT_ARGB8888`：矢量后端在别的格式上每次绘制都会额外分配一整块
全尺寸 ARGB8888 临时缓冲、清空再混合回来；图元后端也需要 alpha 通道做混合。像素用
`heap_caps_aligned_alloc` 从 PSRAM 拿（约 110 KB），不走 `lv_draw_buf_create()` ——
`lv_malloc` 会走 LVGL 自己那个 1024 KB 的池，塞进去会挤掉 UI 自己的内存。

## 9. 状态

- 编译通过（ESP-IDF v6.1 工具链，`-fsyntax-only` 零诊断）
- 实机运行正常：开机不再崩溃，表情可用
- 与旧矢量后端**没有做过逐像素比对**。两侧差异应当只限于上面「已知代价」里的那几条
