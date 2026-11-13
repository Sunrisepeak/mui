// The material lab (T1.13): matter designed live through the session an agent has — the lab's own subject under
// the agent contract's suite; a 13th material that is a row of data and lights, burns and is ash by the laws
// every material has; a row changed and the same scene played again under it, as the preview said it would be;
// sources with a problem that change nothing and say where; two agents at one lab, each holding its part; their
// branches merged by line; the scene's edits as a source played again from disk to the same bits; the picture.
import std;
import mlibs.agent;
import mlibs.agent.cts;
import mlibs.base64;
import mlibs.image;
import mlibs.json;
import mlibs.testing;
import openas;
import xengine.matter.json;
import xengine.matter.lab;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace matter = xengine::matter;
namespace lab = xengine::matter::lab;
namespace lattice = xengine::cellular;

namespace {

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error, message, text;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};
Answered ask(agent::SubjectSession& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        expect(fatal(parsed.has_value())) << record;
        const auto kind = parsed->root()["kind"].text();
        if (kind == "error") out.error = parsed->root()["code"].text().value_or(""), out.message = parsed->root()["text"].text().value_or(""), out.text = record;
        if (kind == "data" && !out.data) out.data.emplace(std::move(*parsed)), out.text = record;
    });
    return out;
}
// A request that is to run: its answer.
Answered told(agent::SubjectSession& session, std::string_view line, std::source_location at = std::source_location::current()) {
    Answered out = ask(session, line);
    expect(fatal(out.data.has_value() && out.error.empty()), at) << line << " → " << out.text;
    return out;
}

// A new lab in a directory of its own: the standard definition, a small field of air.
std::unique_ptr<lab::Lab> lab_of(lattice::Shape shape = {24, 12}, lab::Options options = {}) {
    static const std::filesystem::path root = mlibs::testing::scratch_dir("matter-lab");
    static int made = 0;
    auto created = lab::Lab::create(root / std::format("lab-{}", made++), {.shape = shape}, std::move(options));
    expect(fatal(created.has_value())) << (created ? "" : created.error());
    return std::move(*created);
}
agent::SubjectSession session_of(lab::Lab& here, std::string_view name = "a", openas::Kind kind = openas::Kind::agent) {
    return agent::SubjectSession(here.subjects(), {.actor = {kind, std::string(name), {}}});
}

// How many cells of the lab's field are a material.
std::size_t count(lab::Lab& here, std::string_view name) {
    const auto material = here.matter().program().material(name);
    if (!material) return 0;
    const auto& state = here.field().state();
    const std::size_t area = static_cast<std::size_t>(state.shape.width) * state.shape.height;
    return static_cast<std::size_t>(std::ranges::count(std::span(state.cells).first(area), *material));
}

// A strip of wood on a floor of stone, lit at its left end.
void hearth(agent::SubjectSession& session) {
    told(session, R"({"op":"matter.place","material":"stone","region":[0,11,24,1]})");
    told(session, R"({"op":"matter.place","material":"wood","region":[2,10,8,1]})");
    told(session, R"({"op":"matter.apply","channel":"temperature","set":900,"region":[2,10,1,1]})");
}

inline constexpr std::string_view coal = R"({"op":"lab.material","name":"coal","properties":{"density":1400,"thermal":1,"ignition":700,"fuel":600,"heat":120,"burnt":"ash","look":{"color":"#22201e","grain":0.2}},"intent":"a 13th material: coal"})";

std::string read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

