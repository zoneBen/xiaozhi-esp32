#include "cat_face_internal.h"

#include <cmath>

namespace cat_face {
namespace {

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// A pupil is filled in the ink color, so it reads as a solid dot inside the
// stroked eye. `frac` is the pupil radius as a fraction of the eye radius.
void DrawPupil(Emitter& e, const Pose& pose, float px, float py, float rx, float ry, float frac) {
    e.SetFill(pose.line_color);
    e.SetNoStroke();
    shapes::Ellipse(e, px, py, rx * frac, ry * frac);
}

// Three whiskers per side, rooted low on the cheek so the whole fan passes
// below the eye. `whisker_lift` rotates each tip about its root; the right side
// mirrors the sign so both sides sweep the same way visually.
void DrawWhiskers(Emitter& e, const Pose& pose, const internal::FaceMetrics& m) {
    const float root_y = m.cy + m.ry * internal::kWhiskerRootUy;
    const float spread = internal::kWhiskerTipSpreadUy + pose.whisker_spread;
    internal::ApplyInkThin(e, pose);

    for (int side = 0; side < 2; ++side) {
        const float s = (side == 0) ? -1.0f : 1.0f;
        const float root_x = m.cx + s * m.rx * internal::kWhiskerRootUx;
        const float tip_x = m.cx + s * m.rx * internal::kWhiskerTipUx;

        for (int i = 0; i < 3; ++i) {
            const float u = static_cast<float>(i) - 1.0f;  // -1, 0, +1
            const float ry_root = root_y + u * m.ry * 0.115f;
            const float ty = m.cy + m.ry * (internal::kWhiskerRootUy + u * spread);

            const float vx = tip_x - root_x;
            const float vy = ty - ry_root;
            const float c = cosf(pose.whisker_lift * s);
            const float sn = sinf(pose.whisker_lift * s);
            const float ex = root_x + vx * c - vy * sn;
            const float ey = ry_root + vx * sn + vy * c;

            // A slight downward bow stops them reading as rigid spokes.
            const float mx = (root_x + ex) * 0.5f;
            const float my = (ry_root + ey) * 0.5f + m.ry * 0.045f;

            e.BeginPath();
            e.MoveTo(root_x, ry_root);
            e.QuadTo(mx, my, ex, ey);
            e.EndPath();
        }
    }
}

// One brow. `toward_nose` is +1 for the left eye (the nose is to its right) and
// -1 for the right eye. brow_tilt > 0 swings the nose-side end down and in --
// the angry "V" -- while < 0 lifts it into a worried tilt.
void DrawBrow(Emitter& e, const Pose& pose, float ex, float ey, float r, float toward_nose) {
    const float inner_x = ex + toward_nose * r * 0.95f;  // nose-side end
    const float outer_x = ex - toward_nose * r * 1.18f;  // temple-side end
    // Level at rest, so brow_tilt alone decides the direction. Biasing the rest
    // pose would mean no tilt value could produce a neutral brow.
    //
    // Sitting this close above the eye is deliberate: further up the brows
    // float free of the face and read as a second pair of eyes.
    const float base_y = ey - r * 1.30f;
    const float drop = pose.brow_tilt * r * 1.60f;

    internal::ApplyInkThin(e, pose);
    // The temple end barely moves, so the angulation reads as a pivot there.
    shapes::Line(e, outer_x, base_y - drop * 0.30f, inner_x, base_y + drop);
}

}  // namespace

namespace internal {

void ApplyInk(Emitter& e, const Pose& pose) {
    e.SetNoFill();
    e.SetStroke(pose.line_color, pose.line_width, StrokeCap::kRound, StrokeJoin::kRound);
}

void ApplyInkThin(Emitter& e, const Pose& pose) {
    e.SetNoFill();
    e.SetStroke(pose.line_color, pose.line_width * 0.72f, StrokeCap::kRound, StrokeJoin::kRound);
}

void DrawEye(Emitter& e, const Pose& pose, const FaceMetrics& m, const EyePose& eye, float ex,
             float ey, float rx, float ry) {
    // Per-emotion size, further varied per invocation by the animator.
    rx *= eye.scale;
    ry *= eye.scale;

    // Blinking works by collapsing the vertical extent of whatever shape the
    // mode draws, rather than switching modes, so every eye type animates.
    const float open = Clamp(eye.open, 0.0f, 1.0f);
    const float rv = ry * open;
    const float px = ex + pose.pupil_dx;
    const float py = ey + pose.pupil_dy;

    switch (eye.mode) {
        case EyeMode::kOpen:
            ApplyInk(e, pose);
            shapes::Ellipse(e, ex, ey, rx, rv);
            DrawPupil(e, pose, px, py, rx, rv, 0.40f);
            break;

        case EyeMode::kNarrow:
            ApplyInk(e, pose);
            shapes::Ellipse(e, ex, ey, rx, rv * 0.42f);
            DrawPupil(e, pose, px, py, rx, rv * 0.42f, 0.44f);
            break;

        case EyeMode::kWide:
            ApplyInk(e, pose);
            shapes::Ellipse(e, ex, ey, rx * 1.22f, rv * 1.22f);
            DrawPupil(e, pose, px, py, rx * 1.22f, rv * 1.22f, 0.30f);
            break;

        case EyeMode::kArcUp: {
            // Smiling closed eye. Collapses to a line as open -> 0.
            ApplyInk(e, pose);
            const float base_y = ey + rv * 0.40f;
            const float ctrl_y = ey - rv * 1.45f;
            shapes::Curve(e, ex - rx, base_y, ex, ctrl_y, ex + rx, base_y);
            break;
        }

        case EyeMode::kArcDown: {
            ApplyInk(e, pose);
            const float base_y = ey - rv * 0.40f;
            const float ctrl_y = ey + rv * 1.45f;
            shapes::Curve(e, ex - rx, base_y, ex, ctrl_y, ex + rx, base_y);
            break;
        }

        case EyeMode::kHeart: {
            // Skipped when nearly shut -- a collapsed heart reads as a glitch,
            // unlike a collapsed ellipse which just looks like a blink.
            if (open < 0.12f) {
                break;
            }
            e.SetFill(pose.palette.blush);
            e.SetStroke(pose.line_color, pose.line_width * 0.75f, StrokeCap::kRound,
                        StrokeJoin::kRound);
            shapes::Heart(e, ex, ey, rx * 0.92f * open);
            break;
        }

        case EyeMode::kSpiral:
            ApplyInk(e, pose);
            shapes::Spiral(e, ex, ey, rx * (0.30f + 0.70f * open), 2.2f);
            break;

        case EyeMode::kTeary:
            ApplyInk(e, pose);
            shapes::Ellipse(e, ex, ey, rx, rv);
            DrawPupil(e, pose, px, py, rx, rv, 0.34f);
            // A bead of water welling at the lower rim.
            e.SetFill(pose.palette.tear);
            e.SetNoStroke();
            shapes::Drop(e, ex - rx * 0.55f, ey + rv * 0.95f, rx * 0.34f);
            break;
    }
}

void DrawMouth(Emitter& e, const Pose& pose, const FaceMetrics& m) {
    const float cx = m.cx;
    const float cy = m.cy + m.ry * kMouthUy;
    const float hw = m.rx * kMouthUw;
    const float open = Clamp(pose.mouth_open, 0.0f, 1.0f);
    ApplyInk(e, pose);

    switch (pose.mouth) {
        case MouthMode::kCatW: {
            // The classic cat "omega": two shallow loops meeting under the nose.
            const float top = cy - hw * 0.10f;
            const float depth = hw * (0.52f + 0.40f * open);
            e.BeginPath();
            e.MoveTo(cx - hw, top);
            e.QuadTo(cx - hw * 0.45f, top + depth, cx, top);
            e.QuadTo(cx + hw * 0.45f, top + depth, cx + hw, top);
            e.EndPath();
            break;
        }

        case MouthMode::kSmile:
            shapes::Curve(e, cx - hw, cy - hw * 0.15f, cx, cy + hw * (0.62f + 0.30f * open),
                          cx + hw, cy - hw * 0.15f);
            break;

        case MouthMode::kFrown:
            shapes::Curve(e, cx - hw, cy + hw * 0.32f, cx, cy - hw * 0.45f, cx + hw,
                          cy + hw * 0.32f);
            break;

        case MouthMode::kFlat:
            shapes::Line(e, cx - hw * 0.85f, cy, cx + hw * 0.85f, cy);
            break;

        case MouthMode::kOpenOh: {
            // Squat rather than tall: a narrow ellipse at this size reads as a
            // leaf, not an open mouth.
            const float orx = hw * 0.52f;
            const float ory = hw * (0.34f + 0.46f * open);
            shapes::Ellipse(e, cx, cy + ory * 0.35f, orx, ory);
            break;
        }

        case MouthMode::kGrin: {
            // Flat top with a bulging lower lip.
            const float top = cy - hw * (0.20f + 0.16f * open);
            const float bottom = top + hw * (0.95f + 0.30f * open);
            const float wide = hw * 1.10f;
            e.BeginPath();
            e.MoveTo(cx - wide, top);
            e.LineTo(cx + wide, top);
            e.QuadTo(cx, bottom + hw * 0.45f, cx - wide, top);
            e.Close();
            e.EndPath();
            break;
        }

        case MouthMode::kSadOpen: {
            const float orx = hw * 0.50f;
            const float ory = hw * (0.30f + 0.34f * open);
            const float oy = cy + ory * 0.30f;
            shapes::Ellipse(e, cx, oy, orx, ory);
            // Wavy upper lip.
            e.BeginPath();
            e.MoveTo(cx - orx, oy - ory * 0.55f);
            e.QuadTo(cx - orx * 0.40f, oy - ory * 1.05f, cx, oy - ory * 0.55f);
            e.QuadTo(cx + orx * 0.40f, oy - ory * 0.05f, cx + orx, oy - ory * 0.55f);
            e.EndPath();
            break;
        }

        case MouthMode::kTongue: {
            shapes::Curve(e, cx - hw, cy - hw * 0.15f, cx, cy + hw * 0.70f, cx + hw,
                          cy - hw * 0.15f);
            e.SetFill(pose.palette.tongue);
            e.SetStroke(pose.line_color, pose.line_width * 0.70f, StrokeCap::kRound,
                        StrokeJoin::kRound);
            shapes::Ellipse(e, cx, cy + hw * 0.62f, hw * 0.42f, hw * 0.40f);
            break;
        }
    }
}

}  // namespace internal

void Draw(const Pose& pose, PathSink& sink) {
    const internal::FaceMetrics m{pose.face_cx, pose.face_cy, pose.face_rx, pose.face_ry};

    // Clamped so 1/squash can neither blow up nor invert the face.
    const float squash = Clamp(pose.squash, 0.55f, 1.90f);

    Xform face;
    face.ox = pose.face_cx;
    face.oy = pose.face_cy;
    face.rot = pose.face_rot;
    face.sx = squash;
    face.sy = 1.0f / squash;
    face.dx = pose.dx;
    face.dy = pose.dy;

    // Marks hover: they follow the bob but must not lean or squash with the
    // face, or they would look painted onto the skin.
    Xform hover;
    hover.dx = pose.dx;
    hover.dy = pose.dy;

    Emitter e(sink, face);
    Emitter eh(sink, hover);

    // Blush is part of the face surface, not a feature, so it goes down before
    // anything that could be drawn on top of it -- otherwise a large blush
    // would clip the eye outlines.
    if (pose.blush > 0.01f) {
        RgbColor blush = pose.palette.blush;
        blush.a = static_cast<uint8_t>(Clamp(pose.blush, 0.0f, 1.0f) * 255.0f);
        e.SetFill(blush);
        e.SetNoStroke();
        const float bx = m.rx * internal::kBlushUx;
        const float by = m.cy + m.ry * internal::kBlushUy;
        shapes::Ellipse(e, m.cx - bx, by, m.rx * internal::kBlushUrx, m.ry * internal::kBlushUry);
        shapes::Ellipse(e, m.cx + bx, by, m.rx * internal::kBlushUrx, m.ry * internal::kBlushUry);
    }

    DrawWhiskers(e, pose, m);

    // Brows only appear when an emotion sets a tilt; zero means none.
    const float ex_l = m.cx - m.rx * internal::kEyeUx;
    const float ex_r = m.cx + m.rx * internal::kEyeUx;
    const float ey = m.cy + m.ry * internal::kEyeUy;
    const float ex_radius = m.rx * internal::kEyeUr;
    const float ey_radius = m.ry * internal::kEyeUr;

    if (fabsf(pose.brow_tilt) > 0.01f) {
        // Left eye: the nose is to its right. Right eye: to its left.
        DrawBrow(e, pose, ex_l, ey, ex_radius, 1.0f);
        DrawBrow(e, pose, ex_r, ey, ex_radius, -1.0f);
    }

    // The visor replaces the eyes rather than covering them, so no pupils poke
    // out from behind the lenses.
    if (pose.mark != MarkMode::kVisor) {
        internal::DrawEye(e, pose, m, pose.eye_l, ex_l, ey, ex_radius, ey_radius);
        internal::DrawEye(e, pose, m, pose.eye_r, ex_r, ey, ex_radius, ey_radius);
    }

    // Nose: a small solid triangle pointing down.
    {
        const float nose_y = m.cy + m.ry * internal::kNoseUy;
        const float nw = m.rx * internal::kNoseUw;
        const float nh = m.ry * internal::kNoseUh;
        e.SetFill(pose.line_color);
        e.SetNoStroke();
        e.BeginPath();
        e.MoveTo(m.cx - nw, nose_y - nh);
        e.LineTo(m.cx + nw, nose_y - nh);
        e.LineTo(m.cx, nose_y);
        e.Close();
        e.EndPath();
    }

    internal::DrawMouth(e, pose, m);

    internal::DrawMark(eh, pose, m);
}

}  // namespace cat_face
