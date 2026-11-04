// A node-addressed device input targets what is actually presented. Semantic
// act remains a separate operation; explicit point input keeps its coordinates.
import std;
import openmui.agent;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

namespace {
struct Moving {
    typeset::FontCollection fonts;
    Runtime runtime;
    Signal<double> offset = signal(10.0);
    int presses = 0;

    explicit Moving(Stages stages = Stages::paint) : runtime(options(fonts, stages)) {
        auto control = box({.id = "control", .width = 60, .height = 30, .background = "#e11d48", .label = "Moving",
            .on = {{"press", [this](const Value&) { ++presses; }}}});
        control.prop("translate_x", Source(derive([x = offset] { return Value(x.get()); })));
        control.prop("motion", Source(Value("morph motion.normal")));
        runtime.mount("main", stack({.width = 260, .height = 160}, {std::move(control)}));
        frame(0); frame(0.1);
        offset.set(170); frame(1); frame(1.25);
        if (stages == Stages::paint) {
            check(runtime.hold_motion(node())); runtime.pause_motion(node()); frame(1.25);
        }
    }
    static RuntimeOptions options(typeset::FontCollection& fonts, Stages stages) {
        RuntimeOptions result; result.fonts = &fonts; result.size = {260, 160}; result.stages = stages;
        result.theme.motion["motion.normal"] = Transition::tween(seconds(1), hanim::ease::linear);
        return result;
    }
    NodeId node() const { return runtime.find("main/control"); }
    void frame(double t, std::vector<InputEvent> events = {}) { runtime.frame({seconds(t), std::move(events)}); }
};
std::vector<Value> deliver(agent::Protocol& protocol, Moving& fixture, std::string_view request, Point expected) {
    std::vector<Value> replies;
    protocol.request(request, [&](std::string_view line) {
        auto parsed = parse_json(line); check(parsed.has_value()); if (parsed) replies.push_back(std::move(*parsed));
    });
    double time = 1.25;
    while (auto input = protocol.take_input()) {
        for (const auto& event : input->events) if (const auto* pointer = std::get_if<PointerEvent>(&event)) {
            check(std::abs(pointer->position.x - expected.x) < 0.01f && std::abs(pointer->position.y - expected.y) < 0.01f,
                  "the queued device events preserve the resolved point");
        }
        fixture.runtime.set_input_actor(input->actor);
        fixture.frame(time += 1.0 / 60, std::move(input->events));
        fixture.runtime.set_input_actor(std::nullopt);
        protocol.poll();
    }
    protocol.poll();
    check(!replies.empty());
    if (!replies.empty()) check(replies.back().get("exitCode") && replies.back().get("exitCode")->as_int() == 0);
    return replies;
}
}

int main(int argc, char** argv) {
    Suite suite{"agent.presentation-input"};
    suite.test("node-addressed real input activates the sampled moving control and preserves actor provenance", [] {
        Moving fixture;
        const auto shown = fixture.runtime.drawn_bounds(fixture.node());
        const auto target = fixture.runtime.screen_bounds(fixture.node());
        check(std::abs(shown.x - target.x) > 80, "the fixture is a genuine middle frame, far from its target");
        check(fixture.runtime.hit_test(shown.center()) == fixture.node());
        check(fixture.runtime.hit_test(target.center()) != fixture.node());
        agent::Protocol protocol(fixture.runtime, {.input = true});
        const auto replies = deliver(protocol, fixture,
            R"({"op":"input","actor":"agent:motion-check","events":[{"type":"click","node":"main/control"}]})", shown.center());
        check(fixture.presses == 1, "the native pointer sequence reaches the visibly presented object");
        const auto journal = fixture.runtime.action_journal();
        check(!journal.empty());
        if (!journal.empty()) check(journal.back().path == "main/control" && journal.back().actor == "agent:motion-check;via=service:input");
        if (!replies.empty() && replies.front().get("data")) {
            const auto* pressed = replies.front().get("data")->get("pressed");
            check(pressed && pressed->list() && pressed->list()->size() == 1);
            if (pressed && pressed->list() && !pressed->list()->empty()) {
                const auto* semantic = pressed->list()->front().get("semantic");
                check(semantic && semantic->as_string() == "main/control");
            }
        }
    });
    suite.test("explicit coordinates at a future location stay literal and do not activate the moving object", [] {
        Moving fixture; const auto target = fixture.runtime.screen_bounds(fixture.node()).center();
        agent::Protocol protocol(fixture.runtime, {.input = true});
        const auto request = std::format(R"({{"op":"input","events":[{{"type":"click","x":{},"y":{}}}]}})", target.x, target.y);
        (void)deliver(protocol, fixture, request, target);
        check(fixture.presses == 0, "a point click is not silently converted to a semantic activation at the target layout");
    });
    suite.test("layout-only hosts resolve nodes without requiring a painted frame", [] {
        Moving fixture(Stages::layout); const auto point = fixture.runtime.screen_bounds(fixture.node()).center();
        agent::Protocol protocol(fixture.runtime, {.input = true});
        (void)deliver(protocol, fixture, R"({"op":"input","events":[{"type":"click","node":"main/control"}]})", point);
        check(fixture.presses == 1);
    });
    return suite.run(argc, argv);
}
