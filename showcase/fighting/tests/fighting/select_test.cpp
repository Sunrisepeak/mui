// The application as a player meets it: the title's modes, the select
// screen's cursors (keys, a gamepad, an agent's actions), both players
// ready, the fight on the stage with whom they chose on the first one's
// home; the round over, back to the select screen. The screens' pictures
// are pinned and saved.
import std;
import omui;
import omui.showcase.fighting;
import mlibs.testing;
import mcpplibs.mtest;
import gongfu;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

struct App {
    Headless h{HeadlessOptions{.size = {960, 540}}};
    explicit App(fighting::Start start = {}) {
        AppSpec spec = fighting::app(std::move(start));
        spec.setup(h.runtime());
        h.mount("main", spec.view());
        h.step();
    }
    bool act(std::string_view node, std::string_view action, Value args = {}) {
        Invocation c;
        c.actor = {"agent", "test", {}};
        c.args = std::move(args);
        return h.tree().act(node, action, c).exit_code() == std::optional<int>(0);
    }
    Value seen(std::string_view id) {
        for (const auto& n : h.tree().snapshot({}).nodes)
            if (n.id == id) return n.value;
        return {};
    }
    const fighting::Select& select() { return *h.runtime().component<fighting::Select>(h.runtime().find("main/select")); }
    const fighting::Stage& stage() { return *h.runtime().component<fighting::Stage>(h.runtime().find("main/stage")); }
    std::uint64_t pixels() {
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        return std::stoull(hash.hex(), nullptr, 16);
    }
};

Value player(int p) { return Value{}.set("player", p); }
KeyEvent key(Key k, bool down = true) { return {.kind = down ? KeyKind::down : KeyKind::up, .key = k}; }

}  // namespace

