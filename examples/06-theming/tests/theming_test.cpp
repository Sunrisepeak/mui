// The theme preview as its user and an agent use it: the player runs the TypeScript
// script over openmui's themes and the application's brand file; choosing a
// theme, compact and reduced motion composes them; the gallery's semantics
// are the same under every composition, its metrics follow the density; a
// saved edit of the brand file shows at once, a broken one says where and
// changes nothing; the pixels of three compositions are pinned.
import std;
import omui;
import omui.player;
import omui.entry.player;
import omui.lang.js;
import omui.theme;
import omui.theme.default_theme;
import omui.theme.compact;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path(); }

void settle(Headless& h, player::Player& p) {
    for (int i = 0; i < 4000 && !p.idle(); ++i) {
        h.step();
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    h.settle();
}

std::int64_t act(Headless& h, std::string_view node, std::string_view action, Value args = {}) {
    Invocation call;
    call.actor = {"agent", "test", {}};
    call.args = std::move(args);
    return h.tree().act(node, action, call).exit_code().value_or(-1);
}

// The gallery's semantics: everything but where things are.
std::string semantics(Headless& h) {
    std::string out;
    for (const auto& n : h.tree().snapshot({.root = "main/gallery"}).nodes)
        out += std::format("{}|{}|{}|{}\n", n.id, spec::name(n.role), n.name, n.states.bits);
    return out;
}

std::string pixels(Headless& h) {
    h.runtime().focus(NodeId{});
    h.settle();
    Hash hash;
    const auto& px = h.pixels();
    for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
    return hash.hex();
}

}  // namespace

UTest cases{"example.theming", [] {
    register_base_components();
    expect(themes::add_default().has_value() && themes::add_compact().has_value()) << "openmui's themes";

    "compositions from the script: light, dark + compact + reduced motion, the brand; semantics kept, density applied"_test = [] {
        auto m = read_manifest(here() / "mcpp.toml");
        expect(m.has_value()) << (m ? "" : m.error());
        if (!m) return;
        expect(m->themes.size() == 1 && m->theme == std::vector<std::string>{"light"}) << "the manifest's themes and composition";
        const auto shots = scratch_dir("example-theming");
        Headless h(HeadlessOptions{.size = m->size});
        player::Player p(h.runtime(), *m, {lang::js::typescript()});
        auto started = p.start();
        expect(started.has_value()) << (started ? "" : started.error());
        settle(h, p);
        expect(p.problems().empty()) << (p.problems().empty() ? "" : p.problems()[0]);
        expect(eq(h.tree().node("main/current")->name, std::string("Composed: light")));
        const std::string light_semantics = semantics(h);
        const float comfortable = h.runtime().bounds(h.runtime().find("main/gallery/buttons/primary")).height;
        expect(eq(comfortable, 36.0f));
        const std::string light_pixels = pixels(h);
        std::println("example.theming light {}", light_pixels);
        expect(h.save_png(shots / "light.png").has_value());

        expect(eq(act(h, "main/controls/scheme", "set_value", Value{}.set("value", "dark")), std::int64_t{0}));
        settle(h, p);
        expect(eq(act(h, "main/controls/compact", "toggle"), std::int64_t{0}));
        settle(h, p);
        expect(eq(act(h, "main/controls/calm", "toggle"), std::int64_t{0}));
        settle(h, p);
        expect(eq(h.tree().node("main/current")->name, std::string("Composed: dark+compact+reduced_motion")));
        expect(h.runtime().theme().dark && h.runtime().theme().density == 0.8f);
        expect(eq(semantics(h), light_semantics));
        expect(eq(h.runtime().bounds(h.runtime().find("main/gallery/buttons/primary")).height, 29.0f));
        const std::string dense_pixels = pixels(h);
        std::println("example.theming dark+compact+reduced_motion {}", dense_pixels);
        expect(h.save_png(shots / "dark-compact.png").has_value());

        expect(eq(act(h, "main/controls/compact", "toggle"), std::int64_t{0}));
        settle(h, p);
        expect(eq(act(h, "main/controls/calm", "toggle"), std::int64_t{0}));
        settle(h, p);
        expect(eq(act(h, "main/controls/scheme", "set_value", Value{}.set("value", "brand")), std::int64_t{0}));
        settle(h, p);
        expect(eq(h.tree().node("main/current")->name, std::string("Composed: brand")));
        expect(h.runtime().theme().color("accent").g == Color::hex(0x0f766e).g) << "the brand's accent";
        expect(eq(semantics(h), light_semantics));
        const std::string brand_pixels = pixels(h);
        std::println("example.theming brand {}", brand_pixels);
        expect(h.save_png(shots / "brand.png").has_value());
        expect(light_pixels != dense_pixels && light_pixels != brand_pixels);
        expect(eq(light_pixels + " " + dense_pixels + " " + brand_pixels, std::string("b71fdb4f2d783359 8799418b5f024785 d5848d7daebb5f06")));
    };

    "a saved edit of the application's theme file shows at once; a broken one says where and changes nothing"_test = [] {
        // A copy of the application, to edit.
        const auto app = scratch_dir("example-theming-edit");
        for (const char* d : {"themes", "logic"}) std::filesystem::copy(here() / d, app / d, std::filesystem::copy_options::recursive);
        std::filesystem::copy_file(here() / "mcpp.toml", app / "mcpp.toml");
        auto m = read_manifest(app / "mcpp.toml");
        expect(m.has_value()) << (m ? "" : m.error());
        if (!m) return;
        m->theme = {"brand"};
        Headless h(HeadlessOptions{.size = m->size});
        player::Player p(h.runtime(), *m, {lang::js::typescript()});
        expect(p.start().has_value());
        settle(h, p);
        expect(h.runtime().theme().color("accent").r == Color::hex(0x0f766e).r);
        const auto brand = app / "themes" / "brand.theme.omui.toml";
        // Each save a later time than the last, whatever the clock's grain
        // (Windows': about 15 ms, so two quick saves would read the same).
        const auto start = std::filesystem::last_write_time(brand);
        int saves = 0;
        auto save = [&](std::string_view from, std::string_view to) {
            std::ifstream in(brand, std::ios::binary);
            std::string text{std::istreambuf_iterator<char>(in), {}};
            in.close();
            text.replace(text.find(from), from.size(), to);
            std::ofstream(brand, std::ios::binary) << text;
            std::filesystem::last_write_time(brand, start + std::chrono::seconds(2 * ++saves));
        };
        save("accent         = \"#0f766e\"", "accent         = \"#be123c\"");
        const auto reloaded = p.poll();
        settle(h, p);
        expect(reloaded.size() == 1) << std::format("{} reloaded", reloaded.size());
        expect(h.runtime().theme().color("accent").r == Color::hex(0xbe123c).r) << "the edit shows";
        expect(eq(h.runtime().theme().name, std::string("brand")));
        save("accent         = \"#be123c\"", "accent         = \"crimson\"");
        (void)p.poll();
        settle(h, p);
        expect(!p.problems().empty() && p.problems().back().find("brand.theme.omui.toml:10:18:") != std::string::npos &&
                  p.problems().back().find("colour `accent` must be a colour") != std::string::npos) << (p.problems().empty() ? "" : p.problems().back());
        expect(h.runtime().theme().color("accent").r == Color::hex(0xbe123c).r) << "a broken edit changes nothing";
    };
}};
