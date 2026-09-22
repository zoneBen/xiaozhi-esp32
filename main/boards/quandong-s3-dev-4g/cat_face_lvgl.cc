#include "cat_face_lvgl.h"

#if LV_USE_VECTOR_GRAPHIC

#include <cmath>

// lvgl.h pulls in lv_draw_vector.h but not the primitive draw descriptors, so
// they come in by path -- the lvgl component exports its src/ directory as an
// include dir. lv_draw.h goes first because lv_draw_line.h uses
// lv_draw_dsc_base_t without including it (inside LVGL it is always reached
// through lv_draw_private.h).
#include "draw/lv_draw.h"
#include "draw/lv_draw_line.h"
#include "draw/lv_draw_triangle.h"

namespace {

// RgbColor carries its own alpha and the geometry layer fades marks by lowering
// it. LVGL splits color from opacity, so the alpha belongs in the opacity while
// the color stays fully opaque -- folding it into both would square the fade.
lv_color32_t ToColor32(cat_face::RgbColor color) {
    lv_color32_t out;
    out.red = color.r;
    out.green = color.g;
    out.blue = color.b;
    out.alpha = 0xFF;
    return out;
}

lv_vector_stroke_cap_t ToCap(cat_face::StrokeCap cap) {
    return cap == cat_face::StrokeCap::kRound ? LV_VECTOR_STROKE_CAP_ROUND
                                              : LV_VECTOR_STROKE_CAP_BUTT;
}

lv_vector_stroke_join_t ToJoin(cat_face::StrokeJoin join) {
    return join == cat_face::StrokeJoin::kRound ? LV_VECTOR_STROKE_JOIN_ROUND
                                                : LV_VECTOR_STROKE_JOIN_MITER;
}

// Same split as above, for the primitive descriptors: lv_color_t is the 16-bit
// screen colour (this build runs LV_COLOR_DEPTH 16), the alpha rides in the
// descriptor's opa. The panel is 16-bit too, so nothing is lost on the way out.
lv_color_t ToColor(cat_face::RgbColor color) {
    return lv_color_make(color.r, color.g, color.b);
}

// ------------------------------------------------------- primitive backend

// Flattening tolerance, in canvas pixels: the most the polyline may differ from
// the curve it replaces. Half a pixel is under what the canvas can show, and it
// keeps the point counts small (a 90px-wide eye lands around 7 points per
// quarter-arc).
constexpr float kFlatnessPx = 0.5f;

// Per-curve ceiling on the segment count, so a curve whose control points are
// far outside its endpoints cannot run away with the point buffer.
constexpr int kMaxCurveSegments = 24;

// How many straight segments to split a curve into. `deviation` is how far the
// curve's midpoint sits from the chord between its endpoints, which stands in
// for how much the curve bulges: the error of an n-segment polyline falls off
// as deviation / n^2, so n = sqrt(deviation / tolerance).
int SegmentsFor(float deviation) {
    // Written so a NaN deviation (degenerate control points) also takes the
    // one-segment path instead of propagating.
    if (!(deviation > kFlatnessPx)) {
        return 1;
    }
    const float segments = ceilf(sqrtf(deviation / kFlatnessPx));
    if (!(segments < static_cast<float>(kMaxCurveSegments))) {
        return kMaxCurveSegments;
    }
    return static_cast<int>(segments);
}

// A vertex between two segments of the same contour is the contour's own joint
// and takes the join style; the two loose ends of an open contour take the cap
// style instead.
bool RoundAt(bool interior, cat_face::StrokeJoin join, cat_face::StrokeCap cap) {
    if (interior) {
        return join == cat_face::StrokeJoin::kRound;
    }
    return cap == cat_face::StrokeCap::kRound;
}

}  // namespace

LvglPathSink::LvglPathSink(lv_layer_t* layer) {
    dsc_ = lv_draw_vector_dsc_create(layer);
    if (dsc_ == nullptr) {
        return;
    }
    // Establish a known style. The geometry layer always sets style explicitly
    // before it draws, but a zeroed descriptor would silently drop the first
    // path (lv_draw_vector_dsc_add_path returns early when both opacities are 0).
    SetNoFill();
    SetStroke(stroke_color_, 1.0f, cat_face::StrokeCap::kRound, cat_face::StrokeJoin::kRound);
}

