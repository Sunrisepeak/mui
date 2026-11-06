// The style packs: each reads clean, composes over its theme, and under each
// the same interface keeps its semantic tree while its pixels change.
import std;
import openmui.core;
import openmui.view;
import openmui.theme;
import openmui.theme.styles;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

int main(int argc, char** argv) {
    Suite s{"theme.styles"};

    s.test("every pack reads, composes over its theme and differs from the others", [] {
        auto added = themes::add_styles();
        check(added.has_value(), added ? std::string{} : added.error().front().text());
        std::set<std::string> names;
        for (const auto& n : theme::library().styles()) names.insert(n);
        for (std::string_view n : themes::style_names) {
            check(names.contains(std::string(n)), std::string(n));
            auto t = theme::library().compose_style(n);
            check(t.has_value(), t ? std::string{} : t.error());
            const auto st = theme::library().find_style(n);
            check(st && !st->effects.empty(), std::format("{} has effects", n));
        }
    });

    s.test("under each pack, the same semantic tree; different pixels", [] {
        (void)themes::add_styles();
        Runtime rt({.size = {320, 200}});
        rt.mount("main", column({.id = "ui", .padding = 16, .gap = 10},
                                {column({.id = "card", .padding = 12, .background = "surface", .border_color = "border", .border_width = 1, .radius = 12},
                                        {text({.text = "Settings", .style = "heading"}), box({.id = "go", .width = 90, .height = 28, .background = "accent", .radius = 8})})
                                     .prop("role", Source(Value("region")))}));
        rt.frame();
        auto tree = [&] {
            std::vector<std::string> out;
            std::function<void(NodeId)> visit = [&](NodeId id) {
                const auto n = rt.semantic_node(id);
                out.push_back(std::format("{} {} {}", n.id, spec::name(n.role), n.name));
                for (NodeId c : rt.children(id)) visit(c);
            };
            visit(rt.find("main"));
            return out;
        };
        const auto first = tree();
        std::set<std::uint64_t> looks;
        for (std::string_view n : themes::style_names) {
            check(theme::apply_style(rt, n, false).has_value(), std::string(n));
            rt.frame();
            check(tree() == first, std::format("{}: the same tree", n));
            Hash f;
            for (const auto& c : rt.display_list().commands()) f.word(c.index());
            std::uint64_t h = f.value() ^ (static_cast<std::uint64_t>(rt.display_list().commands().size()) << 32);
            looks.insert(h ^ std::hash<std::string>{}(rt.theme().name));
        }
        check_eq(looks.size(), std::size_t{5});
    });

    return s.run(argc, argv);
}
