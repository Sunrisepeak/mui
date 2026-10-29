// What the game sounds like, heard offline frame by frame as a player would
// hear it: the menus' main theme and cues, the announcer's calls, the
// fighters' voices. Each rendered, saved as a WAV and its bits pinned.
import std;
import openmui;
import openmui.audio;
import openmui.showcase.fighting;
import openmui.testing;
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
    fighting::Select& select_mut() { return *h.runtime().component<fighting::Select>(h.runtime().find("main/select")); }
    fighting::Stage& stage_mut() { return *h.runtime().component<fighting::Stage>(h.runtime().find("main/stage")); }
    const fighting::Select& select() { return select_mut(); }
    const fighting::Stage& stage() { return stage_mut(); }
};

// A stage alone, paused, played a frame at a time; its sound rendered with each frame.
struct Fight {
    Headless           h{HeadlessOptions{.size = {960, 540}}};
    fighting::Stage*   stage = nullptr;
    std::vector<float> all, one;
    Fight(std::string_view arena, std::string_view mode, std::string_view player1, std::string_view player2) {
        AppSpec spec = fighting::app();
        spec.setup(h.runtime());
        h.mount("main", fighting::stage({.id = "stage", .arena = std::string(arena), .mode = std::string(mode), .paused = true, .player1 = std::string(player1),
                                         .player2 = std::string(player2)}));
        h.step();
        stage = h.runtime().component<fighting::Stage>(h.runtime().find("main"));
        one.resize(static_cast<std::size_t>(2 * stage->sounds().frame_samples()));
    }
    bool act(std::string_view action, Value args = {}) {
        Invocation c;
        c.actor = {"agent", "test", {}};
        c.args = std::move(args);
        return h.tree().act("main", action, c).exit_code() == std::optional<int>(0);
    }
    // Frames played and heard; the loudest sample of them.
    float step(int frames) {
        const std::size_t from = all.size();
        for (int i = 0; i < frames; ++i) {
            check(act("step"));
            stage->sounds().mixer().render(one);
            all.insert(all.end(), one.begin(), one.end());
        }
        return audio::peak(std::span(all).subspan(from));
    }
    const gongfu::State& state() const { return stage->match().state(); }
    void restore(const gongfu::State& st) { const_cast<gongfu::Match&>(stage->match()).restore(st); }
};

Value player(int p) { return Value{}.set("player", p); }
Value pad(int player, std::string_view p) { return Value{}.set("player", player).set("pad", p); }

void save(const std::vector<float>& all, std::int32_t rate, std::string_view dir, std::string_view name) {
    const auto w = audio::wav(all, rate);
    std::ofstream(scratch_dir(dir) / name, std::ios::binary).write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size()));
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"showcase.fighting.sound"};

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
        Fight f("plain", "versus", "mo", "ni");  // the plain room has no music: what is heard is the announcer
        audio::Mixer& mixer = f.stage->sounds().mixer();
        const float before = f.step(19), fight = f.step(60);
        check(before == 0 && fight > 0.05f, std::format("silent ({}), then \"fight\" ({})", before, fight));
        gongfu::State st = f.state();
        st.timer = 3, st.f[0].health = 5000;  // the clock nearly out, player 1 hurt: player 2's round
        f.restore(st);
        const std::int64_t begun = mixer.begun();
        f.step(3);
        check(f.state().over != 0 && f.state().winner == 1, "time: player 2's round");
        const float time = f.step(70), name = f.step(72), flawless = f.step(140);
        check(time > 0.05f && name > 0.05f && flawless > 0.05f, std::format("\"time\" {}, \"player 2\" {}, \"flawless victory\" {}", time, name, flawless));
        check_eq(mixer.begun() - begun, std::int64_t{3});
        save(f.all, mixer.rate(), "fighting-announcer", "announcer.wav");
        std::println("    announcer heard {:016x} ({:.1f} s)", audio::hash(f.all), static_cast<double>(f.all.size()) / 2 / mixer.rate());
        check_eq(audio::hash(f.all), std::uint64_t{0x200738cca43d3a91});  // GCC and LLVM, every platform
    });

    s.test("the fighters' voices: 坤 strains with his heavy blow and 墨 cries out under it, not again for a breath; at her K.O. she falls with a cry of her own, not two", [] {
        Fight f("plain", "versus", "kun", "mo");
        audio::Mixer& mixer = f.stage->sounds().mixer();
        const auto& said = f.stage->sounds();
        check(f.act("press", pad(0, "6")));
        for (int i = 0; i < 200 && (f.state().f[1].pos.x - f.state().f[0].pos.x).to_double() > 100; ++i) f.step(1);
        check(f.act("press", pad(0, "5")));
        f.step(8);
        check(f.act("press", pad(0, "5HP")));
        f.step(1);
        check(f.act("press", pad(0, "5")));
        for (int i = 0; i < 30 && f.state().f[1].health == f.stage->match().fighter(1).def.health; ++i) f.step(1);
        check(f.state().f[1].health < f.stage->match().fighter(1).def.health, "the heavy landed");
        f.step(4);
        check_eq(said.spoken(), std::int64_t{2});  // his strain, her cry
        f.step(50);
        // Her last breath of health: his heavy again, a K.O.
        gongfu::State st = f.state();
        st.f[1].health = 100;
        f.restore(st);
        check(f.act("press", pad(0, "6")));
        for (int i = 0; i < 200 && (f.state().f[1].pos.x - f.state().f[0].pos.x).to_double() > 100; ++i) f.step(1);
        check(f.act("press", pad(0, "5")));
        f.step(8);
        check(f.act("press", pad(0, "5HP")));
        f.step(1);
        check(f.act("press", pad(0, "5")));
        for (int i = 0; i < 30 && !f.state().over; ++i) f.step(1);
        check(f.state().over != 0, "the K.O.");
        const float fall = f.step(60);
        check_eq(said.spoken(), std::int64_t{4});  // his strain, her fall
        check(fall > 0.05f, std::format("heard ({})", fall));
        save(f.all, mixer.rate(), "fighting-voices", "voices.wav");
        std::println("    voices heard {:016x} ({:.1f} s)", audio::hash(f.all), static_cast<double>(f.all.size()) / 2 / mixer.rate());
        check_eq(audio::hash(f.all), std::uint64_t{0x0d2f508a19a8c669});  // GCC and LLVM, every platform
    });

    return s.run(argc, argv);
}
