// The application as a player meets it: the title's modes, the select
// screen's cursors (keys, a gamepad, an agent's actions), both players
// ready, the fight on the stage with whom they chose on the first one's
// home; the round over, back to the select screen. The screens' pictures
// are pinned and saved.
import std;
import openmui;
import openmui.showcase.fighting;
import openmui.testing;
import gongfu;

using namespace omui::testing;
using namespace omui;

namespace {

struct App {
    Headless h{HeadlessOptions{.size = {960, 540}}};
    App() {
        AppSpec spec = fighting::app();
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

int main(int argc, char** argv) {
    Suite s{"showcase.fighting.select"};

    s.test("the application opens on the title: versus under the cursor; down is training, confirm goes to the select screen", [] {
        App a;
        check_eq(a.select().phase(), std::string_view("title"));
        check_eq(std::string(a.seen("main/select").get("mode")->as_string()), std::string("versus"));
        a.h.step({key(Key::s), key(Key::s, false)});
        check_eq(std::string(a.seen("main/select").get("mode")->as_string()), std::string("training"));
        a.h.step({key(Key::w), key(Key::w, false), key(Key::u), key(Key::u, false)});
        check_eq(a.select().phase(), std::string_view("fighters"));
        check_eq(std::string(a.seen("main/select").get("mode")->as_string()), std::string("versus"));
    });

    s.test("choosing: each player's cursor over the roster, both ready, the fight on the first one's home — 坤 on his rooftop against 霓", [] {
        App a;
        check(a.act("main/select", "confirm", player(0)), "versus");
        const auto& c = fighting::Select::choices();
        const auto at = [&](std::string_view n) { return static_cast<std::int32_t>(std::ranges::find(c, n) - c.begin()); };
        check(a.act("main/select", "move", player(0).set("by", at("kun") - a.select().cursor(0))), "player 1 to 坤");
        check(a.act("main/select", "move", player(1).set("by", at("ni") - a.select().cursor(1))), "player 2 to 霓");
        check(a.act("main/select", "confirm", player(0)) && a.select().ready(0) && !a.select().ready(1), "player 1 ready");
        check(a.act("main/select", "back", player(0)) && !a.select().ready(0), "and not again");
        check(a.act("main/select", "confirm", player(0)) && a.act("main/select", "confirm", player(1)), "both ready");
        a.h.advance(seconds(1.2));  // the VS has its moment
        const auto& m = a.stage().match();
        check_eq(m.fighter(0).def.name, std::string("Kun"));
        check_eq(m.fighter(1).def.name, std::string("Ni"));
        check(!a.stage().props.paused, "the fight plays");
        a.h.advance(seconds(1));
        check(m.state().frame > 30, std::format("frames played: {}", m.state().frame));
        const auto dir = scratch_dir("fighting-select");
        check(a.h.save_png(dir / "fight.png").has_value());
    });

    s.test("a gamepad chooses too: south on the title, the d-pad across the roster, south ready; east not ready, and back to the title", [] {
        App a;
        a.h.step({GamepadEvent{.kind = GamepadKind::connected, .device = 3, .name = "pad"}});
        a.h.step({GamepadEvent{.kind = GamepadKind::down, .device = 3, .button = GamepadButton::south}});
        check_eq(a.select().phase(), std::string_view("fighters"));
        const std::int32_t was = a.select().cursor(0);
        a.h.step({GamepadEvent{.kind = GamepadKind::down, .device = 3, .button = GamepadButton::dpad_right}});
        check_eq(a.select().cursor(0), (was + 1) % static_cast<std::int32_t>(fighting::Select::choices().size()));
        a.h.step({GamepadEvent{.kind = GamepadKind::down, .device = 3, .button = GamepadButton::south}});
        check(a.select().ready(0), "ready");
        a.h.step({GamepadEvent{.kind = GamepadKind::down, .device = 3, .button = GamepadButton::east}});
        check(!a.select().ready(0), "not ready");
        a.h.step({GamepadEvent{.kind = GamepadKind::down, .device = 3, .button = GamepadButton::east}});
        check_eq(a.select().phase(), std::string_view("title"));
    });

    s.test("best of three: a round to player 1, a red seal beside her name and the second round three seconds later; the second hers too, the match over and the select screen again", [] {
        App a;
        check(a.act("main/select", "confirm", player(0)) && a.act("main/select", "confirm", player(0)) && a.act("main/select", "confirm", player(1)), "both ready");
        a.h.advance(seconds(3));  // past the round's call and FIGHT!
        check(!a.stage().props.paused, "the fight plays");
        auto round_to_player_1 = [&] {
            auto& match = const_cast<gongfu::Match&>(a.stage().match());
            gongfu::State st = match.state();
            st.timer = 2, st.f[1].health = 5000;
            match.restore(st);
            a.h.advance(seconds(0.3));
        };
        round_to_player_1();
        const Value seen = a.seen("main/stage");
        check(seen.get("round") && seen.get("round")->as_int(0) == 1 && seen.get("wins") && seen.get("wins")->list() && (*seen.get("wins")->list())[0].as_int(0) == 1,
              "round 1 to player 1");
        check(a.h.save_png(scratch_dir("fighting-rounds") / "round-1-won.png").has_value());
        const std::uint64_t won = a.pixels();
        a.h.advance(seconds(3));
        check(a.stage().match().state().over == 0 && a.seen("main/stage").get("round")->as_int(0) == 2, "the second round");
        round_to_player_1();
        check(a.seen("main/stage").get("decided")->as_bool(false), "the match decided");
        a.h.advance(seconds(5));
        check(a.select().phase() == std::string_view("fighters") && a.stage().props.paused, "back to the select screen");
        std::println("    rounds {:016x}", won);
        check_eq(won, std::uint64_t{0x3c2c599876115416});  // GCC and LLVM
    });

    s.test("the title and the select screen, pinned: the logo over the study, the modes; the roster, both fighters' previews, VS", [] {
        App a;
        a.h.advance(seconds(0.5));
        const auto dir = scratch_dir("fighting-menus");
        check(a.h.save_png(dir / "title.png").has_value());
        const std::uint64_t title = a.pixels();
        check(a.act("main/select", "confirm", player(0)));
        a.h.advance(seconds(0.5));
        check(a.h.save_png(dir / "select.png").has_value());
        const std::uint64_t select = a.pixels();
        std::println("    menus title {:016x} select {:016x}", title, select);
        check(title != select, "two screens");
        check_eq(title, std::uint64_t{0x643fb24c0ef4dddf});   // GCC and LLVM
        check_eq(select, std::uint64_t{0x83bc99e206d0d05c});  // GCC and LLVM
    });

    return s.run(argc, argv);
}
