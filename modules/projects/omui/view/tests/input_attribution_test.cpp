import std;
import openmui.view;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

namespace {
struct Fixture {
    std::shared_ptr<AgentAccess> access = std::make_shared<AgentAccess>();
    Runtime rt{RuntimeOptions{.size = {240, 300}, .stages = Stages::layout, .access = access}};
    std::shared_ptr<const AgentSession> session;
    Signal<int> edits{0};
    int sent = 0, person = 0;
    Fixture() {
        access->set_policy({.mode = AgentMode::development});
        AgentRestrictions limits;
        limits.cap = openas::Cap{openas::Effect::reversible};
        session = access->open_session(limits);
        check(rt.verbs()
                  .add({.name = "task.edit", .effect = Effect::reversible},
                       [&](const Value&) -> VerbResult {
                           edits.set(edits.peek() + 1);
                           return Value{};
                       })
                  .has_value());
        check(rt.verbs()
                  .add({.name = "mail.send", .effect = Effect::external, .answer = "send"},
                       [&](const Value&) -> VerbResult {
                           ++sent;
                           return Value{};
                       })
                  .has_value());
        check(rt.verbs()
                  .add({.name = "person.confirm", .effect = Effect::read, .person = true},
                       [&](const Value&) -> VerbResult {
                           ++person;
                           return Value{};
                       })
                  .has_value());
        check(rt.verbs()
                  .add({.name = "policy.narrow", .effect = Effect::reversible},
                       [&](const Value&) -> VerbResult {
                           access->set_cap(openas::Cap{openas::Effect::read});
                           return Value{};
                       })
                  .has_value());
        check(rt.verbs()
                  .add({.name = "policy.revoke", .effect = Effect::reversible},
                       [&](const Value&) -> VerbResult {
                           check(access->revoke_session(*session));
                           return Value{};
                       })
                  .has_value());
        auto control = [](std::string id, std::string verb) {
            return box({.id = id, .width = 100, .height = 24, .label = id}).prop("verb", Source{Value(verb), {}, {}});
        };
        rt.mount("main", column({}, {
                                        control("agent_edit", "task.edit"),
                                        control("agent_send", "mail.send"),
                                        control("agent_person", "person.confirm"),
                                        control("human_edit", "task.edit"),
                                        control("human_send", "mail.send"),
                                        control("human_person", "person.confirm"),
                                        control("narrow", "policy.narrow"),
                                        control("revoke", "policy.revoke"),
                                        text({.id = "status", .text = derive([e = edits] { return std::format("{} edits", e.get()); })}),
                                    }));
        (void)rt.frame({seconds(0), {}});
    }
    void click(std::vector<InputEvent>& events, std::string_view id) {
        const auto node = rt.find(std::format("main/{}", id));
        check(rt.valid(node));
        const auto at = rt.drawn_bounds(node).center();
        events.emplace_back(PointerEvent{.position = at});
        events.emplace_back(PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1});
        events.emplace_back(PointerEvent{.kind = PointerKind::up, .position = at, .button = MouseButton::left});
    }
    void actor_entries(std::string_view node, bool remote, int exit) {
        int found = 0;
        for (const auto& entry : rt.action_journal()) {
            if (entry.path != std::format("main/{}", node)) continue;
            ++found;
            check(entry.exit_code == exit, entry.path);
            check(remote ? entry.actor.starts_with("agent:editor;via=service:development-session-") : entry.actor == "person:user",
                  entry.actor);
        }
        check(found == 1, std::format("one actual action at {}", node));
    }
};
} // namespace

