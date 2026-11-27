// hanim.timeline:direct — a show directed from what each step of a history changed (FE4; the presentation design's
// §4.2, D-282): the facts are measured by whoever holds the history (omui.generate measures a design's), the
// decisions are this pure function's. A show is a timeline of four conventional tracks:
//
//   states    video      a clip a beat: its source the step shown, its name the transition into it (a registered
//                        recipe's name: hanim's `settle_in`, `fade_out` …, the theme's `morph`)
//   camera    animation  one clip over the whole show: `x`, `y` (the frame's point at the picture's centre, in the
//                        frame's pixels) and `zoom` keyed — navigation, eased as navigation is
//   captions  events     a clip a group of words: its name the words (a beat with no visible change joins its words
//                        to the next beat's, and its group lasts long enough to read them all)
//   (sound)   audio      not made here: a score's beats are the markers, one at each beat's start (`beat <n>`)
//
// The same beats and rules give the same timeline, bit for bit (no clock, no randomness, no transcendental
// function; the package compiles without contraction): the CTS `timeline.direct` holds it.
//
//     std::vector<hanim::timeline::Beat> beats = …;          // measured: a step's changed rectangles, its nodes
//     auto show = hanim::timeline::direct(beats, 1280, 800);  // the rules' defaults, or read_direction(text, file)
export module hanim.timeline:direct;

import std;
import :model;
import :file;

export namespace hanim::timeline {

// A region of the frame whose pixels a step changed: a rectangle in the frame's pixels, and its share of what changed.
struct Region {
    double x = 0, y = 0, width = 0, height = 0;
    double weight = 1;
};

// One step of a history as measured, in no domain's words: what it shows, its words, where its pixels changed
// (none: no visible change), its nodes by what happened to them, and how long what it changes is seen to move when
// its transition plays at its own pace (seconds; 0: not measured) — written on its `states` clip as the property
// `moving`, so a player paces the transition to fill its beat.
struct Beat {
    std::string         source;   // what the step shows (`design:<history>#<step>`): the `states` clip's source
    std::string         caption;  // its words
    std::vector<Region> regions;
    std::uint32_t       appeared = 0, removed = 0, retexted = 0, moved = 0, restyled = 0;
    double              moving = 0;
};

// The director's rules, each a value with its default. The defaults are assumptions (the presentation design's
// §4.2 table) until side-by-side measurement replaces them; a show's own rules are data (`direction@1`).
struct Direction {
    Rate        rate{60, 1};          // a beat starts and ends on a frame of this rate
    double      shortest = 0.8;       // seconds a beat with a visible change lasts at the least,
    double      longest = 2.4;        // and at the most, by how much of the frame it changed:
    double      full = 0.25;          // a change of this share of the frame (or more) lasts `longest`
    double      still = 0.3;          // seconds a beat with no visible change lasts (its words join the next beat's)
    double      reading = 18;         // characters of a caption read a second;
    double      glance = 0.6;         // and never less than this to read one
    double      near = 0.25;          // a change within this share of the frame is pushed in on,
    double      fill = 1.0 / 3.0;     // its longer side made this share of the frame's shorter side,
    double      deepest = 3;          // pushed in at most this far
    double      stay = 0.15;          // a next place within this share of the shorter side keeps the camera where it is
    double      move = 0.6;           // seconds the camera takes to a new place (within its beat)
    std::string appear = "settle_in";  // the transition into a beat that added nodes,
    std::string vanish = "fade_out";   // that removed some,
    std::string retext = "morph";      // that changed words,
    std::string relocate = "morph";    // that moved nodes,
    std::string otherwise = "fade_in"; // and any other
    // Its sound (none by default): a score under the whole show — a `music` track, one clip of the music file named
    // (its player's to resolve, e.g. beside the show) — and a cue on each visible beat's first frame — an `sfx` track
    // of clips `sound:<cue>`, the sound's name in the player's bank — so the music's hits land on the beats.
    std::string score;
    std::string cue;
    // Every rule the same (written out: GCC 16.1 stops with an internal error on a defaulted one here).
    [[nodiscard]] bool operator==(const Direction& other) const;
};

// The show of `beats` on a frame of `width` × `height` pixels, by `rules`. A pure function.
[[nodiscard]] Timeline direct(std::span<const Beat> beats, std::int32_t width, std::int32_t height, const Direction& rules = {});

// The rules a show states, as data: `{ "$format": "direction@1", "direction": { … } }` with any of Direction's keys
// (`rate` frames a second, the rest numbers of seconds, shares or recipe names); a key absent keeps its default.
// Every problem at its JSON pointer, line and column, the nearest right word with it.
[[nodiscard]] std::expected<Direction, std::vector<Problem>> read_direction(std::string_view text, std::string_view file);

}  // namespace hanim::timeline
