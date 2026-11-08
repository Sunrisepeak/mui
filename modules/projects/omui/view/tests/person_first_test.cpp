// A person's direct operation wins (spec S-14, Part 2 §4.1 D): while a
// person holds a node with the pointer, or has just typed into it, an
// agent's action on it is refused — `conflict`, with when to try again —
// and changes nothing; it does not wait behind the person. When they let go
// (or a second has passed since they typed), the agent's action runs. Input
// an agent gives as a person's devices would is the agent's own.
import std;
import omui.view;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

namespace {

struct Fixture {
    Runtime rt{RuntimeOptions{.size = {240, 200}, .stages = Stages::layout}};
    int     edits = 0;
    Fixture() {
        check(rt.verbs()
                  .add({.name = "task.edit", .effect = Effect::reversible},
                       [&](const Value&) -> VerbResult {
                           ++edits;
                           return Value{};
                       })
                  .has_value());
        rt.mount("main", column({}, {box({.id = "card", .width = 120, .height = 40, .label = "Card"}).prop("verb", Source{Value("task.edit"), {}, {}}),
                                     box({.id = "other", .width = 120, .height = 40, .label = "Other"}).prop("verb", Source{Value("task.edit"), {}, {}})}));
        (void)rt.frame({seconds(0), {}});
    }
    Point centre(std::string_view id) { return rt.screen_bounds(rt.find(id)).center(); }
    Envelope agent(std::string_view id) {
        Invocation call;
        call.actor = Actor{"agent", "helper", {}};
        call.remote = true;
        return rt.act(rt.find(id), "activate", call);
    }
};

std::string code_of(const Envelope& e) {
    for (const spec::Record& r : e.records())
        if (r.kind == spec::RecordKind::error && r.body.get("code")) return std::string(r.body.get("code")->as_string());
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"view.person_first"};

    s.test("held by the person: an agent's action on it is refused (conflict, retry_after) and changes nothing; on another node it runs", [] {
        Fixture f;
        const Point at = f.centre("main/card");
        (void)f.rt.frame({seconds(0.1), {PointerEvent{.position = at},
                                         PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1}}});
        const Envelope held = f.agent("main/card");
        check_eq(code_of(held), std::string("conflict"));
        check(held.exit_code() == spec::exit_code::failure && f.edits == 0, "nothing changed");
        bool says_when = false;
        for (const spec::Record& r : held.records())
            if (r.kind == spec::RecordKind::error && r.body.get("retry_after")) says_when = r.body.get("retry_after")->as_double() > 0;
        check(says_when, "it says when to try again");
        check(f.agent("main/other").exit_code() == spec::exit_code::success && f.edits == 1, "a node the person does not hold is the agent's to act on");
        // Let go (their click runs the verb itself): then the agent's action runs too.
        (void)f.rt.frame({seconds(0.2), {PointerEvent{.kind = PointerKind::up, .position = at, .button = MouseButton::left}}});
        const int after_click = f.edits;
        check(f.agent("main/card").exit_code() == spec::exit_code::success && f.edits == after_click + 1, "after the person let go");
    });

    s.test("an agent's own pointer is its own: holding with it is no person's operation", [] {
        Fixture f;
        const Point at = f.centre("main/card");
        f.rt.set_input_actor(Actor{"agent", "player", {}});
        (void)f.rt.frame({seconds(0.1), {PointerEvent{.position = at},
                                         PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1}}});
        f.rt.set_input_actor(std::nullopt);
        const Envelope e = f.agent("main/card");
        check(code_of(e) != "conflict", "not a person's hold: " + code_of(e));
    });

    s.test("a person's own call (not over the bus) is never held back", [] {
        Fixture f;
        const Point at = f.centre("main/card");
        (void)f.rt.frame({seconds(0.1), {PointerEvent{.position = at},
                                         PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1}}});
        check(f.rt.act(f.rt.find("main/card"), "activate").exit_code() == spec::exit_code::success, "the person's own");
    });

    return s.run(argc, argv);
}
