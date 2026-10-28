// The six levels' bricks. Level 1 (semantics): buttons, toggles (two hits:
// each flips it), sliders (three: each takes a third off), cards, and two
// hidden bricks only the semantic lens shows. Level 2 (styles): a settings
// panel's controls, its rows each cleared bringing the next style pack.
module openmui.showcase.widget_breakout;

import std;
import openmui;
import openmui.showcase.widget_breakout.sim;

namespace omui::breakout {

namespace {

LevelDef semantics() {
    LevelDef l;
    l.number = 1;
    l.title = "语义 · Semantics";
    l.lesson = "Every brick was a real component with a role and an id. The semantic tree — what agents, screen readers and tests read — changed as you played.";
    for (const char* b : {"Save", "Open", "Share", "Print", "Undo", "Redo", "Find"}) l.bricks.push_back({Kind::button, 1, b});
    for (const char* t : {"Wi-Fi", "Bluetooth", "", "Sound", "Dark mode", ""}) {
        if (*t) l.bricks.push_back({Kind::toggle, 2, t});
        else l.bricks.push_back({Kind::hidden, 1, "hidden"});
    }
    for (const char* s : {"Volume", "Brightness", "Speed"}) l.bricks.push_back({Kind::slider, 3, s, 220});
    for (const char* c : {"role: button", "role: switch", "role: slider", "id: brick.n"}) l.bricks.push_back({Kind::card, 1, c, 170});
    return l;
}

LevelDef styles() {
    LevelDef l;
    l.number = 2;
    l.title = "风格 · Styles";
    l.lesson = "Five style packs, one interface: the same components, the same semantic tree, the same physics — the hash of the play never changed. Only pixels did.";
    l.speed = 7;
    l.seed = 2;
    for (const char* b : {"Save", "Cancel", "Apply", "Reset", "Export", "Import"}) l.bricks.push_back({Kind::button, 1, b, 160});
    for (const char* t : {"Glass", "Neon", "Retro", "Ink", "Liquid"}) l.bricks.push_back({Kind::toggle, 2, t, 160});
    for (const char* s : {"Blur", "Glow", "Grain"}) l.bricks.push_back({Kind::slider, 3, s, 240});
    for (const char* c : {"the same tree", "the same physics", "the same hash", "only pixels change"}) l.bricks.push_back({Kind::card, 1, c, 190});
    return l;
}

LevelDef time_level() {
    LevelDef l;
    l.number = 4;
    l.title = "时间 · Time";
    l.lesson = "Every frame is a function of time: the simulation seeks any frame within 30 seconds exactly, and the components follow it — rewinding is not a recording played backwards.";
    l.speed = 6.5;
    l.seed = 4;
    l.gate_from = 15 * steps_per_second;  // open from 0:15 to 0:18, once
    l.gate_to = 18 * steps_per_second;
    // The vault: reached only through the gate.
    for (const char* b : {"Undo", "Redo", "Seek", "Branch", "Replay", "Snapshot"}) l.bricks.push_back({Kind::button, 1, b});
    l.bricks.push_back({Kind::wall, 1, "", 0, 44, 1});
    l.bricks.push_back({Kind::gate, 1, "Opens at 0:15, once", 300});
    l.bricks.push_back({Kind::wall, 1, "", 0, 44, 1});
    for (const char* s : {"Speed", "Scale", "Time"}) l.bricks.push_back({Kind::slider, 3, s, 220});
    for (const char* c : {"hold R: rewind", "slow capsule: ½ time", "the gate opens once", "frame = f(time)"}) l.bricks.push_back({Kind::card, 1, c, 190});
    l.rows = {0, 6, 9, 12};
    return l;
}

// The rows a level's bricks are laid out in: a break before each change of kind (a hidden brick sits in its neighbours' row).
std::vector<std::size_t> row_starts(const LevelDef& l) {
    if (!l.rows.empty()) return l.rows;
    std::vector<std::size_t> starts{0};
    auto group = [](Kind k) { return k == Kind::hidden ? Kind::toggle : k; };
    for (std::size_t i = 1; i < l.bricks.size(); ++i)
        if (group(l.bricks[i].kind) != group(l.bricks[i - 1].kind)) starts.push_back(i);
    return starts;
}

}  // namespace

std::span<const LevelDef> levels() {
    static const std::array<LevelDef, 3> all{semantics(), styles(), time_level()};
    return all;
}

const LevelDef& level(std::int32_t number) {
    for (const LevelDef& l : levels())
        if (l.number == number) return l;
    return levels().front();
}

std::int32_t next_level(std::int32_t number) {
    for (const LevelDef& l : levels())
        if (l.number > number) return l.number;
    return 0;
}

std::string brick_id(std::size_t index) { return std::format("brick.{}", index); }

Element brick_view(const BrickDef& b, std::size_t index) {
    const std::string id = brick_id(index);
    const Dim w{b.width}, h{b.height};
    switch (b.kind) {
        case Kind::button: return button({.id = id, .text = b.label, .width = w, .height = h});
        case Kind::toggle: return toggle({.id = id, .checked = true, .label = b.label, .width = w, .height = h});
        case Kind::slider: return slider({.id = id, .label = b.label, .max = 1, .min = 0, .step = 0.01, .value = 1.0, .width = w, .height = h});
        case Kind::card:  // a card: its text in a box of the brick's size
            return std::move(column({.id = id, .width = w, .height = h, .justify = "center", .align = "center", .border_width = 1.0f, .radius = 10.0f},
                                    {text({.text = b.label, .style = "caption"})})
                                 .prop("role", Source(Value("region"))));  // a panel: a style's panel effects reach it
        case Kind::hidden: return button({.id = id, .text = b.label, .width = w, .height = h, .opacity = 0.0f});
        case Kind::gate: return progress({.id = id, .label = b.label, .value = 0.0, .width = w, .height = h});
        case Kind::wall:
            if (b.grow > 0) return column({.id = id, .height = h, .grow = b.grow});
            break;
    }
    return column({.id = id, .width = w, .height = h}, {text({.text = b.label})});
}

// A level's bricks in rows: the field's children (each row a real flex row,
// named by its level: a new level's rows are new nodes, laid out afresh).
std::vector<Element> level_rows(const LevelDef& l) {
    std::vector<Element> rows;
    const auto starts = row_starts(l);
    for (std::size_t r = 0; r < starts.size(); ++r) {
        const std::size_t from = starts[r], to = r + 1 < starts.size() ? starts[r + 1] : l.bricks.size();
        std::vector<Element> bricks;
        for (std::size_t i = from; i < to; ++i) bricks.push_back(brick_view(l.bricks[i], i));
        const bool spans = std::ranges::any_of(std::span(l.bricks).subspan(from, to - from), [](const BrickDef& b) { return b.grow > 0; });
        if (spans) rows.push_back(row({.id = std::format("row.{}.{}", l.number, r), .width = Dim(layout::pct(100))}, std::move(bricks)));
        else rows.push_back(row({.id = std::format("row.{}.{}", l.number, r), .gap = 12.0f, .justify = "center"}, std::move(bricks)));
    }
    return rows;
}

}  // namespace omui::breakout
