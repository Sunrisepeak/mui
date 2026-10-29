// The five rooms. 1 勿扰: a switch opens the shield round the core. 2 亮度:
// capacitors, beams, and a switch that discharges one early. 3 云: kickers
// up into a cloud, and a chamber above it (a canvas two screens tall). 4
// 深色模式: dark but round the ball, hidden caches, a gate that comes round.
// 5 同步: the copilot at the top, everything at once.
module openmui.showcase.widget_breakout;

import std;
import openmui;
import openmui.showcase.widget_breakout.sim;
import openmui.ui.canvas;

namespace omui::breakout {

namespace {

BrickDef at(Kind kind, std::string label, double x, double y, float w, float h, std::int32_t durability = 1) {
    BrickDef b;
    b.kind = kind;
    b.label = std::move(label);
    b.x = x, b.y = y, b.width = w, b.height = h;
    b.durability = durability;
    return b;
}
BrickDef wired(BrickDef b, std::uint32_t wire, std::uint32_t on = 0) {
    b.wire = wire;
    b.on = on;
    return b;
}
BrickDef slot(BrickDef b) {
    b.slot = true;
    return b;
}

RoomDef do_not_disturb() {
    RoomDef r;
    r.number = 1;
    r.name = "勿扰";
    r.title = "Do Not Disturb";
    r.style = "glass";
    r.speed = 6.0;
    r.seed = 11;
    r.hint = "Space  launch along the aim";
    r.aim_phase = 21;  // the first forecast runs into the switch
    r.made = "Every brick is an openmui component placed by layout: the switch is a real toggle, the shield real plates, the core a real button. "
             "On its first step the field reads where layout put each one into the simulation; from then on every hit changes the component "
             "itself, and the semantic tree with it (the changes are listed here).";
    auto& b = r.bricks;
    b.push_back(at(Kind::core, "Shut down", 550, 196, 180, 56, 3));
    b.push_back(wired(at(Kind::shutter, "", 522, 186, 20, 86), 1));
    b.push_back(wired(at(Kind::shutter, "", 738, 186, 20, 86), 1));
    b.push_back(wired(at(Kind::shutter, "", 522, 272, 236, 16), 1));
    b.push_back(wired(at(Kind::shutter, "", 522, 168, 236, 16), 1));
    b.push_back(wired(at(Kind::toggle, "Do Not Disturb", 330, 392, 230, 44), 1, 1));
    std::array<const char*, 8> apps{"Mail", "Chat", "Calendar", "News", "Photos", "Maps", "Music", "Weather"};
    for (std::size_t i = 0; i < apps.size(); ++i) b.push_back(at(Kind::card, apps[i], 76 + static_cast<double>(i) * 144, 70, 124, 40));
    b.push_back(at(Kind::button, "Mute", 880, 380, 120, 40));
    b.push_back(at(Kind::button, "Snooze", 1020, 380, 120, 40));
    b.push_back(at(Kind::button, "Later", 150, 300, 120, 40));
    b.push_back(at(Kind::button, "Remind me", 1000, 300, 140, 40));
    b.push_back(at(Kind::card, "Calls", 120, 480, 120, 40));
    b.push_back(at(Kind::card, "Alerts", 860, 480, 120, 40));
    b.push_back(at(Kind::button, "Banners", 780, 300, 120, 40));
    b.push_back(at(Kind::button, "Sounds", 450, 560, 120, 40));
    b.push_back(at(Kind::button, "Previews", 710, 560, 120, 40));
    return r;
}

RoomDef brightness() {
    RoomDef r;
    r.number = 2;
    r.name = "亮度";
    r.title = "Brightness";
    r.style = "neon";
    r.speed = 6.5;
    r.seed = 12;
    r.made = "Brightness and Volume are real sliders: each hit raises the value. A full one fires a beam: one query along its row in the "
             "simulation, not a test per pixel. Every beam, flip and charge is an event of the step, and the components follow the events.";
    auto& b = r.bricks;
    // The top row is a dam (no gap a ball gets through): Brightness's beam breaks it open.
    b.push_back(at(Kind::core, "Shut down", 688, 136, 150, 44, 4));
    b.push_back(at(Kind::wall, "", 0, 140, 24, 36));
    b.push_back(wired(at(Kind::slider, "Brightness", 28, 140, 212, 36, 3), 1));
    b.push_back(at(Kind::card, "HDR", 248, 140, 138, 36, 3));
    b.push_back(at(Kind::card, "True Tone", 394, 140, 138, 36, 3));
    b.push_back(at(Kind::card, "Night Shift", 540, 140, 140, 36, 3));
    b.push_back(at(Kind::card, "Auto-lock", 846, 140, 138, 36, 3));
    b.push_back(at(Kind::card, "Zoom", 992, 140, 138, 36, 3));
    b.push_back(at(Kind::card, "Display", 1138, 140, 114, 36, 3));
    b.push_back(at(Kind::wall, "", 1256, 140, 24, 36));
    b.push_back(at(Kind::card, "Screen Time", 688, 188, 150, 24, 3));  // under the core
    // Volume's row: its beam flips Auto-brightness, which fires Brightness early.
    b.push_back(wired(at(Kind::toggle, "Auto-brightness", 380, 270, 220, 36), 1, 1));
    b.push_back(at(Kind::slider, "Volume", 900, 270, 210, 36, 3));
    b.push_back(at(Kind::button, "Balance", 1130, 270, 120, 36));
    b.push_back(at(Kind::button, "Bass", 150, 270, 110, 36));
    std::array<const char*, 6> row{"Ringtone", "Haptics", "Keyboard", "Lock sound", "Alerts", "Media"};
    for (std::size_t i = 0; i < row.size(); ++i) {
        BrickDef d = at(Kind::button, row[i], 160 + static_cast<double>(i) * 170, 400, 120, 36);
        b.push_back(i == 1 || i == 4 ? slot(std::move(d)) : std::move(d));
    }
    return r;
}

RoomDef cloud() {
    RoomDef r;
    r.number = 3;
    r.name = "云";
    r.title = "The Cloud";
    r.style = "ink";
    r.speed = 6.5;
    r.seed = 13;
    r.canvas = true;
    r.height = 2 * field_h;
    r.chamber = 700;
    r.hint = "Hold Z  see the whole room";
    r.made = "The room is an infinite canvas (openmui.ui.canvas): a scene of nodes, drawn far away as one instanced batch of boxes and "
             "promoted near to live components. The camera is part of the simulation's state, so a rewind brings it back exactly; it never "
             "zooms out past half, so the ball stays big enough to follow.";
    auto& b = r.bricks;
    b.push_back(wired(at(Kind::toggle, "Wi-Fi", 540, 1160, 200, 44), 1, 1));
    b.push_back(wired(at(Kind::kicker, "Upload", 110, 1020, 130, 30), 1, 1));
    b.push_back(wired(at(Kind::kicker, "Upload", 1040, 1020, 130, 30), 1, 0));
    std::array<const char*, 5> files{"Photos", "Notes", "Mail", "Drive", "Backup"};
    for (std::size_t i = 0; i < files.size(); ++i) b.push_back(at(Kind::button, files[i], 150 + static_cast<double>(i) * 215, 880, 130, 36));
    for (int i = 0; i < 12; ++i) {
        BrickDef c = at(Kind::card, "", 10 + i * 106.0, 680, 100, 20);
        c.fill = 0x7f93b8;
        b.push_back(std::move(c));
    }
    BrickDef core = at(Kind::core, "Storage full", 520, 110, 240, 52, 4);
    core.fill = 0xe0524a;
    b.push_back(std::move(core));
    // The chamber: forty files, colours from cool at the top to warm by the cloud.
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 8; ++col) {
            const std::uint32_t t = static_cast<std::uint32_t>(row * 255 / 4);
            BrickDef f = at(Kind::card, std::format("IMG_{:04}", 412 + row * 8 + col), 90 + col * 140.0 + (row % 2) * 35, 220 + row * 62.0, 96, 28);
            f.fill = ((0x40 + t * 3 / 4) << 16) | ((0x90 - t / 3) << 8) | (0xf0 - t / 2);
            b.push_back((row == 2 && (col == 1 || col == 6)) ? slot(std::move(f)) : std::move(f));
        }
    for (BrickDef& d : b)
        if (d.fill == 0) d.fill = 0x2b3550;
    return r;
}

RoomDef dark_mode() {
    RoomDef r;
    r.number = 4;
    r.name = "深色模式";
    r.title = "Dark Mode";
    r.style = "retro";
    r.speed = 6.75;
    r.seed = 14;
    r.dark = true;
    r.gate_from = 2 * steps_per_second;  // open two seconds of every seven
    r.gate_period = 7 * steps_per_second;
    r.gate_open = 2 * steps_per_second;
    r.hint = "X  X-ray: what hides in the dark";
    r.made = "The hidden caches are real buttons drawn with no opacity: the X-ray shows what the semantic tree always knew. A rewind is the "
             "simulation's timeline — a snapshot every half second and every input since, replayed to the same bytes — and its cost is an "
             "input too, so a replay pays it again.";
    auto& b = r.bricks;
    b.push_back(at(Kind::core, "Shut down", 550, 70, 180, 50, 3));
    b.push_back(at(Kind::wall, "", 0, 170, 470, 22));
    b.push_back(at(Kind::gate, "Updating…", 470, 170, 340, 22));
    b.push_back(at(Kind::wall, "", 810, 170, 470, 22));
    b.push_back(wired(at(Kind::toggle, "Dark mode", 40, 222, 200, 40), 0, 1));  // up in a corner: the lights are earned
    std::array<const char*, 8> row{"Wallpaper", "Icons", "Fonts", "Accent", "Contrast", "Motion", "Display", "Text size"};
    for (std::size_t i = 0; i < row.size(); ++i) b.push_back(at(Kind::button, row[i], 70 + static_cast<double>(i) * 145, 300, 110, 34));
    for (const auto [x, y] : std::array<std::pair<double, double>, 6>{{{180, 390}, {1010, 390}, {360, 560}, {840, 560}, {560, 460}, {1000, 230}}})
        b.push_back(at(Kind::hidden, "cache", x, y, 90, 28));
    b.push_back(at(Kind::wall, "", 1170, 205, 16, 80));
    b.push_back(at(Kind::wall, "", 1236, 205, 16, 80));
    b.push_back(slot(at(Kind::hidden, "secret", 1188, 210, 46, 22)));
    return r;
}

RoomDef sync_room() {
    RoomDef r;
    r.number = 5;
    r.name = "同步";
    r.title = "Sync";
    r.style = "liquid";
    r.speed = 7.0;
    r.seed = 15;
    r.second_paddle = true;
    r.hint = "Your copilot feeds you · 1 hold · 2 bank left · 3 bank right · 4 feed me";
    r.made = "Your teammate is an agent like any other: thirty times a second it reads the field's semantic node and the bricks still standing "
             "in the semantic tree, and moves through the field's guard_to action as agent:copilot — journaled, the same interface a test or "
             "an outside agent uses. Your orders are a value of the field it reads.";
    auto& b = r.bricks;
    b.push_back(at(Kind::core, "Shut down", 545, 330, 190, 50, 6));
    b.push_back(at(Kind::wall, "", 515, 304, 250, 16));  // its roof: the copilot cannot pound it from above
    b.push_back(wired(at(Kind::shutter, "", 515, 320, 20, 70), 1));
    b.push_back(wired(at(Kind::shutter, "", 745, 320, 20, 70), 1));
    b.push_back(wired(at(Kind::shutter, "", 515, 390, 250, 16), 1));
    b.push_back(wired(at(Kind::toggle, "Pair", 110, 470, 180, 40), 1, 1));
    b.push_back(wired(at(Kind::toggle, "Pair", 990, 470, 180, 40), 1, 1));
    b.push_back(at(Kind::slider, "Signal", 40, 335, 200, 40, 3));
    b.push_back(at(Kind::slider, "Signal", 1040, 335, 200, 40, 3));
    std::array<const char*, 8> devices{"Watch", "Phone", "Tablet", "Laptop", "Speaker", "TV", "Car", "Ring"};
    for (std::size_t i = 0; i < devices.size(); ++i) {
        BrickDef d = at(Kind::button, devices[i], 80 + static_cast<double>(i) * 145, 210, 120, 34);
        b.push_back(i == 2 || i == 5 ? slot(std::move(d)) : std::move(d));
    }
    b.push_back(at(Kind::button, "Hub", 260, 345, 110, 30));
    b.push_back(at(Kind::button, "Bridge", 385, 345, 110, 30));
    b.push_back(at(Kind::button, "Relay", 785, 345, 110, 30));
    b.push_back(at(Kind::button, "Node", 910, 345, 110, 30));
    b.push_back(wired(at(Kind::kicker, "Boost", 30, 600, 110, 24), 0, 1));
    b.push_back(wired(at(Kind::kicker, "Boost", 1140, 600, 110, 24), 0, 0));
    return r;
}

constexpr std::array<Contraption, 5> all_contraptions{{
    {Gift::wide, "wide", "Long paddle", "A wider paddle for a while: huge and brief, or wide and long.", {260, 95}, {"Huge · 4 s", "Wide · 12 s"}},
    {Gift::magnet, "magnet", "Magnet", "The paddle holds the ball, and you aim again.", {2, 5}, {"2 catches", "5 catches"}},
    {Gift::multi, "multi", "Splitter", "Every ball in flight splits.", {1, 4}, {"Into two", "Into five"}},
    {Gift::pierce, "pierce", "Drill", "The ball goes through bricks: short and often, or long and rare.", {2, 10}, {"2 s · often", "10 s · rare"}},
    {Gift::slow, "slow", "Molasses", "Time runs slow: very slow and short, or a little slow for long.", {25, 75}, {"25 % · 5 s", "75 % · 16 s"}},
}};

}  // namespace