LvglPathSink::~LvglPathSink() {
    // An abandoned path (BeginPath with no EndPath) is legal in the geometry
    // layer, so it must not leak here either.
    if (path_ != nullptr) {
        lv_vector_path_delete(path_);
        path_ = nullptr;
    }
    Flush();
    if (dsc_ != nullptr) {
        lv_draw_vector_dsc_delete(dsc_);
        dsc_ = nullptr;
    }
}

void LvglPathSink::Flush() {
    if (dsc_ == nullptr || flushed_) {
        return;
    }
    flushed_ = true;
    // lv_draw_vector copies the descriptor into the layer's draw task list, so
    // dsc_ is safe to delete immediately afterwards.
    lv_draw_vector(dsc_);
}

void LvglPathSink::BeginPath() {
    if (dsc_ == nullptr || path_ != nullptr) {
        return;
    }
    path_ = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_MEDIUM);
}

void LvglPathSink::MoveTo(float x, float y) {
    if (path_ == nullptr) {
        return;
    }
    const lv_fpoint_t p{x, y};
    lv_vector_path_move_to(path_, &p);
}

void LvglPathSink::LineTo(float x, float y) {
    if (path_ == nullptr) {
        return;
    }
    const lv_fpoint_t p{x, y};
    lv_vector_path_line_to(path_, &p);
}

void LvglPathSink::QuadTo(float cx, float cy, float x, float y) {
    if (path_ == nullptr) {
        return;
    }
    const lv_fpoint_t ctrl{cx, cy};
    const lv_fpoint_t end{x, y};
    lv_vector_path_quad_to(path_, &ctrl, &end);
}

void LvglPathSink::CubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) {
    if (path_ == nullptr) {
        return;
    }
    const lv_fpoint_t c1{c1x, c1y};
    const lv_fpoint_t c2{c2x, c2y};
    const lv_fpoint_t end{x, y};
    lv_vector_path_cubic_to(path_, &c1, &c2, &end);
}

void LvglPathSink::Close() {
    if (path_ == nullptr) {
        return;
    }
    lv_vector_path_close(path_);
}

void LvglPathSink::EndPath() {
    if (dsc_ == nullptr || path_ == nullptr) {
        return;
    }

    // Style first: add_path samples the current style and bails out entirely if
    // neither fill nor stroke is visible. LV_OPA_TRANSP is an enumeration, so
    // the ternary needs the cast to stay a single type.
    const lv_opa_t no_fill_opa = static_cast<lv_opa_t>(LV_OPA_TRANSP);
    lv_draw_vector_dsc_set_fill_color32(dsc_, ToColor32(fill_color_));
    lv_draw_vector_dsc_set_fill_opa(dsc_, has_fill_ ? fill_color_.a : no_fill_opa);

    lv_draw_vector_dsc_set_stroke_color32(dsc_, ToColor32(stroke_color_));
    lv_draw_vector_dsc_set_stroke_opa(dsc_, has_stroke_ ? stroke_color_.a : no_fill_opa);
    lv_draw_vector_dsc_set_stroke_width(dsc_, stroke_width_);
    lv_draw_vector_dsc_set_stroke_cap(dsc_, cap_);
    lv_draw_vector_dsc_set_stroke_join(dsc_, join_);

    lv_draw_vector_dsc_add_path(dsc_, path_);

    // add_path deep-copies the path, so this is not a dangling reference.
    lv_vector_path_delete(path_);
    path_ = nullptr;
}

void LvglPathSink::SetStroke(cat_face::RgbColor color, float width, cat_face::StrokeCap cap,
                             cat_face::StrokeJoin join) {
    has_stroke_ = true;
    stroke_color_ = color;
    // A zero-width stroke renders as nothing and reads as a missing feature
    // rather than as a thin one, so keep a hairline floor.
    stroke_width_ = width > 0.05f ? width : 0.05f;
    cap_ = ToCap(cap);
    join_ = ToJoin(join);
}

