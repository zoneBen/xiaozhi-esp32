#ifndef CAT_FACE_INTERNAL_H
#define CAT_FACE_INTERNAL_H

#include "cat_face_shapes.h"

// Shared anatomy between cat_face.cc (eyes, brows, nose, mouth, whiskers) and
// cat_face_marks.cc (the floating expression glyphs). Not part of the public
// geometry API.

namespace cat_face {
namespace internal {

// The extent the features are laid out over. There is no head outline to draw
// -- the panel IS the head -- so these are not the dimensions of a shape, they
// are the region the face occupies. They are deliberately close to the full
// canvas so the features reach the edges instead of clustering in the middle.
//
// Features are placed as fractions of the radii rather than absolute pixels.
// That keeps the proportions if the face is ever resized, but it also means
// enlarging the radii pushes everything outward: the floating marks in
// cat_face_marks.cc scale off these too, and the ones that hang off the upper
// corners are the first to run off the canvas. They were re-tuned for these
// values.
// These must match the Pose defaults in cat_face.h: Draw() constructs this from
// the Pose, so the values here are only ever seen if something builds a
// FaceMetrics directly. They agree so that doing so is not a silent layout
// change.
struct FaceMetrics {
    float cx = 84.0f;
    float cy = 82.0f;
    float rx = 62.0f;
    float ry = 58.0f;
};

// Feature placement as fractions of the radii.
inline constexpr float kEyeUx = 0.430f;   // eye offset from face centre
inline constexpr float kEyeUy = -0.060f;  // negative: eyes sit above centre
inline constexpr float kEyeUr = 0.290f;   // eye radius
inline constexpr float kNoseUy = 0.430f;
inline constexpr float kNoseUw = 0.085f;  // nose half-width
inline constexpr float kNoseUh = 0.130f;  // nose height
inline constexpr float kMouthUy = 0.640f;
inline constexpr float kMouthUw = 0.340f;  // mouth half-width
inline constexpr float kBlushUx = 0.640f;
inline constexpr float kBlushUy = 0.620f;
inline constexpr float kBlushUrx = 0.185f;
inline constexpr float kBlushUry = 0.090f;

// Whiskers: three per side, rooted on the cheek beside the muzzle and swept
// out toward the canvas edge.
//
// Two clearances decide these numbers, and both are tight:
//   - The roots must sit BELOW the eye's lower rim, or the outward sweep of the
//     top whisker cuts straight across the eye. Landing the topmost root about
//     7px below the rim at the root's own x is what kWhiskerRootUy buys.
//   - The roots must sit OUTBOARD of the mouth's corners (kMouthUw), or the fan
//     tangles with the mouth into one dark knot at the bottom of the face.
//     That is what kWhiskerRootUx buys -- at 0.360 the roots landed almost
//     exactly on the mouth corners.
inline constexpr float kWhiskerRootUx = 0.470f;
inline constexpr float kWhiskerRootUy = 0.470f;
inline constexpr float kWhiskerTipUx = 1.010f;
inline constexpr float kWhiskerTipSpreadUy = 0.200f;

// Face ink: strokes only, round caps and joins.
void ApplyInk(Emitter& e, const Pose& pose);
// Thinner ink for whiskers and fine details.
void ApplyInkThin(Emitter& e, const Pose& pose);

// One eye. rx/ry are the nominal (fully open) radii; eye.open scales the
// vertical extent so a blink animates smoothly for every eye mode.
void DrawEye(Emitter& e, const Pose& pose, const FaceMetrics& m, const EyePose& eye, float ex,
             float ey, float rx, float ry);

void DrawMouth(Emitter& e, const Pose& pose, const FaceMetrics& m);

// Floating expression glyph. `e` carries a translate-only transform so the
// mark hovers instead of bobbing with the face.
void DrawMark(Emitter& e, const Pose& pose, const FaceMetrics& m);

}  // namespace internal
}  // namespace cat_face

#endif  // CAT_FACE_INTERNAL_H
