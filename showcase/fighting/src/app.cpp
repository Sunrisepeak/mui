// The application: the title and the select screen, then the fight filling
// the window; the round over, back to the select screen (the same fighters
// under the cursors) for the next. The stage and the select screen are
// registered by name for view documents, scripts and agents.
module openmui.showcase.fighting;

import std;
import openmui;

namespace omui::fighting {

namespace {

// What the application keeps between its setup and its view: which screen
// is up, and what the select screen chose.
struct Lobby {
    Runtime*            runtime = nullptr;
    Signal<bool>        fighting = signal(false);
    Signal<std::string> phase = signal(std::string("title"));
    Signal<std::string> mode = signal(std::string("versus")), player1 = signal(std::string()), player2 = signal(std::string()),
                        arena = signal(std::string());
    std::uint64_t       back = 0;  // the timer taking the players back to the select screen
};

}  // namespace

void register_components() {
    register_type(component_type<Stage>());
    register_type(component_type<Select>());
}

AppSpec app() {
    AppSpec a;
    a.id = "org.openmui.fighting";
    a.title = "拳意 · 格斗游戏";
    a.size = {1280, 720};
    auto l = std::make_shared<Lobby>();
    a.setup = [l](Runtime& rt) {
        l->runtime = &rt;
        register_components();
    };
    a.view = [l] {
        Element fight = stage({.id = "stage",
                               .arena = l->arena,
                               .mode = l->mode,
                               .paused = computed([l] { return !l->fighting.get(); }),
                               .player1 = l->player1,
                               .player2 = l->player2,
                               .width = Dim(layout::pct(100)),
                               .height = Dim(layout::pct(100)),
                               .visible = l->fighting});
        // The round over: a few seconds for the K.O. and the finish, then the select screen again.
        fight.on("round_over", [l](const Value&) {
            Runtime* rt = l->runtime;
            if (!rt || l->back) return;
            l->back = rt->after(seconds(4.5), [l] {
                l->back = 0;
                l->phase.set("fighters");
                l->fighting.set(false);
            });
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
            l->fighting.set(true);
        });
        return stack({.id = "game", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}, {std::move(fight), std::move(menu)});
    };
    return a;
}

}  // namespace omui::fighting
