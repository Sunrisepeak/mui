// The application as a player meets it: the title's modes, the select
// screen's cursors (keys, a gamepad, an agent's actions), both players
// ready, the fight on the stage with whom they chose on the first one's
// home; the round over, back to the select screen. The screens' pictures
// are pinned and saved.
import std;
import openmui;
import openmui.showcase.fighting;
import openmui.testing;
import openmui.audio;
import gongfu;

using namespace omui::testing;
using namespace omui;
namespace audio = omui::audio;

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
    fighting::Select& select_mut() { return *h.runtime().component<fighting::Select>(h.runtime().find("main/select")); }
    fighting::Stage& stage_mut() { return *h.runtime().component<fighting::Stage>(h.runtime().find("main/stage")); }
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

    s.test("the menus heard: the main theme calm on the title, the beat on the select screen, a cue for each move and choice; fading under the VS, and the fight's own theme ending with the round — the same bits every run", [] {
        App a;
        audio::Mixer& mixer = a.select_mut().sounds().mixer();
        const std::int64_t per = a.select_mut().sounds().frame_samples();
        std::vector<float> all, one(static_cast<std::size_t>(2 * per));
        // A frame of the menus, then its sound: the mixer's clock keeps the screen's.
        auto play = [&](double s) {
            const std::size_t from = all.size();
            for (int i = 0; i < static_cast<int>(s * 60); ++i) {
                a.h.advance(seconds(1.0 / 60));
                mixer.render(one);
                all.insert(all.end(), one.begin(), one.end());
            }
            double sum = 0;
            for (std::size_t i = from; i < all.size(); ++i) sum += static_cast<double>(all[i]) * all[i];
            return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(all.size() - from, 1)));
        };
        check_eq(a.select_mut().sounds().playing(), std::string_view("menu"));
        play(5);                       // the intro (two bars at 96)
        const double title = play(5);  // the loop, calm
        check(a.act("main/select", "move", player(0).set("by", 1)) && a.act("main/select", "move", player(0).set("by", -1)), "down and up");
        check(a.act("main/select", "confirm", player(0)), "versus");
        play(2.5);                      // the beat comes in at the bar
        const double select = play(5);
        check(a.act("main/select", "move", player(0).set("by", 2)) && a.act("main/select", "confirm", player(0)), "player 1 ready");
        play(0.5);
        check(a.act("main/select", "confirm", player(1)), "both ready");
        play(0.75);                     // the VS: the theme fades under it, and the fight starts
        check(!a.stage().props.paused, "the fight plays");
        check(a.select_mut().sounds().playing().empty(), "the menus' theme finished");
        check_eq(a.stage_mut().sounds().playing(), std::string_view("rooftop"));
        play(1.5);                      // the VS's tail
        const double after = play(2);
        std::println("    menu music rms: {:.4f} title, {:.4f} select, {:.5f} under the fight", title, select, after);
        check(title > 0.01, "the theme is heard on the title");
        check(select > title * 1.15, "more of it on the select screen");
        check(after < 2e-3, "quiet behind the fight");
        // The round over (the clock run out here, as a K.O. would end it): the stage's theme goes to its outro.
        auto& match = const_cast<gongfu::Match&>(a.stage().match());
        gongfu::State st = match.state();
        st.timer = 2;
        match.restore(st);
        a.h.advance(seconds(0.2));
        check(a.stage().match().state().over != 0, "the round is over");
        check(a.stage_mut().sounds().playing().empty(), "the fight's theme finished with the round");
        const auto w = audio::wav(all, mixer.rate());
        std::ofstream(scratch_dir("fighting-menus-heard") / "menus.wav", std::ios::binary).write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size()));
        std::println("    menus heard {:016x} ({:.1f} s)", audio::hash(all), static_cast<double>(all.size()) / 2 / mixer.rate());
        check_eq(audio::hash(all), std::uint64_t{0x626cba52de69bc95});  // GCC and LLVM, every platform
    });

    s.test("the announcer: \"fight\" as a round begins; the clock run out, \"time\", then whose round it was — player 2, never touched: a flawless victory", [] {
        Headless h{HeadlessOptions{.size = {960, 540}}};
        AppSpec spec = fighting::app();
        spec.setup(h.runtime());
        // The plain room has no music: what is heard is the announcer.
        h.mount("main", fighting::stage({.id = "stage", .arena = std::string("plain"), .mode = std::string("versus"), .paused = true, .player1 = std::string("mo"),
                                         .player2 = std::string("ni")}));
        h.step();
        auto& stage = *h.runtime().component<fighting::Stage>(h.runtime().find("main"));  // the mount is the stage
        audio::Mixer& mixer = stage.sounds().mixer();
        std::vector<float> all, one(static_cast<std::size_t>(2 * stage.sounds().frame_samples()));
        auto step = [&](int frames) {
            const std::size_t from = all.size();
            for (int i = 0; i < frames; ++i) {
                Invocation c;
                c.actor = {"agent", "test", {}};
                check(h.tree().act("main", "step", c).exit_code() == std::optional<int>(0));
                mixer.render(one);
                all.insert(all.end(), one.begin(), one.end());
            }
            return audio::peak(std::span(all).subspan(from));
        };
        const float before = step(19), fight = step(60);
        check(before == 0 && fight > 0.05f, std::format("silent ({}), then \"fight\" ({})", before, fight));
        auto& match = const_cast<gongfu::Match&>(stage.match());
        gongfu::State st = match.state();
        st.timer = 3, st.f[0].health = 5000;  // the clock nearly out, player 1 hurt: player 2's round
        match.restore(st);
        const std::int64_t begun = mixer.begun();
        step(3);
        check(stage.match().state().over != 0 && stage.match().state().winner == 1, "time: player 2's round");
        const float time = step(70), name = step(72), flawless = step(140);
        check(time > 0.05f && name > 0.05f && flawless > 0.05f, std::format("\"time\" {}, \"player 2\" {}, \"flawless victory\" {}", time, name, flawless));
        check_eq(mixer.begun() - begun, std::int64_t{3});
        const auto w = audio::wav(all, mixer.rate());
        std::ofstream(scratch_dir("fighting-announcer") / "announcer.wav", std::ios::binary).write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size()));
        std::println("    announcer heard {:016x} ({:.1f} s)", audio::hash(all), static_cast<double>(all.size()) / 2 / mixer.rate());
        check_eq(audio::hash(all), std::uint64_t{0x200738cca43d3a91});  // GCC and LLVM, every platform
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
        check_eq(title, std::uint64_t{0x3ab841bcfbc8c66c});   // GCC and LLVM
        check_eq(select, std::uint64_t{0xf0289556acac5436});  // GCC and LLVM
    });

    return s.run(argc, argv);
}
