// The Core component contracts (spec §11): present, consistent with the
// registry, and complete enough for agents to operate them.
import std;
import openmui.spec;
import openmui.testing;

using namespace omui::testing;
namespace spec = omui::spec;

int main(int argc, char** argv) {
    Suite s{"spec.catalog"};

    s.test("the Core profile's contracts exist", [] {
        for (auto name : {"text", "image", "button", "toggle", "checkbox", "radio", "slider", "text_box", "select",
                          "menu", "menu_item", "tabs", "tab", "dialog", "tooltip", "scroll_view", "list", "progress"})
            check(spec::catalog::find(name) != nullptr, name);
    });

    s.test("effects are never weaker than the registry's", [] {
        for (const spec::ContractInfo* c : spec::catalog::core)
            for (const auto& a : c->actions)
                if (auto common = spec::parse_action(a.name))
                    check(spec::tier(a.effect) >= spec::tier(spec::actions[static_cast<std::size_t>(*common)].effect),
                          std::format("{}.{}", c->name, a.name));
    });

    s.test("interactive contracts offer actions; keys name actions or commands", [] {
        for (const spec::ContractInfo* c : spec::catalog::core) {
            if (spec::interactive(c->role)) check(!c->actions.empty(), c->name);
            for (const auto& k : c->keys) {
                auto base = k.target.substr(0, k.target.find('('));
                bool known = c->action(base) != nullptr ||
                             std::ranges::any_of(c->commands, [&](const auto& cmd) { return cmd.name == base; });
                check(known, std::format("{}: {}", c->name, k.target));
            }
        }
    });

    s.test("slider: properties, defaults, events, actions", [] {
        const spec::ContractInfo& slider = spec::catalog::slider;
        check(slider.role == spec::Role::slider);
        const spec::PropInfo* value = slider.prop("value");
        check(value && value->type == spec::PropType::f64 && value->bindable);
        check(value->default_value.kind == spec::DefaultValue::Kind::real && value->default_value.d == 0.0);
        check(slider.prop("max")->default_value.d == 100.0);
        check(slider.event("changed")->coalesce_per_frame);
        check(slider.action("set_value")->args.size() == 1);
        check(slider.action("activate") == nullptr);
    });

    s.test("choice properties default to one of their values", [] {
        for (const spec::ContractInfo* c : spec::catalog::core)
            for (const auto& p : c->props)
                if (p.type == spec::PropType::choice)
                    check(std::ranges::find(p.values, p.default_value.s) != p.values.end(),
                          std::format("{}.{}", c->name, p.name));
    });

    s.test("motion parts name registry tokens", [] {
        for (const spec::ContractInfo* c : spec::catalog::core)
            for (const auto& m : c->motion)
                check(m.token == "decay" || spec::find_motion_token(m.token) != nullptr, std::format("{}.{}", c->name, m.part));
    });

    return s.run(argc, argv);
}