UTest cases{"matter.lab", [] {
    "the agent contract on the lab: observation, control, preview, causes, provenance, history and a budget pass; time is the field's, and said so"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> {
                auto here = lab_of({16, 8});
                {
                    agent::SubjectSession session = session_of(*here, "the scene");
                    told(session, R"({"op":"matter.place","material":"stone","region":[0,7,16,1]})");
                    told(session, R"({"op":"matter.place","material":"wood","region":[2,6,8,1]})");
                    told(session, R"({"op":"matter.apply","channel":"temperature","set":900,"region":[2,6,1,1]})");
                    told(session, R"({"op":"field.step","steps":60})");
                    told(session, R"({"op":"lab.commit","message":"a hearth"})");
                }
                return std::unique_ptr<agent::Subject>(here.release());
            },
            .read = R"({"op":"lab.describe"})",
            .change = std::string(coal),
            .undo = R"({"op":"lab.revert","to":"HEAD~1"})",
            .preview = R"({"op":"lab.try","material":"wood","properties":{"ignition":5000},"steps":40})",
            .explain = R"({"op":"lab.explain","part":"materials/coal"})",
            .source = R"({"op":"lab.explain","part":"materials/wood","property":"ignition"})",
            .budgeted = R"({"op":"lab.describe","budget":400})",
            .budget = 400,
        };
        contract::declare(probe, {.subject = "xengine.matter.lab"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.time.replays"}) << std::format("{}", unsupported);
    };

    "a 13th material is a row of data: no law added, it lies where it is placed, lights at its own temperature, burns and is ash"_test = [] {
        auto here = lab_of();
        agent::SubjectSession session = session_of(*here);
        hearth(session);
        told(session, R"({"op":"field.step","steps":120})");
        const std::uint64_t field_before = here->field().state_hash();
        const std::size_t laws = here->matter().program().laws().size(), materials = here->matter().program().materials().size();

        const Answered added = told(session, coal);
        std::println("    lab.material coal → {}", added.text);
        expect(added["changed"].dump() == R"(["materials/coal.json"])");
        expect(added["differ"].integer() == 0) << "a material nothing is made of yet: the scene played again is the scene";
        expect(added["world"]["materials"].integer() == static_cast<std::int64_t>(materials + 1) && added["world"]["laws"].integer() == static_cast<std::int64_t>(laws));
        expect(added["world"]["steps"].integer() == 120) << "played again to the same step";
        expect(added["commit"]["actor"].text() == "agent:a" && added["commit"]["message"].text() == "a 13th material: coal");
        expect(added["commit"]["changed"].dump() == R"(["materials/coal.json","scene.json"])") << "a commit says the sources it changed: the row, and the hearth placed since the last";
        expect(here->matter().program().material("coal").has_value());
        expect(here->field().state_hash() != field_before) << "the definition is another: the cells' material numbers are too";
        expect(count(*here, "wood") + count(*here, "ash") == 8) << "the hearth as it was: every piece wood or ash";
        expect(std::filesystem::is_regular_file(here->directory() / "materials" / "coal.json"));
        expect(read_file(here->directory() / "world.matter.omui.json").contains("\"coal\"")) << "what a world ships: the snapshot beside the sources";
        expect(matter::prepare_document(read_file(here->directory() / "world.matter.omui.json"), "world.matter.omui.json").has_value());

        // Cold, it lies there; lit, it burns by `burns`, the law wood burns by, and is ash.
        told(session, R"({"op":"matter.place","material":"coal","region":[13,10,8,1]})");
        told(session, R"({"op":"field.step","steps":60})");
        expect(count(*here, "coal") == 8) << "coal at 293 K does nothing";
        told(session, R"({"op":"matter.apply","channel":"temperature","set":650,"region":[13,10,1,1]})");
        told(session, R"({"op":"field.step","steps":60})");
        expect(count(*here, "coal") == 8) << "650 K is under its ignition (700): wood would have lit";
        told(session, R"({"op":"matter.apply","channel":"temperature","set":1000,"region":[13,10,1,1]})");
        told(session, R"({"op":"field.step","steps":40})");
        const Answered why = told(session, R"({"op":"matter.explain","cell":[13,10]})");
        std::println("    matter.explain [13,10] → {}", why.text.substr(0, 900));
        expect(why.text.contains("\"burns\"")) << "why it is alight: the law that held";
        for (int rounds = 0; rounds < 12 && count(*here, "coal") > 0; ++rounds) told(session, R"({"op":"field.step","steps":400})");
        expect(count(*here, "coal") == 0) << std::format("{} pieces of coal left", count(*here, "coal"));
        expect(here->matter().program().laws().size() == laws) << "no law was added";
        const Answered events = told(session, R"({"op":"matter.events","event":"burn_out","region":[13,10,8,1]})");
        expect(events.text.contains("burn_out"));
    };

    "a row changed: the same scene played again under it — what the preview said — and taken back"_test = [] {
        auto here = lab_of();
        agent::SubjectSession session = session_of(*here);
        hearth(session);
        told(session, R"({"op":"field.step","steps":900})");
        told(session, R"({"op":"lab.commit","message":"a hearth, burning"})");
        const std::size_t ash = count(*here, "ash");
        expect(ash > 0 && ash + count(*here, "wood") == 8) << std::format("{} ash", ash);
        const std::uint64_t before = here->state_hash();

        // Tried first: nothing changes, and it says what would.
        const Answered tried = told(session, R"({"op":"lab.try","material":"wood","properties":{"ignition":5000}})");
        std::println("    lab.try wood ignition 5000 → {}", tried.text);
        expect(here->state_hash() == before) << "a preview leaves the lab as it was";
        expect(tried["materials"]["wood"].dump() == std::format("[{},8]", 8 - ash)) << tried["materials"].dump();
        expect(tried["happened"]["ignite"][0].integer().value_or(0) > 0 && tried["happened"]["ignite"][1].integer() == 0) << tried["happened"].dump();

        // Made: the answer is the preview's, and the world is that.
        const Answered made = told(session, R"({"op":"lab.material","name":"wood","properties":{"ignition":5000},"intent":"wood that a torch cannot light"})");
        std::println("    lab.material wood ignition 5000 → {}", made.text);
        expect(made["materials"].dump() == tried["materials"].dump() && made["differ"].dump() == tried["differ"].dump());
        expect(made["differ"].integer() == static_cast<std::int64_t>(tried["differ"].integer().value_or(-1)) && made["differ"].integer().value_or(0) >= static_cast<std::int64_t>(ash));
        expect(count(*here, "wood") == 8 && count(*here, "ash") == 0) << "900 K lights nothing now";
        expect(made["world"]["steps"].integer() == 900);

        // Why the row is what it is: the line, and who wrote it, what for.
        const Answered why = told(session, R"({"op":"lab.explain","part":"materials/wood","property":"ignition"})");
        std::println("    lab.explain materials/wood ignition → {}", why.text);
        expect(why["now"]["text"].text().value_or("").contains("\"ignition\": 5000"));
        expect(why["changes"][0]["by"].text() == "agent:a" && why["changes"][0]["message"].text() == "wood that a torch cannot light");
        expect(why["changes"][0]["before"].text().value_or("").contains("570") && why["changes"][0]["after"].text().value_or("").contains("5000"));
        expect(why["changes"][1]["message"].text() == "a lab begun") << "and before that, the standard row";

        // Taken back: the commit before it, as a new commit; the world is the one before.
        told(session, R"({"op":"lab.revert","to":"HEAD~1"})");
        expect(here->state_hash() == before && count(*here, "ash") == ash);
        const Answered log = told(session, R"({"op":"lab.history"})");
        expect(log["log"].size() == 4) << log.text;
        for (const auto& commit : log["log"].items())
            expect(here->history().thumbnail(commit["id"].text().value_or("")).has_value()) << "every commit has its picture";
    };

    "sources with a problem change nothing and say where"_test = [] {
        auto here = lab_of();
        agent::SubjectSession session = session_of(*here);
        hearth(session);
        const std::uint64_t before = here->state_hash();
        const auto refused = [&](std::string_view line, std::string_view where, std::source_location at = std::source_location::current()) {
            const Answered answer = ask(session, line);
            expect(answer.error == "invalid-argument" && answer.message.contains(where), at) << line << " → " << answer.text;
            expect(here->state_hash() == before, at) << "nothing changed";
            std::println("    {} → {}", line.substr(0, 96), answer.message);
        };
        refused(R"({"op":"lab.material","name":"ghost","properties":{"density":1,"burnt":"soot"}})", "materials/ghost.json");
        expect(!std::filesystem::exists(here->directory() / "materials" / "ghost.json"));
        refused(R"({"op":"lab.material","name":"ghost","properties":{"densty":1}})", "materials/ghost.json");
        refused(R"({"op":"lab.law","name":"wood burns twice","law":{"when":"material == wood","add":{"temperature":5}}})", "laws.json");
        refused(R"({"op":"lab.material","name":"ash","remove":true})", "materials/");
        refused(R"({"op":"lab.material","name":"wood","properties":{"look":{"color":"brown"}}})", "materials/wood.json /look");
        refused(R"({"op":"lab.edit","path":"materials/Bad Name.json","text":"{}"})", "`path`");
        refused(R"({"op":"lab.edit","path":"look.json","text":"{\"materials\":{\"wood\":{\"color\":\"#000000\"}}}"})", "its own row");
        refused(R"({"op":"lab.edit","path":"laws.json","text":"[{\"name\": \"burns\""})", "line 1");
        refused(R"({"op":"lab.edit","path":"scene.json","text":"{\"shape\":[24,12],\"filled\":\"air\",\"edits\":[{\"step\":0,\"place\":\"unobtainium\",\"region\":[0,0,1,1]}]}"})", "scene.json /edits/0");
        refused(R"({"op":"lab.try","material":"wood","properties":{"melted":"nothing_there"}})", "materials/wood.json");
        const Answered missing = ask(session, R"({"op":"lab.read","path":"materials/ghost.json"})");
        expect(missing.error == "not-found") << missing.text;
    };

    "two agents at one lab: a part held keeps the other's change waiting, says who and what for; a person is never held; a lease ends"_test = [] {
        double clock = 100;
        auto here = lab_of({24, 12}, {.clock = [&clock] { return clock; }});
        agent::SubjectSession first = session_of(*here, "a"), second = session_of(*here, "b"), person = session_of(*here, "mia", openas::Kind::person);
        const Answered claimed = told(first, R"({"op":"lab.claim","part":"materials/wood","intent":"tuning how it lights"})");
        expect(claimed["by"].text() == "agent:a" && claimed["seconds"].integer() == 30) << claimed.text;

        const Answered waits = ask(second, R"({"op":"lab.material","name":"wood","properties":{"ignition":600}})");
        std::println("    b on a's part → {}", waits.message);
        expect(waits.error == "conflict" && waits.message.contains("agent:a") && waits.message.contains("tuning how it lights")) << waits.text;
        told(second, R"({"op":"lab.material","name":"sand","properties":{"friction":0.8}})");  // another part: its own
        expect(ask(second, R"({"op":"lab.claim","part":"materials","subtree":true})").error == "conflict") << "all materials holds a's wood too";
        expect(ask(second, R"({"op":"lab.edit","path":"materials/wood.json","remove":true})").error == "conflict");
        told(first, R"({"op":"lab.material","name":"wood","properties":{"ignition":560}})");  // the holder's own change
        told(person, R"({"op":"lab.material","name":"wood","properties":{"ignition":555}})");  // a person is never held
        expect(told(second, R"({"op":"lab.claims"})")["claims"].size() == 1);

        // A history operation that would change the held part waits as well.
        expect(ask(second, R"({"op":"lab.revert","to":"HEAD~1"})").error == "conflict");

        // The scene is a part: while a holds it, b reads the field and cannot change it.
        told(first, R"({"op":"lab.claim","part":"scene","intent":"an experiment","seconds":20})");
        expect(ask(second, R"({"op":"matter.place","material":"water","region":[0,0,2,2]})").error == "conflict");
        expect(ask(second, R"({"op":"field.step","steps":5})").error == "conflict");
        told(second, R"({"op":"matter.observe"})");
        told(first, R"({"op":"matter.place","material":"water","region":[0,0,2,2]})");
        expect(told(first, R"({"op":"lab.release","part":"scene"})")["released"].boolean() == true);
        told(second, R"({"op":"field.step","steps":5})");

        // A lease ends unless renewed.
        clock += 31;
        expect(told(second, R"({"op":"lab.claims"})")["claims"].size() == 0);
        told(second, R"({"op":"lab.material","name":"wood","properties":{"ignition":600}})");
        expect(told(second, R"({"op":"lab.explain","part":"materials/wood","property":"ignition"})")["changes"][0]["by"].text() == "agent:b");
    };

    "two branches merged by source and by line: two materials added are both there, two properties of one both set; the same line on both merges nothing; a row removed stays removed"_test = [] {
        auto here = lab_of();
        agent::SubjectSession first = session_of(*here, "a"), second = session_of(*here, "b");
        told(first, R"({"op":"lab.branch","name":"resins"})");
        // a, on main: coal, and wood lights sooner.
        told(first, coal);
        told(first, R"({"op":"lab.material","name":"wood","properties":{"ignition":540}})");
        // b, on its branch: resin, wood is lighter, and jelly is taken away.
        told(second, R"({"op":"lab.switch","to":"resins"})");
        expect(!here->matter().program().material("coal").has_value()) << "the branch's definition runs: no coal here";
        told(second, R"({"op":"lab.material","name":"resin","properties":{"phase":"liquid","density":1050,"thermal":1,"ignition":450,"fuel":150,"heat":90,"burnt":"smoke"}})");
        told(second, R"({"op":"lab.material","name":"wood","properties":{"density":480}})");
        told(second, R"({"op":"lab.material","name":"jelly","remove":true})");
        told(first, R"({"op":"lab.switch","to":"main"})");
        expect(here->matter().program().material("coal").has_value() && here->matter().program().material("jelly").has_value());

        const Answered merged = told(first, R"({"op":"lab.merge","from":"resins"})");
        std::println("    lab.merge resins → {}", merged.text.substr(0, 400));
        const matter::Program& program = here->matter().program();
        expect(program.material("coal").has_value() && program.material("resin").has_value()) << "both agents' rows";
        expect(!program.material("jelly").has_value()) << "the row one side removed and the other left";
        const std::string wood = told(first, R"({"op":"lab.read","path":"materials/wood.json"})")["text"].text().value_or("");
        expect(wood.contains("\"ignition\": 540") && wood.contains("\"density\": 480")) << wood;
        expect(merged["commit"]["parents"].size() == 2);
        expect(!std::filesystem::exists(here->directory() / "materials" / "jelly.json") && std::filesystem::exists(here->directory() / "materials" / "resin.json"));

        // The same line changed on both: nothing merged, and it says which source.
        told(first, R"({"op":"lab.branch","name":"hot"})");
        told(first, R"({"op":"lab.material","name":"wood","properties":{"ignition":620}})");
        told(second, R"({"op":"lab.switch","to":"hot"})");
        told(second, R"({"op":"lab.material","name":"wood","properties":{"ignition":650}})");
        told(first, R"({"op":"lab.switch","to":"main"})");
        const std::uint64_t before = here->state_hash();
        const Answered conflict = ask(first, R"({"op":"lab.merge","from":"hot"})");
        std::println("    lab.merge hot → {}", conflict.message);
        expect(!conflict.error.empty() && conflict.message.contains("materials/wood.json") && here->state_hash() == before) << conflict.text;
        const Answered differs = told(first, R"({"op":"lab.diff","from":"hot"})");
        expect(differs["files"][0]["path"].text() == "materials/wood.json" && differs["files"][0]["before"].text().value_or("").contains("650")) << differs.text;
    };

    "the scene is a source: edits kept as they are made, with who made them; a seek drops what came after; from disk it plays to the same bits"_test = [] {
        auto here = lab_of({24, 12}, {.keep = 128});
        agent::SubjectSession session = session_of(*here);
        hearth(session);
        told(session, R"({"op":"field.step","steps":30})");
        told(session, R"({"op":"matter.apply","channel":"temperature","add":12.5,"region":[4,10,2,1]})");
        told(session, R"({"op":"field.place","channel":0,"value":0,"region":[9,10,1,1]})");  // the field's own edit: the material's channel
        const std::int64_t frame = here->field().frame();
        const std::string scene = told(session, R"({"op":"lab.read","path":"scene.json"})")["text"].text().value_or("");
        std::println("    scene.json\n{}", scene);
        expect(scene.contains(R"({"step": 0, "place": "wood", "region": [2, 10, 8, 1], "by": "agent:a"})"));
        expect(scene.contains(R"({"step": 30, "channel": "temperature", "add": 12.5, "region": [4, 10, 2, 1], "by": "agent:a"})"));
        expect(scene.contains(R"("channel": "material", "set": "air")"));
        expect(read_file(here->directory() / "scene.json") == scene) << "written as it is made";
        told(session, R"({"op":"field.step","steps":50})");
        const std::uint64_t field = here->field().state_hash();
        const std::uint64_t whole = here->state_hash();

        // The same directory opened anew: played to its last edit, then on — the same bits.
        {
            auto again = lab::Lab::open(here->directory());
            expect(fatal(again.has_value())) << (again ? "" : again.error());
            expect((*again)->steps() == 30) << "to its last edit";
            (*again)->run(50);
            expect((*again)->field().state_hash() == field && (*again)->state_hash() == whole);
        }

        // A change of the definition plays the same edits: the scene's text is as it was.
        told(session, coal);
        expect(told(session, R"({"op":"lab.read","path":"scene.json"})")["text"].text() == scene && here->steps() == 80);

        // Back to before the last two edits: they are no longer the scene's.
        told(session, std::format(R"({{"op":"field.seek","frame":{}}})", frame - 2));
        const std::string sought = told(session, R"({"op":"lab.read","path":"scene.json"})")["text"].text().value_or("");
        expect(!sought.contains("12.5") && !sought.contains("\"material\"") && sought.contains("\"set\": 900")) << sought;

        // Written by hand, or begun anew.
        const Answered edited = told(session, R"({"op":"lab.edit","path":"scene.json","text":"{\"shape\":[24,12],\"filled\":\"air\",\"edits\":[{\"step\":0,\"place\":\"water\",\"region\":[0,0,24,2]},{\"step\":20,\"place\":\"sand\",\"region\":[10,0,4,1]}]}"})");
        expect(count(*here, "water") == 48 && count(*here, "sand") == 4 && here->steps() == 30) << edited.text;  // the sand let go over water that had fallen
        const Answered reset = told(session, R"({"op":"lab.reset","shape":[12,6],"filled":"water"})");
        expect(here->steps() == 0 && count(*here, "water") == 72 && reset["world"]["shape"].dump() == "[12,6]") << reset.text;
    };

    "served: two who connect to the lab's endpoint are two actors — one holds a part, the other is told so, and each commit says whose it is"_test = [] {
        auto here = lab_of();
        const auto directory = mlibs::testing::scratch_dir("matter-lab-endpoint");
        agent::Endpoint endpoint({.name = "matter-lab", .directory = directory, .app = "xengine.matter.lab", .interface_name = std::string(here->interface_name()),
                                  .session = [&] { return here->session(); }});
        auto started = endpoint.start();
        if (!started && started.error().find("not supported") != std::string::npos) {
            std::println("    skipped: local endpoints are not supported here ({})", started.error());
            return;
        }
        expect(fatal(started.has_value())) << (started ? "" : started.error());
        auto first = agent::Connection::connect("matter-lab", directory), second = agent::Connection::connect("matter-lab", directory);
        expect(fatal(first.has_value() && second.has_value()));
        // One request and its records up to the result, the endpoint pumped meanwhile: the data, or the error.
        const auto call = [&](agent::Connection& connection, std::string_view line) {
            expect(connection.send(line).has_value());
            std::string data, error;
            for (int turn = 0; turn < 20000; ++turn) {
                endpoint.pump();
                auto got = connection.receive(std::chrono::milliseconds(1));
                if (!got) break;
                if (!*got) continue;
                auto parsed = mlibs::json::Document::parse(**got, "record");
                expect(fatal(parsed.has_value()));
                const auto kind = parsed->root()["kind"].text();
                if (kind == "data") data = **got;
                if (kind == "error") error = **got;
                if (kind == "result") break;
            }
            return std::pair(data, error);
        };
        expect(call(*first, R"({"op":"subjects"})").first.contains("\"name\":\"lab\"") && call(*second, R"({"op":"subjects"})").first.contains("\"name\":\"frame\""));
        expect(call(*first, R"({"op":"lab.claim","part":"materials/wood","intent":"its fuel"})").first.contains("agent:session-1"));
        const auto waits = call(*second, R"({"op":"lab.material","name":"wood","properties":{"fuel":300}})");
        expect(waits.second.contains("conflict") && waits.second.contains("agent:session-1") && waits.second.contains("its fuel")) << waits.second;
        expect(call(*second, R"({"op":"lab.material","name":"sand","properties":{"friction":0.8},"intent":"coarser sand"})").first.contains("\"actor\":\"agent:session-2\""));
        expect(call(*first, R"({"op":"lab.material","name":"wood","properties":{"fuel":300}})").first.contains("\"actor\":\"agent:session-1\""));
        const std::string log = call(*second, R"({"op":"lab.history"})").first;
        expect(log.contains("agent:session-1") && log.contains("agent:session-2") && log.contains("program:matter-lab")) << log;
    };

    "the README's session is real: its script, played by a new lab, draws the picture kept beside it — the same bytes"_test = [] {
        const auto media = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "media";
        auto here = lab_of({64, 32});
        agent::SubjectSession session = session_of(*here, "claude");
        std::ifstream script(media / "coal-beside-wood.jsonl");
        expect(fatal(script.good())) << "media/coal-beside-wood.jsonl";
        std::string drawn;
        int requests = 0;
        for (std::string line; std::getline(script, line); ++requests) {
            const Answered answer = told(session, line);
            if (line.contains("lab.picture")) drawn = answer["path"].text().value_or("");
        }
        expect(requests == 17 && here->steps() == 600 && count(*here, "coal") == 44) << "nothing of the coal is ash yet: it burns for 900 steps";
        const std::string kept = read_file(media / "coal-beside-wood.png");
        expect(!kept.empty() && read_file(drawn) == kept) << "run the script again (README: matter-lab run) and keep its picture";
    };

    "the picture: the field through the renderer's frame, read by the same session — a pixel traced, a PNG kept — and the look a row's own"_test = [] {
        auto here = lab_of();
        agent::SubjectSession session = session_of(*here);
        hearth(session);
        const std::string empty = told(session, R"({"op":"frame.layers"})")["hash"].text().value_or("");
        const Answered wood = told(session, R"({"op":"frame.at","at":[22,42]})");  // the cell [5,10], a cell 4 pixels
        expect(wood["chain"][0]["layer"].text() == "field") << wood.text;
        const std::string brown = wood["pixel"].dump();
        const Answered air = told(session, R"({"op":"frame.at","at":[22,10]})");
        expect(air["pixel"].dump() == "[13,17,23,255]") << "air is clear: the backdrop — " << air["pixel"].dump();
        expect(brown != air["pixel"].dump());

        // The look is the row's own: another colour, the same cells, another pixel.
        const std::uint64_t field = here->field().state_hash();
        told(session, R"({"op":"lab.material","name":"wood","properties":{"look":{"color":"#2e8b57","grain":0}}})");
        expect(here->field().state_hash() == field) << "a look changes no cell";
        expect(told(session, R"({"op":"frame.at","at":[22,42]})")["pixel"].dump() == "[46,139,87,255]");
        expect(told(session, R"({"op":"frame.layers"})")["hash"].text() != empty);

        // A fire gives light: the picture changes with the field, and is kept as a PNG by its content.
        told(session, R"({"op":"field.step","steps":300})");
        const Answered kept = told(session, R"({"op":"lab.picture","scale":6})");
        std::println("    lab.picture → {}", kept.text);
        expect(kept["size"].dump() == "[144,72]");
        const std::string file = kept["path"].text().value_or("");
        const std::string bytes = read_file(file);
        const auto image = mlibs::image::decode_png(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
        expect(fatal(image.has_value()));
        expect(image->width == 144 && image->height == 72);
        const Answered captured = told(session, R"({"op":"frame.capture"})");
        const auto png = mlibs::base64::decode(captured["png"].text().value_or(""));
        expect(fatal(png.has_value()));
        const auto shown = mlibs::image::decode_png(*png);
        expect(fatal(shown.has_value()));
        expect(shown->width == 96 && shown->height == 48) << "the lab's own scale: 4 pixels a cell";
        expect(told(session, R"({"op":"lab.picture","scale":6})")["path"].text() == file) << "the same picture, the same file";
    };
}};