std::span<const RoomDef> rooms() {
    static const std::array<RoomDef, room_count> all{do_not_disturb(), brightness(), cloud(), dark_mode(), sync_room()};
    return all;
}

const RoomDef& room(std::int32_t number) {
    for (const RoomDef& r : rooms())
        if (r.number == number) return r;
    return rooms().front();
}

std::span<const Contraption> contraptions() { return all_contraptions; }

const Contraption* contraption(Gift g) {
    for (const Contraption& c : all_contraptions)
        if (c.gift == g) return &c;
    return nullptr;
}

const Contraption* contraption(std::string_view script) {
    for (const Contraption& c : all_contraptions)
        if (c.script == script) return &c;
    return nullptr;
}

std::array<Gift, 2> offers(std::int32_t after, std::span<const Gift> kit) {
    // By the room just cleared, in order of preference; the first two not taken yet.
    static constexpr std::array<std::array<Gift, 5>, 4> order{{{Gift::wide, Gift::magnet, Gift::multi, Gift::pierce, Gift::slow},
                                                               {Gift::multi, Gift::pierce, Gift::wide, Gift::magnet, Gift::slow},
                                                               {Gift::slow, Gift::magnet, Gift::wide, Gift::multi, Gift::pierce},
                                                               {Gift::pierce, Gift::multi, Gift::magnet, Gift::wide, Gift::slow}}};
    std::array<Gift, 2> out{Gift::none, Gift::none};
    std::size_t n = 0;
    for (const Gift g : order[static_cast<std::size_t>(std::clamp(after, 1, 4) - 1)])
        if (n < out.size() && std::ranges::find(kit, g) == kit.end()) out[n++] = g;
    return out;
}

