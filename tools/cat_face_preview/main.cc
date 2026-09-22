// Host-side preview for the cat face.
//
// Renders every emotion to SVG so the artwork can be iterated in a browser
// instead of re-flashing the board. It compiles the SAME geometry sources the
// firmware uses (main/boards/quandong-s3-dev-4g/cat_face*), which is the whole
// point of keeping that layer free of LVGL and ESP-IDF dependencies.
//
//   g++ -std=c++20 -O2 -I<board dir> -o preview main.cc <board dir>/cat_face*.cc
//   ./preview            -> writes preview.html
//   ./preview --selftest  -> runs invariant checks, non-zero exit on failure
//
// See build.bat in this directory for the exact Windows command line.

#include "cat_face.h"
#include "cat_face_anim.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kCanvas = cat_face::kCanvasSize;
constexpr int kFrameMs = 33;
constexpr int kSettleTicks = 24;  // let the pop-in finish before sampling

std::string Hex(cat_face::RgbColor c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

std::string Num(float v) {
    if (!std::isfinite(v)) {
        return "0";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// ---------------------------------------------------------------------------
// SVG backend
// ---------------------------------------------------------------------------

class SvgPathSink : public cat_face::PathSink {
public:
    void BeginPath() override { d_.clear(); }

    void MoveTo(float x, float y) override { d_ += "M" + Num(x) + " " + Num(y); }
    void LineTo(float x, float y) override { d_ += "L" + Num(x) + " " + Num(y); }
    void QuadTo(float cx, float cy, float x, float y) override {
        d_ += "Q" + Num(cx) + " " + Num(cy) + " " + Num(x) + " " + Num(y);
    }
    void CubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override {
        d_ += "C" + Num(c1x) + " " + Num(c1y) + " " + Num(c2x) + " " + Num(c2y) + " " + Num(x) +
              " " + Num(y);
    }
    void Close() override { d_ += "Z"; }

    void EndPath() override {
        if (d_.empty()) {
            return;
        }
        // A path with neither fill nor stroke would be invisible; skip it.
        if (!fill_ && !stroke_) {
            d_.clear();
            return;
        }

        out_ += "  <path d=\"";
        out_ += d_;
        out_ += "\"";

        if (fill_) {
            out_ += " fill=\"" + Hex(fill_color_) + "\"";
            if (fill_color_.a < 255) {
                out_ += " fill-opacity=\"" + Num(static_cast<float>(fill_color_.a) / 255.0f) + "\"";
            }
        } else {
            out_ += " fill=\"none\"";
        }

        if (stroke_) {
            out_ += " stroke=\"" + Hex(stroke_color_) + "\"";
            out_ += " stroke-width=\"" + Num(stroke_width_) + "\"";
            out_ += std::string(" stroke-linecap=\"") +
                    (stroke_cap_ == cat_face::StrokeCap::kRound ? "round" : "butt") + "\"";
            out_ += std::string(" stroke-linejoin=\"") +
                    (stroke_join_ == cat_face::StrokeJoin::kRound ? "round" : "miter") + "\"";
            if (stroke_color_.a < 255) {
                out_ +=
                    " stroke-opacity=\"" + Num(static_cast<float>(stroke_color_.a) / 255.0f) + "\"";
            }
        }

        out_ += "/>\n";
        d_.clear();
        ++path_count_;
    }

    void SetStroke(cat_face::RgbColor color, float width, cat_face::StrokeCap cap,
                   cat_face::StrokeJoin join) override {
        stroke_ = true;
        stroke_color_ = color;
        stroke_width_ = width;
        stroke_cap_ = cap;
        stroke_join_ = join;
    }

    void SetFill(cat_face::RgbColor color) override {
        fill_ = true;
        fill_color_ = color;
    }

    void SetNoStroke() override { stroke_ = false; }
    void SetNoFill() override { fill_ = false; }

    const std::string& svg_body() const { return out_; }
    int path_count() const { return path_count_; }

private:
    std::string d_;
    std::string out_;
    bool fill_ = false;
    cat_face::RgbColor fill_color_{};
    bool stroke_ = false;
    cat_face::RgbColor stroke_color_{};
    float stroke_width_ = 1.0f;
    cat_face::StrokeCap stroke_cap_ = cat_face::StrokeCap::kButt;
    cat_face::StrokeJoin stroke_join_ = cat_face::StrokeJoin::kMiter;
    int path_count_ = 0;
};

// One face, as a standalone <svg>.
std::string FaceSvg(const cat_face::Pose& pose, const char* ink_hex, const char* card_hex,
                    int pixel_size) {
    cat_face::Pose styled = pose;
    // Parse the requested ink so the same page can show a light-theme and a
    // dark-theme rendering of every face.
    unsigned r = 0;
    unsigned g = 0;
    unsigned b = 0;
    std::sscanf(ink_hex, "#%2x%2x%2x", &r, &g, &b);
    styled.line_color = cat_face::RgbColor(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                           static_cast<uint8_t>(b));

    SvgPathSink sink;
    cat_face::Draw(styled, sink);

    std::string svg;
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + std::to_string(kCanvas) +
           " " + std::to_string(kCanvas) + "\" width=\"" + std::to_string(pixel_size) +
           "\" height=\"" + std::to_string(pixel_size) + "\">";
    svg += "<rect width=\"100%\" height=\"100%\" fill=\"";
    svg += card_hex;
    svg += "\"/>";
    svg += sink.svg_body();
    svg += "</svg>";
    return svg;
}

bool WriteFile(const std::string& path, const std::string& body) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::printf("could not open %s for writing\n", path.c_str());
        return false;
    }
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
    return true;
}

cat_face::Pose SettledPose(cat_face::Emotion emotion, uint32_t seed);

// One standalone .svg per emotion, so a single face can be blown up to any size
// for close inspection without regenerating the whole page.
void WriteFaceSvgs(const std::string& dir, int pixel_size) {
    const int emotion_count = static_cast<int>(cat_face::Emotion::kCount);
    for (int i = 0; i < emotion_count; ++i) {
        const auto emotion = static_cast<cat_face::Emotion>(i);
        const cat_face::Pose pose = SettledPose(emotion, 0x1234u + static_cast<uint32_t>(i));
        const std::string path = dir + "/face_" + cat_face::EmotionName(emotion) + ".svg";
        if (!WriteFile(path, FaceSvg(pose, "#2b2b2b", "#f4f1ec", pixel_size))) {
            return;
        }
    }
    std::printf("wrote %d svg files to %s\n", emotion_count, dir.c_str());
}

// Sample the animator across time and return `count` consecutive faces. Used
// for the filmstrips, so the motion arc is visible at a glance.
std::vector<cat_face::Pose> SampleFrames(cat_face::Emotion emotion, uint32_t seed, int count,
                                         int ticks_per_frame) {
    cat_face::Animator anim(seed);
    anim.SetInk(cat_face::RgbColor(0x2b, 0x2b, 0x2b), 3.0f);
    anim.SetEmotion(emotion);
    for (int i = 0; i < kSettleTicks; ++i) {
        anim.Tick(kFrameMs);
    }

    std::vector<cat_face::Pose> frames;
    frames.reserve(static_cast<size_t>(count));
    for (int f = 0; f < count; ++f) {
        frames.push_back(anim.pose());
        for (int i = 0; i < ticks_per_frame; ++i) {
            anim.Tick(kFrameMs);
        }
    }
    return frames;
}

cat_face::Pose SettledPose(cat_face::Emotion emotion, uint32_t seed) {
    cat_face::Animator anim(seed);
    anim.SetInk(cat_face::RgbColor(0x2b, 0x2b, 0x2b), 3.0f);
    anim.SetEmotion(emotion);
    for (int i = 0; i < kSettleTicks; ++i) {
        anim.Tick(kFrameMs);
    }
    return anim.pose();
}

// ---------------------------------------------------------------------------
// Preview page
// ---------------------------------------------------------------------------

void WritePage(const std::string& path) {
    std::string html;
    html += "<!doctype html><meta charset=\"utf-8\"><title>cat face preview</title>\n";
    html += "<style>\n";
    html += " body{margin:0;padding:28px;background:#201d1a;color:#e8e2d8;";
    html += "font:14px/1.5 ui-sans-serif,system-ui,sans-serif}\n";
    html += " h1{font-size:19px;font-weight:600;margin:0 0 4px}\n";
    html += " h2{font-size:15px;font-weight:600;margin:38px 0 12px;color:#c9bfae}\n";
    html += " p.note{margin:0 0 22px;color:#8d8478;max-width:70ch}\n";
    html += " .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(148px,1fr));";
    html += "gap:14px}\n";
    html += " .cell{background:#2b2724;border-radius:10px;padding:10px;text-align:center}\n";
    html += " .cell .name{margin-top:6px;font-size:12px;color:#a99f92;letter-spacing:.03em}\n";
    html += " .strip{display:flex;gap:6px;overflow-x:auto;background:#2b2724;";
    html += "border-radius:10px;padding:10px;margin-bottom:14px}\n";
    html += " .strip svg{flex:0 0 auto;border-radius:6px}\n";
    html += " .strip-label{font-size:12px;color:#c9bfae;margin:0 0 6px;letter-spacing:.04em}\n";
    html += "</style>\n";

    html += "<h1>cat face preview</h1>\n";
    html +=
        "<p class=\"note\">Generated by tools/cat_face_preview from the same geometry "
        "sources the firmware compiles. Light-theme ink on the top grid, dark-theme ink "
        "below it, then filmstrips sampled across roughly 4&nbsp;seconds of animation.</p>\n";

    const int emotion_count = static_cast<int>(cat_face::Emotion::kCount);

    // Light-theme grid.
    html += "<h2>all 21 emotions &mdash; light theme ink</h2>\n<div class=\"grid\">\n";
    for (int i = 0; i < emotion_count; ++i) {
        const auto emotion = static_cast<cat_face::Emotion>(i);
        const cat_face::Pose pose = SettledPose(emotion, 0x1234u + static_cast<uint32_t>(i));
        html += "<div class=\"cell\">" + FaceSvg(pose, "#2b2b2b", "#f4f1ec", 168) +
                "<div class=\"name\">" + cat_face::EmotionName(emotion) + "</div></div>\n";
    }
    html += "</div>\n";

    // Dark-theme grid: the ink color has to come from the theme, so verify it
    // reads on a dark card too.
    html += "<h2>all 21 emotions &mdash; dark theme ink</h2>\n<div class=\"grid\">\n";
    for (int i = 0; i < emotion_count; ++i) {
        const auto emotion = static_cast<cat_face::Emotion>(i);
        const cat_face::Pose pose = SettledPose(emotion, 0x5678u + static_cast<uint32_t>(i));
        html += "<div class=\"cell\">" + FaceSvg(pose, "#ece6dc", "#232020", 168) +
                "<div class=\"name\">" + cat_face::EmotionName(emotion) + "</div></div>\n";
    }
    html += "</div>\n";

    // Filmstrips for the emotions whose motion is the point.
    const cat_face::Emotion strips[] = {
        cat_face::Emotion::kSleepy, cat_face::Emotion::kAngry,    cat_face::Emotion::kHappy,
        cat_face::Emotion::kSilly,  cat_face::Emotion::kShocked,  cat_face::Emotion::kCrying,
        cat_face::Emotion::kLoving, cat_face::Emotion::kConfused,
    };

    html += "<h2>motion filmstrips &mdash; 12 frames, ~130ms apart</h2>\n";
    for (size_t s = 0; s < sizeof(strips) / sizeof(strips[0]); ++s) {
        html += "<p class=\"strip-label\">" + std::string(cat_face::EmotionName(strips[s])) +
                "</p>\n<div class=\"strip\">\n";
        const auto frames = SampleFrames(strips[s], 0xabcd0000u + static_cast<uint32_t>(s), 12, 4);
        for (const cat_face::Pose& frame : frames) {
            html += FaceSvg(frame, "#ece6dc", "#232020", 104);
        }
        html += "</div>\n";
    }

    if (!WriteFile(path, html)) {
        return;
    }
    std::printf("wrote %s (%zu bytes)\n", path.c_str(), html.size());
}

// ---------------------------------------------------------------------------
// Self test
// ---------------------------------------------------------------------------

// Records what a draw call actually emitted, so the invariants can be checked
// on real output rather than on intent.
class StatsSink : public cat_face::PathSink {
public:
    void BeginPath() override { has_geom_ = false; }
    void MoveTo(float x, float y) override { Note(x, y); }
    void LineTo(float x, float y) override { Note(x, y); }
    void QuadTo(float cx, float cy, float x, float y) override {
        Note(cx, cy);
        Note(x, y);
    }
    void CubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override {
        Note(c1x, c1y);
        Note(c2x, c2y);
        Note(x, y);
    }
    void Close() override {}
    void EndPath() override {
        // A path only counts if it actually carried geometry -- drawing code is
        // allowed to BeginPath() and abandon it.
        if (has_geom_) {
            ++paths_;
        }
        has_geom_ = false;
    }

    void SetStroke(cat_face::RgbColor, float, cat_face::StrokeCap, cat_face::StrokeJoin) override {}
    void SetFill(cat_face::RgbColor) override {}
    void SetNoStroke() override {}
    void SetNoFill() override {}

    int paths() const { return paths_; }
    int non_finite() const { return non_finite_; }
    float min_x() const { return min_x_; }
    float max_x() const { return max_x_; }
    float min_y() const { return min_y_; }
    float max_y() const { return max_y_; }

private:
    void Note(float x, float y) {
        has_geom_ = true;
        if (!std::isfinite(x) || !std::isfinite(y)) {
            ++non_finite_;
            return;
        }
        if (!has_point_) {
            min_x_ = max_x_ = x;
            min_y_ = max_y_ = y;
            has_point_ = true;
            return;
        }
        if (x < min_x_)
            min_x_ = x;
        if (x > max_x_)
            max_x_ = x;
        if (y < min_y_)
            min_y_ = y;
        if (y > max_y_)
            max_y_ = y;
    }

    bool has_geom_ = false;
    int paths_ = 0;
    int non_finite_ = 0;
    bool has_point_ = false;
    float min_x_ = 0.0f;
    float max_x_ = 0.0f;
    float min_y_ = 0.0f;
    float max_y_ = 0.0f;
};

int RunSelfTest() {
    int failures = 0;
    const int emotion_count = static_cast<int>(cat_face::Emotion::kCount);

    auto fail = [&failures](const std::string& message) {
        std::printf("FAIL: %s\n", message.c_str());
        ++failures;
    };

    std::printf("running %d emotion checks\n", emotion_count);

    std::vector<std::string> names;

    for (int i = 0; i < emotion_count; ++i) {
        const auto emotion = static_cast<cat_face::Emotion>(i);
        const cat_face::EmotionSpec& spec = cat_face::SpecFor(emotion);
        const std::string label = spec.name != nullptr ? spec.name : "<null>";
        names.push_back(label);

        if (spec.name == nullptr || spec.name[0] == '\0') {
            fail("emotion " + std::to_string(i) + " has no name");
            continue;
        }

        // Name round-trips through the parser the firmware uses.
        cat_face::Emotion parsed = cat_face::Emotion::kNeutral;
        if (!cat_face::ParseEmotion(spec.name, parsed) || parsed != emotion) {
            fail(label + ": name does not round-trip through ParseEmotion");
        }

        // Motion sanity.
        const cat_face::MotionSpec& mo = spec.motion;
        if (!(mo.blink_min_s > 0.0f) || !(mo.blink_max_s >= mo.blink_min_s)) {
            fail(label + ": blink interval range is inverted or non-positive");
        }
        if (!(mo.blink_hold_s > 0.0f)) {
            fail(label + ": blink hold is not positive");
        }
        if (!(mo.mark_rate >= 0.0f)) {
            fail(label + ": negative rate");
        }
        if (!(mo.jitter >= 0.0f)) {
            fail(label + ": negative jitter");
        }

        // THE INVARIANT: randomization must not be able to change which
        // emotion a pose represents. Across many seeds, the defining fields --
        // eye mode, mouth mode, mark -- must stay identical, the gaze must stay
        // within its declared drift, and the face must not change size.
        for (uint32_t seed = 1; seed <= 64; ++seed) {
            cat_face::Animator anim(seed * 2654435761u);
            anim.SetInk(spec.base.line_color, spec.base.line_width);
            anim.SetEmotion(emotion);
            // Sample early and late so the pop-in and the steady state are both
            // covered.
            for (int tick = 0; tick < 40; ++tick) {
                anim.Tick(kFrameMs);
                const cat_face::Pose& pose = anim.pose();

                if (pose.eye_l.mode != spec.base.eye_l.mode ||
                    pose.eye_r.mode != spec.base.eye_r.mode) {
                    fail(label + ": eye mode changed under randomization");
                    break;
                }
                if (pose.mouth != spec.base.mouth) {
                    fail(label + ": mouth mode changed under randomization");
                    break;
                }
                if (pose.mark != spec.base.mark) {
                    fail(label + ": expression mark changed under randomization");
                    break;
                }
                if (pose.face_rx != spec.base.face_rx || pose.face_ry != spec.base.face_ry) {
                    fail(label + ": face size changed under randomization");
                    break;
                }

                // Gaze may drift, but only within the declared amount.
                const float gaze_limit = mo.pupil_drift + 0.01f;
                if (std::fabs(pose.pupil_dx - spec.base.pupil_dx) > gaze_limit ||
                    std::fabs(pose.pupil_dy - spec.base.pupil_dy) > gaze_limit) {
                    fail(label + ": pupil drift exceeded the declared range");
                    break;
                }

                if (!(pose.squash >= 0.55f && pose.squash <= 1.90f)) {
                    fail(label + ": squash left the safe range");
                    break;
                }

                // An emotion with no blush must never acquire any.
                if (spec.base.blush <= 0.0f && pose.blush > 0.0f) {
                    fail(label + ": gained blush it should not have");
                    break;
                }

                StatsSink sink;
                cat_face::Draw(pose, sink);
                if (sink.non_finite() != 0) {
                    fail(label + ": draw produced non-finite coordinates");
                    break;
                }
                if (sink.paths() < 3) {
                    fail(label + ": draw produced suspiciously few paths (" +
                         std::to_string(sink.paths()) + ")");
                    break;
                }
                if (sink.paths() > 120) {
                    fail(label + ": draw produced too many paths (" + std::to_string(sink.paths()) +
                         ")");
                    break;
                }
                // Marks and whiskers legitimately reach past the feature
                // layout, but nothing should escape the canvas by much.
                if (sink.min_x() < -kCanvas * 0.6f || sink.max_x() > kCanvas * 1.6f ||
                    sink.min_y() < -kCanvas * 0.6f || sink.max_y() > kCanvas * 1.6f) {
                    fail(label + ": geometry escaped the canvas");
                    break;
                }
            }
        }
    }

    // Names must be unique, or ParseEmotion would be ambiguous.
    for (size_t a = 0; a < names.size(); ++a) {
        for (size_t b = a + 1; b < names.size(); ++b) {
            if (names[a] == names[b]) {
                fail("duplicate emotion name: " + names[a]);
            }
        }
    }

    // Unknown names must be rejected so the board can fall back to the stock
    // bitmap emoji instead of drawing a wrong face.
    cat_face::Emotion ignored = cat_face::Emotion::kNeutral;
    if (cat_face::ParseEmotion("robot_2", ignored)) {
        fail("ParseEmotion accepted an unknown name");
    }
    if (cat_face::ParseEmotion(nullptr, ignored)) {
        fail("ParseEmotion accepted null");
    }
    if (cat_face::ParseEmotion("", ignored)) {
        fail("ParseEmotion accepted an empty name");
    }

    if (failures == 0) {
        std::printf("OK: %d emotions, all invariants hold\n", emotion_count);
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) {
        return RunSelfTest();
    }
    if (argc > 2 && std::strcmp(argv[1], "--svg") == 0) {
        // --svg <dir> [pixels]: one standalone .svg per emotion.
        const int pixels = argc > 3 ? std::atoi(argv[3]) : 512;
        WriteFaceSvgs(argv[2], pixels);
        return 0;
    }
    WritePage(argc > 1 ? argv[1] : "preview.html");
    return 0;
}
