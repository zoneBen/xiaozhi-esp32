#include "cat_face_shapes.h"

namespace cat_face {

void Emitter::MoveTo(float x, float y) {
    float tx = 0.0f;
    float ty = 0.0f;
    xform_.Apply(x, y, tx, ty);
    sink_.MoveTo(tx, ty);
}

void Emitter::LineTo(float x, float y) {
    float tx = 0.0f;
    float ty = 0.0f;
    xform_.Apply(x, y, tx, ty);
    sink_.LineTo(tx, ty);
}

void Emitter::QuadTo(float cx, float cy, float x, float y) {
    float tx = 0.0f;
    float ty = 0.0f;
    xform_.Apply(cx, cy, tx, ty);
    float ex = 0.0f;
    float ey = 0.0f;
    xform_.Apply(x, y, ex, ey);
    sink_.QuadTo(tx, ty, ex, ey);
}

void Emitter::CubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) {
    float t1x = 0.0f;
    float t1y = 0.0f;
    xform_.Apply(c1x, c1y, t1x, t1y);
    float t2x = 0.0f;
    float t2y = 0.0f;
    xform_.Apply(c2x, c2y, t2x, t2y);
    float ex = 0.0f;
    float ey = 0.0f;
    xform_.Apply(x, y, ex, ey);
    sink_.CubicTo(t1x, t1y, t2x, t2y, ex, ey);
}

