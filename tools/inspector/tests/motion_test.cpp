// The motion workbench (Part 2 H): point at a moment and an object, get the
// step that moves it, tune its numbers — as the person in the GUI and as an
// Agent through the same verbs; a change that does not read is refused and
// changes nothing; every change is a step of history.
import std;
import omui;
import omui.inspector;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;
namespace bench = omui::inspector::motion;

namespace {

std::filesystem::path example(std::string_view name) {
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path() / "modules" /
           "projects" / "hanim" / "studio" / "examples" / name;
}

std::string pixels(Headless& host, std::string_view id) {
    const auto image = host.png(host.runtime().screen_bounds(host.runtime().find(id)));
    return image ? std::format("{:016x}", mlibs::hash::fnv1a(std::as_bytes(std::span(*image)))) : std::string();
}

}  // namespace

int main(int argc, char** argv) {
    Suite suite{"inspector.motion"};

    suite.test("the session: a moment and an object to the step that moves it; its knobs; a change that does not read refused, nothing changed; undo, redo", [] {
        bench::Session session;
        check(session.perform("open", Value{}.set("file", generic_utf8(example("bounce.animation.omui.json")))).has_value(), "opened");
        const double length = session.duration();
        check(length > 3 && length < 8, std::format("{} s", length));
        const auto found = session.perform("at", Value{}.set("time", 0.3).set("object", "ball"));
        check(found.has_value());
        if (!found) return;
        const Value& innermost = found->get("steps")->list()->front();
        check_eq(std::string(innermost.get("where")->as_string()), std::string("/program/items/0"));
        check_eq(std::string(innermost.get("do")->as_string()), std::string("drop"));
        const auto knobs = session.perform("knobs", Value{}.set("where", "/program/items/0"));
        check(knobs.has_value());
        if (!knobs) return;
        std::set<std::string> keys;
        for (const Value& knob : *knobs->get("knobs")->list()) keys.insert(std::string(knob.get("key")->as_string()));
        check((keys == std::set<std::string>{"bounces", "by.0", "by.1", "duration"}), std::format("{}", keys));
        // Not a count: refused with the reader's words, and the document as it was.
        const auto before = session.perform("document");
        const auto wrong = session.perform("set", Value{}.set("where", "/program/items/0/bounces").set("value", "x"));
        check(!wrong.has_value() && wrong.error().contains("number"), wrong ? "taken" : wrong.error());
        check(session.perform("document") == before && session.perform("snapshot")->get("undo")->as_int() == 0, "nothing changed");
        // A longer drop: the animation is longer; undone and redone.
        check(session.perform("set", Value{}.set("where", "/program/items/0/duration").set("value", 2.0)).has_value());
        const double longer = session.duration();
        check(longer > length + 0.5, std::format("{} → {}", length, longer));
        check(session.perform("undo").has_value() && session.duration() == length, "undone");
        check(session.perform("redo").has_value() && session.duration() == longer, "redone");
        check(!session.perform("redo").has_value(), "nothing more to redo");
    });

    suite.test("the workbench: a click on the ball selects its step and shows its knobs; an Agent tunes it through the protocol; the stage redraws", [] {
        const auto session = std::make_shared<bench::Session>();
        Headless host({.size = {960, 900}, .reduce_motion = true, .parallel_raster = false, .complete_frames = true});
        auto access = std::make_shared<AgentAccess>();
        access->set_policy({.mode = AgentMode::development, .source = "test-host"});
        host.runtime().set_agent_access(std::move(access));
        AppSpec spec = bench::app(session, example("bounce.animation.omui.json"));
        spec.setup(host.runtime());
        for (const auto& verb : spec.verbs) check(host.runtime().verbs().add(verb.desc, verb.fn).has_value());
        host.mount("main", spec.view());
        host.step();
        check(session->perform("seek", Value{}.set("time", 0.3)).has_value());
        host.settle();
        // Where the ball is now, on the stage as drawn.
        const auto states = session->perform("at", Value{}.set("object", "ball"));
        check(states.has_value() && !states->get("steps")->list()->empty(), "the ball moves at 0.3 s");
        const Rect stage = host.runtime().screen_bounds(host.runtime().find("main/motion/stage"));
        Point ball{};
        bool found = false;
        for (float y = stage.y + 4; y < stage.bottom() && !found; y += 4)
            for (float x = stage.x + 4; x < stage.right() && !found; x += 4)
                if (session->object_at({x - stage.x, y - stage.y}, {0, 0, stage.width, stage.height}) == "ball") ball = {x, y}, found = true;
        check(found, "the ball is somewhere on the stage");
        host.click(ball);
        host.settle();
        check_eq(session->selected.peek(), std::string("/program/items/0"));
        check(host.tree().node("main/motion/knobs/controls/field-0/knob-0").has_value(), "its knobs shown");
        const std::string drawn = pixels(host, "main/motion/stage");
        // An Agent tunes it as the person would: the same verb, through the protocol.
        agent::Protocol protocol(host.runtime(), {.app = "motion", .default_actor = "agent:tuner"});
        std::int64_t exit = -1;
        protocol.request(Value{}
                             .set("op", "invoke")
                             .set("verb", "motion.set")
                             .set("args", Value{}.set("where", "/program/items/0/by/1").set("value", 120))
                             .json(),
                         [&](std::string_view line) {
                             if (const auto record = parse_json(line); record && record->get("exitCode")) exit = record->get("exitCode")->as_int();
                         });
        check_eq(exit, std::int64_t{0});
        host.settle();
        check(pixels(host, "main/motion/stage") != drawn, "a shorter drop, drawn");
        check(session->perform("snapshot")->get("undo")->as_int() == 1, "one step of history");
        (void)host.save_png(scratch_dir("motion-workbench") / "tuned.png");
    });

    return suite.run(argc, argv);
}