std::string brick_id(std::size_t index) { return std::format("brick.{}", index); }

Element brick_view(const BrickDef& b, std::size_t index) {
    const std::string id = brick_id(index);
    const Dim w{b.width}, h{b.height};
    Element e = [&]() -> Element {
        switch (b.kind) {
            case Kind::button: return button({.id = id, .text = b.label, .width = w, .height = h});
            case Kind::core: return button({.id = id, .text = b.label, .variant = ButtonVariant::danger, .width = w, .height = h});
            case Kind::kicker: return button({.id = id, .text = b.label, .variant = ButtonVariant::secondary, .width = w, .height = h});
            case Kind::toggle: return toggle({.id = id, .checked = b.on != 0, .label = b.label, .width = w, .height = h});
            case Kind::slider: return slider({.id = id, .label = b.label, .max = 1, .min = 0, .step = 0.01, .value = 0.0, .width = w, .height = h});
            case Kind::card:  // a card: its text in a box of the brick's size
                return std::move(column({.id = id, .width = w, .height = h, .justify = "center", .align = "center", .border_width = 1.0f, .radius = 10.0f},
                                        {text({.text = b.label, .style = "caption"})})
                                     .prop("role", Source(Value("region"))));  // a panel: a style's panel effects reach it
            case Kind::hidden: return button({.id = id, .text = b.label, .width = w, .height = h, .opacity = 0.0f});
            case Kind::gate: return progress({.id = id, .label = b.label, .value = 0.0, .width = w, .height = h});
            case Kind::wall:
            case Kind::shutter: break;
        }
        return column({.id = id, .width = w, .height = h});  // a plate: the field draws it
    }();
    // Where it stands, in shares of the field (the field's units are 1280 × 720 at any size).
    return std::move(e.prop("position", Source(Value("absolute")))
                         .prop("left", Source(Value(std::format("{}%", b.x / field_w * 100))))
                         .prop("top", Source(Value(std::format("{}%", b.y / field_h * 100)))));
}

