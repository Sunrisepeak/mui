// openmui.layout:style — what a node asks of layout (spec §4, V-2).
//
// A subset of CSS: flexbox, grid, a z-stack, absolute positioning, aspect
// ratio, min/max sizes, padding, border, margins and gaps; the names and
// defaults are CSS's except where a UI toolkit's are clearer (a stack, and
// `direction` defaulting to row as CSS does).
export module openmui.layout:style;

import std;
import openmui.spec;

export namespace omui::layout {

using spec::Insets;
using spec::Point;
using spec::Rect;
using spec::Size;

struct Length {
    enum class Unit : std::uint8_t { auto_, points, percent };
    Unit  unit  = Unit::auto_;
    float value = 0;

    [[nodiscard]] static constexpr Length automatic() noexcept { return {}; }
    [[nodiscard]] static constexpr Length points(float v) noexcept { return {Unit::points, v}; }
    [[nodiscard]] static constexpr Length percent(float v) noexcept { return {Unit::percent, v}; }
    [[nodiscard]] constexpr bool is_auto() const noexcept { return unit == Unit::auto_; }
    // The length against a reference size; nullopt for auto, or a percentage
    // of an indefinite size.
    [[nodiscard]] constexpr std::optional<float> resolve(std::optional<float> reference) const noexcept {
        switch (unit) {
            case Unit::points: return value;
            case Unit::percent:
                if (reference) return *reference * value / 100.0f;
                return std::nullopt;
            case Unit::auto_: return std::nullopt;
        }
        return std::nullopt;
    }
    friend constexpr bool operator==(Length a, Length b) noexcept { return a.unit == b.unit && a.value == b.value; }
};

// Shorthands: layout::pt(12), layout::pct(50).
[[nodiscard]] constexpr Length pt(float v) noexcept { return Length::points(v); }
[[nodiscard]] constexpr Length pct(float v) noexcept { return Length::percent(v); }

enum class Display : std::uint8_t { flex, grid, stack, none };
enum class Position : std::uint8_t { relative, absolute };
enum class Direction : std::uint8_t { row, column, row_reverse, column_reverse };
enum class Wrap : std::uint8_t { no_wrap, wrap, wrap_reverse };
enum class Justify : std::uint8_t { start, end, center, space_between, space_around, space_evenly };
enum class Align : std::uint8_t { auto_, start, end, center, stretch, baseline };
enum class Overflow : std::uint8_t { visible, clip, scroll };

// A grid track's sizing function.
struct Track {
    enum class Kind : std::uint8_t { points, percent, fr, auto_, min_content, max_content, minmax };
    Kind  kind = Kind::auto_;
    float value = 0;           // points, percent or fr
    // minmax(min, max): the bounds as tracks themselves (no nested minmax)
    Kind  min_kind = Kind::auto_;
    float min_value = 0;
    Kind  max_kind = Kind::auto_;
    float max_value = 0;

    [[nodiscard]] static constexpr Track points(float v) noexcept { return {Kind::points, v}; }
    [[nodiscard]] static constexpr Track percent(float v) noexcept { return {Kind::percent, v}; }
    [[nodiscard]] static constexpr Track fr(float v = 1) noexcept { return {Kind::fr, v}; }
    [[nodiscard]] static constexpr Track automatic() noexcept { return {Kind::auto_, 0}; }
    [[nodiscard]] static constexpr Track min_content() noexcept { return {Kind::min_content, 0}; }
    [[nodiscard]] static constexpr Track max_content() noexcept { return {Kind::max_content, 0}; }
    [[nodiscard]] static constexpr Track minmax(Track lo, Track hi) noexcept {
        return {Kind::minmax, 0, lo.kind, lo.value, hi.kind, hi.value};
    }
};

// A grid line: 1-based line number (negative counts from the end), a span,
// or automatic placement.
struct GridLine {
    std::int16_t line = 0;  // 0: auto
    std::int16_t span = 1;

    [[nodiscard]] static constexpr GridLine at(std::int16_t l, std::int16_t s = 1) noexcept { return {l, s}; }
    [[nodiscard]] static constexpr GridLine spanning(std::int16_t s) noexcept { return {0, s}; }
};

enum class GridFlow : std::uint8_t { row, column };

struct Style {
    Display   display   = Display::flex;
    Position  position  = Position::relative;
    Overflow  overflow_x = Overflow::visible;
    Overflow  overflow_y = Overflow::visible;

    // Box
    Length width, height, min_width, min_height, max_width, max_height;
    float  aspect_ratio = 0;  // width / height; 0 is none
    Insets margin, padding, border;

    // Absolute insets (position = absolute)
    Length left, top, right, bottom;

    // Flex container
    Direction direction = Direction::row;
    Wrap      wrap = Wrap::no_wrap;
    Justify   justify = Justify::start;
    Align     align_items = Align::stretch;
    Align     align_content = Align::start;
    float     row_gap = 0;
    float     column_gap = 0;

    // Flex item
    float  grow = 0;
    float  shrink = 1;
    Length basis;
    Align  align_self = Align::auto_;
    Align  justify_self = Align::auto_;  // stack and grid items

    // Grid container
    std::vector<Track> columns;
    std::vector<Track> rows;
    Track              auto_rows = Track::automatic();
    Track              auto_columns = Track::automatic();
    GridFlow           grid_flow = GridFlow::row;

    // Grid item
    GridLine column_start, row_start;

    friend bool operator==(const Style&, const Style&) noexcept;
};

}  // namespace omui::layout
