#ifndef CAT_FACE_SHAPES_H
#define CAT_FACE_SHAPES_H

#include "cat_face.h"

// Primitive shapes for the cat face.
//
// Style contract: the caller sets stroke/fill immediately before each shape.
// A shape MAY change the style mid-way when it needs to (a composite glyph
// like "?" strokes the hook but fills the dot); because Draw() re-establishes
// the style before every shape, that never leaks into the next one.
//
// All coordinates are local and untransformed -- the Emitter applies the
// Xform, so shape code never deals with rotation or scaling.

namespace cat_face {

// Applies an Xform on the way out to the sink, so shapes can be written in
// plain local coordinates. Also remembers the current stroke so composite
// glyphs can match it.
class Emitter {
public:
    Emitter(PathSink& sink, const Xform& xform) : sink_(sink), xform_(xform) {}

    void BeginPath() { sink_.BeginPath(); }
    void MoveTo(float x, float y);
    void LineTo(float x, float y);
    void QuadTo(float cx, float cy, float x, float y);
    void CubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y);
    void Close() { sink_.Close(); }
    void EndPath() { sink_.EndPath(); }

    void SetStroke(RgbColor color, float width, StrokeCap cap, StrokeJoin join) {
        stroke_color_ = color;
        stroke_width_ = width;
        sink_.SetStroke(color, width, cap, join);
    }
    void SetFill(RgbColor color) { sink_.SetFill(color); }
    void SetNoStroke() { sink_.SetNoStroke(); }
    void SetNoFill() { sink_.SetNoFill(); }

    RgbColor stroke_color() const { return stroke_color_; }
    float stroke_width() const { return stroke_width_; }

private:
    PathSink& sink_;
    Xform xform_;
    RgbColor stroke_color_{};
    float stroke_width_ = 3.0f;
};

namespace shapes {

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kTau = 2.0f * kPi;

// Append an elliptical arc from a0 to a1 (radians, clockwise in screen
// coordinates) as cubic segments. This is the workhorse: ellipses, arcs, the
// round parts of drops and hearts all route through it.
//
// Pass move_first=false to continue an existing subpath without a MoveTo.
void AppendArc(Emitter& e, float cx, float cy, float rx, float ry, float a0, float a1,
               bool move_first = true);

// Closed full ellipse.
void Ellipse(Emitter& e, float cx, float cy, float rx, float ry);

// Open arc (no Close).
void Arc(Emitter& e, float cx, float cy, float rx, float ry, float a0, float a1);

// Straight line segment.
void Line(Emitter& e, float x0, float y0, float x1, float y1);

// Open quadratic curve.
void Curve(Emitter& e, float x0, float y0, float cx, float cy, float x1, float y1);

// Filled heart, r = half-width.
void Heart(Emitter& e, float cx, float cy, float r);

// Filled teardrop, tip pointing up.
void Drop(Emitter& e, float cx, float cy, float r);

// Filled four-point sparkle.
void Star4(Emitter& e, float cx, float cy, float r);

// The letter "z", drawn as three strokes. Used for the sleepy mark.
void ZGlyph(Emitter& e, float cx, float cy, float size);

// Question mark: a hook plus a dot. Used for thinking and confusion.
void QuestionGlyph(Emitter& e, float cx, float cy, float size);

// Archimedean swirl, drawn as a coarse polyline. Used for dizzy eyes.
void Spiral(Emitter& e, float cx, float cy, float r_max, float turns);

// The anime popping-vein mark.
void AngerMark(Emitter& e, float cx, float cy, float r);

// Eighth note: filled head, stem, flag.
void NoteGlyph(Emitter& e, float cx, float cy, float size);

// Horizontal bar with rounded ends. The visor lens is two of these plus arms.
void RoundedBar(Emitter& e, float x0, float y0, float x1, float y1, float radius);

}  // namespace shapes
}  // namespace cat_face

#endif  // CAT_FACE_SHAPES_H
