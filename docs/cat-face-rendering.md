# Cat Face Vector Rendering (quandong-s3-dev-4g)

This document records how the procedural cat face on the `quandong-s3-dev-4g` board is rendered,
along with the investigation into the boot-time crash it used to cause and the workaround that
replaced it.

## 1. Overview

The cat face is not a bitmap asset. It is vector art drawn by function calls: 21 emotions, each
built from ellipses, Bézier curves, drops, four-point sparkles, hearts and musical notes, all in a
168x168 logical space.

The code has three layers, separated by one pure interface, `cat_face::PathSink`:

| Layer | Files | Role |
| --- | --- | --- |
| Geometry | `cat_face.h`, `cat_face.cc`, `cat_face_shapes.*`, `cat_face_marks.cc`, `cat_face_table.cc`, `cat_face_anim.*` under `main/boards/quandong-s3-dev-4g/` | Poses, animation and shapes. Calls `PathSink` only, and deliberately includes no LVGL or ESP-IDF header |
| Device backend | `cat_face_lvgl.h`, `cat_face_lvgl.cc` in the same directory | Two `PathSink` implementations: `LvglPrimitiveSink` (LVGL primitives, the default) and `LvglPathSink` (LVGL vector paths rasterised by ThorVG) |
| Host preview | `tools/cat_face_preview/` | Compiles the same geometry layer into a PC program that writes SVG / HTML previews, so shaping the face does not require a flash |

Because the geometry layer depends on no board-side header, the same sources compile on a host --
see `tools/cat_face_preview/README.md`. What the preview shows is the same code that runs on the
device, not a copy of it.

Board-side parameters (`quandong_s3_dev_4g_display.cc`):

- 168x168 canvas, `LV_COLOR_FORMAT_ARGB8888`, pixels in PSRAM (~110 KB)
- 30 fps (`kCatFramePeriodMs = 33`)
- One average/peak render-cost log line every 300 frames, to check the 33 ms budget
- Line colour is sampled from the theme's `text_color` every frame, so a theme change carries over

## 2. Symptom

The first cat-face draw at boot crashed, every time. The crash was inside the `DrawCatFrame()` at
the end of `CreateCatFace()`, with the serial backtrace stopping at:

```
heap_caps_free
  → find_containing_heap
```

on the main task (Core 0).

## 3. Localisation: serial console only

There was no debugger on hand, only the serial console. The approach was to release the paths the
geometry layer feeds the backend **one at a time**: a counting proxy around `PathSink` printed the
index after each `EndPath()`, with a constant deciding "let the first N through". Change one
number, build, flash, read which index the serial output stops at.

That converged on the window `[6, 7)` -- the 7th path -- and specifically on its **teardown**:
the path itself drew fine, and the crash came when that ThorVG software canvas was destroyed.

Corroborating evidence from the heap side:

- The victim was a plain `free()`, not LVGL's `lv_free`
- The `registered_heaps` list head had been overwritten with a **small integer**
- Free-heap and per-region counters were **unchanged** -- a one-shot wild write, not a leak or a
  gradual overflow

This diagnostic code has since been deleted. To reproduce it, rewrite the "counting proxy plus a
constant gate" described above.

## 4. Root cause analysis: two allocators meeting in one free path

The mechanism below fits the observations closely. Without a debugger it cannot be proven
byte-for-byte, but it explains every symptom and was enough to decide which way to go around it:

- **LVGL side**: `CONFIG_LV_USE_BUILTIN_MALLOC=y`, so `lv_malloc` / `lv_free` serve LVGL's own
  TLSF pool (`CONFIG_LV_MEM_SIZE_KILOBYTES=1024`), and `lcd_display.cc` adds another 2560 KB
  PSRAM pool on top of it.
- **ThorVG side**: vendored inside LVGL, where part of its `malloc` / `free` has been replaced with
  `lv_malloc` / `lv_free` -- while its C++ objects still go through `new` / `delete` and land on
  the IDF heap.

So both allocators are live inside one free path. Once a pointer that came from the IDF heap is
handed to `lv_free`, TLSF reads the bytes in front of it as a block header -- whatever they happen
to be -- and writes free-list flag words to an address derived from that garbage, corrupting state
around `registered_heaps`. The next ordinary `free()` then dies in `find_containing_heap`.

Only the vector backend could reach it because `LvglPathSink` creates a ThorVG software canvas
**once per frame** and destroys it **immediately after drawing**, which is by far the highest
create/destroy rate and object count anywhere in the path.

## 5. Solution: the primitive backend

The conclusion was to go around the combination rather than dig further -- digging would only
produce an upstream patch we cannot verify. The new `LvglPrimitiveSink` draws an entire frame with
LVGL's own primitives (`lv_draw_line` / `lv_draw_triangle`), creates **no ThorVG canvas at all**,
and touches nothing but LVGL's allocator. The geometry layer is unchanged.

The mappings are exact equivalents, not lookalikes:

### 5.1 Stroke -> `lv_draw_line`

Every stroke in the geometry layer is round-capped with round joins. LVGL's `round_start` /
`round_end` paint a filled disc of diameter `dsc->width` at an endpoint, while the line body
itself is butt-cut at both ends. Hence:

| Vertex | Treatment |
| --- | --- |
| Interior vertex of a contour | `round_*` on both sides -- two discs on the same point cover exactly the disc a round join paints |
| The two ends of an open contour | `round_*` inside, off outside -- round cap, flat tail |

