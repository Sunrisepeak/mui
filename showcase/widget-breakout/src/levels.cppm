// openmui.showcase.widget_breakout:levels — the levels as data: each brick's
// kind, durability and what it is as a component (a label, a toggle's state,
// a slider's value). A level's view is built from this, and its simulation's
// bricks are these in the same order, placed where the view's layout puts
// them; their sizes are explicit, so a style never moves one.
export module openmui.showcase.widget_breakout:levels;

import std;
import openmui;
import openmui.showcase.widget_breakout.sim;

export namespace omui::breakout {

struct BrickDef {
    Kind         kind = Kind::button;
    std::int32_t durability = 1;
    std::string  label;
    float        width = 150, height = 44;
    float        grow = 0;   // > 0: a share of its row's width instead (the row then spans the field, no gaps)
};

struct LevelDef {
    std::int32_t          number = 1;
    std::string           title;     // what the level shows of openmui
    std::string           lesson;    // the card a cleared level leaves: a sentence or two
    std::vector<BrickDef> bricks;
    std::vector<std::size_t> rows;     // where each row starts (empty: at each change of kind)
    double                speed = 6.5;
    std::int32_t          lives = 3;
    std::uint64_t         seed = 1;
    std::int64_t          gate_from = 0, gate_to = 0;  // the frames its gates stand open
};

// The levels made so far (of six), by number.
[[nodiscard]] std::span<const LevelDef> levels();
[[nodiscard]] const LevelDef& level(std::int32_t number);  // that one (none: the first)
[[nodiscard]] std::int32_t next_level(std::int32_t number);  // the one after it, 0 after the last

// A brick as its component: a button, a toggle, a slider, a card (text), a
// hidden one (nothing drawn: only the semantic tree knows it is there), a
// wall (a plate), or a gate (a progress bar: how near its opening is).
[[nodiscard]] Element brick_view(const BrickDef& b, std::size_t index);
// The id the n-th brick's node has: "brick.<n>".
[[nodiscard]] std::string brick_id(std::size_t index);
// A level's bricks in rows (each a real flex row): the field's children.
[[nodiscard]] std::vector<Element> level_rows(const LevelDef& l);

}  // namespace omui::breakout
