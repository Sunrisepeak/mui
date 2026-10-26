// The generated registry tables: names round-trip, the clause helpers hold,
// and the tables are usable in constant expressions.
import std;
import openmui.spec;
import openmui.testing;

using namespace omui::testing;
namespace spec = omui::spec;

static_assert(spec::parse_role("button") == spec::Role::button);
static_assert(spec::name(spec::Role::switch_) == "switch");
static_assert(spec::requires_answer(spec::Effect::irreversible));
static_assert(!spec::requires_answer(spec::Effect::read));
static_assert(spec::interactive(spec::Role::slider) && !spec::interactive(spec::Role::text));
static_assert(spec::catalog::find("slider") != nullptr);

int main(int argc, char** argv) {
    Suite s{"spec.registry"};

    s.test("every enumeration round-trips through its name", [] {
        for (std::size_t i = 0; i < spec::roles.size(); ++i)
            check(spec::parse_role(spec::roles[i].name) == static_cast<spec::Role>(i), spec::roles[i].name);
        for (std::size_t i = 0; i < spec::states.size(); ++i)
            check(spec::parse_state(spec::states[i].name) == static_cast<spec::State>(i), spec::states[i].name);
        for (std::size_t i = 0; i < spec::effects.size(); ++i)
            check(spec::parse_effect(spec::effects[i].name) == static_cast<spec::Effect>(i));
        for (std::size_t i = 0; i < spec::actions.size(); ++i)
            check(spec::parse_action(spec::actions[i].name) == static_cast<spec::Action>(i));
        check(!spec::parse_role("Button").has_value(), "names are case-sensitive");
        check(spec::states.size() <= 32, "states fit a 32-bit set");
    });

    s.test("effects: tiers order the classes, unknown is strongest", [] {
        check(spec::tier(spec::Effect::read) < spec::tier(spec::Effect::reversible));
        check(spec::tier(spec::Effect::reversible) < spec::tier(spec::Effect::irreversible));
        check(spec::tier(static_cast<spec::Effect>(200)) > spec::tier(spec::Effect::external));
        check(spec::requires_answer(static_cast<spec::Effect>(200)), "an unknown effect needs an answer");
    });

    s.test("actions carry their arguments", [] {
        const auto& set_value = spec::actions[static_cast<std::size_t>(spec::Action::set_value)];
        check(set_value.args.size() == 1 && set_value.args[0].name == "value");
        check(spec::actions[static_cast<std::size_t>(spec::Action::activate)].effect == spec::Effect::reversible);
    });

    s.test("extensions and their requirements exist", [] {
        for (const auto& e : spec::extensions) {
            check(e.name.starts_with("OMUI_EXT_"), e.name);
            for (auto r : e.requires_) check(spec::find_extension(r) != nullptr, r);
        }
    });

    s.test("motion tokens are timed curves or springs", [] {
        for (const auto& m : spec::motion_tokens) check(m.duration_ms >= 0 || m.stiffness > 0, m.name);
        check(spec::find_motion_token("motion.fast")->duration_ms == 120);
    });

    s.test("the full profile requires the three script languages", [] {
        const auto& full = spec::profiles[0];
        check_eq(full.name, std::string_view("full"));
        check(full.scripts.size() == 3);
    });

    s.test("expression functions have combinators where code builds them", [] {
        check_eq(spec::find_function("format")->combinators.cpp, std::string_view("fmt"));
        check_eq(spec::find_function("format")->combinators.python, std::string_view("fmt"));
        check(spec::find_function("nope") == nullptr);
    });

    return s.run(argc, argv);
}
