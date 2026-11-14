// The TypeScript application as its user and an agent use it: run by the
// player from its manifest, typing updates the counts (the script's and the
// runtime's), Clear empties the text, and the pixels are pinned.
import std;
import omui;
import omui.player;
import omui.entry.player;
import omui.lang.js;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path(); }

std::string name_of(Headless& h, std::string_view id) {
    auto n = h.tree().node(id);
    return n ? n->name : std::string("<none>");
}

void settle(Headless& h, player::Player& p) {
    for (int i = 0; i < 4000 && !p.idle(); ++i) {
        h.step();
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    h.settle();
}

}  // namespace

UTest cases{"example.hello-ts", [] {
    register_base_components();

    "the manifest names the script; the player runs it"_test = [] {
        auto m = read_manifest(here() / "mcpp.toml");
        expect(m.has_value()) << (m ? "" : m.error());
        if (!m) return;
        expect(eq(m->id, std::string("org.openmui.text-stats")));
        expect(m->scripts.size() == 1 && m->scripts[0].entry == "logic/main.ts");  // the language by its extension
        expect(m->size.width == 480 && m->size.height == 320) << std::format("{}x{}", m->size.width, m->size.height);

        Headless h(HeadlessOptions{.size = m->size});
        player::Player p(h.runtime(), *m, {lang::js::typescript()});
        auto started = p.start();
        expect(started.has_value()) << (started ? "" : started.error());
        settle(h, p);
        expect(p.problems().empty()) << (p.problems().empty() ? "" : p.problems()[0]);
        expect(eq(name_of(h, "main/stats/characters"), std::string("0 characters")));
        expect(eq(name_of(h, "main/stats/words"), std::string("0 words")));

        h.runtime().focus(h.runtime().find("main/input"));
        h.type("Hello there. How are you? 👋🏽");
        settle(h, p);
        expect(eq(name_of(h, "main/stats/characters"), std::string("27 characters")));  // the emoji is one
        expect(eq(name_of(h, "main/stats/words"), std::string("6 words")));
        expect(eq(name_of(h, "main/stats/sentences"), std::string("2 sentences")));
        expect(eq(name_of(h, "main/stats/reading"), std::string("1 min read")));

        // The caret blinks on the clock, and how many frames the script took
        // varies: the pixels are taken without the focus.
        h.runtime().focus(NodeId{});
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("example.hello-ts pixels {}", hash.hex());
        expect(h.save_png(scratch_dir("example-hello-ts") / "stats.png").has_value());
        expect(eq(hash.hex(), std::string("871664f11753d877")));

        Invocation call;
        call.actor = {"agent", "test", {}};
        expect(eq(h.tree().act("main/clear", "activate", call).exit_code().value_or(-1), 0));
        settle(h, p);
        expect(eq(name_of(h, "main/stats/words"), std::string("0 words")));
        expect(h.tree().node("main/clear")->states.has(spec::State::disabled)) << "nothing to clear";
    };
}};
