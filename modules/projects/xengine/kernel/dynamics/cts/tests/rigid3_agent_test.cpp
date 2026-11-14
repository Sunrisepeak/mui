// A 3D rigid world as a subject of the agent kernel: the agent contract's suite on it — what it says it cannot do
// yet reported so, never passed — and the world read, rayed, pushed, stepped, sought and branched through the
// same session an agent has.
import std;
import mlibs.agent.cts;
import mlibs.json;
import xengine.dynamics.rigid3;
import xengine.dynamics.rigid3.agent;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace rigid3 = xengine::dynamics::rigid3;

namespace {

// A floor, three boxes stacked and a ball rolling slowly toward them, 120 steps on: the same each time.
std::unique_ptr<rigid3::agent::Scene> settled() {
    auto world = rigid3::World::make();
    expect(fatal(world.has_value()));
    (void)world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
    for (int level = 0; level < 3; ++level) (void)world->add({.at = {0, 0.5f + static_cast<float>(level) * 1.01f, 0}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}), {.density = 500});
    (void)world->add({.at = {-8, 0.5f, 0}, .velocity = {1, 0, 0}, .spin = {0, 0, -2}}, rigid3::Shape::sphere(0.5f), {.density = 2000});
    for (int step = 0; step < 120; ++step) world->step();
    return std::make_unique<rigid3::agent::Scene>(std::move(*world));
}

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};
Answered ask(agent::SubjectSession& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        expect(fatal(parsed.has_value())) << record;
        const auto kind = parsed->root()["kind"].text();
        if (kind == "error") out.error = parsed->root()["code"].text().value_or("");
        if (kind == "data" && !out.data) out.data.emplace(std::move(*parsed));
    });
    return out;
}

}  // namespace

UTest cases{"dynamics.rigid3.agent", [] {
    // Body ids: the floor 1, the boxes 2 to 4 (bottom to top), the ball 5 — the world's, in the order made.
    "the agent contract on a 3D rigid world: what it has passes, what it says it has not is unsupported"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> { return settled(); },
            .read = R"({"op":"rigid3.observe"})",
            .change = R"({"op":"rigid3.step","steps":4})",
            .undo = R"({"op":"rigid3.seek","frame":120})",
            .preview = R"({"op":"rigid3.branch","steps":30,"body":5,"impulse":[8000,0,0]})",
            .script = {R"({"op":"rigid3.push","body":5,"impulse":[8000,0,0]})", R"({"op":"rigid3.step","steps":30})", R"({"op":"rigid3.observe"})",
                       R"({"op":"rigid3.push","body":4,"impulse":[0,0,300],"at":[0,3,0]})", R"({"op":"rigid3.step","steps":60})", R"({"op":"rigid3.bodies"})"},
            .budgeted = R"({"op":"rigid3.observe","budget":200})",
            .budget = 200,
        };
        contract::declare(probe, {.subject = "xengine.dynamics.rigid3.agent"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.source.chain", "agent.actor.recorded", "agent.causes.explained"})
            << "causes and the source of what it puts out: not yet, and said so";
    };

    "the world is read as it is; a ray finds the top box; a push sends the ball into the stack, and the contact is the step's output"_test = [] {
        auto scene = settled();
        agent::SubjectSession session({scene.get()});
        const auto seen = ask(session, R"({"op":"rigid3.observe"})");
        expect(seen["frame"].integer() == 120 && seen["bodies"].integer() == 5);
        const auto listed = ask(session, R"({"op":"rigid3.bodies"})");
        expect(listed["bodies"].size() == 5u && listed["bodies"][0]["mass"].number() == 0.0) << "the floor is fixed";
        const auto down = ask(session, R"({"op":"rigid3.ray","from":[0,20,0],"to":[0,-20,0]})");
        expect(down["hit"].boolean() == true && down["body"].integer() == 4) << "straight down: the top box";
        expect(std::abs(down["at"][1].number().value_or(0) - 3.0) < 0.02) << "at its top face, the three settled on each other";
        expect(ask(session, R"({"op":"rigid3.ray","from":[9,5,9],"to":[9,8,9]})")["hit"].boolean() == false);

        const auto pushed = ask(session, R"({"op":"rigid3.push","body":5,"impulse":[8000,0,0]})");
        expect(pushed["effective"].integer() == 121) << "the next step gives it";
        bool struck = false;
        for (int step = 0; step < 60 && !struck; ++step) {
            (void)ask(session, R"({"op":"rigid3.step"})");
            const auto after = ask(session, R"({"op":"rigid3.observe"})");
            for (std::size_t index = 0; index < after["contacts"].size(); ++index) {
                const auto contact = after["contacts"][index];
                struck = struck || (contact["began"].boolean() == true && contact["one"].integer() == 2 && contact["other"].integer() == 5);
            }
        }
        expect(struck) << "the ball met the bottom box, and the step said so";
        expect(ask(session, R"({"op":"rigid3.push","body":99,"impulse":[1,0,0]})").error == "invalid-argument") << "no such body";
        expect(ask(session, R"({"op":"rigid3.push","body":5,"impulse":[1,0]})").error == "invalid-argument");
        expect(ask(session, R"({"op":"rigid3.step","steps":0})").error == "invalid-argument");
    };

    "time is the world's: a frame kept is reached again exactly, pushes and all; what would be is asked without touching it"_test = [] {
        auto scene = settled();
        agent::SubjectSession session({scene.get()});
        const std::uint64_t settled_hash = scene->state_hash();
        const auto future = ask(session, R"({"op":"rigid3.branch","steps":90,"body":5,"impulse":[8000,0,0]})");
        expect(future["frame"].integer() == 210);
        expect(eq(scene->state_hash(), settled_hash) && eq(scene->frame(), std::int64_t{120})) << "the world itself did not move";

        (void)ask(session, R"({"op":"rigid3.push","body":5,"impulse":[8000,0,0]})");
        const auto played = ask(session, R"({"op":"rigid3.step","steps":90})");
        expect(played["hash"].text() == future["hash"].text()) << "the same steps, played";
        // Back past a checkpoint (every 60 steps: 180) to a frame between: restored and the push given again.
        const std::uint64_t at_150 = [&] {
            auto again = settled();
            agent::SubjectSession other({again.get()});
            (void)ask(other, R"({"op":"rigid3.push","body":5,"impulse":[8000,0,0]})");
            (void)ask(other, R"({"op":"rigid3.step","steps":30})");
            return again->state_hash();
        }();
        const auto back = ask(session, R"({"op":"rigid3.seek","frame":150})");
        expect(back["frame"].integer() == 150 && eq(scene->state_hash(), at_150)) << "sought back: the same bits as a run that stopped there";
        expect(ask(session, R"({"op":"rigid3.seek","frame":120})")["frame"].integer() == 120 && eq(scene->state_hash(), settled_hash));
        expect(ask(session, R"({"op":"rigid3.seek","frame":99999})").error == "stale");
        expect(ask(session, R"({"op":"rigid3.seek","frame":10})").error == "stale") << "before the history kept";
    };
}};
