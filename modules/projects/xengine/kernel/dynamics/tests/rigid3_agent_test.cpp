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

// A floor, three boxes stacked and a ball rolling slowly toward them; aside, a paddle on a hinge to the world (joint
// 1) and a character standing (character 1); 120 steps on: the same each time.
std::unique_ptr<rigid3::agent::Scene> settled() {
    auto world = rigid3::World::make();
    expect(fatal(world.has_value()));
    (void)world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
    for (int level = 0; level < 3; ++level) (void)world->add({.at = {0, 0.5f + static_cast<float>(level) * 1.01f, 0}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}), {.density = 500});
    (void)world->add({.at = {-8, 0.5f, 0}, .velocity = {1, 0, 0}, .spin = {0, 0, -2}}, rigid3::Shape::sphere(0.5f), {.density = 2000});
    const auto paddle = *world->add({.at = {8, 2, 8}}, rigid3::Shape::box({0.8f, 0.1f, 0.1f}));
    rigid3::Joint hinge = rigid3::Joint::hinge(paddle, {}, {8, 2, 8}, {0, 0, 1});
    hinge.motor.most = 500;
    expect(fatal(world->join(hinge).has_value()));
    expect(fatal(world->add(rigid3::CharacterDef{.at = {-8, 0, 8}}).has_value()));
    for (int step = 0; step < 120; ++step) world->step();
    return std::make_unique<rigid3::agent::Scene>(std::move(*world));
}

// A floor, a wheel on a hinge to the world (joint 1) and a character standing (character 1), 30 steps on.
std::unique_ptr<rigid3::agent::Scene> jointed() {
    auto world = rigid3::World::make();
    expect(fatal(world.has_value()));
    (void)world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
    const auto wheel = *world->add({.at = {0, 2, 4}}, rigid3::Shape::compound({{rigid3::Shape::box({0.8f, 0.1f, 0.1f}), {}, {}}, {rigid3::Shape::box({0.1f, 0.8f, 0.1f}), {}, {}}}));
    rigid3::Joint hinge = rigid3::Joint::hinge(wheel, {}, {0, 2, 4}, {0, 0, 1});
    hinge.motor.most = 500;
    expect(fatal(world->join(hinge).has_value()));
    expect(fatal(world->add(rigid3::CharacterDef{.at = {-4, 0, 0}}).has_value()));
    for (int step = 0; step < 30; ++step) world->step();
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
    // Body ids: the floor 1, the boxes 2 to 4 (bottom to top), the ball 5, the paddle 6 — the world's, in the order made.
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
        expect(seen["frame"].integer() == 120 && seen["bodies"].integer() == 6 && seen["joints"].integer() == 1 && seen["characters"].integer() == 1);
        const auto listed = ask(session, R"({"op":"rigid3.bodies"})");
        expect(listed["bodies"].size() == 6u && listed["bodies"][0]["mass"].number() == 0.0) << "the floor is fixed";
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

    "joints and characters through the session: a motor driven, a character walked and jumped, each played again by a seek; a shape cast"_test = [] {
        auto scene = jointed();
        agent::SubjectSession session({scene.get()});
        const auto joints = ask(session, R"({"op":"rigid3.joints"})");
        expect(joints["joints"].size() == 1u && joints["joints"][0]["form"].text() == "hinge" && joints["joints"][0]["other"].integer() == 0) << "a hinge to the world";
        const auto standing = ask(session, R"({"op":"rigid3.ground"})");
        expect(standing["grounds"].size() == 1u && standing["grounds"][0]["ground"].text() == "on_ground" && standing["grounds"][0]["standing_on"].integer() == 1);

        expect(ask(session, R"({"op":"rigid3.drive","joint":1,"mode":"velocity","target":2})")["effective"].integer() == 31);
        expect(ask(session, R"({"op":"rigid3.move","character":1,"velocity":[2,0,0]})")["effective"].integer() == 31);
        (void)ask(session, R"({"op":"rigid3.step","steps":60})");
        const auto turning = ask(session, R"({"op":"rigid3.joints"})");
        expect(turning["joints"][0]["motor"].text() == "velocity" && turning["joints"][0]["target"].number() == 2.0);
        expect(std::abs(scene->world().pose(rigid3::BodyId{2})->spin.z - 2) < 0.05f) << "the wheel turns at its motor's speed";
        const auto walked = ask(session, R"({"op":"rigid3.where","character":1})");
        expect(walked["at"][0].number().value_or(0) > -2.2 && walked["ground"].text() == "on_ground") << "walked 2 m in a second, on the floor";
        expect(ask(session, R"({"op":"rigid3.jump","character":1,"speed":5})")["ground"].text() == "on_ground");
        (void)ask(session, R"({"op":"rigid3.step","steps":20})");
        expect(ask(session, R"({"op":"rigid3.ground"})")["grounds"][0]["ground"].text() == "in_air") << "in the air a third of a second after";
        (void)ask(session, R"({"op":"rigid3.step","steps":40})");
        // Back to frame 100 (between the jump at 90 and now): the drive, the walk and the jump played again.
        const std::uint64_t at_100 = [&] {
            auto again = jointed();
            agent::SubjectSession other({again.get()});
            (void)ask(other, R"({"op":"rigid3.drive","joint":1,"mode":"velocity","target":2})");
            (void)ask(other, R"({"op":"rigid3.move","character":1,"velocity":[2,0,0]})");
            (void)ask(other, R"({"op":"rigid3.step","steps":60})");
            (void)ask(other, R"({"op":"rigid3.jump","character":1,"speed":5})");
            (void)ask(other, R"({"op":"rigid3.step","steps":10})");
            return again->state_hash();
        }();
        expect(ask(session, R"({"op":"rigid3.seek","frame":100})")["frame"].integer() == 100 && eq(scene->state_hash(), at_100)) << "sought back: the same bits";

        const auto cast = ask(session, R"({"op":"rigid3.cast","shape":"box","half":[0.5,0.5,0.5],"from":[6,10,0],"to":[6,-10,0]})");
        expect(cast["hit"].boolean() == true && cast["body"].integer() == 1 && std::abs(cast["fraction"].number().value_or(0) - 0.475) < 1e-3) << "a box swept down meets the floor";
        expect(ask(session, R"({"op":"rigid3.drive","joint":9,"target":1})").error == "invalid-argument") << "no such joint";
        expect(ask(session, R"({"op":"rigid3.drive","joint":1,"mode":"spin"})").error == "invalid-argument");
        expect(ask(session, R"({"op":"rigid3.move","character":7,"velocity":[1,0,0]})").error == "invalid-argument") << "no such character";
        expect(ask(session, R"({"op":"rigid3.cast","shape":"cone","from":[0,1,0],"to":[0,0,0]})").error == "invalid-argument");
    };
}};
