#include "cat_face.h"

#include <cstring>

// The emotion table.
//
// This is the single place to tune an emotion: its `base` pose says what makes
// it recognizable, and its `motion` says how it moves. Both are per-emotion on
// purpose -- "how it moves" carries as much of an expression as "how it looks",
// so a sleepy cat and an angry cat get completely different motion, not a
// shared default with a different face.
//
// Randomization never touches `base`. The animator only layers bounded offsets
// on top, so an emotion can never drift into looking like a different one.

namespace cat_face {
namespace {

// Fluent builders. Every Pose/MotionSpec field has a sensible default, so a
// row only states what it overrides -- but the defaults are deliberately
// neutral, which is why each row below sets the channels it cares about
// explicitly.
struct Poser {
    Pose p;

    Poser& Eye(EyeMode mode) {
        p.eye_l.mode = mode;
        p.eye_r.mode = mode;
        return *this;
    }
    Poser& Eyes(EyeMode left, EyeMode right) {
        p.eye_l.mode = left;
        p.eye_r.mode = right;
        return *this;
    }
    Poser& EyeOpen(float left, float right) {
        p.eye_l.open = left;
        p.eye_r.open = right;
        return *this;
    }
    Poser& EyeScale(float scale) {
        p.eye_l.scale = scale;
        p.eye_r.scale = scale;
        return *this;
    }
    Poser& Mouth(MouthMode mode, float open = 0.0f) {
        p.mouth = mode;
        p.mouth_open = open;
        return *this;
    }
    Poser& Mark(MarkMode mode) {
        p.mark = mode;
        return *this;
    }
    Poser& Blush(float value) {
        p.blush = value;
        return *this;
    }
    Poser& Brows(float tilt) {
        p.brow_tilt = tilt;
        return *this;
    }
    Poser& Pupil(float dx, float dy) {
        p.pupil_dx = dx;
        p.pupil_dy = dy;
        return *this;
    }
    Poser& Whiskers(float lift, float spread = 0.0f) {
        p.whisker_lift = lift;
        p.whisker_spread = spread;
        return *this;
    }
};

struct Mot {
    MotionSpec m;

