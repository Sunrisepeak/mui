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
        check_eq(all.hex(), std::string("69f87621ca1ba262"));
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

    s.test("liquid glass that answers: a pressed button's glass swells (thicker, bending more) and eases back; the tab bar's lens slides to the tab chosen, stretching on its way, and rests round", [] {
        App a("glass");
        // The glass layers in the frame (their params and shapes), from the display list.
        const auto glass_layers = [&] {
            std::vector<render::PushLayer> out;
            const auto glass = render::effects().find("liquid_glass");
            for (const auto& c : a.h.runtime().display_list().commands())
                if (const auto* l = std::get_if<render::PushLayer>(&c); l && glass && l->effect.id == glass->id && !l->params.empty()) out.push_back(*l);
            return out;
        };
        // The buttons' glass: one shape each; their thicknesses.
        const auto thicknesses = [&] {
            std::vector<float> t;
            for (const auto& l : glass_layers())
                if (l.shapes.size() == 1) t.push_back(l.params[0]);
            return t;
        };
        const auto count = [](const std::vector<float>& v, auto&& pred) { return std::ranges::count_if(v, pred); };
        const std::string button = "main/sidebar/pack-neon/show-neon";
        a.h.settle();
        const auto rest = thicknesses();
        check(!rest.empty() && count(rest, [](float t) { return std::abs(t - 10) < 0.01f; }) == static_cast<long>(rest.size()), std::format("{} glass buttons at rest", rest.size()));
        const Rect b = a.h.runtime().screen_bounds(a.h.runtime().find(button));
        const Point c{b.x + b.width / 2, b.y + b.height / 2};
        a.h.step({PointerEvent{.kind = PointerKind::down, .position = c, .button = MouseButton::left, .buttons = 1}});
        a.h.advance(seconds(0.08));
        check_eq(count(thicknesses(), [](float t) { return t > 10.5f && t < 17.5f; }), 1L);  // on its way
        a.h.advance(seconds(0.6));
        check_eq(count(thicknesses(), [](float t) { return std::abs(t - 18) < 0.05f; }), 1L);  // swollen, the pressed one alone
        a.h.step({PointerEvent{.kind = PointerKind::up, .position = {c.x + 400, c.y}, .button = MouseButton::left}});  // let go away from it: no click
        a.h.advance(seconds(0.7));
        const auto back = thicknesses();
        check(count(back, [](float t) { return std::abs(t - 10) < 0.05f; }) == static_cast<long>(back.size()), "eased back");
        // The tab bar: two shapes, the bar and its lens under Overview; Settings chosen, the lens slides there, longer on its way than at rest.
        const auto lens = [&]() -> std::optional<Rect> {
            for (const auto& l : glass_layers())
                if (l.shapes.size() == 2) return l.shapes[1].rect;
            return std::nullopt;
        };
        const auto l0 = lens();
        check(l0.has_value(), "a lens in the bar");
        if (!l0) return;
        check(a.h.click("main/preview/nav/sections/settings"), "Settings");
        a.h.advance(seconds(0.05));
        const auto moving = lens();
        a.h.advance(seconds(1));
        const auto l1 = lens();
        check(moving && l1 && l1->x > l0->x + 50, std::format("slid from {} to {}", l0->x, l1 ? l1->x : -1));
        check(moving && l1 && moving->width > std::max(l0->width, l1->width) + 2,
              std::format("stretched on its way: {} (at rest {} and {})", moving ? moving->width : -1, l0->width, l1 ? l1->width : -1));
        check_eq(a.studio.section.peek(), std::string("settings"));
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

    s.test("the interface works under every pack, clicked where it is drawn: a switch, the slider, saving, cancelling", [] {
        for (std::string_view pack : themes::style_names) {
            App a(pack);
            const std::string settings = "main/preview/content/panels/column:0/settings/";
            // A person's click on the switch where the pack draws it (the retro screen bulges: the click reaches what is seen there).
            check(a.h.click(settings + "cellular"), std::format("{}: clicked", pack));
            a.h.settle();
            check(a.studio.cellular.peek(), std::format("{}: switched on by a click", pack));
            check_eq(a.h.tree().act(settings + "volume_row/volume", "set_value", as_agent(Value{}.set("value", Value(0.25)))).exit_code().value_or(-1),
                     std::int64_t{0});
            a.h.settle();
            check(std::abs(a.studio.volume.peek() - 0.25) < 1e-9, std::format("{}: the volume", pack));
            check(a.studio.settings_changed() && a.h.tree().node(settings + "actions/status")->name == "Unsaved changes", "said");
            check(a.h.click(settings + "actions/save"), std::format("{}: save clicked", pack));
            a.h.settle();
            check(a.studio.saved.peek() == 1 && a.h.tree().node(settings + "actions/status")->name == "Saved.",
                  std::format("{}: saved, and said ({} '{}')", pack, a.studio.saved.peek(), a.h.tree().node(settings + "actions/status")->name));
            check(a.h.tree().node(settings + "actions/cancel")->states.has(State::disabled), "nothing to cancel");
            // A change, then Cancel: back to what was saved.
            check_eq(a.h.tree().act(settings + "backup", "toggle", as_agent()).exit_code().value_or(-1), std::int64_t{0});
            a.h.settle();
            check(a.h.click(settings + "actions/cancel"), std::format("{}: cancel clicked", pack));
            a.h.settle();
            check(a.studio.backup.peek() && a.h.tree().node(settings + "actions/status")->name == "Changes discarded.", std::format("{}: cancelled", pack));
            const auto v = a.h.tree().violations();
            for (const auto& x : v) std::println("    {} {} {}", x.clause, x.id, x.message);
            check(v.empty());
        }
    });

    s.test("the workshop's edits: one step a field, undone, redone, reset; a refused value changes nothing and says why", [] {
        App a("glass");
        const auto shipped = theme::write_style(a.studio.draft());
        check(!a.studio.can_undo() && !a.studio.edited());
        check(a.studio.set("corners", Value(4.0)).has_value());
        check(a.studio.set("corners", Value(2.0)).has_value());  // the same field a moment later: the same step
        check(a.studio.set("accent", Value("#ff0066")).has_value());
        check(a.studio.edited());
        // Refused: nothing changes, and why is said (to the person, and to an agent).
        const auto before = theme::write_style(a.studio.draft());
        auto bad = a.studio.set("accent", Value("pink"));
        check(!bad && bad.error().contains("not a colour") && !a.studio.problem.peek().empty(), bad ? "" : bad.error());
        auto out_of_range = a.h.runtime().verbs().invoke("styles.set", as_agent(Value{}.set("what", "role:region/blur").set("value", 1e9)));
        check(out_of_range.exit_code() == std::optional<std::int64_t>(1), out_of_range.ndjson());
        check_eq(theme::write_style(a.studio.draft()), before);
        // Undo: the accent, then the corners (both moves at once); redo; reset to the pack as shipped.
        check(a.studio.undo().has_value());
        check(a.studio.undo().has_value());
        check_eq(theme::write_style(a.studio.draft()), shipped);
        check(!a.studio.undo().has_value(), "nothing more");
        check(a.studio.redo().has_value() && a.studio.edited());
        check(a.studio.reset().has_value() && !a.studio.edited());
        check(a.studio.undo().has_value() && a.studio.edited(), "reset is itself undone");
        check_eq(a.h.runtime().verbs().invoke("styles.reset", as_agent()).exit_code().value_or(-1), std::int64_t{0});
        check(!a.studio.edited());
    });

    s.test("inside another program: the workshop is a theme scope — a pack draws it as in its own window, the page around keeps its theme, a menu it opens is in the pack", [] {
        // The host: a dark page, a strip of its own above the workshop.
        Headless h{HeadlessOptions{.size = {1440, 1000}}};
        h.runtime().set_theme(Theme::dark_theme());
        register_base_components();
        styles::Studio studio;
        studio.scope = "workshop";
        studio.can_export = false;
        studio.runtime = &h.runtime();
        check(studio.show("glass", false).has_value());
        AppSpec host;
        styles::add_verbs(host, studio);
        for (auto& v : host.verbs) (void)h.runtime().verbs().add(v.desc, v.fn);
        check(std::ranges::none_of(host.verbs, [](const auto& v) { return v.desc.name == "styles.export"; }), "no export: nowhere to write");
        h.mount("main", column({}, {box({.id = "strip", .width = 1440, .height = 100, .background = "surface"}),
                                    box({.id = "frame", .width = 1440, .height = 900}, {styles::view(studio)})}));
        h.settle();
        // The workshop's area beside the same pack in a window of its own:
        // every pixel within one level (the glass's frost grain follows the
        // screen, so 100 pixels down it falls a level apart here and there),
        // but for the sidebar's foot, where the window has its Export.
        auto off = [&](std::string_view pack) {
            App a(pack);
            const auto& mine = h.pixels();
            const auto& theirs = a.h.pixels();
            std::size_t apart = 0;
            for (int y = 0; y < 900; ++y)
                for (int x = 0; x < 1440; ++x) {
                    if (x < 220 && y > 800) continue;
                    const auto p = mine.at(x, y + 100), q = theirs.at(x, y);
                    if (std::abs(p.r - q.r) > 1 || std::abs(p.g - q.g) > 1 || std::abs(p.b - q.b) > 1 || std::abs(p.a - q.a) > 1) ++apart;
                }
            return apart;
        };
        const auto strip = h.pixels().at(700, 50);
        check(strip.r == 0x11 && strip.g == 0x18 && strip.b == 0x27, "the page's own surface");
        check_eq(off("glass"), std::size_t{0});
        check(!h.tree().node("main/frame/studio/sidebar/export"), "no Export button");
        // An agent switches packs: the workshop changes, the page does not.
        check(h.runtime().verbs().invoke("styles.show", as_agent(Value{}.set("name", Value("neon")))).exit_code() == std::optional<std::int64_t>(0));
        h.settle();
        check_eq(off("neon"), std::size_t{0});
        const auto after = h.pixels().at(700, 50);
        check(after.r == strip.r && after.g == strip.g && after.b == strip.b, "the page kept its theme");
        // A select's menu: an overlay above everything, drawn in the pack it was opened from.
        std::string select;
        for (const auto& n : h.tree().snapshot({}).nodes)
            if (n.id.ends_with("/region")) select = n.id;
        check(!select.empty() && h.tree().act(select, "expand", as_agent()).exit_code() == std::optional<int>(0), select);
        h.settle();
        NodeId popup{};
        for (const auto& n : h.tree().snapshot({}).nodes)
            if (n.id.starts_with(select + "/") && n.role == Role::list_box) popup = h.runtime().find(n.id);
        check(popup != NodeId{}, "the menu is open");
        const Theme* pack = h.runtime().scope_theme("workshop");
        check(pack && h.runtime().theme_of(popup).color("surface") == pack->color("surface") &&
                  pack->color("surface") != Theme::dark_theme().color("surface"),
              "in the pack's theme, not the page's");
    });

    return s.run(argc, argv);
}
