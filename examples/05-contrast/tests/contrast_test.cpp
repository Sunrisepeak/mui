// The contrast checker as its user and an agent use it: the player runs
// logic/main.ts over the application's native module; typing a colour
// updates the ratio, the levels, the simulations and — from a worker — the
// nearest passing colour; a half-typed colour says why and keeps the last
// answer; the pixels are pinned. The native module's own answers are
// checked against WCAG's published values.
import std;
import nlohmann.json;
import openmui;
import openmui.player;
import openmui.entry.player;
import openmui.lang.js;
import openmui.native;
import colors;
import openmui.testing;

using namespace omui::testing;
using namespace omui;
using nlohmann::json;

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

std::int64_t set_value(Headless& h, std::string_view node, std::string_view text) {
    Invocation call;
    call.actor = {"agent", "test", {}};
    call.args = Value{}.set("value", std::string(text));
    return h.tree().act(node, "set_value", call).exit_code().value_or(-1);
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"example.contrast"};
    register_base_components();
    check(colors::contrast::define().has_value(), "the application's native module");

    s.test("the native module: WCAG's ratios, the levels, the simulations, a suggestion", [] {
        auto bw = native::call("contrast", "check", json::array({"#000", "#ffffff"}));
        check(bw && (*bw)["text"] == "21.00:1" && (*bw)["aaa"] == true, bw ? bw->dump() : bw.error().message);
        auto gray = native::call("contrast", "check", json::array({"#777777", "#fff"}));
        check(gray && (*gray)["text"] == "4.47:1" && (*gray)["aa"] == false && (*gray)["aa_large"] == true, gray ? gray->dump() : "");
        auto views = native::call("contrast", "simulate", json::array({"#ff0000", "#00ff00"}));
        check(views && views->size() == 4 && (*views)[3]["kind"] == "achromatopsia", views ? views->dump() : "");
        auto fix = native::call("contrast", "suggest", json::array({"#777777", "#ffffff", 4.5}));
        check(fix && fix->is_string(), fix ? fix->dump() : fix.error().message);
        if (fix && fix->is_string()) {
            auto again = native::call("contrast", "check", json::array({*fix, "#ffffff"}));
            check(again && (*again)["aa"] == true, "the suggestion meets the target it was asked for");
        }
        auto bad = native::call("contrast", "check", json::array({"blue", "#fff"}));
        check(!bad && bad.error().code == "failed" && bad.error().message.find("`blue` is not a colour") != std::string::npos,
              bad ? "" : bad.error().message);
    });

    s.test("the TypeScript interface, run by the player: typing, a worker's suggestion, a half-typed colour, pixels", [] {
        auto m = read_manifest(here() / "mcpp.toml");
        check(m.has_value(), m ? "" : m.error());
        if (!m) return;
        check_eq(m->id, std::string("org.openmui.contrast"));
        Headless h(HeadlessOptions{.size = m->size});
        player::Player p(h.runtime(), *m, {lang::js::typescript()});
        auto started = p.start();
        check(started.has_value(), started ? "" : started.error());
        settle(h, p);
        check(p.problems().empty(), p.problems().empty() ? "" : p.problems()[0]);
        check_eq(name_of(h, "main/ratio"), std::string("4.83:1"));
        check_eq(name_of(h, "main/levels/aa"), std::string("✓ AA"));
        check_eq(name_of(h, "main/levels/aaa"), std::string("✗ AAA"));
        check_eq(name_of(h, "main/suggestion"), std::string("Meets AA as it is"));
        check(name_of(h, "main/views/protanopia/column:0/text:1").ends_with(":1"), name_of(h, "main/views/protanopia/column:0/text:1"));

        check_eq(set_value(h, "main/colours/fg/foreground", "#9ca3af"), std::int64_t{0});
        settle(h, p);
        check_eq(name_of(h, "main/ratio"), std::string("2.53:1"));
        check_eq(name_of(h, "main/levels/aa"), std::string("✗ AA"));
        const std::string suggested = name_of(h, "main/suggestion");
        check(suggested.starts_with("Nearest that meets AA: #"), suggested);

        check_eq(set_value(h, "main/colours/fg/foreground", "#12"), std::int64_t{0});
        settle(h, p);
        check_eq(name_of(h, "main/problem"), std::string("`#12` is not a colour: write #rgb or #rrggbb"));
        check_eq(name_of(h, "main/ratio"), std::string("2.53:1"));

        check_eq(set_value(h, "main/colours/fg/foreground", "#1f2937"), std::int64_t{0});
        settle(h, p);
        check(!h.tree().node("main/problem"), "a colour again: the problem leaves");
        check_eq(name_of(h, "main/ratio"), std::string("14.67:1"));

        h.runtime().focus(NodeId{});
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("example.contrast pixels {}", hash.hex());
        check(h.save_png(scratch_dir("example-contrast") / "contrast.png").has_value());
        check_eq(hash.hex(), std::string("d2247b21c27d38f2"));
    });

    return s.run(argc, argv);
}
