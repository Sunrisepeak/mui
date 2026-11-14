import std;
import omui.host.web;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {
std::vector<InputEvent> click(Point at) {
    return {PointerEvent{.position = at}, PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1},
        PointerEvent{.kind = PointerKind::up, .position = at, .button = MouseButton::left}};
}
}

UTest cases{"host.web.mixed_input", [] {
    "native human input and endpoint input retain different identities in the same frame"_test = [] {
        Web web({.size = {240, 160}});
        auto access = std::make_shared<AgentAccess>();
        access->set_policy({.mode = AgentMode::development});
        web.runtime().set_agent_access(access);
        int person = 0, edited = 0;
        expect(web.runtime().verbs().add({.name = "person.confirm", .effect = Effect::read, .person = true},
            [&](const Value&) -> VerbResult { ++person; return Value{}; }).has_value());
        expect(web.runtime().verbs().add({.name = "task.edit", .effect = Effect::reversible},
            [&](const Value&) -> VerbResult { ++edited; return Value{}; }).has_value());
        web.mount("main", column({}, {
            box({.id = "person", .width = 100, .height = 30, .label = "Confirm"}).prop("verb", Source{Value("person.confirm"), {}, {}}),
            box({.id = "agent", .width = 100, .height = 30, .label = "Edit"}).prop("verb", Source{Value("task.edit"), {}, {}}),
        }));
        (void)web.step();
        const auto human = click(web.runtime().drawn_bounds(web.runtime().find("main/person")).center());
        web.synthesize(human);
        std::vector<Value> reply;
        web.request(R"({"op":"input","actor":"agent:editor","events":[{"type":"click","node":"main/agent"}]})",
            [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
        (void)web.step();
        expect(person == 1) << "the native person's S-10 action must not inherit the simultaneous agent actor";
        for (int i = 0; i < 8 && reply.empty(); ++i) (void)web.step();
        expect(edited == 1 && !reply.empty() && reply.back().get("exitCode")->as_int() == 0);
        bool human_entry = false, agent_entry = false;
        for (const auto& entry : web.runtime().action_journal()) {
            if (entry.path == "main/person") human_entry = entry.actor == "person:user" && entry.exit_code == 0;
            if (entry.path == "main/agent") agent_entry = entry.actor.starts_with("agent:editor;via=service:development-session-") && entry.exit_code == 0;
        }
        expect(human_entry && agent_entry) << "the same frame journals human presence separately from the trusted agent session";
    };
}};
