// A person's direct operation wins (spec S-14, Part 2 §4.1 D): while a
// person holds a node with the pointer, or has just typed into it, an
// agent's action on it is refused — `conflict`, with when to try again —
// and changes nothing; it does not wait behind the person. When they let go
// (or a second has passed since they typed), the agent's action runs. Input
// an agent gives as a person's devices would is the agent's own.
import std;
import omui.view;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {

struct Fixture {
    Runtime rt{RuntimeOptions{.size = {240, 200}, .stages = Stages::layout}};
    int     edits = 0;
    Fixture() {
        expect(rt.verbs()
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

UTest cases{"view.person_first", [] {

    "held by the person: an agent's action on it is refused (conflict, retry_after) and changes nothing; on another node it runs"_test = [] {
        Fixture f;
        const Point at = f.centre("main/card");
        (void)f.rt.frame({seconds(0.1), {PointerEvent{.position = at},
                                         PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1}}});
        const Envelope held = f.agent("main/card");
        expect(eq(code_of(held), std::string("conflict")));
        expect(held.exit_code() == spec::exit_code::failure && f.edits == 0) << "nothing changed";
        bool says_when = false;
        for (const spec::Record& r : held.records())
            if (r.kind == spec::RecordKind::error && r.body.get("retry_after")) says_when = r.body.get("retry_after")->as_double() > 0;
        expect(says_when) << "it says when to try again";
        expect(f.agent("main/other").exit_code() == spec::exit_code::success && f.edits == 1) << "a node the person does not hold is the agent's to act on";
        // Let go (their click runs the verb itself): then the agent's action runs too.
        (void)f.rt.frame({seconds(0.2), {PointerEvent{.kind = PointerKind::up, .position = at, .button = MouseButton::left}}});
        const int after_click = f.edits;
        expect(f.agent("main/card").exit_code() == spec::exit_code::success && f.edits == after_click + 1) << "after the person let go";
    };

    "an agent's own pointer is its own: holding with it is no person's operation"_test = [] {
        Fixture f;
        const Point at = f.centre("main/card");
        f.rt.set_input_actor(Actor{"agent", "player", {}});
        (void)f.rt.frame({seconds(0.1), {PointerEvent{.position = at},
                                         PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1}}});
        f.rt.set_input_actor(std::nullopt);
        const Envelope e = f.agent("main/card");
        expect(code_of(e) != "conflict") << "not a person's hold: " + code_of(e);
    };

    "a person's own call (not over the bus) is never held back"_test = [] {
        Fixture f;
        const Point at = f.centre("main/card");
        (void)f.rt.frame({seconds(0.1), {PointerEvent{.position = at},
                                         PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1}}});
        expect(f.rt.act(f.rt.find("main/card"), "activate").exit_code() == spec::exit_code::success) << "the person's own";
    };

    "the person takes over: every change an agent asks for is refused (conflict, no retry_after) until they hand back; reading stays open"_test = [] {
        Fixture f;
        expect(f.agent("main/card").exit_code() == spec::exit_code::success && f.edits == 1) << "before: the agent acts";
        f.rt.take_over();
        expect(f.rt.taken_over()) << "taken over";
        const Envelope held = f.agent("main/other");
        expect(eq(code_of(held), std::string("conflict")));
        bool says_when = false;
        for (const spec::Record& r : held.records())
            if (r.kind == spec::RecordKind::error && r.body.get("retry_after")) says_when = true;
        expect(!says_when && f.edits == 1) << "no time to try again — the person decides — and nothing changed";
        Invocation agent;
        agent.actor = Actor{"agent", "helper", {}};
        agent.remote = true;
        expect(eq(code_of(f.rt.invoke("task.edit", agent)), std::string("conflict")));
        expect(f.rt.verbs().add({.name = "task.count", .effect = Effect::read}, [&](const Value&) -> VerbResult { return Value(static_cast<std::int64_t>(f.edits)); })
                  .has_value());
        expect(f.rt.invoke("task.count", agent).exit_code() == spec::exit_code::success) << "reading stays open";
        // The agent cannot hand itself back (S-10): the person's own verb.
        expect(f.rt.invoke("agents.hand_back", agent).exit_code() != spec::exit_code::success && f.rt.taken_over()) << "the person's alone";
        // The person acts meanwhile; then hands back, and the agent acts again.
        expect(f.rt.act(f.rt.find("main/card"), "activate").exit_code() == spec::exit_code::success && f.edits == 2) << "the person's own";
        expect(f.rt.invoke("agents.hand_back").exit_code() == spec::exit_code::success && !f.rt.taken_over()) << "handed back by the person's verb";
        expect(f.agent("main/other").exit_code() == spec::exit_code::success && f.edits == 3) << "after: the agent acts again";
    };

    "turn about, a dozen times: the person and an agent alternate on one thing, the person taking over between — nothing lost, every step undone a step at a time"_test = [] {
        RuntimeOptions options{.size = {240, 200}, .stages = Stages::layout};
        Runtime rt{options};
        int value = 0;
        expect(rt.verbs()
                  .add({.name = "count.add", .effect = Effect::reversible, .undoable = true},
                       [&](const Value& args) -> VerbResult {
                           const int by = static_cast<int>(args.get("by") ? args.get("by")->as_int() : 1);
                           value += by;
                           rt.taken_back_by([&, by] { value -= by; }, [&, by] { value += by; });
                           return Value(static_cast<std::int64_t>(value));
                       })
                  .has_value());
        rt.mount("main", column({}, {box({.id = "x", .width = 10, .height = 10})}));
        (void)rt.frame({seconds(0), {}});
        Invocation agent;
        agent.actor = Actor{"agent", "helper", {}};
        agent.remote = true;
        int expected = 0, refused = 0;
        for (int turn = 0; turn < 12; ++turn) {
            const bool person = turn % 2 == 0;
            if (person) rt.take_over();
            // While the person holds it, the agent's try is refused and changes nothing.
            if (person && rt.invoke("count.add", agent).exit_code() != spec::exit_code::success) ++refused;
            Value by;
            by.set("by", static_cast<std::int64_t>(turn + 1));
            const Envelope done = person ? rt.invoke("count.add", Invocation{.args = by}) : rt.invoke("count.add", [&] {
                Invocation a = agent;
                a.args = by;
                return a;
            }());
            expect(done.exit_code() == spec::exit_code::success) << std::format("turn {}", turn);
            expected += turn + 1;
            // Nor can it take the person's step back while they hold it.
            if (person) expect(eq(code_of(rt.undo(rt.root(), agent)), std::string("conflict")));
            if (person) rt.hand_back();
        }
        expect(eq(value, expected));
        expect(eq(refused, 6));
        // Every step, the agent's and the person's, undone one at a time back to the start.
        int undone = 0;
        while (rt.undo(rt.root()).exit_code() == spec::exit_code::success && undone < 20) ++undone;
        expect(eq(undone, 12));
        expect(eq(value, 0));
    };
}};