namespace shapes {

void AppendArc(Emitter& e, float cx, float cy, float rx, float ry, float a0, float a1,
               bool move_first) {
    const float sweep = a1 - a0;
    int segments = static_cast<int>(ceilf(fabsf(sweep) / (kPi * 0.5f)));
    if (segments < 1) {
        segments = 1;
    }
    const float dt = sweep / static_cast<float>(segments);
    // Standard cubic approximation of a circular arc: the control points sit
    // at (4/3)tan(dt/4) times the parametric derivative from each end. The
    // rx/ry factors below scale that derivative for the ellipse.
    const float alpha = (4.0f / 3.0f) * tanf(dt * 0.25f);

    float t0 = a0;
    float px = cx + rx * cosf(t0);
    float py = cy + ry * sinf(t0);
    if (move_first) {
        e.MoveTo(px, py);
    }

    for (int i = 0; i < segments; ++i) {
        const float t1 = t0 + dt;
        const float qx = cx + rx * cosf(t1);
        const float qy = cy + ry * sinf(t1);

        // d/dt (cx + rx cos t, cy + ry sin t) = (-rx sin t, ry cos t)
        const float c0x = px - alpha * rx * sinf(t0);
        const float c0y = py + alpha * ry * cosf(t0);
        const float c1x = qx + alpha * rx * sinf(t1);
        const float c1y = qy - alpha * ry * cosf(t1);

        e.CubicTo(c0x, c0y, c1x, c1y, qx, qy);

        t0 = t1;
        px = qx;
        py = qy;
    }
}

void Ellipse(Emitter& e, float cx, float cy, float rx, float ry) {
    e.BeginPath();
    AppendArc(e, cx, cy, rx, ry, 0.0f, kTau, true);
    e.Close();
    e.EndPath();
}

void Arc(Emitter& e, float cx, float cy, float rx, float ry, float a0, float a1) {
    e.BeginPath();
    AppendArc(e, cx, cy, rx, ry, a0, a1, true);
    e.EndPath();
}

void Line(Emitter& e, float x0, float y0, float x1, float y1) {
    e.BeginPath();
    e.MoveTo(x0, y0);
    e.LineTo(x1, y1);
    e.EndPath();
}

void Curve(Emitter& e, float x0, float y0, float cx, float cy, float x1, float y1) {
    e.BeginPath();
    e.MoveTo(x0, y0);
    e.QuadTo(cx, cy, x1, y1);
    e.EndPath();
}

void Heart(Emitter& e, float cx, float cy, float r) {
    e.BeginPath();
    e.MoveTo(cx, cy + r * 0.78f);
    e.CubicTo(cx - r * 1.55f, cy - r * 0.45f, cx - r * 0.62f, cy - r * 1.12f, cx, cy - r * 0.34f);
    e.CubicTo(cx + r * 0.62f, cy - r * 1.12f, cx + r * 1.55f, cy - r * 0.45f, cx, cy + r * 0.78f);
    e.Close();
    e.EndPath();
}

void Drop(Emitter& e, float cx, float cy, float r) {
    e.BeginPath();
    e.MoveTo(cx, cy - r);
    e.CubicTo(cx + r * 0.75f, cy - r * 0.05f, cx + r * 0.92f, cy + r * 0.42f, cx + r * 0.62f,
              cy + r * 0.66f);
    e.CubicTo(cx + r * 0.30f, cy + r * 0.95f, cx - r * 0.30f, cy + r * 0.95f, cx - r * 0.62f,
              cy + r * 0.66f);
    e.CubicTo(cx - r * 0.92f, cy + r * 0.42f, cx - r * 0.75f, cy - r * 0.05f, cx, cy - r);
    e.Close();
    e.EndPath();
}

void Star4(Emitter& e, float cx, float cy, float r) {
    const float waist = r * 0.16f;
    e.BeginPath();
    e.MoveTo(cx, cy - r);
    e.QuadTo(cx + waist, cy - waist, cx + r, cy);
    e.QuadTo(cx + waist, cy + waist, cx, cy + r);
    e.QuadTo(cx - waist, cy + waist, cx - r, cy);
    e.QuadTo(cx - waist, cy - waist, cx, cy - r);
    e.Close();
    e.EndPath();
}

void ZGlyph(Emitter& e, float cx, float cy, float size) {
    const float half_w = size * 0.34f;
    const float half_h = size * 0.5f;
    e.BeginPath();
    e.MoveTo(cx - half_w, cy - half_h);
    e.LineTo(cx + half_w, cy - half_h);
    e.MoveTo(cx + half_w, cy - half_h);
    e.LineTo(cx - half_w, cy + half_h);
    e.MoveTo(cx - half_w, cy + half_h);
    e.LineTo(cx + half_w, cy + half_h);
    e.EndPath();
}

void QuestionGlyph(Emitter& e, float cx, float cy, float size) {
    // Bowl: swept the "over the top" way from just above the left equator to
    // just below the right one, so it opens downward the way a question mark
    // does. Stopping short of a full turn is what stops it reading as a circle
    // with a tail -- a nearly-closed loop was the earlier mistake here.
    const float r = size * 0.32f;
    const float bowl_cy = cy - r * 0.32f;
    e.BeginPath();
    AppendArc(e, cx, bowl_cy, r, r, kPi * 1.08f, kPi * 2.10f, true);
    // Tail: swing out to the right, then curl back in to the stem base.
    e.QuadTo(cx + r * 1.12f, bowl_cy + r * 1.08f, cx, bowl_cy + r * 0.98f);
    e.EndPath();

    // Dot, filled in the stroke color so the two parts read as one glyph.
    e.SetFill(e.stroke_color());
    e.SetNoStroke();
    Ellipse(e, cx, bowl_cy + r * 1.48f, r * 0.19f, r * 0.19f);
}

void Spiral(Emitter& e, float cx, float cy, float r_max, float turns) {
    // Coarse polyline; at this size the facets are invisible and it keeps the
    // per-frame path count low.
    int points = static_cast<int>(turns * 10.0f);
    if (points < 8) {
        points = 8;
    }
    e.BeginPath();
    for (int i = 0; i <= points; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(points);
        const float angle = u * turns * kTau;
        const float radius = r_max * u;
        const float px = cx + radius * cosf(angle);
        const float py = cy + radius * sinf(angle);
        if (i == 0) {
            e.MoveTo(px, py);
        } else {
            e.LineTo(px, py);
        }
    }
    e.EndPath();
}

void AngerMark(Emitter& e, float cx, float cy, float r) {
    // The anime "popping vein": four short arms in an X, each slightly bent.
    e.BeginPath();
    for (int i = 0; i < 4; ++i) {
        const float angle = kPi * 0.25f + static_cast<float>(i) * kPi * 0.5f;
        const float ca = cosf(angle);
        const float sa = sinf(angle);
        const float bend = r * 0.16f;
        e.MoveTo(cx + ca * r * 0.28f, cy + sa * r * 0.28f);
        e.QuadTo(cx + ca * r * 0.62f - sa * bend, cy + sa * r * 0.62f + ca * bend, cx + ca * r,
                 cy + sa * r);
    }
    e.EndPath();
}

void NoteGlyph(Emitter& e, float cx, float cy, float size) {
    // Head.
    e.BeginPath();
    AppendArc(e, cx - size * 0.28f, cy + size * 0.30f, size * 0.26f, size * 0.20f, 0.0f, kTau,
              true);
    e.Close();
    e.EndPath();

    // Stem and flag.
    e.BeginPath();
    e.MoveTo(cx - size * 0.02f, cy + size * 0.30f);
    e.LineTo(cx - size * 0.02f, cy - size * 0.45f);
    e.QuadTo(cx + size * 0.34f, cy - size * 0.30f, cx + size * 0.16f, cy - size * 0.02f);
    e.EndPath();
}

void RoundedBar(Emitter& e, float x0, float y0, float x1, float y1, float radius) {
    const float r = radius;
    e.BeginPath();
    e.MoveTo(x0 + r, y0);
    e.LineTo(x1 - r, y0);
    e.QuadTo(x1, y0, x1, y0 + r);
    e.LineTo(x1, y1 - r);
    e.QuadTo(x1, y1, x1 - r, y1);
    e.LineTo(x0 + r, y1);
    e.QuadTo(x0, y1, x0, y1 - r);
    e.LineTo(x0, y0 + r);
    e.QuadTo(x0, y0, x0 + r, y0);
    e.Close();
    e.EndPath();
}

}  // namespace shapes
}  // namespace cat_face
