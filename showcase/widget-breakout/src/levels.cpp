// The six levels' bricks. Level 1 (semantics): buttons, toggles (two hits:
// each flips it), sliders (three: each takes a third off), cards, and two
// hidden bricks only the semantic lens shows. Level 2 (styles): a settings
// panel's controls, its rows each cleared bringing the next style pack.
module openmui.showcase.widget_breakout;

import std;
import openmui;
import openmui.showcase.widget_breakout.sim;
import openmui.ui.canvas;

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

LevelDef agents() {
    LevelDef l;
    l.number = 5;
    l.title = "智能体 · Agents";
    l.lesson = "Your teammate was an agent: it saw the field through the semantic tree and moved through the same actions you can call — no special API, no reading pixels.";
    l.speed = 6.5;
    l.seed = 5;
    l.second_paddle = true;
    l.top = 210;
    for (const char* b : {"observe", "decide", "act", "observe", "decide", "act"}) l.bricks.push_back({Kind::button, 1, b});
    for (const char* t : {"semantic tree", "actions", "effects", "envelopes", "actors"}) l.bricks.push_back({Kind::toggle, 2, t, 170});
    for (const char* c : {"agent: guard_to(x)", "you: the pointer", "the same field"}) l.bricks.push_back({Kind::card, 1, c, 210});
    return l;
}

LevelDef canvas_level() {
    LevelDef l;
    l.number = 3;
    l.title = "画布 · Canvas";
    l.lesson = "The wall was an infinite canvas: far away its bricks were boxes drawn in one batch, near they became live cards — the same scene, the camera deciding how much of it is real.";
    l.speed = 7;
    l.seed = 3;
    l.canvas = true;
    l.height = 4 * field_h;
    l.top = 0;
    // A wall of notes four screens tall, its colours a gradient from the paddle up: a sentence about the canvas, a word a note.
    const std::array<std::string_view, 24> words{"an", "infinite", "canvas", "a", "scene", "of", "nodes", "seen", "through", "a", "camera", "far",
                                                 "boxes", "near", "cards", "one", "batch", "live", "when", "close", "the", "same", "wall", "zoom"};
    constexpr int cols = 10, rows = 16;
    constexpr double w = 104, h = 34, gap_x = 120, gap_y = 74, left = (field_w - (cols - 1) * gap_x - w) / 2, top = 1200;  // its lowest rows on the first screen
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const std::uint32_t t = static_cast<std::uint32_t>(r * 255 / (rows - 1));
            const std::uint32_t fill = ((0x40 + t * 3 / 4) << 16) | ((0x90 - t / 3) << 8) | (0xf0 - t / 2);  // blue at the top, warm toward the paddle
            BrickDef b{Kind::card, 1, std::string(words[static_cast<std::size_t>(r * cols + c) % words.size()]), static_cast<float>(w), static_cast<float>(h)};
            b.x = left + c * gap_x;
            b.y = top + r * gap_y + (c % 2) * 18;  // staggered: no straight lane up
            b.fill = fill;
            l.bricks.push_back(std::move(b));
        }
    return l;
}

LevelDef workshop_level() {
    LevelDef l;
    l.number = 6;
    l.title = "多语言 · Languages";
    l.lesson = "The gifts' rules came from scripts — TypeScript, Lua and Python — reloaded while the game ran; the simulation stayed exact: a rewind across a reload replays it without running a script.";
    l.speed = 7;
    l.seed = 6;
    l.workshop = true;
    for (int i = 0; i < 7; ++i) l.bricks.push_back({Kind::slider, 4, "steel", 150});  // four hits each: piercing helps
    for (const Gift g : {Gift::wide, Gift::pierce, Gift::slow, Gift::wide, Gift::pierce, Gift::slow}) {
        BrickDef b{Kind::button, 1, g == Gift::wide ? "wide.ts" : g == Gift::pierce ? "pierce.ts" : "slow.lua"};
        b.gift = g;  // always: a gift guaranteed, not a drop by chance
        l.bricks.push_back(std::move(b));
    }
    for (const char* c : {"E: the workshop", "edit ← tune, Apply", "rules, not frames", "rewind across it"}) l.bricks.push_back({Kind::card, 1, c, 190});
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
    static const std::array<LevelDef, 6> all{semantics(), styles(), canvas_level(), time_level(), agents(), workshop_level()};
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

void fill_wall(const LevelDef& l) {
    auto& scene = world::scene(wall_scene);
    static const bool kinds = [&] {
        // Far: a box of its colour. Near (60 px and up on screen): a live card, its word in it.
        scene.kind("note", world::Kind{.tiers = {{0, world::Tier::Draw::box}, {60, world::Tier::Draw::promote}},
                                       .promote = [](const world::Scene& s, world::NodeRef n) {
                                           return column({.id = std::string(s.key(n)), .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)),
                                                          .justify = "center", .align = "center",
                                                          .background = s.fill(n), .radius = 8.0f},
                                                         {text({.text = std::string(s.text(n, "title")), .style = "label", .color = Color::hex(0xffffff)})});
                                       },
                                       .role = Role::article});
        return true;
    }();
    (void)kinds;
    scene.clear();
    for (std::size_t i = 0; i < l.bricks.size(); ++i) {
        const BrickDef& b = l.bricks[i];
        const std::string key = brick_id(i);
        const world::NodeRef n = scene.add({.kind = "note", .x = b.x, .y = b.y, .width = b.width, .height = b.height, .key = key, .fill = Color::hex(b.fill), .radius = 6});
        scene.set(n, "title", b.label);
    }
}

// A level's bricks in rows: the field's children (each row a real flex row,
// named by its level: a new level's rows are new nodes, laid out afresh).
std::vector<Element> level_rows(const LevelDef& l) {
    std::vector<Element> rows;
    if (l.canvas) {  // the scene's camera is the field's (it takes no pointer: that moves the paddle)
        rows.push_back(std::move(canvas({.id = "wall", .inertia = false, .label = "The wall", .max_zoom = 4, .min_zoom = 0.05, .scene = std::string(wall_scene),
                                         .x = field_w / 2, .y = l.height - field_h / 2, .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))})
                                      .prop("pointer_events", Source(Value("none")))));
        return rows;
    }
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
