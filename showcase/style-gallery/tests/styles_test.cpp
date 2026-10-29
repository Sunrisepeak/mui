// S2 as a designer and an agent use it: five packs over one interface — the
// same semantic tree under each, different pixels (pinned); switching is a
// cross-fade; an edit is seen at once; an export writes a pack that reads
// back as the one edited (and asks its answer of an agent); the interface
// still works, whatever it looks like.
import std;
import openmui;
import openmui.theme;
import openmui.theme.styles;
import openmui.showcase.style_gallery;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

struct App {
    Headless       h;
    styles::Studio studio;
    explicit App(std::string_view first = "glass", float scale = 1) : h{HeadlessOptions{.size = {1440, 900}, .scale = scale}} {
        AppSpec spec = styles::app(studio, first);
        spec.setup(h.runtime());
        for (auto& v : spec.verbs) (void)h.runtime().verbs().add(v.desc, v.fn);
        h.mount("main", spec.view());
        h.settle();
    }
    // The preview's semantic tree: what an agent sees of the interface.
    std::vector<std::string> tree() {
        std::vector<std::string> out;
        for (const auto& n : h.tree().snapshot({}).nodes) {
            if (!n.id.starts_with("main/preview")) continue;
            std::string acts;
            for (const auto& a : n.actions) acts += a.name + ",";
            out.push_back(std::format("{} {} {} [{}] {}", n.id, spec::name(n.role), n.name, acts, n.states.bits));
        }
        return out;
    }
    std::uint64_t pixels() {
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        return std::stoull(hash.hex(), nullptr, 16);
    }
};

Invocation as_agent(Value args = {}, std::string answer = {}) {
    Invocation c;
    c.actor = {"agent", "test", {}};
    c.args = std::move(args);
    c.answer = std::move(answer);
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"showcase.styles"};

    s.test("five packs, one semantic tree; their pixels pinned on every platform", [] {
        App a;
        const auto tree = a.tree();
        check(tree.size() > 20, std::format("the interface: {} nodes", tree.size()));
        Hash all;
        std::set<std::uint64_t> looks;
        for (std::string_view n : themes::style_names) {
            check(a.studio.show(n, false).has_value(), std::string(n));
            a.h.settle();
            check(a.tree() == tree, std::format("{}: the same semantic tree", n));
            const auto px = a.pixels();
            looks.insert(px);
            all.u64(px);
            check(a.h.save_png(scratch_dir(std::format("styles-{}", n)) / std::format("{}.png", n)).has_value());
            std::println("    {:<7} {:016x}", n, px);
        }
        check_eq(looks.size(), std::size_t{5});
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        check_eq(all.hex(), std::string("f8cadeb4977b10a9"));
    });

    s.test("the README's loop: the five packs in turn — an animated PNG under 1 MB", [] {
        App a("glass", 0.4f);  // 576 × 360
        for (std::string_view n : themes::style_names) {  // each pack held a second and a half (a cross-fade changes every pixel: too heavy for a loop)
            check(a.studio.show(n, false).has_value(), std::string(n));
            a.h.settle();
            a.h.record();
        }
        const auto file = scratch_dir("readme-style-gallery") / "five-packs.png";
        check(a.h.save_recording(file, {.delay_num = 3, .delay_den = 2}).has_value());
        const auto size = std::filesystem::file_size(file);
        std::println("    the loop: {} KB", size / 1024);
        check(size < 1024 * 1024, std::format("{} bytes", size));
    });

    s.test("switching is a cross-fade; a person's click and an agent's verb do the same", [] {
        App a;
        check(a.h.click("main/sidebar/pack-neon/show-neon"), "the sidebar's neon");
        a.h.step();
        check_eq(a.studio.current(), std::string("neon"));
        std::size_t fading = 0;
        for (const auto& c : a.h.runtime().display_list().commands())
            if (const auto* l = std::get_if<render::PushLayer>(&c); l && l->effect.id == 0 && l->opacity < 1) ++fading;
        check(fading >= 1, "the frame before, fading over");
        a.h.settle();
        const auto e = a.h.runtime().verbs().invoke("styles.show", as_agent(Value{}.set("name", Value("retro"))));
        check_eq(e.exit_code().value_or(-1), std::int64_t{0});
        check_eq(a.studio.current(), std::string("retro"));
        check(a.h.runtime().verbs().invoke("styles.show", as_agent(Value{}.set("name", Value("nope")))).exit_code() == std::optional<std::int64_t>(1),
              "an unknown pack");
    });

    s.test("an edit is seen at once; the export reads back as the pack edited, and asks an agent its answer", [] {
        App a("neon");
        const auto before = a.pixels();
        check(a.studio.set_color("accent", "#ff0066").has_value());
        check(a.studio.set_param("button", "radius", 22).has_value());
        check(!a.studio.set_param("nothing", "radius", 1).has_value(), "no effect there");
        check(!a.studio.set_color("accent", "pink").has_value(), "not a colour");
        a.h.settle();
        check(a.pixels() != before, "seen at once");
        const Color accent = a.h.runtime().theme().color("accent");
        check(std::abs(accent.r - 1) < 1e-6 && std::abs(accent.b - 0.4f) < 0.01f, "the theme's accent");
        a.studio.export_dir = scratch_dir("styles-export");
        const auto refused = a.h.runtime().verbs().invoke("styles.export", as_agent());
        check(refused.exit_code() == std::optional<std::int64_t>(2), "external: an agent states its answer");
        const auto done = a.h.runtime().verbs().invoke("styles.export", as_agent({}, "yes"));
        check_eq(done.exit_code().value_or(-1), std::int64_t{0});
        const auto file = a.studio.export_dir / "neon.style.omui.toml";
        std::ifstream in(file, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        auto back = theme::read_styles(text, "neon.style.omui.toml");
        check(back.has_value(), back ? text : back.error().front().text());
        if (back) {
            check_eq(theme::write_style(back->front()), theme::write_style(a.studio.draft()));
            const auto& rules = back->front().effects;
            const auto it = std::ranges::find(rules, std::string("button"), &EffectRule::selector);
            check(it != rules.end() && it->effect.get("radius")->as_double() == 22, "the edited radius");
        }
    });

    s.test("the interface works under every pack: a switch, the slider, saving", [] {
        App a("ink");
        check_eq(a.h.tree().act("main/preview/panels/column:0/settings/cellular", "toggle", as_agent()).exit_code().value_or(-1), std::int64_t{0});
        a.h.settle();
        check(a.studio.cellular.peek(), "switched on");
        check_eq(a.h.tree().act("main/preview/panels/column:0/settings/volume", "set_value", as_agent(Value{}.set("value", Value(0.25)))).exit_code().value_or(-1),
                 std::int64_t{0});
        a.h.settle();
        check(std::abs(a.studio.volume.peek() - 0.25) < 1e-9, "the volume");
        const auto save = a.h.tree().node("main/preview/panels/column:0/settings/row:0/save");
        check(save && save->bounds, "the save button");
        check(a.h.click("main/preview/panels/column:0/settings/row:0/save"), "clicked");
        a.h.settle();
        check_eq(a.studio.saved.peek(), std::int64_t{1});
        const auto v = a.h.tree().violations();
        for (const auto& x : v) std::println("    {} {} {}", x.clause, x.id, x.message);
        check(v.empty());
    });

    return s.run(argc, argv);
}
