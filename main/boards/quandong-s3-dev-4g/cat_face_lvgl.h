#ifndef CAT_FACE_LVGL_H
#define CAT_FACE_LVGL_H

#include "cat_face.h"

#include <lvgl.h>

#if LV_USE_VECTOR_GRAPHIC

// Device backends for the cat face. Both map cat_face::PathSink onto LVGL, but
// they rasterise through completely different machinery:
//
//   LvglPathSink       LVGL's vector path API, rasterised by the vendored ThorVG.
//   LvglPrimitiveSink  LVGL's own line/triangle primitives. No ThorVG at all.
//
// Both live here because of a boot-time heap corruption that only the vector
// backend could reach: ThorVG allocates its C++ objects on the IDF heap while
// its vendored copy has malloc/free swapped for lv_malloc/lv_free, so two
// allocators meet inside one teardown path. See the note in
// quandong_s3_dev_4g_display.cc. The board draws with the primitive backend;
// the vector one is kept as the reference to A/B against.
//
// The geometry layer is shared verbatim with the host SVG preview, so this file
// is the only place the renderers can diverge -- keep the mapping honest
// (quad -> quad, cubic -> cubic, and no extra shapes invented here).
//
// Both backends require an ARGB8888 canvas layer rather than the screen. For
// the vector backend that is not an optimisation: on any other format
// lv_draw_sw_vector allocates a full-size temporary ARGB8888 buffer, clears it,
// and blends it back for every draw (see lv_draw_sw_vector.c); on a 168x168
// canvas that is a ~110KB alloc + clear + blend every single frame.
class LvglPathSink : public cat_face::PathSink {
public:
    // `layer` must outlive this object; it is not retained beyond the frame.
    explicit LvglPathSink(lv_layer_t* layer);
    ~LvglPathSink() override;

    LvglPathSink(const LvglPathSink&) = delete;
    LvglPathSink& operator=(const LvglPathSink&) = delete;

    // Hand the accumulated paths to the layer as one draw task. Call after the
    // last EndPath() and before lv_canvas_finish_layer(); the destructor calls
    // it too, so an early return needs no special handling.
    void Flush();

    void BeginPath() override;
    void MoveTo(float x, float y) override;
    void LineTo(float x, float y) override;
    void QuadTo(float cx, float cy, float x, float y) override;
    void CubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override;
    void Close() override;
    void EndPath() override;

    void SetStroke(cat_face::RgbColor color, float width, cat_face::StrokeCap cap,
                   cat_face::StrokeJoin join) override;
    void SetFill(cat_face::RgbColor color) override;
    void SetNoStroke() override;
    void SetNoFill() override;

private:
    lv_draw_vector_dsc_t* dsc_ = nullptr;
    lv_vector_path_t* path_ = nullptr;  // non-null only between Begin and End

    // Pending style. LVGL samples the style when the path is added, while the
    // geometry layer sets style independently of Begin/EndPath -- so it has to
    // be held here and applied at EndPath rather than pushed straight through.
    bool has_stroke_ = true;
    bool has_fill_ = false;
    cat_face::RgbColor stroke_color_;
    cat_face::RgbColor fill_color_;
    float stroke_width_ = 1.0f;
    lv_vector_stroke_cap_t cap_ = LV_VECTOR_STROKE_CAP_ROUND;
    lv_vector_stroke_join_t join_ = LV_VECTOR_STROKE_JOIN_ROUND;

    bool flushed_ = false;
};

// The backend the board actually draws with: every path becomes ordinary LVGL
// draw tasks (lv_draw_line / lv_draw_triangle) on the canvas layer. Nothing in
// this path allocates through anything but lv_malloc, and no ThorVG canvas is
// ever created -- which is the whole point, because the per-frame ThorVG canvas
// create/rasterise/destroy cycle was where the heap corruption surfaced.
//
// Mapping. The geometry layer's contract makes this exact rather than a
// lookalike:
//   stroke -> one lv_draw_line per flattened segment. Interior vertices get
//             round caps at both sides, and two round caps meeting at a vertex
//             cover the same disc a round join draws, so a round join is
//             reproduced exactly, not approximated.
//   fill   -> a triangle fan around the centroid of the flattened outline.
//             Correct for any outline that is star-shaped about its centroid,
//             which is every filled glyph the layer emits (ellipse, drop,
//             four-point sparkle, heart, note head). Wrong for a
//             self-intersecting outline; none are emitted.
//
// The only approximation is curve flattening, which is bounded by
// kFlatnessPx in the .cc.
class LvglPrimitiveSink : public cat_face::PathSink {
public:
    // `layer` must outlive this object; it is not retained beyond the frame.
    explicit LvglPrimitiveSink(lv_layer_t* layer);
    ~LvglPrimitiveSink() override;

    LvglPrimitiveSink(const LvglPrimitiveSink&) = delete;
    LvglPrimitiveSink& operator=(const LvglPrimitiveSink&) = delete;

    void BeginPath() override;
    void MoveTo(float x, float y) override;
    void LineTo(float x, float y) override;
    void QuadTo(float cx, float cy, float x, float y) override;
    void CubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override;
    void Close() override;
    void EndPath() override;

    void SetStroke(cat_face::RgbColor color, float width, cat_face::StrokeCap cap,
                   cat_face::StrokeJoin join) override;
    void SetFill(cat_face::RgbColor color) override;
    void SetNoStroke() override;
    void SetNoFill() override;

private:
    // A path is flattened into at most this many points before it is emitted.
    // The longest path the layer emits is a full ellipse -- four cubic segments
    // -- which lands near 30 points at the tolerance in the .cc, so the ceiling
    // is slack rather than tight.
    static constexpr int kMaxPoints = 96;

    void AddPoint(float x, float y);
    void Resume();
    void FlattenQuad(float x0, float y0, float cx, float cy, float x1, float y1);
    void FlattenCubic(float x0, float y0, float c1x, float c1y, float c2x, float c2y, float x1,
                      float y1);
    void EmitFill();
    void EmitStroke();

    lv_layer_t* layer_ = nullptr;

    // Flattened outline of the path being built, in canvas coordinates.
    // lv_point_precise_t (not lv_fpoint_t) because that is what the draw
    // descriptors take, and the two are distinct types even when both are
    // float-backed.
    lv_point_precise_t pts_[kMaxPoints];
    int count_ = 0;
    bool closed_ = false;

    // Sticky style, same semantics as the other backends.
    bool has_stroke_ = true;
    bool has_fill_ = false;
    cat_face::RgbColor stroke_color_;
    cat_face::RgbColor fill_color_;
    float stroke_width_ = 1.0f;
    cat_face::StrokeCap cap_ = cat_face::StrokeCap::kRound;
    cat_face::StrokeJoin join_ = cat_face::StrokeJoin::kRound;
};

#endif  // LV_USE_VECTOR_GRAPHIC

#endif  // CAT_FACE_LVGL_H
