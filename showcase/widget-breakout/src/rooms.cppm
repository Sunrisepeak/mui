// openmui.showcase.widget_breakout:rooms — the run's five rooms as data:
// each brick's kind, durability, circuit and what it is as a component (a
// label, a switch's state, a capacitor's size), where it stands in the
// field's units (1280 × 720 a screen). A room's view is built from this,
// and its simulation's bricks are these in the same order, placed where the
// view's layout puts them; their sizes are explicit, so a style never moves
// one. And the contraptions offered between rooms.
export module openmui.showcase.widget_breakout:rooms;

import std;
import openmui;
import openmui.showcase.widget_breakout.sim;

export namespace omui::breakout {

struct BrickDef {
    Kind          kind = Kind::button;
    std::int32_t  durability = 1;     // a capacitor: the hits that fill it
    std::string   label;
    double        x = 0, y = 0;       // its top left, in the field's units
    float         width = 120, height = 36;
    std::uint32_t wire = 0, on = 0;   // its circuit, and how it starts (a switch on, a shutter open, a kicker pointing right)
    bool          slot = false;       // drops one of the run's contraptions, broken
    std::uint32_t fill = 0;           // on a canvas room: its colour (0xRRGGBB)
};

struct RoomDef {
    std::int32_t          number = 1;
    std::string           name;      // 勿扰
    std::string           title;     // Do Not Disturb
    std::string           style;     // its style pack
    std::string           made;      // the inspector's note: how it is made
    std::string           hint;      // its one hint, shown once, when it is needed
    std::vector<BrickDef> bricks;
    double                speed = 6.5;
    std::uint64_t         seed = 1;
    std::int32_t          aim_phase = 60;     // where the first launch's aim starts sweeping
    std::int64_t          gate_from = 0, gate_period = 0, gate_open = 0;  // its gates come round
    bool                  second_paddle = false;  // a paddle at the top: the copilot's
    double                height = field_h;       // taller than a screen: a canvas room
    double                chamber = 0;            // its upper chamber above this line
    bool                  canvas = false;         // its bricks are nodes of a canvas's scene (room 3)
    bool                  dark = false;           // lit only around the balls while its switch is on (room 4)
};

inline constexpr std::int32_t room_count = 5;
[[nodiscard]] std::span<const RoomDef> rooms();
[[nodiscard]] const RoomDef& room(std::int32_t number);  // that one (none: the first)

// A contraption: the gift it is, its script, what the card says, and its
// script's two presets for the marked value (the power spent briefly or
// steadily).
struct Contraption {
    Gift                            gift = Gift::none;
    std::string_view                script;   // the verb gift.<script>, the file <script>.<language>
    std::string_view                name, line;
    std::array<double, 2>           presets{};
    std::array<std::string_view, 2> preset_names{};
};
[[nodiscard]] std::span<const Contraption> contraptions();
[[nodiscard]] const Contraption* contraption(Gift g);
[[nodiscard]] const Contraption* contraption(std::string_view script);
// The two offered after room `after`, none already in the kit.
[[nodiscard]] std::array<Gift, 2> offers(std::int32_t after, std::span<const Gift> kit);

// A brick as its component, where it stands: a button, a switch (toggle), a
// capacitor (slider), a card (text), a hidden cache (nothing drawn: only the
// semantic tree knows it is there), a wall or a shutter (a plate), a gate (a
// progress bar), a kicker (a button), the core (a danger button).
[[nodiscard]] Element brick_view(const BrickDef& b, std::size_t index);
// The id the n-th brick's node has: "brick.<n>".
[[nodiscard]] std::string brick_id(std::size_t index);
// A room's bricks: the field's child, a panel of them named by the room (a canvas room: its canvas).
[[nodiscard]] std::vector<Element> room_bricks(const RoomDef& r);

// The scene a canvas room's bricks are in (openmui.ui.canvas): far, boxes
// of their colour; near, live cards and switches.
inline constexpr std::string_view cloud_scene = "breakout.cloud";
// The scene filled with the room's bricks (its kinds registered once).
void fill_scene(const RoomDef& r);

}  // namespace omui::breakout
