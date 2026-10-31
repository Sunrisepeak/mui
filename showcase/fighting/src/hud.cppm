// What is drawn over the match: the versus HUD (hud/*.webp's art), and in
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

// The HUD's pictures, registered with the stage's renderer.
struct Art {
    render::ImageRef frames, type, names, fill;
};

// The versus HUD over the match: each side's portrait (its look's head),
// health with the recent damage draining, posture, super meter and level,
// its name in calligraphy (by its files' name); the timer; a combo's count
// and damage; COUNTER and PUNISH COUNTER; K.O.; FIGHT! as the round begins;
// once a round is won, each side's rounds as red seals beside its name (of
// `to_win`).
void versus(render::DisplayList& out, const gongfu::Match& match, const gongfu::present2d::Effects& effects, const Art& art,
            const std::array<const gongfu::present2d::Look*, 2>& looks, const std::array<std::string_view, 2>& names,
            std::array<std::int32_t, 2> wins = {}, std::int32_t to_win = 2);
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

// A card over the match: paused (Esc, a pad's start), or the match's
// results. The match dimmed; a heading — the winner's name in calligraphy
// with 胜, else words —, a line under it; the items as the menus' lacquer
// bars, the lit one bright; the keys beneath.
struct Card {
    std::string_view                  heading;  // words (empty: the winner's)
    std::string_view                  winner;   // the results: the winner by its files' name (empty: a draw)
    std::string_view                  sub;      // under the heading
    std::span<const std::string_view> items;
    std::int32_t                      lit = 0;
    std::span<const std::string_view> keys;  // lines beneath
};
void card(render::DisplayList& out, render::ImageRef menu_frames, render::ImageRef names, Words& words, typeset::FontCollection& fonts, const Card& card);
// Where the card's item `i` of `n` is, in the picture's units (a pointer finds it there).
[[nodiscard]] Rect card_item(std::size_t i, std::size_t n);

// The menus' lacquer bars (hud/menu-frames.webp), plain and lit.
inline constexpr Rect menu_bar{19, 262, 562, 75}, menu_bar_lit{15, 345, 568, 90};

// What a dummy is doing, for its side's corner.
enum class Dummy : std::uint8_t { none, recording, playing };

void readings(render::DisplayList& out, const gongfu::training::Readout& readout, Words& words, typeset::FontCollection& fonts,
              std::array<Dummy, 2> dummies, std::int32_t frame);

}  // namespace omui::fighting::hud