void LvglPathSink::SetFill(cat_face::RgbColor color) {
    has_fill_ = true;
    fill_color_ = color;
}

void LvglPathSink::SetNoStroke() { has_stroke_ = false; }

void LvglPathSink::SetNoFill() { has_fill_ = false; }

// ------------------------------------------------------- primitive backend

LvglPrimitiveSink::LvglPrimitiveSink(lv_layer_t* layer) : layer_(layer) {}

LvglPrimitiveSink::~LvglPrimitiveSink() {
    // A contour left unfinished is dropped, which is what the vector backend
    // does with its pending path too.
}

void LvglPrimitiveSink::BeginPath() {
    // A nested BeginPath keeps the contour already in progress, matching the
    // vector backend (which ignores the second call while a path is open).
    if (count_ > 0) {
        return;
    }
    closed_ = false;
}

void LvglPrimitiveSink::MoveTo(float x, float y) {
    // A MoveTo inside a path starts a second contour -- the anger mark emits
    // four arms in one path -- so the contour collected so far is finished off
    // here instead of being joined to the new one.
    EndPath();
    AddPoint(x, y);
}

void LvglPrimitiveSink::LineTo(float x, float y) {
    Resume();
    AddPoint(x, y);
}

void LvglPrimitiveSink::QuadTo(float cx, float cy, float x, float y) {
    Resume();
    if (count_ == 0) {
        return;
    }
    FlattenQuad(pts_[count_ - 1].x, pts_[count_ - 1].y, cx, cy, x, y);
}

void LvglPrimitiveSink::CubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) {
    Resume();
    if (count_ == 0) {
        return;
    }
    FlattenCubic(pts_[count_ - 1].x, pts_[count_ - 1].y, c1x, c1y, c2x, c2y, x, y);
}

void LvglPrimitiveSink::Close() {
    if (count_ > 0) {
        closed_ = true;
    }
}

void LvglPrimitiveSink::EndPath() {
    if (count_ > 0) {
        // Fill under outline, the same order the geometry layer paints them in
        // within a single path.
        EmitFill();
        EmitStroke();
    }
    count_ = 0;
    closed_ = false;
}

void LvglPrimitiveSink::SetStroke(cat_face::RgbColor color, float width, cat_face::StrokeCap cap,
                                  cat_face::StrokeJoin join) {
    has_stroke_ = true;
    stroke_color_ = color;
    // Same hairline floor as the vector backend. This backend can only draw
    // whole pixels, so anything thinner than half a pixel becomes one.
    stroke_width_ = width > 0.05f ? width : 0.05f;
    cap_ = cap;
    join_ = join;
}

void LvglPrimitiveSink::SetFill(cat_face::RgbColor color) {
    has_fill_ = true;
    fill_color_ = color;
}

void LvglPrimitiveSink::SetNoStroke() { has_stroke_ = false; }

void LvglPrimitiveSink::SetNoFill() { has_fill_ = false; }

void LvglPrimitiveSink::Resume() {
    if (!closed_ || count_ == 0) {
        return;
    }
    // After Close() the contour is finished, and per the drawing model a
    // following command starts a new one at the same start point. Nothing in
    // the geometry layer does that, but the two must not be welded together.
    const lv_point_precise_t start = pts_[0];
    EndPath();
    AddPoint(start.x, start.y);
}

void LvglPrimitiveSink::AddPoint(float x, float y) {
    // Past the ceiling the tail of the contour is dropped. The longest path the
    // geometry layer emits needs about 30 points, so this is a backstop against
    // a pathological one, not a limit that is ever reached.
    if (count_ >= kMaxPoints) {
        return;
    }
    pts_[count_].x = x;
    pts_[count_].y = y;
    count_++;
}

