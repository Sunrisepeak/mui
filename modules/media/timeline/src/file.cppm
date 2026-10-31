// openmui.timeline:file — a timeline as people and agents read it:
//
//     [timeline]
//     name = "promo"
//     rate = 60                  # frames a second, or "30000/1001"
//     sample_rate = 48000
//     size = [1920, 1080]
//
//     [[marker]]
//     at = "2s"
//     name = "beat"
//
//     [[track]]
//     name = "picture"
//     kind = "video"             # video, audio, animation, events
//
//     [[track.clip]]
//     name = "title"
//     source = "views/title.view.omui.json"
//     at = "0f"                  # 90f: frames; 1.5s, 250ms; 123fl: flicks; a number: seconds
//     length = "3s"
//     in = "0f"                  # where in its source it begins
//     speed = 1
//     level = 1
//     fade = { in = "15f", out = "0.25s", ease = "out_cubic" }
//
//     [[track.clip.property]]
//     name = "opacity"
//     keys = [["0f", 0, "out_cubic"], ["15f", 1], ["2s", 1, "hold"]]
//
// Every problem at its line and column, the nearest right word with it;
// what write_timeline writes reads back as the same timeline.
export module openmui.timeline:file;

import std;
import :model;

export namespace omui::timeline {

struct Problem {
    std::string file;
    int         line = 0, column = 0;
    std::string message, hint;
    [[nodiscard]] std::string text() const;
};

[[nodiscard]] std::expected<Timeline, std::vector<Problem>> read_timeline(std::string_view text, std::string_view file);
[[nodiscard]] std::string write_timeline(const Timeline& timeline);

// A time as text: "90f" (frames at `rate`), "1.5s", "250ms", "123fl" (flicks),
// a minus sign before any; nothing when it is not one, or not a whole number of flicks.
[[nodiscard]] std::optional<Flicks> parse_time(std::string_view text, Rate rate);
// The time as written: frames when it is on one, else seconds when they are exact, else flicks.
[[nodiscard]] std::string time_text(Flicks t, Rate rate);

}  // namespace omui::timeline
