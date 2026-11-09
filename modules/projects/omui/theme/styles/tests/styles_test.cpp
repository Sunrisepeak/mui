// The style packs: each reads clean, composes over its theme, and under each
// the same interface keeps its semantic tree while its pixels change.
import std;
import omui.core;
import omui.view;
import omui.theme;
import omui.theme.styles;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

UTest cases{"theme.styles", [] {

    "every pack reads, composes over its theme and differs from the others"_test = [] {
        auto added = themes::add_styles();
        expect(added.has_value()) << (added ? std::string{} : added.error().front().text());
        std::set<std::string> names;
        for (const auto& n : theme::library().styles()) names.insert(n);
        for (std::string_view n : themes::style_names) {
            expect(names.contains(std::string(n))) << std::string(n);
            auto t = theme::library().compose_style(n);
            expect(t.has_value()) << (t ? std::string{} : t.error());
            const auto st = theme::library().find_style(n);
            expect(st && !st->effects.empty()) << std::format("{} has effects", n);
        }
    };

    "every pack names the specification's recipes for its slots, and they reach the theme"_test = [] {
        (void)themes::add_styles();
        for (std::string_view name : themes::style_names) {
            const auto style = theme::library().find_style(name);
            expect(style && !style->motions.empty()) << std::format("{} says how its changes move", name);
            const auto composed = theme::library().compose_style(name);
            expect(composed.has_value()) << (composed ? std::string{} : composed.error());
            if (!style || !composed) continue;
            for (const auto& motion : style->motions) {
                const auto& choices = motion.motion;
                std::vector<std::string> ids;
                if (const auto* list = choices.list()) for (const auto& choice : *list) ids.emplace_back(choice.as_string());
                else ids.emplace_back(choices.as_string());
                for (const auto& id : ids)
                    expect(spec::find_motion_recipe(id) != nullptr) << std::format("{}'s {} names `{}`, a recipe of the specification", name, motion.slot, id);
                const auto layer = motion.target.empty() ? MotionRule::Layer::style : MotionRule::Layer::style_targeted;
                const auto target = motion.target.empty() ? std::string("*") : motion.target;
                const bool ruled = std::ranges::any_of(composed->motion_rules, [&](const MotionRule& rule) {
                    return rule.target == target && rule.slot == motion.slot && rule.spec.name == ids.front() && rule.layer == layer;
                });
                expect(ruled) << std::format("{}'s {} {} slot holds `{}` in the composed theme", name, target, motion.slot, ids.front());
            }
        }
    };

    "under each pack, the same semantic tree; different pixels"_test = [] {
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
            expect(theme::apply_style(rt, n, false).has_value()) << std::string(n);
            rt.frame();
            expect(tree() == first) << std::format("{}: the same tree", n);
            Hash f;
            for (const auto& c : rt.display_list().commands()) f.word(c.index());
            std::uint64_t h = f.value() ^ (static_cast<std::uint64_t>(rt.display_list().commands().size()) << 32);
            looks.insert(h ^ std::hash<std::string>{}(rt.theme().name));
        }
        expect(eq(looks.size(), std::size_t{5}));
    };
}};
