// A crowd of skeletal characters as a subject of the agent kernel: the agent contract's suite on it — what it says
// it cannot do yet reported so, never passed — and the characters read, sent, set, layered, sought and branched
// through the same session an agent has; the guided tasks of its catalog run as they are written.
import std;
import mlibs.agent.cts;
import mlibs.json;
import hanim.skeleton;
import hanim.skeleton.cts;
import hanim.skeleton.agent;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace skeleton = hanim::skeleton;

namespace {

// Four characters (idle, walking, running, idle), 30 steps on.
std::unique_ptr<skeleton::agent::Scene> playing() {
    skeleton::Crowd crowd = skeleton::cts::crowd(4);
    for (int step = 0; step < 30; ++step) crowd.step();
    return std::make_unique<skeleton::agent::Scene>(std::move(crowd));
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

UTest cases{"hanim.skeleton.agent", [] {
    "the agent contract on a crowd of characters: what it has passes, what it says it has not is unsupported"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> { return playing(); },
            .read = R"({"op":"skeleton.observe"})",
            .change = R"({"op":"skeleton.step","steps":4})",
            .undo = R"({"op":"skeleton.seek","frame":30})",
            .preview = R"({"op":"skeleton.branch","steps":60,"character":0,"state":"run"})",
            .script = {R"({"op":"skeleton.set","character":0,"parameter":"speed","value":3})", R"({"op":"skeleton.step","steps":30})",
                       R"({"op":"skeleton.blend","character":1,"clip":"wave","weight":1,"mask":"upper_chest"})", R"({"op":"skeleton.step","steps":30})",
                       R"({"op":"skeleton.joint","character":1,"joint":"hand_r"})"},
            .budgeted = R"({"op":"skeleton.observe","budget":1600})",
            .budget = 1600,
        };
        contract::declare(probe, {.subject = "hanim.skeleton.agent"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.source.chain", "agent.actor.recorded", "agent.causes.explained"})
            << "causes and the source of what it puts out: not yet, and said so";
    };

    "read, set, sent, layered: a run by speed, a state by name, a wave on the upper body, a joint where it is"_test = [] {
        auto scene = playing();
        agent::SubjectSession session({scene.get()});
        const auto seen = ask(session, R"({"op":"skeleton.observe"})");
        expect(seen["frame"].integer() == 30 && seen["rig"]["joints"].integer() == 23 && seen["rig"]["clips"].size() == 5u && seen["characters"].size() == 4u);
        expect(seen["rig"]["clips"][1]["compressed_bytes"].integer().value_or(0) > 0 && seen["rig"]["clips"][1]["error"].number().value_or(1) < 0.00015);
        expect(seen["characters"][2]["state"].text() == "run") << "speed 3.2: running";

        (void)ask(session, R"({"op":"skeleton.set","character":0,"parameter":"speed","value":3})");
        (void)ask(session, R"({"op":"skeleton.step","steps":60})");
        const auto ran = ask(session, R"({"op":"skeleton.observe"})");
        expect(ran["characters"][0]["state"].text() == "run" && ran["characters"][0]["last"]["by"].text() == "transition");
        expect(ran["characters"][0]["at"][2].number().value_or(0) > 1.0) << "carried forward by root motion";

        const auto sent = ask(session, R"({"op":"skeleton.play","character":3,"state":"walk","fade":0.5})");
        expect(sent["state"].text() == "walk" && sent["last"]["by"].text() == "play");
        const auto before = ask(session, R"({"op":"skeleton.joint","character":1,"joint":"hand_r"})");
        const auto layered = ask(session, R"({"op":"skeleton.blend","character":1,"layer":0,"clip":"wave","weight":1,"mask":"upper_chest"})");
        expect(layered["layers"].size() == 1u && layered["layers"][0]["mask"].text() == "upper_chest");
        (void)ask(session, R"({"op":"skeleton.step","steps":30})");  // the wave raises the arm over its first third of a second
        const auto raised = ask(session, R"({"op":"skeleton.joint","character":1,"joint":"hand_r"})");
        expect(raised["model"][1].number().value_or(0) > before["model"][1].number().value_or(0) + 0.1) << "the hand raised by the wave";

        const auto sampled = ask(session, R"({"op":"skeleton.sample","clip":"walk","time":1.0})");
        expect(sampled["joints"].size() == 23u && std::abs(sampled["root_motion"]["moved"][2].number().value_or(0) - 1.4) < 1e-3) << "a lap of the walk: 1.4 m";
        expect(sampled["joints"][0]["error"].number().value_or(1) < 0.0005) << "what compression left";
        const auto little = ask(session, R"({"op":"skeleton.sample","clip":"walk","time":0.5,"budget":400})");
        expect(little["more"].integer().value_or(0) > 0) << "within a budget, the rest counted";

        expect(ask(session, R"({"op":"skeleton.play","character":9,"state":"run"})").error == "invalid-argument") << "no such character";
        expect(ask(session, R"({"op":"skeleton.play","character":0,"state":"fly"})").error == "invalid-argument") << "no such state";
        expect(ask(session, R"({"op":"skeleton.set","character":0,"parameter":"mood","value":1})").error == "invalid-argument");
        expect(ask(session, R"({"op":"skeleton.blend","character":0,"clip":"dance","weight":1})").error == "invalid-argument");
        expect(ask(session, R"({"op":"skeleton.joint","character":0,"joint":"tail"})").error == "invalid-argument");
        expect(ask(session, R"({"op":"skeleton.step","steps":0})").error == "invalid-argument");
    };

    "time is the characters': a frame kept is reached again exactly, inputs and all; what would be is asked without touching them"_test = [] {
        auto scene = playing();
        agent::SubjectSession session({scene.get()});
        const std::uint64_t at_30 = scene->state_hash();
        const auto future = ask(session, R"({"op":"skeleton.branch","steps":90,"character":0,"state":"run"})");
        expect(future["frame"].integer() == 120 && eq(scene->state_hash(), at_30)) << "the characters themselves did not move";
        (void)ask(session, R"({"op":"skeleton.play","character":0,"state":"run"})");
        const auto played = ask(session, R"({"op":"skeleton.step","steps":90})");
        expect(played["hash"].text() == future["hash"].text()) << "the same steps, played";
        (void)ask(session, R"({"op":"skeleton.blend","character":2,"clip":"lean","weight":0.8,"additive":true})");
        (void)ask(session, R"({"op":"skeleton.set","character":1,"parameter":"halt","value":1})");
        (void)ask(session, R"({"op":"skeleton.step","steps":45})");
        const std::uint64_t at_165 = scene->state_hash();
        (void)ask(session, R"({"op":"skeleton.set","character":2,"parameter":"speed","value":0})");
        (void)ask(session, R"({"op":"skeleton.step","steps":60})");
        expect(ask(session, R"({"op":"skeleton.seek","frame":165})")["frame"].integer() == 165 && eq(scene->state_hash(), at_165)) << "sought back: the same bits";
        expect(ask(session, R"({"op":"skeleton.seek","frame":30})")["frame"].integer() == 30 && eq(scene->state_hash(), at_30));
        expect(ask(session, R"({"op":"skeleton.seek","frame":99999})").error == "stale");
    };

    "the catalog's guided tasks run as written"_test = [] {
        for (const auto& task : skeleton::agent::guide.tasks) {
            auto scene = playing();
            agent::SubjectSession session({scene.get()});
            for (const auto& step : task.steps) expect(ask(session, step).error.empty()) << task.name << ": " << step;
        }
    };
}};
