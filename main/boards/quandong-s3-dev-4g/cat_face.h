#ifndef CAT_FACE_H
#define CAT_FACE_H

#include <cmath>
#include <cstdint>

// Procedural stick-figure cat FACE, drawn with vector paths.
//
// This layer is pure geometry: no LVGL, no ESP-IDF headers. That is
// deliberate -- the same sources build on the host in
// tools/cat_face_preview/, where they render to SVG, so the artwork can be
// iterated in a browser instead of re-flashing the board for every tweak.
//
// Everything lives in a fixed 168x168 logical space (kCanvasSize) which
// matches the device canvas 1:1, so there is no scaling anywhere.
//
// The screen IS the face: there is no head outline, because the display panel
// is the head. Every feature is placed in open space on the canvas, so the
// face fills the whole screen instead of sitting inside a drawn boundary. The
// cat's ears, body, limbs and tail are real mechanical parts of the robot, not
// graphics -- so nothing here draws them either.

namespace cat_face {

inline constexpr int kCanvasSize = 168;

// ---------------------------------------------------------------------------
// Color and stroke style
// ---------------------------------------------------------------------------

struct RgbColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;  // 255 = opaque. Fades blush and marks in and out.

    constexpr RgbColor() = default;
    constexpr RgbColor(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}
};

enum class StrokeCap { kButt, kRound };
enum class StrokeJoin { kMiter, kRound };

// ---------------------------------------------------------------------------
// Geometry seam
// ---------------------------------------------------------------------------

// Abstract path commands. The device backend maps these onto
// lv_vector_path_*; the host backend onto SVG <path d="...">. Both LVGL and
// SVG build shapes from the same move/line/quad/cubic/close vocabulary, so
// exposing anything higher-level here would break the 1:1 mapping and let the
// two backends drift apart.
//
// Style is sticky: it applies to every path flushed after it is set.
class PathSink {
public:
    virtual ~PathSink() = default;

    virtual void BeginPath() = 0;
    virtual void MoveTo(float x, float y) = 0;
    virtual void LineTo(float x, float y) = 0;
    virtual void QuadTo(float cx, float cy, float x, float y) = 0;
    virtual void CubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) = 0;
    virtual void Close() = 0;
    // Flush everything emitted since BeginPath(), using the current style.
    virtual void EndPath() = 0;

    virtual void SetStroke(RgbColor color, float width, StrokeCap cap, StrokeJoin join) = 0;
    virtual void SetFill(RgbColor color) = 0;
    // Stroke and fill are independent: SetNoStroke() leaves only the fill,
    // SetNoFill() leaves only the stroke. Filled elements (blush, the "?" dot,
    // the visor lens, solid hearts) need the former.
    virtual void SetNoStroke() = 0;
    virtual void SetNoFill() = 0;
};

// ---------------------------------------------------------------------------
// 2D transform
// ---------------------------------------------------------------------------

// One mechanism for the three motion channels that move the whole face:
// lean (rot), squash/stretch (sx/sy), and bob (dx/dy). Face parts get the
// face's transform; floating marks (z, ?, sparkles) get a translate-only one
// so they hover instead of bobbing with the face.
struct Xform {
    float ox = 84.0f;  // rotation and scale origin
    float oy = 82.0f;
    float rot = 0.0f;  // radians
    float sx = 1.0f;
    float sy = 1.0f;
    float dx = 0.0f;
    float dy = 0.0f;

    void Apply(float x, float y, float& out_x, float& out_y) const {
        const float px = (x - ox) * sx;
        const float py = (y - oy) * sy;
        const float c = cosf(rot);
        const float s = sinf(rot);
        out_x = ox + px * c - py * s + dx;
        out_y = oy + px * s + py * c + dy;
    }
};

// ---------------------------------------------------------------------------
// Expression vocabulary
// ---------------------------------------------------------------------------

enum class EyeMode {
    kOpen,     // round eye with a pupil
    kNarrow,   // squashed vertically -- suspicion, concentration, smugness
    kArcUp,    // upward arc (smiling closed eye)
    kArcDown,  // downward arc (sad/worried closed eye)
    kWide,     // enlarged round eye (surprise, shock)
    kHeart,    // heart-shaped eye (love)
    kSpiral,   // swirl (dizzy, bewildered)
    kTeary,    // round eye with a tear welling up
};

enum class MouthMode {
    kCatW,     // the classic cat "omega" mouth
    kSmile,    // single downward-bulging arc
    kOpenOh,   // small open oval
    kGrin,     // wide closed shape with a flat top
    kFrown,    // upward-bulging arc
    kFlat,     // neutral straight line
    kTongue,   // open mouth with a tongue sticking out
    kSadOpen,  // small open oval with a wavy lip
};

enum class MarkMode {
    kNone,
    kBlush,      // cheek blush only, no floating glyph
    kTear,       // falling teardrops
    kSnooze,     // floating "z"
    kQuestion,   // floating "?"
    kAnger,      // anime popping-vein mark
    kSparkle,    // four-point sparkles
    kVisor,      // sunglasses
    kNote,       // music note
    kHeartMark,  // floating hearts
};

// ---------------------------------------------------------------------------
// Pose
// ---------------------------------------------------------------------------

struct EyePose {
    EyeMode mode = EyeMode::kOpen;
    // 1 = fully open, 0 = shut. Scales the vertical extent of whatever shape
    // the mode draws, so a blink animates smoothly for every eye type instead
    // of snapping between modes.
    float open = 1.0f;
    float scale = 1.0f;
};

