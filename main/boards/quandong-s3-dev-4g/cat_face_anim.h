#ifndef CAT_FACE_ANIM_H
#define CAT_FACE_ANIM_H

#include "cat_face.h"

// Drives a Pose over time from an EmotionSpec.
//
// The important invariant: the spec's `base` pose is copied verbatim on every
// rebuild and the animator only ever ADDS bounded offsets to a fixed set of
// motion channels (bob, sway, squash, blink, pupil drift, whisker motion,
// blush pulse, mark phase). It never touches eye mode, mouth mode, the
// expression mark, or the base gaze -- so an emotion can drift, breathe and
// twitch without ever becoming a different emotion.
//
// Randomization is rolled once per SetEmotion() and then held, so an expression
// is consistently "this" cat rather than re-rolling every frame.

namespace cat_face {

class Animator {
public:
    explicit Animator(uint32_t seed = 0x9e3779b9u) : rng_(seed) {}

    // Switch expression. Re-rolls this invocation's random offsets; the pop
    // animation only replays if this is genuinely a new expression (see
    // bottom of cat_face_anim.cc for why).
    void SetEmotion(Emotion emotion);

    // Advance by dt_ms. The step is clamped internally, so a stalled tick --
    // LVGL is stopped during light sleep, see sleep_timer.cc -- cannot snap the
    // pose to a new position on wake.
    void Tick(uint32_t dt_ms);

    const Pose& pose() const { return pose_; }
    Emotion emotion() const { return emotion_; }

    // The board pushes the current theme's ink color here, once per frame, from
    // the draw timer. Doing it per frame is what makes a theme switch take
    // effect without this class needing a SetTheme hook.
    void SetInk(RgbColor line_color, float line_width);

private:
    void Rebuild();
    void AdvanceBlink(float dt);
    // Not const: advances the generator.
    float NextBlinkGap();

    Emotion emotion_ = Emotion::kNeutral;
    Xorshift32 rng_;
    Pose pose_;
    RgbColor ink_{0x2b, 0x2b, 0x2b};
    float ink_width_ = 3.0f;

    // Integrated animation clock, in seconds. Only ever advances by a clamped
    // step.
    float clock_ = 0.0f;

    // Per-invocation randomization. Bounded offsets on phase and amplitude
    // only -- see the invariant above.
    struct Jitter {
        float lean = 0.0f;  // radians of constant face lean
        float bob_phase = 0.0f;
        float sway_phase = 0.0f;
        float whisker_phase = 0.0f;
        float pupil_phase_x = 0.0f;
        float pupil_phase_y = 0.0f;
        float blush_phase = 0.0f;
        float mark_phase = 0.0f;
        float eye_scale = 1.0f;
        float blush_gain = 1.0f;
    };
    Jitter jitter_;

    // Blink scheduling. blink_u_ < 0 means no blink in progress.
    float blink_countdown_ = 0.0f;
    float blink_u_ = -1.0f;

    // Expression pop-in (elastic squash), in seconds since the transition.
    float pop_age_ = 0.0f;
};

}  // namespace cat_face

#endif  // CAT_FACE_ANIM_H