void fill_scene(const RoomDef& r) {
    auto& scene = world::scene(cloud_scene);
    static const bool kinds = [&] {
        // Far: a box of its colour. Near (60 px and up on screen): a live card, its word in it; a switch, a live toggle.
        scene.kind("note", world::Kind{.tiers = {{0, world::Tier::Draw::box}, {60, world::Tier::Draw::promote}},
                                       .promote = [](const world::Scene& s, world::NodeRef n) {
                                           return column({.id = std::string(s.key(n)), .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)),
                                                          .justify = "center", .align = "center", .background = s.fill(n), .radius = 8.0f},
                                                         {text({.text = std::string(s.text(n, "title")), .style = "label", .color = Color::hex(0xffffff)})});
                                       },
                                       .role = Role::article});
        scene.kind("switch", world::Kind{.tiers = {{0, world::Tier::Draw::box}, {60, world::Tier::Draw::promote}},
                                         .promote = [](const world::Scene& s, world::NodeRef n) {
                                             return toggle({.id = std::string(s.key(n)), .checked = s.number(n, "on") != 0,
                                                            .label = std::string(s.text(n, "title")), .width = Dim(layout::pct(100)),
                                                            .height = Dim(layout::pct(100))});
                                         },
                                         .role = Role::switch_});
        return true;
    }();
    (void)kinds;
    scene.clear();
    for (std::size_t i = 0; i < r.bricks.size(); ++i) {
        const BrickDef& b = r.bricks[i];
        const std::string key = brick_id(i);
        const world::NodeRef n = scene.add({.kind = b.kind == Kind::toggle ? "switch" : "note", .x = b.x, .y = b.y, .width = b.width, .height = b.height,
                                            .key = key, .fill = Color::hex(b.fill), .radius = 6});
        scene.set(n, "title", b.label);
        scene.set(n, "on", static_cast<double>(b.on));
    }
}

std::vector<Element> room_bricks(const RoomDef& r) {
    std::vector<Element> out;
    if (r.canvas) {  // the scene's camera is the field's (it takes no pointer: that moves the paddle); the room fills it when it begins
        out.push_back(std::move(canvas({.id = "cloud", .inertia = false, .label = "The cloud", .max_zoom = 4, .min_zoom = 0.05, .scene = std::string(cloud_scene),
                                        .x = field_w / 2, .y = r.height - field_h / 2, .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))})
                                     .prop("pointer_events", Source(Value("none")))));
        return out;
    }
    // In a panel named by its room: a new room's bricks are new nodes, laid out afresh (never the last room's places).
    std::vector<Element> bricks;
    for (std::size_t i = 0; i < r.bricks.size(); ++i) bricks.push_back(brick_view(r.bricks[i], i));
    out.push_back(column({.id = std::format("room.{}", r.number), .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}, std::move(bricks)));
    return out;
}

}  // namespace omui::breakout
