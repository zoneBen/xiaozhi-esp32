#include "cat_face_anim.h"

#include <cmath>

namespace cat_face {
namespace {

// A stalled tick must not advance the animation by the whole gap. LVGL is
// stopped during light sleep (sleep_timer.cc calls lvgl_port_stop), so the
// first tick after wake can carry seconds of dt. Clamping turns that into one
// slightly-late frame instead of a visible jump.
constexpr uint32_t kMaxStepMs = 100;

constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 2.0f * kPi;

// Elastic pop-in on entering an expression.
constexpr float kPopDuration = 0.28f;
constexpr float kPopAmplitude = 0.16f;

// Half-ranges for the per-invocation randomization. Small on purpose: this is
// what stops every "happy" looking identical, not what makes the cat move.
constexpr float kLeanHalfRange = 0.026f;     // ~1.5 degrees
constexpr float kEyeScaleHalfRange = 0.05f;  // ~5%
constexpr float kBlushGainHalfRange = 0.10f;

// A repeated SetEmotion won't replay the pop sooner than this.
constexpr float kRepopCooldownS = 1.0f;

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

void Animator::SetEmotion(Emotion emotion) {
    const bool changed = (emotion != emotion_);
    emotion_ = emotion;
    const MotionSpec& mo = SpecFor(emotion).motion;

    // Roll this invocation's random offsets, once. They are held for the life
    // of the expression so the cat is consistently "this" cat rather than
    // re-rolling every frame -- and every one of them is a bounded phase or
    // gain, so the expression stays recognizable by construction.
    const float j = mo.jitter;
    jitter_.lean = rng_.NextSigned(kLeanHalfRange) * j;
    jitter_.bob_phase = rng_.NextFloat(kTau);
    jitter_.sway_phase = rng_.NextFloat(kTau);
    jitter_.whisker_phase = rng_.NextFloat(kTau);
    jitter_.pupil_phase_x = rng_.NextFloat(kTau);
    jitter_.pupil_phase_y = rng_.NextFloat(kTau);
    jitter_.blush_phase = rng_.NextFloat(kTau);
    jitter_.mark_phase = rng_.NextFloat(1.0f);
    jitter_.eye_scale = 1.0f + rng_.NextSigned(kEyeScaleHalfRange) * j;
    jitter_.blush_gain = 1.0f + rng_.NextSigned(kBlushGainHalfRange) * j;

    // Stagger the first blink so nothing pulses in lockstep.
    blink_u_ = -1.0f;
    blink_countdown_ = NextBlinkGap() * (0.30f + 0.70f * rng_.NextFloat(1.0f));

    // Replay the pop only for a genuinely new expression. A server repeating
    // the same emotion would otherwise re-pop the cat on every message.
    if (changed || pop_age_ >= kRepopCooldownS) {
        pop_age_ = 0.0f;
    }
}

void Animator::Tick(uint32_t dt_ms) {
    const uint32_t step = dt_ms > kMaxStepMs ? kMaxStepMs : dt_ms;
    const float dt = static_cast<float>(step) / 1000.0f;

    clock_ += dt;
    if (pop_age_ < kPopDuration) {
        pop_age_ += dt;
    }

    AdvanceBlink(dt);
    Rebuild();
}

void Animator::SetInk(RgbColor line_color, float line_width) {
    ink_ = line_color;
    ink_width_ = line_width;
}

void Animator::AdvanceBlink(float dt) {
    const EmotionSpec& spec = SpecFor(emotion_);
    // Spiral eyes have no lid; collapsing one would just erase the swirl.
    if (spec.base.eye_l.mode == EyeMode::kSpiral) {
        return;
    }

    if (blink_u_ >= 0.0f) {
        blink_u_ += dt / spec.motion.blink_hold_s;
        if (blink_u_ >= 1.0f) {
            blink_u_ = -1.0f;
            blink_countdown_ = NextBlinkGap();
        }
    } else {
        blink_countdown_ -= dt;
        if (blink_countdown_ <= 0.0f) {
            blink_u_ = 0.0f;
        }
    }
}

float Animator::NextBlinkGap() {
    const MotionSpec& mo = SpecFor(emotion_).motion;
    const float span = mo.blink_max_s - mo.blink_min_s;
    const float base = mo.blink_min_s + (span > 0.0f ? rng_.NextFloat(span) : 0.0f);
    // A little irregularity, so blinks never feel metronomic.
    return base * (0.85f + 0.30f * rng_.NextFloat(1.0f));
}

void Animator::Rebuild() {
    const EmotionSpec& spec = SpecFor(emotion_);
    const MotionSpec& mo = spec.motion;

    // The base pose is copied verbatim. Everything below adds to a motion
    // channel -- no eye mode, mouth mode, mark or base gaze is ever written
    // here, which is what keeps a randomized cat recognizable.
    pose_ = spec.base;
    pose_.line_color = ink_;
    pose_.line_width = ink_width_;

    // Breathing.
    pose_.dy += mo.bob_amp * sinf(kTau * mo.bob_rate * clock_ + jitter_.bob_phase);

    // Micro-sway, plus this invocation's constant lean.
    pose_.face_rot +=
        jitter_.lean + mo.sway_amp * sinf(kTau * mo.sway_rate * clock_ + jitter_.sway_phase);

    // This invocation's eye-size variation, on top of the emotion's own scale.
    pose_.eye_l.scale = spec.base.eye_l.scale * jitter_.eye_scale;
    pose_.eye_r.scale = spec.base.eye_r.scale * jitter_.eye_scale;

    // Blink: collapse the vertical extent of the eye. Scaling rather than
    // switching modes preserves each emotion's base openness, so sleepy's
    // heavy lids stay heavy while still blinking.
    if (blink_u_ >= 0.0f) {
        const float factor = fabsf(cosf(blink_u_ * kPi));  // 1 -> 0 -> 1
        pose_.eye_l.open = spec.base.eye_l.open * factor;
        pose_.eye_r.open = spec.base.eye_r.open * factor;
    }

    // Slow pupil wander, around whatever gaze the emotion set.
    pose_.pupil_dx += mo.pupil_drift * sinf(kTau * 0.13f * clock_ + jitter_.pupil_phase_x);
    pose_.pupil_dy += mo.pupil_drift * 0.60f * sinf(kTau * 0.09f * clock_ + jitter_.pupil_phase_y);

    // Whiskers.
    pose_.whisker_lift += mo.whisker_amp * sinf(kTau * 0.31f * clock_ + jitter_.whisker_phase);

    // Blush pulses -- but only where the emotion has blush at all, so this can
    // never invent blush on an emotion that has none.
    if (spec.base.blush > 0.0f) {
        const float pulse = 1.0f + 0.12f * sinf(kTau * 0.25f * clock_ + jitter_.blush_phase);
        pose_.blush = Clamp(spec.base.blush * jitter_.blush_gain * pulse, 0.0f, 1.0f);
    }

    // Expression pop: an elastic squash on the way into the emotion.
    if (pop_age_ < kPopDuration) {
        const float u = pop_age_ / kPopDuration;
        pose_.squash *= 1.0f + kPopAmplitude * (1.0f - u) * cosf(u * kTau * 2.0f);
    }

    // Mark animation. Glyphs read `phase` as a 0..1 loop and animate themselves
    // along it: drops fall, "z"s rise, sparkles twinkle, veins tremble.
    pose_.mark_phase = clock_ * mo.mark_rate + jitter_.mark_phase;
}

}  // namespace cat_face