int main(int argc, char** argv) {
    Suite s{"view.input_attribution"};
    for (const bool remote_first : {false, true}) {
        s.test(remote_first ? "agent then human events keep separate presence cap state and journal in one frame"
                            : "human then agent events keep separate presence cap state and journal in one frame",
               [remote_first] {
                   Fixture f;
                   std::vector<InputEvent> events;
                   const auto remote = [&] {
                       f.click(events, "agent_edit");
                       f.click(events, "agent_send");
                       f.click(events, "agent_person");
                   };
                   const auto human = [&] {
                       f.click(events, "human_edit");
                       f.click(events, "human_send");
                       f.click(events, "human_person");
                   };
                   if (remote_first) {
                       remote();
                       human();
                   } else {
                       human();
                       remote();
                   }
                   const std::array<InputAttribution, 1> ranges{
                       {{remote_first ? 0u : 9u, remote_first ? 9u : 18u, {"agent", "editor", {}}, f.session}}};
                   FrameInput input{.time = seconds(0.1), .events = std::move(events), .attribution = ranges};
                   const auto output = f.rt.frame(input);
                   check(output.failure.empty(), output.failure);
                   check(f.edits.peek() == 2 && f.sent == 1 && f.person == 1);
                   check(f.rt.semantic_node(f.rt.find("main/status")).name == "2 edits");
                   f.actor_entries("agent_edit", true, 0);
                   f.actor_entries("agent_send", true, 1);
                   f.actor_entries("agent_person", true, 1);
                   f.actor_entries("human_edit", false, 0);
                   f.actor_entries("human_send", false, 0);
                   f.actor_entries("human_person", false, 0);
                   check(f.rt.invoke("person.confirm").exit_code() == 0 && f.person == 2, "the event context is restored after the frame");
               });
    }

    for (const bool revoke : {false, true}) {
        s.test(revoke ? "a human event revokes the session before later remote events in the same frame"
                      : "a human event narrows the cap before later remote events in the same frame",
               [revoke] {
                   Fixture f;
                   std::vector<InputEvent> events;
                   f.click(events, revoke ? "revoke" : "narrow");
                   f.click(events, "agent_edit");
                   f.click(events, "human_person");
                   const std::array<InputAttribution, 1> ranges{{{3, 6, {"agent", "editor", {}}, f.session}}};
                   const auto output = f.rt.frame({.time = seconds(0.1), .events = std::move(events), .attribution = ranges});
                   check(output.failure.empty());
                   check(f.edits.peek() == 0 && f.person == 1 && f.sent == 0);
                   int refused = 0;
                   for (const auto& entry : f.rt.action_journal()) {
                       if (entry.path != "app" || entry.action != "input") continue;
                       ++refused;
                       check(entry.exit_code == 1);
                       check(entry.actor.starts_with("agent:editor"), entry.actor);
                   }
                   check(refused == 3, "each refused raw event is observable, and no target component received it");
                   f.actor_entries("human_person", false, 0);
               });
    }

    s.test("multiple disjoint remote ranges leave the human gap and timers local", [] {
        Fixture f;
        std::vector<InputEvent> events;
        f.click(events, "agent_edit");
        f.click(events, "human_person");
        f.click(events, "agent_person");
        const std::array<InputAttribution, 2> ranges{
            {{0, 3, {"agent", "editor", {}}, f.session}, {6, 9, {"agent", "editor", {}}, f.session}}};
        (void)f.rt.after(seconds(0), [&] { check(f.rt.invoke("person.confirm").exit_code() == 0); });
        const auto output = f.rt.frame({.time = seconds(0.1), .events = std::move(events), .attribution = ranges});
        check(output.failure.empty() && f.edits.peek() == 1 && f.person == 2);
        f.actor_entries("human_person", false, 0);
        f.actor_entries("agent_person", true, 1);
    });

    s.test("invalid borrowed intervals reject the whole frame before clocks timers or handlers", [] {
        Fixture f;
        std::vector<InputEvent> events;
        f.click(events, "human_edit");
        int timer = 0;
        (void)f.rt.after(seconds(0), [&] { ++timer; });
        const std::array<std::array<InputAttribution, 2>, 4> invalid{{
            {{{0, 0, {}, {}}, {1, 2, {}, {}}}},
            {{{0, 4, {}, {}}, {4, 5, {}, {}}}},
            {{{0, 2, {}, {}}, {1, 3, {}, {}}}},
            {{{2, 3, {}, {}}, {0, 1, {}, {}}}},
        }};
        for (const auto& ranges : invalid) {
            FrameInput input{.time = seconds(10), .events = events, .attribution = ranges};
            check(!validate_input_attribution(input));
            const auto output = f.rt.frame(input);
            check(!output.failure.empty());
            check(f.edits.peek() == 0 && timer == 0 && f.rt.now() == seconds(0));
        }
        check(validate_input_attribution({.events = events}).has_value());
        check(f.rt.frame({.time = seconds(0.1), .events = events}).failure.empty());
        check(f.edits.peek() == 1 && timer == 1);
    });
    return s.run(argc, argv);
}
