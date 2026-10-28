// What is drawn over the match: the health bars and the timer, and in
// training the boxes, the frame meter, each player's input history and the
// readings (startup, frame advantage, the combo). Numbers are seven-segment
// bars, so they cost no shaping and look the same everywhere; the few words
// are shaped once and kept.
export module openmui.showcase.fighting:hud;

import std;
import openmui.core;
import openmui.view;
import gongfu.present2d;
import gongfu.training;

export namespace omui::fighting::hud {

inline constexpr float width = 960, height = 540;  // the picture

void bars(render::DisplayList& out, const gongfu::Match& match);
void timer(render::DisplayList& out, std::int32_t seconds);
// A number in seven segments of `size` height ending at `right` (its top at
// `top`); `sign`: a + or − before it. Answers where it begins.
float number(render::DisplayList& out, float right, float top, float size, std::int32_t value, bool sign, Color color);

void boxes(render::DisplayList& out, const gongfu::Match& match, const gongfu::present2d::Scene& scene, const gongfu::present2d::Camera& camera);
void meter(render::DisplayList& out, const gongfu::training::Readout& readout);
void inputs(render::DisplayList& out, const gongfu::training::Readout& readout, std::int32_t player);

// Words shaped once, kept by their text.
class Words {
public:
    // The word laid out: its paragraph, drawn at an origin.
    const typeset::Paragraph& get(typeset::FontCollection& fonts, std::string_view text, float size, Color color);

private:
    std::map<std::string, typeset::Paragraph, std::less<>> kept_;
};

// What a dummy is doing, for its side's corner.
enum class Dummy : std::uint8_t { none, recording, playing };

void readings(render::DisplayList& out, const gongfu::training::Readout& readout, Words& words, typeset::FontCollection& fonts,
              std::array<Dummy, 2> dummies);

}  // namespace omui::fighting::hud