static UTest cases{"showcase.fighting.select", [] {

    "the application opens on the title: versus under the cursor; down is training, confirm goes to the select screen"_test = [] {
        App a;
        expect(eq(a.select().phase(), std::string_view("title")));
        expect(eq(std::string(a.seen("main/select").get("mode")->as_string()), std::string("versus")));
        a.h.step({key(Key::s), key(Key::s, false)});
        expect(eq(std::string(a.seen("main/select").get("mode")->as_string()), std::string("training")));
        a.h.step({key(Key::w), key(Key::w, false), key(Key::u), key(Key::u, false)});
        expect(eq(a.select().phase(), std::string_view("fighters")));
        expect(eq(std::string(a.seen("main/select").get("mode")->as_string()), std::string("versus")));
    };

    "the pointer as player 1: a mode clicked is chosen; a fighter clicked is under the cursor, clicked again chosen"_test = [] {
        App a;
        const auto click = [&](Point at) {
            a.h.step({PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1},
                      PointerEvent{.kind = PointerKind::up, .position = at, .button = MouseButton::left}});
        };
        click({480 + 100, 406 + 20});  // 训练 · TRAINING's bar (the window is the picture, 960 by 540)
        expect(eq(a.select().phase(), std::string_view("fighters")));
        expect(eq(std::string(a.seen("main/select").get("mode")->as_string()), std::string("training")));
        // The roster: three across from x 285, 126 wide with 12 between; from y 84.
        const Point third{285 + 2 * 138 + 63, 84 + 50};
        click(third);
        expect(eq(a.select().cursor(0), 2));
        expect(!a.select().ready(0)) << "under the cursor, not chosen";
        click(third);
        expect(a.select().ready(0)) << "clicked again: chosen";
    };

    "choosing: each player's cursor over the roster, both ready, the fight on the first one's home — 坤 on his rooftop against 霓"_test = [] {
        App a;
        expect(a.act("main/select", "confirm", player(0))) << "versus";
        const auto& c = fighting::Select::choices();
        const auto at = [&](std::string_view n) { return static_cast<std::int32_t>(std::ranges::find(c, n) - c.begin()); };
        expect(a.act("main/select", "move", player(0).set("by", at("kun") - a.select().cursor(0)))) << "player 1 to 坤";
        expect(a.act("main/select", "move", player(1).set("by", at("ni") - a.select().cursor(1)))) << "player 2 to 霓";
        expect(a.act("main/select", "confirm", player(0)) && a.select().ready(0) && !a.select().ready(1)) << "player 1 ready";
        expect(a.act("main/select", "back", player(0)) && !a.select().ready(0)) << "and not again";
        expect(a.act("main/select", "confirm", player(0)) && a.act("main/select", "confirm", player(1))) << "both ready";
        a.h.advance(seconds(1.2));  // the VS has its moment
        const auto& m = a.stage().match();
        expect(eq(m.fighter(0).def.name, std::string("Kun")));
        expect(eq(m.fighter(1).def.name, std::string("Ni")));
        expect(!a.stage().props.paused) << "the fight plays";
        a.h.advance(seconds(1));
        expect(m.state().frame > 30) << std::format("frames played: {}", m.state().frame);
        const auto dir = scratch_dir("fighting-select");
        expect(a.h.save_png(dir / "fight.png").has_value());
    };

    "one at the keys plays the computer: player 1 ready, it chooses someone else and is ready; player 2 joins by moving"_test = [] {
        App a;
        expect(a.act("main/select", "confirm", player(0))) << "versus";
        expect(eq(std::string(a.seen("main/select").get("p2")->get("by")->as_string()), std::string("computer")));
        expect(a.act("main/select", "confirm", player(0)) && a.select().ready(0) && a.select().ready(1)) << "player 1 ready; the computer too";
        expect(a.select().cursor(1) != a.select().cursor(0)) << "someone else";
        a.h.advance(seconds(1.2));
        expect(!a.stage().props.paused && a.stage().props.cpu == 1) << "the fight, against the computer";
        a.h.advance(seconds(2));
        expect(a.seen("main/stage").get("cpu")->as_int() == 1) << a.seen("main/stage").json();
        // Another time, player 2 at the keys: their first move takes the seat (and moves).
        App b;
        expect(b.act("main/select", "confirm", player(0)));
        const auto was = b.select().cursor(1);
        expect(b.act("main/select", "move", player(1).set("by", 1)));
        expect(eq(std::string(b.seen("main/select").get("p2")->get("by")->as_string()), std::string("person")));
        expect(b.select().cursor(1) != was) << "moved too";
        expect(b.act("main/select", "confirm", player(0)) && b.select().ready(0) && !b.select().ready(1)) << "player 2 chooses for themselves";
    };

    "a gamepad chooses too: south on the title, the d-pad across the roster, south ready; east not ready, and back to the title"_test = [] {
        App a;
        a.h.step({GamepadEvent{.kind = GamepadKind::connected, .device = 3, .name = "pad"}});
        a.h.step({GamepadEvent{.kind = GamepadKind::down, .device = 3, .button = GamepadButton::south}});
        expect(eq(a.select().phase(), std::string_view("fighters")));
        const std::int32_t was = a.select().cursor(0);
        a.h.step({GamepadEvent{.kind = GamepadKind::down, .device = 3, .button = GamepadButton::dpad_right}});
        expect(eq(a.select().cursor(0), (was + 1) % static_cast<std::int32_t>(fighting::Select::choices().size())));
        a.h.step({GamepadEvent{.kind = GamepadKind::down, .device = 3, .button = GamepadButton::south}});
        expect(a.select().ready(0)) << "ready";
        a.h.step({GamepadEvent{.kind = GamepadKind::down, .device = 3, .button = GamepadButton::east}});
        expect(!a.select().ready(0)) << "not ready";
        a.h.step({GamepadEvent{.kind = GamepadKind::down, .device = 3, .button = GamepadButton::east}});
        expect(eq(a.select().phase(), std::string_view("title")));
    };

    "best of three: a round to player 1, a red seal beside her name and the second round three seconds later; the second hers too, the match over and the select screen again"_test = [] {
        App a;
        expect(a.act("main/select", "confirm", player(0)) && a.act("main/select", "confirm", player(0)) && a.act("main/select", "confirm", player(1))) << "both ready";
        a.h.advance(seconds(3));  // past the round's call and FIGHT!
        expect(!a.stage().props.paused) << "the fight plays";
        auto round_to_player_1 = [&] {
            auto& match = const_cast<gongfu::Match&>(a.stage().match());
            gongfu::State st = match.state();
            st.timer = 2, st.f[1].health = 5000;
            match.restore(st);
            a.h.advance(seconds(0.3));
        };
        round_to_player_1();
        const Value seen = a.seen("main/stage");
        expect(seen.get("round") && seen.get("round")->as_int(0) == 1 && seen.get("wins") && seen.get("wins")->list() && (*seen.get("wins")->list())[0].as_int(0) == 1) << "round 1 to player 1";
        expect(a.h.save_png(scratch_dir("fighting-rounds") / "round-1-won.png").has_value());
        const std::uint64_t won = a.pixels();
        a.h.advance(seconds(3));
        expect(a.stage().match().state().over == 0 && a.seen("main/stage").get("round")->as_int(0) == 2) << "the second round";
        round_to_player_1();
        expect(a.seen("main/stage").get("decided")->as_bool(false)) << "the match decided";
        a.h.advance(seconds(5));
        const Value over = a.seen("main/stage");
        const Value* menu = over.get("menu");
        expect(menu && menu->get("shown")->as_string() == "results" && !a.stage().props.paused) << "the results over the match, not the select screen at once";
        expect(a.h.save_png(scratch_dir("fighting-rounds") / "results.png").has_value());
        const std::uint64_t results = a.pixels();
        a.h.step({key(Key::s), key(Key::s, false), key(Key::u), key(Key::u, false)});  // down to 选人 · FIGHTERS, chosen
        expect(a.select().phase() == std::string_view("fighters") && a.stage().props.paused) << "back to the select screen";
        std::println("    rounds {:016x} results {:016x}", won, results);
        expect(eq(results, std::uint64_t{0x378f71dd648bdbe1}));  // GCC and LLVM
        expect(eq(won, std::uint64_t{0x3ceac9426b585457}));  // GCC and LLVM
    };

    "straight into a fight (--fight): the keys are the players' from the first frame, Esc opens the card"_test = [] {
        fighting::Start start;
        start.player1 = "mo", start.player2 = "ni";
        App a(start);
        a.h.advance(seconds(3));
        expect(a.h.runtime().focused() == a.h.runtime().find("main/stage")) << std::format("the stage has the keys, not the select screen behind it (focused: '{}')", a.h.runtime().path(a.h.runtime().focused()));
        a.h.step({key(Key::escape), key(Key::escape, false)});
        const Value seen = a.seen("main/stage");  // kept: what is read from it points into it
        const Value* m = seen.get("menu");
        expect(m && m->get("shown") && m->get("shown")->as_string() == "paused") << "Esc paused it before any click";
    };

    "Esc pauses the fight under a card — the match stands, both players' keys on it; resume, restart, the select screen from it, by keys, the pointer and a pad; the next fight a new match"_test = [] {
        App a;
        expect(a.act("main/select", "confirm", player(0)) && a.act("main/select", "confirm", player(0)) && a.act("main/select", "confirm", player(1))) << "both ready";
        a.h.advance(seconds(3));
        const auto shown = [&] {
            const Value v = a.seen("main/stage");
            const Value* m = v.get("menu");
            return m ? std::string(m->get("shown")->as_string()) : std::string();
        };
        a.h.step({key(Key::d)});  // player 1 walking as the card opens
        a.h.step({key(Key::escape), key(Key::escape, false)});
        expect(eq(shown(), std::string("paused")));
        const std::int32_t at = a.stage().match().state().frame;
        a.h.advance(seconds(1));
        expect(eq(a.stage().match().state().frame, at));  // the match stands
        expect(a.h.save_png(scratch_dir("fighting-pause") / "paused.png").has_value());
        const std::uint64_t paused = a.pixels();
        a.h.step({key(Key::escape), key(Key::escape, false)});  // back: resumed
        expect(eq(shown(), std::string()));
        a.h.step({key(Key::d, false)});  // the walk let go under the card: still let go
        a.h.advance(seconds(0.5));
        expect(a.stage().match().state().frame > at + 20) << "playing again";
        // The pointer: the card again, 重来 · RESTART clicked where it is drawn (the window is the picture, 960 by 540).
        a.h.step({key(Key::escape), key(Key::escape, false)});
        const Rect restart = fighting::hud::card_item(1, 3);
        const Point mid{restart.x + restart.width / 2, restart.y + restart.height / 2};
        a.h.step({PointerEvent{.kind = PointerKind::move, .position = mid}});
        const Value lit = a.seen("main/stage");
        expect(eq(lit.get("menu")->get("lit")->as_int(-1), std::int64_t{1}));
        a.h.step({PointerEvent{.kind = PointerKind::down, .position = mid, .button = MouseButton::left, .buttons = 1},
                  PointerEvent{.kind = PointerKind::up, .position = mid, .button = MouseButton::left}});
        expect(shown().empty() && a.stage().match().state().frame < 5) << "restarted";
        // A pad: start pauses; down twice to 选人 · FIGHTERS, A chooses it.
        a.h.advance(seconds(1));
        for (GamepadButton b : {GamepadButton::start, GamepadButton::dpad_down, GamepadButton::dpad_down, GamepadButton::south})
            a.h.step({GamepadEvent{.kind = GamepadKind::down, .device = 7, .button = b}, GamepadEvent{.kind = GamepadKind::up, .device = 7, .button = b}});
        expect(a.select().phase() == std::string_view("fighters") && a.stage().props.paused) << "the select screen";
        // The same fighters chosen again: a new match, not the one left.
        a.h.advance(seconds(0.5));
        expect(a.act("main/select", "confirm", player(0)) && a.act("main/select", "confirm", player(1))) << "both ready again";
        for (int i = 0; i < 600 && a.stage().props.paused; ++i) a.h.advance(seconds(1.0 / 60));  // the versus screen's moment
        expect(!a.stage().props.paused && a.stage().match().state().frame < 5) << "a new match";
        std::println("    paused {:016x}", paused);
        expect(eq(paused, std::uint64_t{0xd8a4d22d4da3f60d}));  // GCC and LLVM
    };

    "training's tools on its pause card, for a person: player 2 recorded — the person's keys play it — and replayed; save and load; the overlays off"_test = [] {
        App a;
        a.h.step({key(Key::s), key(Key::s, false), key(Key::u), key(Key::u, false)});  // training
        a.act("main/select", "confirm", player(0));
        a.act("main/select", "confirm", player(1));
        for (int i = 0; i < 600 && a.stage().props.paused; ++i) a.h.advance(seconds(1.0 / 60));
        expect(!a.stage().props.paused && a.stage().props.mode == "training") << "training";
        a.h.advance(seconds(1));
        const auto items = [&] {
            std::vector<std::string> out;
            const Value v = a.seen("main/stage");
            if (const Value* m = v.get("menu"))
                for (const Value& i : *m->get("items")->list()) out.emplace_back(i.as_string());
            return out;
        };
        const auto training = [&](std::string_view key) {
            const Value v = a.seen("main/stage");
            const Value* t = v.get("training")->get(key);
            return t ? std::string(t->as_string()) : std::string();
        };
        const auto esc = [&] { a.h.step({key(Key::escape), key(Key::escape, false)}); };
        esc();
        expect(eq(items(), std::vector<std::string>{"resume", "save", "record", "fill", "overlays_off", "moves", "restart", "fighters"}));
        expect(a.h.save_png(scratch_dir("fighting-pause") / "training.png").has_value());
        const std::uint64_t card = a.pixels();
        // 录制 2P: down twice, U. The person's keys are player 2's until it stops.
        a.h.step({key(Key::s), key(Key::s, false), key(Key::s), key(Key::s, false), key(Key::u), key(Key::u, false)});
        expect(items().empty() && training("dummy2") == "recording") << "recording player 2";
        const auto x = [&](int p) { return a.stage().match().state().f[static_cast<std::size_t>(p)].pos.x; };
        const auto x1 = x(0), x2 = x(1);
        a.h.step({key(Key::d)});
        a.h.advance(seconds(0.5));
        a.h.step({key(Key::d, false)});
        expect(x(0) == x1 && x(1) != x2) << "D walked player 2, not player 1";
        // Stopped with the pointer (停录, where 录制 was), then replayed from the card.
        esc();
        const auto now = items();
        expect(now.size() > 2 && now[2] == "stop_recording") << "stop recording, where it was";
        const Rect stop = fighting::hud::card_item(2, now.size());
        const Point mid{stop.x + stop.width / 2, stop.y + stop.height / 2};
        a.h.step({PointerEvent{.kind = PointerKind::down, .position = mid, .button = MouseButton::left, .buttons = 1},
                  PointerEvent{.kind = PointerKind::up, .position = mid, .button = MouseButton::left}});
        expect(training("dummy2").empty()) << "stopped";
        esc();
        const auto with = items();
        const auto replay = std::ranges::find(with, "replay");
        expect(replay != with.end()) << "replay offered once there is something";
        for (auto i = replay - with.begin(); i > 0; --i) a.h.step({key(Key::s), key(Key::s, false)});
        a.h.step({key(Key::enter), key(Key::enter, false)});
        expect(eq(training("dummy2"), std::string("playing")));
        const auto from = x(1);
        a.h.advance(seconds(0.4));
        expect(x(1) != from) << "player 2 plays its recording, nobody at the keys";
        // Save, then load: back where it was kept.
        esc();
        a.h.step({key(Key::s), key(Key::s, false), key(Key::u), key(Key::u, false)});  // 存档
        const std::int32_t kept = a.stage().match().state().frame;
        a.h.advance(seconds(0.5));
        esc();
        expect(eq(items()[2], std::string("load")));
        a.h.step({key(Key::s), key(Key::s, false), key(Key::s), key(Key::s, false), key(Key::u), key(Key::u, false)});  // 读档
        expect(eq(a.stage().match().state().frame, kept));
        // The overlays off.
        esc();
        const auto last = items();
        const auto off = std::ranges::find(last, "overlays_off") - last.begin();
        for (auto i = off; i > 0; --i) a.h.step({key(Key::s), key(Key::s, false)});
        a.h.step({key(Key::u), key(Key::u, false)});
        expect(!a.stage().props.overlays) << "the overlays off";
        // The move list: 招式表 from the card, a move chosen is demonstrated over and over; Esc from the list goes back to the card.
        const auto go = [&](std::string_view item) {
            const auto now = items();
            const auto at = std::ranges::find(now, item) - now.begin();
            for (auto i = at; i > 0; --i) a.h.step({key(Key::s), key(Key::s, false)});
            a.h.step({key(Key::u), key(Key::u, false)});
        };
        esc();
        go("moves");
        const auto list = items();
        expect(std::ranges::find(list, "5HP") != list.end()) << "the list has the moves";
        const Value listed = a.seen("main/stage");
        expect(eq(std::string(listed.get("menu")->get("shown")->as_string()), std::string("moves")));
        expect(a.h.save_png(scratch_dir("fighting-pause") / "moves.png").has_value());
        const std::uint64_t moves = a.pixels();
        a.h.step({key(Key::escape), key(Key::escape, false)});
        const auto card_again = items();
        expect(std::ranges::find(card_again, "moves") != card_again.end()) << "back to the card";
        go("moves");
        go("5HP");
        expect(items().empty() && a.stage().props.demo == "5HP") << "5HP demonstrated";
        expect(a.act("main/stage", "demonstrate", Value{}.set("move", "")) && a.stage().props.demo.empty()) << "an agent stops it";
        expect(!a.act("main/stage", "demonstrate", Value{}.set("move", "nothing"))) << "no such move";
        std::println("    move list {:016x}", moves);
        expect(eq(moves, std::uint64_t{0x8efb1c9bf350d7ff}));  // GCC and LLVM
        // Who fights, by an agent: 坤 for player 1, a new match; an unknown name refused.
        expect(a.act("main/stage", "fighters", Value{}.set("player1", "kun").set("player2", ""))) << "fighters";
        expect(eq(a.stage().match().fighter(0).def.name, std::string("Kun")));
        expect(!a.act("main/stage", "fighters", Value{}.set("player1", "").set("player2", "nobody"))) << "no such fighter";
        std::println("    training card {:016x}", card);
        expect(eq(card, std::uint64_t{0x5a92d3d32c58b4f3}));  // GCC and LLVM
    };

    "the title and the select screen, pinned: the logo over the study, the modes; the roster, both fighters' previews, VS"_test = [] {
        App a;
        a.h.advance(seconds(0.5));
        const auto dir = scratch_dir("fighting-menus");
        expect(a.h.save_png(dir / "title.png").has_value());
        const std::uint64_t title = a.pixels();
        expect(a.act("main/select", "confirm", player(0)));
        a.h.advance(seconds(0.5));
        expect(a.h.save_png(dir / "select.png").has_value());
        const std::uint64_t select = a.pixels();
        std::println("    menus title {:016x} select {:016x}", title, select);
        expect(title != select) << "two screens";
        expect(eq(title, std::uint64_t{0x643fb24c0ef4dddf}));   // GCC and LLVM
        expect(eq(select, std::uint64_t{0xc33cdb5cf57ed497}));  // GCC and LLVM
    };
}};
