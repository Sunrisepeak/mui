// A field of falling sand as a subject of the agent kernel: the agent
// contract's suite on it — what it says it cannot do reported so, never
// passed — and the field read by region, placed in, stepped, sought and
// branched, and who changed a cell asked, through the same session an agent has.
import std;
import mlibs.agent.cts;
import mlibs.json;
import openas;
import xengine.cellular.api;
import xengine.cellular.agent;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace cellular = xengine::cellular;

namespace {

// Channel 0: 1 is sand, 0 is empty; sand falls a cell a step into what is empty below it. Channel 1 counts how
// often a cell held sand; channel 2 is left alone. Outside the field is 2: a wall.
cellular::Program sand() {
    const std::array<cellular::Offset, 1> below{{{0, 1}}}, above{{{0, -1}}};
    const std::array<cellular::Rule, 3> rules{{
        {{cellular::Condition::self({0, {1, 1}}), cellular::Condition::neighbors({0, {0, 0}}, below, {1, 1})}, {{0, cellular::WriteKind::set, 0}}},
        {{cellular::Condition::self({0, {0, 0}}), cellular::Condition::neighbors({0, {1, 1}}, above, {1, 1})},
         {{0, cellular::WriteKind::set, 1}, {1, cellular::WriteKind::add_saturating, 1}}},
        {{cellular::Condition::self({0, {1, 1}})}, {{1, cellular::WriteKind::add_saturating, 1}}},
    }};
    return cellular::Program::prepare(3, rules).value();
}

std::unique_ptr<cellular::agent::Field> field() {
    auto made = cellular::agent::Field::make(sand(), {16, 16}, {}, {.boundary = {cellular::Edge::constant, 2}});
    expect(fatal(made.has_value())) << (made ? "" : made.error());
    auto out = std::make_unique<cellular::agent::Field>(std::move(*made));
    out->place(0, 1, {4, 0, 2, 2});  // four grains at the top: frame 1
    return out;
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

UTest cases{"cellular.agent", [] {
    "the agent contract on a field: what it has passes, what it says it has not is unsupported"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> { return field(); },
            .read = R"({"op":"field.observe"})",
            .change = R"({"op":"field.place","channel":0,"value":1,"region":[10,3,3,1]})",
            .undo = R"({"op":"field.seek","frame":1})",
            .preview = R"({"op":"field.branch","steps":8,"channel":0,"value":1,"place":[0,0,16,1]})",
            .script = {R"({"op":"field.place","channel":0,"value":1,"region":[9,0,1,3]})", R"({"op":"field.step","steps":5})", R"({"op":"field.observe","region":[0,8,16,8]})",
                       R"({"op":"field.step","steps":12})", R"({"op":"field.cells","channel":0,"region":[0,13,16,3]})"},
            .explain = R"({"op":"field.explain","cell":[11,3]})",
            .budgeted = R"({"op":"field.observe","budget":180})",
            .budget = 180,
        };
        contract::declare(probe, {.subject = "xengine.cellular.agent"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.source.chain"}) << "which rule wrote a cell — the source of what it puts out — is not here for plain rules, and said so";
    };

    "who changed a cell is on record: every edit with its actor, the latest first; what the program changed in the last step; a seek drops what came after"_test = [] {
        auto sandbox = field();
        agent::SubjectSession ada({sandbox.get()}, {.actor = {openas::Kind::agent, "ada", {}}});
        agent::SubjectSession grace({sandbox.get()}, {.actor = {openas::Kind::person, "grace", {}}});
        // The owner placed four grains when the field was made (frame 1); two more are placed by two actors.
        (void)ask(ada, R"({"op":"field.place","channel":0,"value":1,"region":[4,0,8,1]})");     // frame 2: over the owner's top row
        (void)ask(grace, R"({"op":"field.place","channel":2,"value":7,"region":[5,0,1,1]})");   // frame 3
        const auto top = ask(ada, R"({"op":"field.explain","cell":[5,0]})");
        expect(top["cell"].dump() == "[5,0]" && top["frame"].integer() == 3 && top["values"].dump() == "[1,0,7]") << top.data->root().dump();
        expect(eq(top["edits"].size(), std::size_t{3})) << "three edits cover it";
        expect(top["edits"][0]["frame"].integer() == 3 && top["edits"][0]["by"]["name"].text() == "grace" && top["edits"][0]["by"]["kind"].text() == "person" &&
               top["edits"][0]["wrote"].dump() == R"([{"channel":2,"value":7}])" && top["edits"][0]["region"].dump() == "[5,0,1,1]");
        expect(top["edits"][1]["frame"].integer() == 2 && top["edits"][1]["by"]["name"].text() == "ada" && top["edits"][1]["kind"].text() == "set");
        expect(top["edits"][2]["frame"].integer() == 1 && top["edits"][2]["by"]["name"].text() == "owner" && top["edits"][2]["by"]["kind"].text() == "program");
        expect(!top["last_step"]) << "the last frame was an edit, not a step";
        expect(eq(ask(ada, R"({"op":"field.explain","cell":[10,0]})")["edits"].size(), std::size_t{1})) << "only ada's row reaches here";
        expect(eq(ask(ada, R"({"op":"field.explain","cell":[0,9]})")["edits"].size(), std::size_t{0})) << "no one touched this one";

        // A step: the grain at [8,0] falls — the program changed the cell, and the record says from what to what;
        // the one at [5,0] lies on the owner's grain and stays, counted once more as sand.
        (void)ask(ada, R"({"op":"field.step"})");
        const auto dropped = ask(ada, R"({"op":"field.explain","cell":[8,0]})");
        expect(dropped["last_step"]["frame"].integer() == 4 && dropped["last_step"]["changed"].dump() == R"([{"channel":0,"from":1,"to":0}])") << dropped["last_step"].dump();
        const auto fallen = ask(ada, R"({"op":"field.explain","cell":[5,0]})");
        expect(fallen["last_step"]["changed"].dump() == R"([{"channel":1,"from":0,"to":1}])") << fallen["last_step"].dump();
        expect(ask(ada, R"({"op":"field.explain","cell":[0,9]})")["last_step"]["changed"].dump() == "[]") << "a cell the step left alone";
        expect(eq(fallen["edits"].size(), std::size_t{3})) << "the edits are still its history";

        // Back before grace's edit: it is another branch's now, and no longer on this record.
        const std::uint64_t before = sandbox->state_hash();
        (void)ask(ada, R"({"op":"field.explain","cell":[5,0]})");
        expect(eq(sandbox->state_hash(), before)) << "explaining changes nothing";
        (void)ask(ada, R"({"op":"field.seek","frame":2})");
        const auto earlier = ask(ada, R"({"op":"field.explain","cell":[5,0]})");
        expect(eq(earlier["edits"].size(), std::size_t{2}) && earlier["edits"][0]["by"]["name"].text() == "ada") << earlier["edits"].dump();
        expect(eq(sandbox->edits().size(), std::size_t{2}));
        expect(ask(ada, R"({"op":"field.explain","cell":[16,0]})").error == "invalid-argument");
        expect(ask(ada, R"({"op":"field.explain"})").error == "invalid-argument");

        // The owner's own edits: several channels at once, raised and lowered; one the field cannot hold is not made.
        cellular::agent::Edit both{.kind = cellular::agent::EditKind::add, .count = 2, .region = {0, 15, 2, 1}};
        both.channels = {1, 2};
        both.values = {5, 0xffffffffu};
        expect(sandbox->edit(both) && sandbox->edit(both));
        expect(eq(sandbox->state().cells[1 * 256 + 15 * 16], 10u) && eq(sandbox->state().cells[2 * 256 + 15 * 16 + 1], 0xffffffffu)) << "added twice; saturating";
        both.kind = cellular::agent::EditKind::take;
        both.values = {3, 1};
        expect(sandbox->edit(both));
        expect(eq(sandbox->state().cells[1 * 256 + 15 * 16], 7u) && eq(sandbox->state().cells[2 * 256 + 15 * 16], 0xfffffffeu));
        both.channels = {1, 3};
        expect(!sandbox->edit(both)) << "a channel the field has not";
        both.channels = {1, 2};
        both.region = {15, 15, 2, 1};
        expect(!sandbox->edit(both)) << "a region past the edge";
        expect(!sandbox->edit({})) << "a step is not an edit";
    };

    "sand placed falls and settles: read as numbers by region, cell by cell when asked, within the budget asked"_test = [] {
        auto sandbox = field();
        agent::SubjectSession session({sandbox.get()});
        const auto before = ask(session, R"({"op":"field.observe"})");
        expect(before["frame"].integer() == 1 && before["steps"].integer() == 0);
        expect(before["channels"][0]["sum"].integer() == 4 && before["channels"][0]["filled"].integer() == 4);

        const auto stepped = ask(session, R"({"op":"field.step","steps":20})");
        expect(stepped["frame"].integer() == 21 && stepped["steps"].integer() == 20);
        const auto floor = ask(session, R"({"op":"field.observe","region":[0,14,16,2]})");
        expect(floor["channels"][0]["sum"].integer() == 4) << "all four grains lie in the two rows above the wall";
        expect(ask(session, R"({"op":"field.observe","region":[0,0,16,14]})")["channels"][0]["sum"].integer() == 0) << "none is left above";
        const auto rows = ask(session, R"({"op":"field.cells","channel":0,"region":[3,14,4,2]})");
        expect(rows["rows"][0].dump() == "[0,1,1,0]" && rows["rows"][1].dump() == "[0,1,1,0]") << rows["rows"].dump();

        const auto brief = ask(session, R"({"op":"field.observe","budget":180})");
        expect(eq(brief["channels"].size(), std::size_t{1}) && brief["more"].integer() == 2) << "channels left out from the last, and how many said";

        expect(ask(session, R"({"op":"field.observe","region":[10,10,10,10]})").error == "invalid-argument") << "a region outside the field";
        expect(ask(session, R"({"op":"field.cells","channel":0,"region":[0,0,16,16]})").error.empty());
        expect(ask(session, R"({"op":"field.cells","channel":7,"region":[0,0,1,1]})").error == "invalid-argument");
        auto wide = cellular::agent::Field::make(sand(), {128, 64});
        expect(fatal(wide.has_value()));
        agent::SubjectSession other({&*wide});
        expect(ask(other, R"({"op":"field.cells","channel":0,"region":[0,0,128,64]})").error == "invalid-argument") << "more cells than a request reads";
    };

    "a field runs what is awake: a grain falls by the field's own ticks, the field at rest sleeps, a placement wakes its tiles — the cells those of every tile run"_test = [] {
        // Cells that move (the kernel's exchange): channel 0 how heavy a cell is, channel 1 how it may move.
        const std::array<cellular::Exchange, 1> moving{{{.weight = 0, .mobility = 1}}};
        const auto program = cellular::Program::prepare(2, cellular::Stages{.exchanges = moving});
        expect(fatal(program.has_value()));
        const cellular::Shape shape{16, 16};
        std::vector<std::uint32_t> air(2 * 256);
        std::fill_n(air.begin(), 256, 10u);
        std::fill_n(air.begin() + 256, 256, cellular::falls);
        auto made = cellular::agent::Field::make(*program, shape, air, {.boundary = {cellular::Edge::clamp, 0}, .tile = {4, 4}});
        expect(fatal(made.has_value())) << (made ? "" : made.error());
        agent::SubjectSession session({&*made});

        // The same by hand, every tile run each step: what the field's cells must be.
        std::vector<std::uint32_t> cells = air, next(air.size());
        std::uint64_t tick = 0;
        const auto run = [&](int steps) {
            for (int step = 0; step < steps; ++step, ++tick) {
                expect(fatal(program->step(shape, cells, next, {.boundary = {cellular::Edge::clamp, 0}, .tick = tick}).has_value()));
                cells.swap(next);
            }
        };
        const auto same = [&] { return std::ranges::equal(made->state().cells, cells); };

        // A grain of sand at the top: heavier than air, falling and sliding.
        (void)ask(session, R"({"op":"field.place","channel":0,"value":100,"region":[5,0,1,1]})");
        (void)ask(session, R"({"op":"field.place","channel":1,"value":3,"region":[5,0,1,1]})");
        cells[5] = 100;
        cells[256 + 5] = 3;
        const auto falling = ask(session, R"({"op":"field.step","steps":6})");
        run(6);
        expect(same()) << "six steps on";
        expect(falling["tiles"].integer() == 16 && falling["asleep"].integer().value_or(0) >= 8 && falling["asleep"].integer().value_or(16) < 16)
            << std::format("{} of 16 tiles asleep while the grain falls", falling["asleep"].integer().value_or(-1));
        const auto fallen = ask(session, R"({"op":"field.step","steps":40})");
        run(40);
        expect(same());
        expect(ask(session, R"({"op":"field.cells","channel":0,"region":[5,15,1,1]})")["rows"][0].dump() == "[100]") << "on the floor: the cut moved with the field's steps";
        expect(fallen["asleep"].integer() == 16) << "nothing can move: every tile sleeps";

        // Asleep, a step is the same field; a frame kept is reached again, and played again the same.
        const auto resting = ask(session, R"({"op":"field.step","steps":3})");
        run(3);
        expect(same() && resting["asleep"].integer() == 16);
        (void)ask(session, R"({"op":"field.place","channel":0,"value":100,"region":[12,2,1,1]})");
        (void)ask(session, R"({"op":"field.place","channel":1,"value":3,"region":[12,2,1,1]})");
        cells[2 * 16 + 12] = 100;
        cells[256 + 2 * 16 + 12] = 3;
        const auto woken = ask(session, R"({"op":"field.step"})");
        run(1);
        expect(same() && woken["asleep"].integer().value_or(16) < 16) << "a placement wakes the tiles it is in and beside";
        const std::int64_t here = woken["frame"].integer().value_or(0);
        const auto later = ask(session, R"({"op":"field.step","steps":30})");
        run(30);
        expect(same() && later["asleep"].integer() == 16);
        const auto back = ask(session, std::format(R"({{"op":"field.seek","frame":{}}})", here));
        expect(back["hash"].text() == woken["hash"].text() && back["asleep"].integer() == woken["asleep"].integer()) << "what sleeps is the frame's, with its cells";
        expect(ask(session, R"({"op":"field.step","steps":30})")["hash"].text() == later["hash"].text()) << "played again from there";
    };

    "time is the field's: a frame kept is reached again exactly; what would be is asked without touching it"_test = [] {
        auto sandbox = field();
        agent::SubjectSession session({sandbox.get()});
        const auto at_five = ask(session, R"({"op":"field.step","steps":5})")["hash"].text();
        (void)ask(session, R"({"op":"field.place","channel":0,"value":1,"region":[12,0,1,1]})");
        (void)ask(session, R"({"op":"field.step","steps":9})");
        const auto back = ask(session, R"({"op":"field.seek","frame":6})");
        expect(back["frame"].integer() == 6 && back["hash"].text() == at_five && back["latest"].integer() == 6);
        expect(ask(session, R"({"op":"field.seek","frame":99})").error == "stale");

        const std::uint64_t here = sandbox->state_hash();
        const auto future = ask(session, R"({"op":"field.branch","steps":30,"channel":0,"value":1,"place":[0,0,16,1],"region":[0,13,16,3]})");
        expect(future["frame"].integer() == 37) << "a placement and thirty steps on";
        expect(future["channels"][0]["sum"].integer() == 20) << "sixteen more grains, all fallen";
        expect(eq(sandbox->state_hash(), here) && eq(sandbox->frame(), std::int64_t{6})) << "the field itself did not move";
        (void)ask(session, R"({"op":"field.place","channel":0,"value":1,"region":[0,0,16,1]})");
        const auto played = ask(session, R"({"op":"field.step","steps":30})");
        expect(played["hash"].text() == future["hash"].text()) << "the same frames, played";
    };
}};