// Semantic colors for the few elements where color carries meaning rather than
// being decoration. Kept in one place so a theme could recolor them.
struct Palette {
    RgbColor blush{0xff, 0x7a, 0x9a};
    RgbColor tear{0x5a, 0xa8, 0xe8};
    RgbColor sparkle{0xff, 0xc4, 0x3d};
    RgbColor anger{0xe8, 0x4a, 0x3a};
    RgbColor tongue{0xf2, 0x7a, 0x8c};
    RgbColor visor{0x28, 0x28, 0x30};
};

struct Pose {
    // Where the face sits on the canvas, and its global motion. There is no
    // outline to draw: the panel is the head, and the radii are the extent the
    // features are laid out over. They are sized so the features reach the
    // canvas edges rather than clustering in the middle. FaceMetrics in
    // cat_face_internal.h mirrors these -- keep the two in step.
    float face_cx = 84.0f;
    float face_cy = 82.0f;
    float face_rx = 62.0f;
    float face_ry = 58.0f;
    float face_rot = 0.0f;  // radians
    float dx = 0.0f;
    float dy = 0.0f;
    float squash = 1.0f;  // 1 = neutral; >1 wider and shorter, <1 narrower and taller

    // Eyes.
    EyePose eye_l;
    EyePose eye_r;
    float pupil_dx = 0.0f;
    float pupil_dy = 0.0f;

    // Brows. Positive slants inward and down (angry), negative raises the
    // inner ends (worried).
    float brow_tilt = 0.0f;

    // Mouth.
    MouthMode mouth = MouthMode::kCatW;
    float mouth_open = 0.0f;  // 0 = closed, 1 = wide

    // Whiskers.
    float whisker_lift = 0.0f;
    float whisker_spread = 0.0f;

    // Marks.
    MarkMode mark = MarkMode::kNone;
    float mark_alpha = 1.0f;  // 0..1
    float mark_phase = 0.0f;  // 0..1 loop; each mark animates along it differently
    float blush = 0.0f;       // 0..1

    // Style. line_color comes from the active theme's text color.
    RgbColor line_color{0x2b, 0x2b, 0x2b};
    Palette palette;
    float line_width = 3.0f;
};

// ---------------------------------------------------------------------------
// Emotion set
// ---------------------------------------------------------------------------

// The 21 names match main/boards/quandong-s3-dev-4g's emoji collection
// (noto-color-emoji_128) exactly. Unknown names still fall back to the stock
// bitmap emoji path in LcdDisplay::SetEmotion.
enum class Emotion {
    kNeutral,
    kHappy,
    kLaughing,
    kFunny,
    kSad,
    kAngry,
    kCrying,
    kLoving,
    kEmbarrassed,
    kSurprised,
    kShocked,
    kThinking,
    kWinking,
    kCool,
    kRelaxed,
    kDelicious,
    kKissy,
    kConfident,
    kSleepy,
    kSilly,
    kConfused,
    kCount,
};

// Resolve an emotion name as sent by the server. Returns false when the name
// is not one we draw, in which case the caller keeps the bitmap emoji.
bool ParseEmotion(const char* name, Emotion& out);

// The canonical name of an emotion, for logging.
const char* EmotionName(Emotion emotion);

// ---------------------------------------------------------------------------
// Randomness
// ---------------------------------------------------------------------------

// xorshift32. Seeded by the caller (esp_random() on device) so this layer stays
// platform-independent.
class Xorshift32 {
public:
    explicit Xorshift32(uint32_t seed = 0x9e3779b9u) : state_(seed ? seed : 0x9e3779b9u) {}

    uint32_t Next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    // Uniform in [0, span). Returns 0 for a non-positive span.
    float NextFloat(float span) {
        if (span <= 0.0f) {
            return 0.0f;
        }
        return (static_cast<float>(Next() >> 8) / 16777216.0f) * span;
    }

    // Uniform in [-half, half).
    float NextSigned(float half) { return NextFloat(2.0f * half) - half; }

private:
    uint32_t state_;
};

// ---------------------------------------------------------------------------
// Per-emotion tuning
// ---------------------------------------------------------------------------

// Animation amplitudes for one emotion. Every emotion gets its own numbers
// because "how it moves" is as much a part of an expression as "how it looks":
// a sleepy cat breathes slowly and barely moves, an angry one is tense and
// twitchy, a shocked one overshoots and trembles, a silly one is deliberately
// exaggerated.
//
// Randomization is applied ONLY as bounded offsets on top of these -- see
// EmotionSpec.
struct MotionSpec {
    float bob_amp = 1.5f;      // px of vertical breathing
    float bob_rate = 0.35f;    // Hz
    float sway_amp = 0.020f;   // radians of micro rotation
    float sway_rate = 0.21f;   // Hz
    float blink_min_s = 2.5f;  // random blink interval range
    float blink_max_s = 6.0f;
    float blink_hold_s = 0.11f;  // how long the eyes stay shut
    float pupil_drift = 1.6f;    // px of slow pupil wander
    float whisker_amp = 0.06f;   // radians of whisker motion
    float mark_rate = 0.55f;     // Hz the expression mark loops at
    float jitter = 1.0f;         // scales the per-invocation random offsets
};

// One emotion's definition. The `base` pose carries everything that makes the
// emotion RECOGNIZABLE and is therefore never randomized; `motion` carries how
// it moves.
struct EmotionSpec {
    const char* name;
    Pose base;
    MotionSpec motion;
};

// The single place to tune an emotion: its base pose and its motion together.
const EmotionSpec& SpecFor(Emotion emotion);

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// Emit the whole face into the sink. Ordering: blush, whiskers, brows, eyes,
// nose, mouth, then the expression mark.
void Draw(const Pose& pose, PathSink& sink);

}  // namespace cat_face

#endif  // CAT_FACE_H