A round join is therefore **reproduced**, not approximated. Every stroke on the face is opaque,
and for an opaque stroke painting that disc twice is indistinguishable from painting it once. Line
width has to be a whole number of pixels, so the float style width is rounded to nearest.

### 5.2 Fill -> a triangle fan around the centroid

This LVGL version has no polygon-fill primitive. The fan is built from the centroid of the
flattened outline, emitting `(centroid, p[i], p[i+1])` per edge. That is exact for any outline that
is star-shaped about its centroid, which is every filled glyph the geometry layer emits: ellipse,
drop, four-point sparkle, heart, note head, visor lens. A self-intersecting outline would come out
wrong; the geometry layer does not emit one.

Fills are always treated as closed (matching SVG semantics), with no explicit `Close()` required.

### 5.3 Curve flattening

`deviation` is the distance from the curve's midpoint to the chord between its endpoints: for a
quadratic it is `0.5·|c − mid(p0,p1)|`, for a cubic `0.75·|c1+c2−p0−p1|`. The segment count is
`n = ceil(sqrt(deviation / kFlatnessPx))`, clamped to `[1, 24]` with `kFlatnessPx = 0.5f` (pixels).
An n-segment polyline's error falls off as `deviation / n²`, so at a 0.5 px tolerance a 90 px wide
eye needs only about 7 points per quarter-arc.

The longest path is a full eye ellipse (four cubic segments) at roughly 30 points; the
`kMaxPoints = 96` buffer ceiling is a backstop that is never reached in practice.

### 5.4 Multi-contour paths

When a `MoveTo` appears mid-path -- the anger mark emits four arms in one path -- the contour
collected so far is finished with `EndPath()` and a new one starts, rather than welding the two
together.

> **Include-order gotcha**: the primitive descriptors have to be included by path in the `.cc`:
> `draw/lv_draw.h` → `draw/lv_draw_line.h` / `draw/lv_draw_triangle.h`. `lvgl.h` only pulls in
> `lv_draw_vector.h`, and `lv_draw_line.h` uses `lv_draw_dsc_base_t` without including it (inside
> LVGL it is always reached through `lv_draw_private.h`), so `lv_draw.h` must come first.

## 6. Switching backends

At the top of `quandong_s3_dev_4g_display.cc`:

```cpp
enum class CatRenderBackend { kPrimitive, kVector };
constexpr CatRenderBackend kCatBackend = CatRenderBackend::kPrimitive;
```

Setting it to `kVector` restores the original ThorVG path. Both backends are kept side by side for
A/B comparison, and changing that one constant needs no other edit.

> Both backends are only compiled when `LV_USE_VECTOR_GRAPHIC=y` (`kVector` depends on LVGL's
> vector API), so the switch must stay on even for a board that only draws with primitives.

## 7. Known costs

| Item | Note |
| --- | --- |
| Curve flattening | 0.5 px worst case. The panel is 16-bit colour, so it is not visible |
| Translucent fill seams | LVGL line masks are antialiased, so adjacent fan triangles each cover half of the shared edge, leaving a faint radial seam inside a translucent fill. Opaque fills are unaffected, and the face's fills are essentially all opaque. Overlapping the triangles angularly was considered and rejected: it would double-blend translucent fills darker |
| Whole-pixel stroke width | Float widths are rounded. The face's line width is 3.0, so it lands exactly |
| Stack | `pts_[96]` costs 768 bytes in the frame of the LVGL task (default 7168) |
| Draw task count | ~200-400 draw tasks per frame, already covered by the render-cost log line above |

## 8. Build configuration

`main/boards/quandong-s3-dev-4g/config.json`, under `sdkconfig_append`:

```
CONFIG_LV_USE_VECTOR_GRAPHIC=y
CONFIG_LV_USE_THORVG=y
CONFIG_LV_USE_THORVG_INTERNAL=y
```

`main/CMakeLists.txt` relaxes one warning for the vendored LVGL component only:

```cmake
if(CONFIG_LV_USE_THORVG)
    idf_component_get_property(lvgl_thorvg_lib lvgl__lvgl COMPONENT_LIB)
    if(lvgl_thorvg_lib)
        target_compile_options(${lvgl_thorvg_lib} PRIVATE -Wno-type-limits)
    endif()
endif()
```

The reason: ThorVG guards a sign-bit test with `encoded[3] < 0`, and `char` is unsigned on Xtensa,
so that comparison is dead code -- which ESP-IDF's default `-Wextra` plus `-Werror` rejects as
`-Wtype-limits`. The relaxation is scoped to that one warning class and that one component; our
own code keeps it.

The canvas must be `LV_COLOR_FORMAT_ARGB8888`. On any other format the vector backend allocates a
full-size temporary ARGB8888 buffer, clears it and blends it back for every single draw, and the
primitive backend needs the alpha channel for blending anyway. The pixels come from PSRAM via
`heap_caps_aligned_alloc` (~110 KB) rather than `lv_draw_buf_create()`, because `lv_malloc` would
route them into LVGL's own 1024 KB pool and squeeze out the UI's memory.

## 9. Status

- Compiles clean (ESP-IDF v6.1 toolchain, `-fsyntax-only` with no diagnostics)
- Runs correctly on hardware: no boot crash, emotions work
- **No pixel-by-pixel comparison** against the old vector backend has been done. The differences
  between the two should be limited to the entries under "Known costs" above