    Mot& Bob(float amp, float rate) {
        m.bob_amp = amp;
        m.bob_rate = rate;
        return *this;
    }
    Mot& Sway(float amp, float rate) {
        m.sway_amp = amp;
        m.sway_rate = rate;
        return *this;
    }
    Mot& Blink(float min_s, float max_s) {
        m.blink_min_s = min_s;
        m.blink_max_s = max_s;
        return *this;
    }
    Mot& BlinkHold(float s) {
        m.blink_hold_s = s;
        return *this;
    }
    Mot& Pupils(float drift) {
        m.pupil_drift = drift;
        return *this;
    }
    Mot& Whiskers(float amp) {
        m.whisker_amp = amp;
        return *this;
    }
    Mot& MarkRate(float hz) {
        m.mark_rate = hz;
        return *this;
    }
    Mot& Jitter(float value) {
        m.jitter = value;
        return *this;
    }
};

// Indexed by Emotion; the order must match the enum.
const EmotionSpec kSpecs[] = {
    // neutral: the baseline. Moderate everything, nothing exaggerated.
    {"neutral", Poser().Eye(EyeMode::kOpen).Mouth(MouthMode::kCatW).p,
     Mot().Bob(2.0f, 0.32f).Sway(0.018f, 0.19f).Blink(3.0f, 7.0f).Whiskers(0.05f).Jitter(1.0f).m},

    // happy: bright and bouncy, smiling closed eyes, a little blush.
    {"happy", Poser().Eye(EyeMode::kArcUp).Mouth(MouthMode::kSmile).Blush(0.35f).p,
     Mot().Bob(2.8f, 0.55f).Sway(0.024f, 0.26f).Blink(2.0f, 4.5f).Whiskers(0.09f).Jitter(1.15f).m},

    // laughing: same eyes, open mouth, much bigger bounce.
    {"laughing", Poser().Eye(EyeMode::kArcUp).Mouth(MouthMode::kOpenOh, 0.90f).Blush(0.50f).p,
     Mot()
         .Bob(4.0f, 0.85f)
         .Sway(0.030f, 0.40f)
         .Blink(1.8f, 3.5f)
         .Whiskers(0.12f)
         .MarkRate(0.7f)
         .Jitter(1.30f)
         .m},

    // funny: deliberately lopsided -- one smiling eye, one wide open.
    {"funny",
     Poser()
         .Eyes(EyeMode::kArcUp, EyeMode::kOpen)
         .Mouth(MouthMode::kGrin, 0.60f)
         .Blush(0.30f)
         .Mark(MarkMode::kSparkle)
         .p,
     Mot()
         .Bob(3.2f, 0.70f)
         .Sway(0.028f, 0.33f)
         .Blink(2.2f, 4.0f)
         .Whiskers(0.11f)
         .MarkRate(0.8f)
         .Jitter(1.35f)
         .m},

    // sad: drooping arcs, worried brows, pupils cast down. Slow and small.
    {"sad",
     Poser()
         .Eye(EyeMode::kArcDown)
         .Mouth(MouthMode::kFrown)
         .Blush(0.15f)
         .Brows(-0.10f)
         .Pupil(0.0f, 0.8f)
         .p,
     Mot()
         .Bob(1.1f, 0.18f)
         .Sway(0.010f, 0.11f)
         .Blink(3.5f, 7.5f)
         .Pupils(1.0f)
         .Whiskers(0.02f)
         .Jitter(0.70f)
         .m},

    // angry: narrowed eyes, brows driven down and in, popping-vein mark. Tense
    // and twitchy rather than big -- anger reads as tension, not motion.
    {"angry",
     Poser().Eye(EyeMode::kNarrow).Mouth(MouthMode::kFrown).Brows(0.22f).Mark(MarkMode::kAnger).p,
     Mot()
         .Bob(1.0f, 0.90f)
         .Sway(0.030f, 0.90f)
         .Blink(3.0f, 6.0f)
         .Pupils(0.5f)
         .Whiskers(0.14f)
         .MarkRate(1.10f)
         .Jitter(1.20f)
         .m},

    // crying: teary eyes, wavy open mouth, falling drops. The rhythm is a sob.
    {"crying",
     Poser()
         .Eye(EyeMode::kTeary)
         .Mouth(MouthMode::kSadOpen, 0.50f)
         .Blush(0.20f)
         .Brows(-0.14f)
         .Mark(MarkMode::kTear)
         .p,
     Mot()
         .Bob(2.2f, 0.75f)
         .Sway(0.012f, 0.30f)
         .Blink(2.5f, 5.0f)
         .Whiskers(0.03f)
         .MarkRate(0.85f)
         .Jitter(0.90f)
         .m},

    // loving: heart eyes, heavy blush, floating hearts. Dreamy and slow.
    {"loving",
     Poser()
         .Eye(EyeMode::kHeart)
         .Mouth(MouthMode::kSmile)
         .Blush(0.60f)
         .Mark(MarkMode::kHeartMark)
         .p,
     Mot()
         .Bob(2.4f, 0.30f)
         .Sway(0.030f, 0.22f)
         .Blink(3.0f, 6.0f)
         .Pupils(0.8f)
         .Whiskers(0.06f)
         .MarkRate(0.35f)
         .Jitter(1.0f)
         .m},

    // embarrassed: blushing hard with the gaze slid away, and blinking more
    // than usual. The shifty pupils are the whole joke, so drift is high.
    {"embarrassed",
     Poser()
         .Eye(EyeMode::kOpen)
         .Mouth(MouthMode::kSmile)
         .Blush(0.85f)
         .Brows(-0.06f)
         .Pupil(-3.0f, 1.5f)
         .p,
     Mot()
         .Bob(1.2f, 0.25f)
         .Sway(0.014f, 0.15f)
         .Blink(2.0f, 4.0f)
         .Pupils(2.6f)
         .Whiskers(0.04f)
         .Jitter(0.85f)
         .m},

    // surprised: wide eyes, small open mouth, brows up. Alert but still.
    {"surprised", Poser().Eye(EyeMode::kWide).Mouth(MouthMode::kOpenOh, 0.45f).p,
     Mot()
         .Bob(1.6f, 0.40f)
         .Sway(0.010f, 0.50f)
         .Blink(1.2f, 3.0f)
         .Pupils(0.8f)
         .Whiskers(0.10f)
         .Jitter(1.10f)
         .m},

    // shocked: a startle. Fast tremor and a hard overshoot, plus sparkles.
    {"shocked",
     Poser()
         .Eye(EyeMode::kWide)
         .EyeScale(1.10f)
         .Mouth(MouthMode::kOpenOh, 1.00f)
         .Mark(MarkMode::kSparkle)
         .p,
     Mot()
         .Bob(1.4f, 1.10f)
         .Sway(0.040f, 1.20f)
         .Blink(0.9f, 2.2f)
         .Whiskers(0.16f)
         .MarkRate(1.40f)
         .Jitter(1.50f)
         .m},

    // thinking: gaze up and off to the side, one brow lifted, flat mouth.
    {"thinking",
     Poser()
         .Eye(EyeMode::kNarrow)
         .Mouth(MouthMode::kFlat)
         .Brows(0.06f)
         .Mark(MarkMode::kQuestion)
         .Pupil(2.5f, -1.5f)
         .p,
     Mot()
         .Bob(1.3f, 0.22f)
         .Sway(0.012f, 0.13f)
         .Blink(2.8f, 6.0f)
         .Pupils(1.2f)
         .Whiskers(0.04f)
         .MarkRate(0.50f)
         .Jitter(0.90f)
         .m},

    // winking: one smiling eye, one open. Cheeky little bob.
    {"winking",
     Poser().Eyes(EyeMode::kArcUp, EyeMode::kOpen).Mouth(MouthMode::kSmile).Blush(0.25f).p,
     Mot().Bob(2.6f, 0.60f).Sway(0.026f, 0.30f).Blink(2.5f, 5.0f).Whiskers(0.10f).Jitter(1.10f).m},

    // cool: the visor REPLACES the eyes (see Draw()), so eye mode is unused
    // here. Unhurried and unbothered.
    {"cool", Poser().Mouth(MouthMode::kSmile).Mark(MarkMode::kVisor).p,
     Mot().Bob(1.8f, 0.28f).Sway(0.016f, 0.16f).Blink(3.0f, 7.0f).Whiskers(0.05f).Jitter(0.90f).m},

    // relaxed: deep slow breathing, long blinks. The calmest row.
    {"relaxed", Poser().Eye(EyeMode::kArcUp).Mouth(MouthMode::kSmile).Blush(0.20f).p,
     Mot()
         .Bob(3.0f, 0.20f)
         .Sway(0.014f, 0.10f)
         .Blink(3.5f, 8.0f)
         .Pupils(1.0f)
         .Whiskers(0.05f)
         .Jitter(0.80f)
         .m},

    // delicious: grinning with the tongue out, sparkles, happy-chewing bounce.
    {"delicious",
     Poser().Eye(EyeMode::kArcUp).Mouth(MouthMode::kTongue).Blush(0.35f).Mark(MarkMode::kSparkle).p,
     Mot()
         .Bob(2.6f, 0.65f)
         .Sway(0.022f, 0.35f)
         .Blink(2.4f, 4.5f)
         .Whiskers(0.12f)
         .MarkRate(0.80f)
         .Jitter(1.20f)
         .m},

    // kissy: pursed "omega" mouth, heavy blush, floating hearts.
    {"kissy",
     Poser().Eye(EyeMode::kArcUp).Mouth(MouthMode::kCatW).Blush(0.65f).Mark(MarkMode::kHeartMark).p,
     Mot()
         .Bob(2.0f, 0.50f)
         .Sway(0.020f, 0.40f)
         .Blink(2.2f, 4.2f)
         .Whiskers(0.08f)
         .MarkRate(0.50f)
         .Jitter(1.10f)
         .m},

    // confident: narrowed eyes, a smug brow, an unhurried blink.
    {"confident", Poser().Eye(EyeMode::kNarrow).Mouth(MouthMode::kSmile).Brows(0.08f).p,
     Mot()
         .Bob(1.5f, 0.26f)
         .Sway(0.014f, 0.18f)
         .Blink(3.0f, 6.5f)
         .Pupils(0.8f)
         .Whiskers(0.06f)
         .Jitter(0.85f)
         .m},

    // sleepy: heavy lids, "z"s drifting up. The drowsy reference row -- very
    // slow breathing, long held blinks, almost no sway.
    {"sleepy",
     Poser()
         .Eye(EyeMode::kNarrow)
         .EyeOpen(0.35f, 0.35f)
         .Mouth(MouthMode::kCatW)
         .Blush(0.15f)
         .Mark(MarkMode::kSnooze)
         .p,
     Mot()
         .Bob(3.6f, 0.16f)
         .Sway(0.008f, 0.08f)
         .Blink(5.0f, 11.0f)
         .BlinkHold(0.28f)
         .Pupils(0.6f)
         .Whiskers(0.02f)
         .MarkRate(0.22f)
         .Jitter(0.60f)
         .m},

    // silly: a wonky wink, tongue out, and deliberately exaggerated motion
    // with the highest jitter in the table.
    {"silly",
     Poser().Eyes(EyeMode::kOpen, EyeMode::kArcUp).Mouth(MouthMode::kTongue).Blush(0.40f).p,
     Mot()
         .Bob(3.8f, 0.95f)
         .Sway(0.040f, 0.55f)
         .Blink(1.5f, 3.0f)
         .Pupils(2.6f)
         .Whiskers(0.18f)
         .Jitter(1.60f)
         .m},

    // confused: dizzy spiral eyes, a "?" hanging overhead, unsettled wobble.
    {"confused",
     Poser()
         .Eye(EyeMode::kSpiral)
         .Mouth(MouthMode::kSadOpen, 0.30f)
         .Brows(-0.08f)
         .Mark(MarkMode::kQuestion)
         .p,
     Mot()
         .Bob(1.6f, 0.50f)
         .Sway(0.026f, 0.45f)
         .Blink(4.0f, 9.0f)
         .Whiskers(0.06f)
         .MarkRate(0.45f)
         .Jitter(1.20f)
         .m},
};

static_assert(sizeof(kSpecs) / sizeof(kSpecs[0]) == static_cast<size_t>(Emotion::kCount),
              "every Emotion needs a table row, in enum order");

}  // namespace

const EmotionSpec& SpecFor(Emotion emotion) {
    const int index = static_cast<int>(emotion);
    if (index < 0 || index >= static_cast<int>(Emotion::kCount)) {
        return kSpecs[static_cast<int>(Emotion::kNeutral)];
    }
    return kSpecs[index];
}

bool ParseEmotion(const char* name, Emotion& out) {
    if (name == nullptr) {
        return false;
    }
    // Names come from the table so there is exactly one spelling of each.
    for (int i = 0; i < static_cast<int>(Emotion::kCount); ++i) {
        const Emotion candidate = static_cast<Emotion>(i);
        if (strcmp(name, SpecFor(candidate).name) == 0) {
            out = candidate;
            return true;
        }
    }
    return false;
}

const char* EmotionName(Emotion emotion) { return SpecFor(emotion).name; }

}  // namespace cat_face
