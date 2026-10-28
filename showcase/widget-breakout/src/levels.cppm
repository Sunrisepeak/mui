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
};

struct LevelDef {
    std::int32_t          number = 1;
    std::string           title;     // what the level shows of openmui
    std::string           lesson;    // the card a cleared level leaves: a sentence or two
    std::vector<BrickDef> bricks;
    double                speed = 6.5;
    std::int32_t          lives = 3;
    std::uint64_t         seed = 1;
};

[[nodiscard]] const LevelDef& level(std::int32_t number);  // 1–6 (others: the first)

// A brick as its component: a button, a toggle, a slider, a card (text), or
// a hidden one (nothing drawn: only the semantic tree knows it is there).
[[nodiscard]] Element brick_view(const BrickDef& b, std::size_t index);
// The id the n-th brick's node has: "brick.<n>".
[[nodiscard]] std::string brick_id(std::size_t index);
// A level's bricks in rows (each a real flex row): the field's children.
[[nodiscard]] std::vector<Element> level_rows(const LevelDef& l);

}  // namespace omui::breakout