void LvglPrimitiveSink::FlattenQuad(float x0, float y0, float cx, float cy, float x1, float y1) {
    // The curve's midpoint is (p0 + 2c + p1)/4 and the chord's is (p0 + p1)/2,
    // so the gap between them is half the distance from c to the chord's
    // midpoint. The start point is already in the buffer, so only t in (0, 1]
    // is added.
    const float dev_x = cx - (x0 + x1) * 0.5f;
    const float dev_y = cy - (y0 + y1) * 0.5f;
    const int segments = SegmentsFor(0.5f * sqrtf(dev_x * dev_x + dev_y * dev_y));
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float u = 1.0f - t;
        const float a = u * u;
        const float b = 2.0f * u * t;
        const float c = t * t;
        AddPoint(a * x0 + b * cx + c * x1, a * y0 + b * cy + c * y1);
    }
}

void LvglPrimitiveSink::FlattenCubic(float x0, float y0, float c1x, float c1y, float c2x,
                                     float c2y, float x1, float y1) {
    // Midpoint gap for a cubic: (p0 + 3c1 + 3c2 + p1)/8 against (p0 + p1)/2,
    // which is three eighths of the distance from (c1 + c2) to (p0 + p1).
    const float dev_x = c1x + c2x - x0 - x1;
    const float dev_y = c1y + c2y - y0 - y1;
    const int segments = SegmentsFor(0.75f * sqrtf(dev_x * dev_x + dev_y * dev_y));
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float u = 1.0f - t;
        const float a = u * u * u;
        const float b = 3.0f * u * u * t;
        const float c = 3.0f * u * t * t;
        const float d = t * t * t;
        AddPoint(a * x0 + b * c1x + c * c2x + d * x1, a * y0 + b * c1y + c * c2y + d * y1);
    }
}

void LvglPrimitiveSink::EmitFill() {
    if (layer_ == nullptr || !has_fill_ || fill_color_.a == LV_OPA_TRANSP || count_ < 3) {
        return;
    }

    float cx = 0.0f;
    float cy = 0.0f;
    for (int i = 0; i < count_; ++i) {
        cx += pts_[i].x;
        cy += pts_[i].y;
    }
    cx /= static_cast<float>(count_);
    cy /= static_cast<float>(count_);

    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = ToColor(fill_color_);
    dsc.opa = fill_color_.a;
    dsc.p[0].x = cx;
    dsc.p[0].y = cy;

    // A fill has no loose ends, so the outline is closed whether or not the
    // path said Close() -- the same reading the vector backend gives it.
    for (int i = 0; i < count_; ++i) {
        dsc.p[1] = pts_[i];
        dsc.p[2] = pts_[(i + 1) % count_];
        lv_draw_triangle(layer_, &dsc);
    }
}

void LvglPrimitiveSink::EmitStroke() {
    if (layer_ == nullptr || !has_stroke_ || stroke_color_.a == LV_OPA_TRANSP || count_ < 2) {
        return;
    }
    // The descriptor takes a whole-pixel width and this build has no sub-pixel
    // option, so the style's float width is rounded to the nearest pixel.
    const int32_t width = static_cast<int32_t>(stroke_width_ + 0.5f);
    if (width < 1) {
        return;
    }

    // A closed contour gets one segment more than it has points, because the
    // last segment runs back to the first point.
    const int segments = closed_ ? count_ : count_ - 1;
    for (int i = 0; i < segments; ++i) {
        // At each interior vertex both sides are capped round. Two round caps
        // meeting at a vertex cover exactly the disc a round join draws, so
        // this reproduces the layer's round joins rather than approximating
        // them -- and for an opaque stroke, which is every stroke of the face
        // itself, painting that disc twice is indistinguishable from once.
        const bool start_interior = closed_ || i > 0;
        const bool end_interior = closed_ || i + 1 < segments;

        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = ToColor(stroke_color_);
        dsc.opa = stroke_color_.a;
        dsc.width = width;
        dsc.p1 = pts_[i];
        dsc.p2 = pts_[(i + 1) % count_];
        dsc.round_start = RoundAt(start_interior, join_, cap_);
        dsc.round_end = RoundAt(end_interior, join_, cap_);
        lv_draw_line(layer_, &dsc);
    }
}

#endif  // LV_USE_VECTOR_GRAPHIC
