#include "cat_face_internal.h"

#include <cmath>

// Floating expression marks: the glyphs that hover around the face rather than
// being part of it. The Emitter handed in here carries a translate-only
// transform (see Draw()), so marks follow the face's bob but do not lean or
// squash with it -- otherwise they would look painted onto the skin.
//
// Style discipline: every mark re-establishes its own stroke or fill before
// each shape, because composite glyphs (notably "?") change the style
// internally.

namespace cat_face {
namespace {

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Multiply a palette color's alpha by the mark's overall opacity.
RgbColor Fade(RgbColor color, float alpha) {
    const float a = (static_cast<float>(color.a) / 255.0f) * Clamp(alpha, 0.0f, 1.0f);
    color.a = static_cast<uint8_t>(Clamp(a, 0.0f, 1.0f) * 255.0f);
    return color;
}

float Wrap01(float v) { return v - floorf(v); }

// Ink stroke at a given opacity, for the stroked glyphs.
void StrokeInk(Emitter& e, const Pose& pose, RgbColor color, float alpha, float width_scale) {
    e.SetNoFill();
    e.SetStroke(Fade(color, alpha), pose.line_width * width_scale, StrokeCap::kRound,
                StrokeJoin::kRound);
}

void DrawTears(Emitter& e, const Pose& pose, const internal::FaceMetrics& m, float alpha,
               float phase) {
    const float ex = m.rx * internal::kEyeUx;
    const float ey = m.cy + m.ry * internal::kEyeUy;
    const float drop_r = m.rx * 0.095f;

    for (int i = 0; i < 2; ++i) {
        const float s = (i == 0) ? -1.0f : 1.0f;
        // Offset the two sides so they don't fall in lockstep.
        const float u = Wrap01(phase + (i == 0 ? 0.0f : 0.37f));
        e.SetFill(Fade(pose.palette.tear, alpha * (1.0f - u * 0.85f)));
        e.SetNoStroke();
        // Starts at the lower rim of the eye rather than inside it.
        shapes::Drop(e, m.cx + s * ex, ey + m.ry * (0.24f + u * 0.52f),
                     drop_r * (1.0f - u * 0.25f));
    }
}

void DrawSnooze(Emitter& e, const Pose& pose, const internal::FaceMetrics& m, float alpha,
                float phase) {
    // A trail of two "z"s rising to the upper right, each fading as it climbs.
    // The diagonal offsets keep the two from overlapping each other, and 1.18
    // is about as far right as the trailing glyph can start without its top
    // corner reaching the canvas edge.
    const float sizes[2] = {0.36f, 0.22f};
    const float start_x[2] = {0.92f, 1.18f};
    const float start_y[2] = {0.42f, 0.26f};
    const float offsets[2] = {0.0f, 0.45f};

    for (int i = 0; i < 2; ++i) {
        const float u = Wrap01(phase + offsets[i]);
        // Fades, but stays legible: at 0.6 the trailing "z" was a smudge.
        StrokeInk(e, pose, pose.line_color, alpha * (1.0f - u * 0.45f), 0.85f);
        shapes::ZGlyph(e, m.cx + m.rx * start_x[i], m.cy - m.ry * (start_y[i] + u * 0.62f),
                       m.rx * sizes[i] * (1.0f - u * 0.25f));
    }
}

void DrawQuestion(Emitter& e, const Pose& pose, const internal::FaceMetrics& m, float alpha,
                  float phase) {
    const float pulse = 1.0f + 0.10f * sinf(phase * shapes::kTau);
    StrokeInk(e, pose, pose.line_color, alpha, 0.85f);
    // Hangs off the upper right, clear of the right eye and inside the canvas.
    shapes::QuestionGlyph(e, m.cx + m.rx * 0.98f, m.cy - m.ry * 0.55f, m.rx * 0.46f * pulse);
}

void DrawAnger(Emitter& e, const Pose& pose, const internal::FaceMetrics& m, float alpha,
               float phase) {
    // A tense tremble, not a smooth drift.
    const float jx = m.rx * 0.022f * sinf(phase * shapes::kTau * 3.0f);
    const float jy = m.ry * 0.022f * cosf(phase * shapes::kTau * 2.0f);
    StrokeInk(e, pose, pose.palette.anger, alpha, 0.95f);
    // Above the right eye's outer end, where a comic vein belongs.
    shapes::AngerMark(e, m.cx + m.rx * 0.94f + jx, m.cy - m.ry * 0.40f + jy, m.rx * 0.26f);
}

void DrawSparkles(Emitter& e, const Pose& pose, const internal::FaceMetrics& m, float alpha,
                  float phase) {
    struct Spot {
        float ux;
        float uy;
        float ur;
        float phase_offset;
    };
    const Spot spots[3] = {
        {-0.88f, -1.02f, 0.20f, 0.00f},
        {0.94f, -0.62f, 0.15f, 0.35f},
        {0.80f, -1.16f, 0.13f, 0.68f},
    };

    for (const Spot& spot : spots) {
        const float twinkle =
            0.55f + 0.45f * sinf(Wrap01(phase + spot.phase_offset) * shapes::kTau);
        e.SetFill(Fade(pose.palette.sparkle, alpha * twinkle));
        e.SetNoStroke();
        shapes::Star4(e, m.cx + m.rx * spot.ux, m.cy + m.ry * spot.uy,
                      m.rx * spot.ur * (0.70f + 0.40f * twinkle));
    }
}

void DrawVisor(Emitter& e, const Pose& pose, const internal::FaceMetrics& m, float alpha) {
    // Sits where the eyes would be and is drawn INSTEAD of them -- see Draw().
    // Two lenses with a bridge rather than one bar: a single bar spanning the
    // whole eye band reads as a blindfold, not sunglasses.
    const float ey = m.cy + m.ry * internal::kEyeUy;
    const float lens_out = m.rx * 0.80f;
    const float lens_in = m.rx * 0.16f;
    const float lens_h = m.ry * 0.22f;
    const float radius = m.ry * 0.15f;

    e.SetFill(Fade(pose.palette.visor, alpha));
    e.SetStroke(pose.line_color, pose.line_width * 0.80f, StrokeCap::kRound, StrokeJoin::kRound);
    shapes::RoundedBar(e, m.cx - lens_out, ey - lens_h, m.cx - lens_in, ey + lens_h, radius);
    shapes::RoundedBar(e, m.cx + lens_in, ey - lens_h, m.cx + lens_out, ey + lens_h, radius);

    // Bridge over the nose, then the temple arms.
    shapes::Line(e, m.cx - lens_in, ey - lens_h * 0.30f, m.cx + lens_in, ey - lens_h * 0.30f);
    shapes::Line(e, m.cx - lens_out, ey - lens_h * 0.10f, m.cx - m.rx * 0.99f, ey - m.ry * 0.12f);
    shapes::Line(e, m.cx + lens_out, ey - lens_h * 0.10f, m.cx + m.rx * 0.99f, ey - m.ry * 0.12f);
}

void DrawNote(Emitter& e, const Pose& pose, const internal::FaceMetrics& m, float alpha,
              float phase) {
    const float u = Wrap01(phase);
    StrokeInk(e, pose, pose.line_color, alpha * (1.0f - u * 0.55f), 0.85f);
    shapes::NoteGlyph(e, m.cx + m.rx * 0.84f, m.cy - m.ry * (0.48f + u * 0.34f), m.rx * 0.46f);
}

void DrawHearts(Emitter& e, const Pose& pose, const internal::FaceMetrics& m, float alpha,
                float phase) {
    struct Spot {
        float ux;
        float uy;
        float ur;
        float phase_offset;
    };
    const Spot hearts[3] = {
        {-0.90f, -0.50f, 0.16f, 0.00f},
        {0.94f, -0.78f, 0.20f, 0.40f},
        {0.72f, -0.20f, 0.12f, 0.70f},
    };

    for (const Spot& spot : hearts) {
        const float u = Wrap01(phase + spot.phase_offset);
        e.SetFill(Fade(pose.palette.blush, alpha * (1.0f - u * 0.80f)));
        e.SetStroke(pose.line_color, pose.line_width * 0.70f, StrokeCap::kRound,
                    StrokeJoin::kRound);
        shapes::Heart(e, m.cx + m.rx * spot.ux, m.cy + m.ry * (spot.uy - u * 0.50f),
                      m.rx * spot.ur);
    }
}

}  // namespace

namespace internal {

void DrawMark(Emitter& e, const Pose& pose, const FaceMetrics& m) {
    const float alpha = Clamp(pose.mark_alpha, 0.0f, 1.0f);
    if (alpha <= 0.01f || pose.mark == MarkMode::kNone || pose.mark == MarkMode::kBlush) {
        return;
    }

    const float phase = Wrap01(pose.mark_phase);

    switch (pose.mark) {
        case MarkMode::kTear:
            DrawTears(e, pose, m, alpha, phase);
            break;
        case MarkMode::kSnooze:
            DrawSnooze(e, pose, m, alpha, phase);
            break;
        case MarkMode::kQuestion:
            DrawQuestion(e, pose, m, alpha, phase);
            break;
        case MarkMode::kAnger:
            DrawAnger(e, pose, m, alpha, phase);
            break;
        case MarkMode::kSparkle:
            DrawSparkles(e, pose, m, alpha, phase);
            break;
        case MarkMode::kVisor:
            DrawVisor(e, pose, m, alpha);
            break;
        case MarkMode::kNote:
            DrawNote(e, pose, m, alpha, phase);
            break;
        case MarkMode::kHeartMark:
            DrawHearts(e, pose, m, alpha, phase);
            break;
        case MarkMode::kNone:
        case MarkMode::kBlush:
            break;
    }
}

}  // namespace internal
}  // namespace cat_face
