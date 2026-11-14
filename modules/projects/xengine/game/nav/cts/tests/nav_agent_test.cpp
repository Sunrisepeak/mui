// A navigation world as a subject of the agent kernel: the agent contract's suite on it — what it says it cannot do
// yet reported so, never passed — and the world built, asked, walked, blocked, sought and branched through the same
// session an agent has.
import std;
import mlibs.agent.cts;
import mlibs.json;
import xengine.nav;
import xengine.nav.cts;
import xengine.nav.agent;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace nav = xengine::nav;

namespace {

// The room with its link and a crowd of four sent across, 30 steps on: walkers 1 to 4, link 1.
std::unique_ptr<nav::agent::Scene> walking() {
    nav::World world = nav::cts::crowd(4);
    for (int step = 0; step < 30; ++step) world.step();
    return std::make_unique<nav::agent::Scene>(std::move(world));
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

UTest cases{"nav.agent", [] {
    "the agent contract on a navigation world: what it has passes, what it says it has not is unsupported"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> { return walking(); },
            .read = R"({"op":"nav.observe"})",
            .change = R"({"op":"nav.step","steps":4})",
            .undo = R"({"op":"nav.seek","frame":30})",
            .preview = R"({"op":"nav.branch","steps":60,"walker":1,"to":[-10,0,10]})",
            .script = {R"({"op":"nav.add","at":[-8,0,0]})", R"({"op":"nav.target","walker":5,"to":[8,0,4]})", R"({"op":"nav.step","steps":60})",
                       R"({"op":"nav.obstacle","shape":"cylinder","at":[0,0,11.5],"radius":1.2,"height":2})", R"({"op":"nav.step","steps":60})", R"({"op":"nav.where"})"},
            .budgeted = R"({"op":"nav.where","budget":300})",
            .budget = 300,
        };
        contract::declare(probe, {.subject = "xengine.nav.agent"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.source.chain", "agent.actor.recorded", "agent.causes.explained"})
            << "causes and the source of what it puts out: not yet, and said so";
    };

    "asked, walked, blocked: a path through the doorway, a walker sent along it, an obstacle that shuts it"_test = [] {
        auto scene = walking();
        agent::SubjectSession session({scene.get()});
        const auto seen = ask(session, R"({"op":"nav.observe"})");
        expect(seen["frame"].integer() == 30 && seen["walkers"].integer() == 4 && seen["links"].integer() == 1);
        const auto way = ask(session, R"({"op":"nav.path","from":[-5,0,-5],"to":[5,0,-5]})");
        expect(way["reaches"].boolean() == true && way["corners"].size() >= 3u);
        const auto ray = ask(session, R"({"op":"nav.ray","from":[-5,0,-5],"to":[5,0,-5]})");
        expect(ray["clear"].boolean() == false) << "the wall";
        expect(ask(session, R"({"op":"nav.nearest","at":[100,0,100]})")["found"].boolean() == false);

        const auto added = ask(session, R"({"op":"nav.add","at":[-8,0,0]})");
        expect(added["walker"].integer() == 5);
        const auto sent = ask(session, R"({"op":"nav.target","walker":5,"to":[8,0,4]})");
        expect(sent["going"].text() == "walking");
        (void)ask(session, R"({"op":"nav.step","steps":900})");
        const auto there = ask(session, R"({"op":"nav.where","walker":5})");
        expect(there["at"][0].number().value_or(0) > 6 && std::abs(there["at"][2].number().value_or(0) - 4) < 1.5) << std::format("through the doorway, at its target: {} at x {}", there["going"].text().value_or(""), there["at"][0].number().value_or(0));

        const auto shut = ask(session, R"({"op":"nav.obstacle","shape":"box","at":[0,1,11.5],"half":[1,1,3.6]})");
        expect(shut["obstacle"].integer() == 1);
        expect(ask(session, R"({"op":"nav.path","from":[-5,0,-5],"to":[5,0,-5]})")["reaches"].boolean() == false) << "the doorway shut";
        expect(ask(session, R"({"op":"nav.target","walker":5,"to":[-8,0,0]})")["going"].text() == "unreachable") << "back the way it came: shut";
        (void)ask(session, R"({"op":"nav.obstacle","remove":1})");
        expect(ask(session, R"({"op":"nav.path","from":[-5,0,-5],"to":[5,0,-5]})")["reaches"].boolean() == true) << "open again";

        expect(ask(session, R"({"op":"nav.target","walker":99,"to":[1,0,1]})").error == "invalid-argument") << "no such walker";
        expect(ask(session, R"({"op":"nav.path","from":[100,0,100],"to":[1,0,1]})").error == "not-found") << "off the mesh";
        expect(ask(session, R"({"op":"nav.obstacle","shape":"cone","at":[0,0,0]})").error == "invalid-argument");
        expect(ask(session, R"({"op":"nav.step","steps":0})").error == "invalid-argument");
    };

    "time is the world's: a frame kept is reached again exactly, inputs and all; what would be is asked without touching it; a build replaces the mesh"_test = [] {
        auto scene = walking();
        agent::SubjectSession session({scene.get()});
        const std::uint64_t at_30 = scene->state_hash();
        const auto future = ask(session, R"({"op":"nav.branch","steps":90,"walker":1,"to":[-10,0,10]})");
        expect(future["frame"].integer() == 120 && eq(scene->state_hash(), at_30)) << "the world itself did not move";
        (void)ask(session, R"({"op":"nav.target","walker":1,"to":[-10,0,10]})");
        const auto played = ask(session, R"({"op":"nav.step","steps":90})");
        expect(played["hash"].text() == future["hash"].text()) << "the same steps, played";
        // Inputs between checkpoints (every 60 steps), then back to a frame between them.
        (void)ask(session, R"({"op":"nav.add","at":[-8,0,0]})");
        (void)ask(session, R"({"op":"nav.link","from":[-12,0,12],"to":[-12,0,-12]})");
        (void)ask(session, R"({"op":"nav.step","steps":45})");
        const std::uint64_t at_165 = scene->state_hash();
        (void)ask(session, R"({"op":"nav.obstacle","shape":"cylinder","at":[0,0,11.5],"radius":1.2})");
        (void)ask(session, R"({"op":"nav.step","steps":60})");
        expect(ask(session, R"({"op":"nav.seek","frame":165})")["frame"].integer() == 165 && eq(scene->state_hash(), at_165)) << "sought back: the same bits";
        expect(ask(session, R"({"op":"nav.seek","frame":30})")["frame"].integer() == 30 && eq(scene->state_hash(), at_30));
        expect(ask(session, R"({"op":"nav.seek","frame":99999})").error == "stale");

        const auto built = ask(session, R"({"op":"nav.build","points":[[-10,0,-10],[-10,0,10],[10,0,10],[10,0,-10]],"corners":[0,1,2,0,2,3]})");
        expect(built["frame"].integer() == 30 && built["walkers"].integer() == 0 && built["polygons"].integer() > 0) << "a floor, its walkers gone";
        expect(ask(session, R"({"op":"nav.ray","from":[-5,0,-5],"to":[5,0,-5]})")["clear"].boolean() == true) << "no wall";
        (void)ask(session, R"({"op":"nav.step","steps":10})");
        expect(ask(session, R"({"op":"nav.seek","frame":30})")["frame"].integer() == 30 && eq(scene->state_hash(), at_30)) << "the room back";
        expect(ask(session, R"({"op":"nav.build","points":[[0,0,0]],"corners":[0,0]})").error == "invalid-argument");
    };
}};
