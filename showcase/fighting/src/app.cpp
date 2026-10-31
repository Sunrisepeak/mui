// The application: the title and the select screen, then the fight filling
// the window, best of three; the match over, its results — a rematch, or
// back to the select screen (the same fighters under the cursors). The stage and the select screen are
// registered by name for view documents, scripts and agents.
module openmui.showcase.fighting;

import std;
import openmui;

namespace omui::fighting {

namespace {

// What the application keeps between its setup and its view: which screen
// is up, and what the select screen chose.
struct Lobby {
    Signal<bool>        fighting = signal(false);
    Signal<std::string> phase = signal(std::string("title"));
    Signal<std::string> mode = signal(std::string("versus")), player1 = signal(std::string()), player2 = signal(std::string()),
                        arena = signal(std::string());
    Signal<std::int32_t> cpu = signal(std::int32_t{-1});  // the player the computer plays (-1: none)
    std::int32_t         cpu_level = 2, turn_frames = 0;
};

}  // namespace

void register_components() {
    register_type(component_type<Stage>());
    register_type(component_type<Select>());
}

AppSpec app(Start start) {
    AppSpec a;
    a.id = "org.openmui.fighting";
    a.title = "拳意 · 格斗游戏";
    a.size = {1280, 720};
    auto l = std::make_shared<Lobby>();
    if (!start.player1.empty()) {  // straight in a fight
        l->player1.set(start.player1);
        l->player2.set(start.player2.empty() ? start.player1 : start.player2);
        l->arena.set(start.arena);
        l->fighting.set(true);
    }
    l->cpu.set(start.cpu);
    l->cpu_level = start.cpu_level;
    l->turn_frames = start.turn_frames;
    a.setup = [](Runtime&) {
        register_components();
    };
    a.view = [l] {
        Element fight = stage({.id = "stage",
                               .arena = l->arena,
                               .cpu = l->cpu,
                               .cpu_level = l->cpu_level,
                               .mode = l->mode,
                               .paused = computed([l] { return !l->fighting.get(); }),
                               .player1 = l->player1,
                               .player2 = l->player2,
                               .turn_frames = l->turn_frames,
                               .width = Dim(layout::pct(100)),
                               .height = Dim(layout::pct(100)),
                               .visible = l->fighting});
        // The players chose the select screen (the results' card, or the pause card's): the same fighters under the cursors.
        fight.on("leave", [l](const Value&) {
            l->phase.set("fighters");
            l->fighting.set(false);
        });
        Element menu = select({.id = "select",
                               .active = computed([l] { return !l->fighting.get(); }),
                               .phase = l->phase,
                               .width = Dim(layout::pct(100)),
                               .height = Dim(layout::pct(100)),
                               .visible = computed([l] { return !l->fighting.get(); })});
        menu.on("chosen", [l](const Value& args) {
            auto text = [&](std::string_view key) { return args.get(key) ? std::string(args.get(key)->as_string()) : std::string(); };
            l->mode.set(text("mode"));
            l->arena.set(text("mode") == "training" ? std::string() : text("arena"));
            l->player1.set(text("player1"));
            l->player2.set(text("player2"));
            l->cpu.set(static_cast<std::int32_t>(args.get("cpu") ? args.get("cpu")->as_int(-1) : -1));
            l->fighting.set(true);
        });
        return stack({.id = "game", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}, {std::move(fight), std::move(menu)});
    };
    return a;
}

}  // namespace omui::fighting
