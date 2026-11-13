// A field of matter as a subject of the agent kernel (D-169): the agent contract's suite on it — what it says
// it has not (time and history: the field's, beside it in the same session) reported so, never passed — and
// the questions an agent asks of matter, through the session it has. Why a cell is alight, back through the
// neighbours that lit it to the hand that applied the heat; why wet wood is not; what happened, by name; what
// would. And the record held to the kernel: a cell's slice steps to the same bits as the field, and what is on
// record of a step is what the reference step says happened in it, cells moving or not.
import std;
import mlibs.agent;
import mlibs.agent.cts;
import mlibs.control;
import mlibs.json;
import openas;
import xengine.matter.json;
import xengine.cellular.matter.agent;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace matter = xengine::matter;
namespace lattice = xengine::cellular;
namespace subject = xengine::cellular::matter_agent;

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

subject::World world_of(lattice::Shape shape, subject::Options options = {}) {
    auto world = subject::World::make(*matter::standard(), shape, "air", std::move(options));
    expect(fatal(world.has_value())) << (world ? "" : world.error());
    return std::move(*world);
}
agent::SubjectSession session_of(subject::World& world, std::string_view actor = "tester") {
    return agent::SubjectSession({world.field.get(), world.matter.get()}, {.actor = {openas::Kind::agent, std::string(actor), {}}});
}

// A strip of wood on stone, lit at its left end: the fire goes along it piece by piece.
void strip(agent::SubjectSession& session) {
    told(session, R"({"op":"matter.place","material":"stone","region":[0,4,12,1]})");
    told(session, R"({"op":"matter.place","material":"wood","region":[2,3,8,1]})");
    told(session, R"({"op":"matter.apply","channel":"temperature","set":700,"region":[2,3,1,1]})");
}

// The matter subject with the world it speaks of: what the contract's probe makes — one subject, alone in a
// session, its field its own.
class Whole final : public agent::Subject {
public:
    explicit Whole(subject::World world) : world_(std::move(world)) {}
    [[nodiscard]] std::string_view name() const noexcept override { return world_.matter->name(); }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return world_.matter->interface_name(); }
    [[nodiscard]] agent::Capabilities capabilities() const noexcept override { return world_.matter->capabilities(); }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return world_.matter->operations(); }
    [[nodiscard]] std::string_view listing() const noexcept override { return world_.matter->listing(); }
    [[nodiscard]] std::uint64_t state_hash() const override { return world_.matter->state_hash(); }
    void operate(std::string_view operation, const agent::Request& request, agent::Answer& answer) override { world_.matter->operate(operation, request, answer); }

private:
    subject::World world_;
};

// A busy scene for the checks against the kernel, each part apart from the others on a floor of stone: a fire
// along a strip of wood, oil alight in a basin, ice beside lava, water on lava, powder set off, sand let go, and
// wet wood that nothing lights.
inline constexpr lattice::Shape wide{28, 14};
inline constexpr int placements = 11, applications = 4;
void busy(agent::SubjectSession& session) {
    for (const std::string_view line : {
             R"({"op":"matter.place","material":"stone","region":[0,13,28,1]})",
             R"({"op":"matter.place","material":"wood","region":[1,12,6,1]})",
             R"({"op":"matter.apply","channel":"temperature","set":800,"region":[1,12,1,1]})",
             R"({"op":"matter.place","material":"stone","region":[8,11,5,2]})",
             R"({"op":"matter.place","material":"oil","region":[9,11,3,1]})",
             R"({"op":"matter.apply","channel":"temperature","set":650,"region":[9,11,1,1]})",
             R"({"op":"matter.place","material":"lava","region":[14,11,4,2]})",
             R"({"op":"matter.place","material":"ice","region":[18,12,2,1]})",
             R"({"op":"matter.place","material":"water","region":[15,10,2,1]})",
             R"({"op":"matter.place","material":"gunpowder","region":[22,11,3,2]})",
             R"({"op":"matter.apply","channel":"temperature","set":900,"region":[22,12,1,1]})",
             R"({"op":"matter.place","material":"sand","region":[20,2,2,2]})",
             R"({"op":"matter.place","material":"wood","region":[26,12,2,1]})",
             R"({"op":"matter.apply","channel":"wetness","set":12,"region":[26,12,2,1]})",
             R"({"op":"matter.place","material":"water","region":[3,2,2,1]})",
         })
        told(session, line);
}

// What the reference step says happened in a step from `input` on a closed grid: every cell an instance, each
// beside the four next to it.
std::vector<matter::Event> reference_events(const matter::Program& program, lattice::Shape shape, std::span<const std::uint32_t> input) {
    const std::size_t area = static_cast<std::size_t>(shape.width) * shape.height;
    std::vector<matter::Pair> pairs;
    for (std::uint32_t y = 0; y < shape.height; ++y)
        for (std::uint32_t x = 0; x < shape.width; ++x) {
            const std::uint32_t cell = y * shape.width + x;
            if (x + 1 < shape.width) pairs.push_back({cell, cell + 1, 1});
            if (y + 1 < shape.height) pairs.push_back({cell, cell + shape.width, 1});
        }
    std::vector<std::uint32_t> output(input.size());
    std::vector<matter::Event> events;
    matter::Workspace workspace;
    const auto stepped = program.step({area, pairs}, input, output, workspace, &events);
    expect(fatal(stepped.has_value()));
    return events;
}

// A record and an event as the same thing said twice: (kind, cell, the law or the channel, from, to).
using Happened = std::tuple<int, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;
std::vector<Happened> of_records(std::span<const subject::Record> records, std::int64_t frame) {
    std::vector<Happened> out;
    for (const subject::Record& record : records) {
        if (record.frame != frame || record.kind == subject::RecordKind::placed || record.kind == subject::RecordKind::applied) continue;
        const bool became = record.kind == subject::RecordKind::became;
        out.emplace_back(became ? 0 : record.kind == subject::RecordKind::rose ? 1 : 2, record.cell, became ? record.law : record.channel, record.from, record.to);
    }
    std::ranges::sort(out);
    return out;
}
std::vector<Happened> of_events(std::span<const matter::Event> events) {
    std::vector<Happened> out;
    for (const matter::Event& event : events) {
        const bool became = event.kind == matter::EventKind::became;
        out.emplace_back(became ? 0 : event.kind == matter::EventKind::rose ? 1 : 2, event.instance, became ? event.law : event.channel, event.from, event.to);
    }
    std::ranges::sort(out);
    return out;
}

std::map<std::string, std::int64_t> counted(mlibs::json::Node events) {
    std::map<std::string, std::int64_t> out;
    for (const auto& [name, count] : events.members()) out[std::string(name)] = count.integer().value_or(-1);
    return out;
}

}  // namespace

static UTest cases{"matter.agent", [] {
    "the agent contract on a field of matter: what it has passes; time and history are the field's, and said so"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> {
                subject::World world = world_of({12, 5});
                {
                    agent::SubjectSession session = session_of(world, "the scene");
                    strip(session);
                    told(session, R"({"op":"field.step","steps":120})");
                }
                return std::make_unique<Whole>(std::move(world));
            },
            .read = R"({"op":"matter.observe"})",
            .change = R"({"op":"matter.place","material":"water","region":[5,0,2,1]})",
            .preview = R"({"op":"matter.branch","steps":60,"place":{"material":"water","region":[2,0,8,2]}})",
            .explain = R"({"op":"matter.explain","cell":[5,0]})",
            .source = R"({"op":"matter.explain","cell":[4,3]})",
            .budgeted = R"({"op":"matter.observe","budget":320})",
            .budget = 320,
        };
        contract::declare(probe, {.subject = "xengine.cellular.matter.agent"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.undo.returns", "agent.time.replays"}) << std::format("{}", unsupported);
    };

    "why a cell is alight: the law that held and what it read, the neighbour that gave the heat and why it could, back to the hand that applied it"_test = [] {
        subject::World world = world_of({12, 5});
        agent::SubjectSession session = session_of(world, "the one who lit it");
        strip(session);
        told(session, R"({"op":"field.step","steps":120})");
        const Answered why = told(session, R"({"op":"matter.explain","cell":[4,3],"depth":3})");
        std::println("{}", why.text);
        expect(why["now"]["material"].text() == "wood" && why["now"]["burning"].number() == 1.0);
        const auto ignition = why["records"][0];
        expect(ignition["what"].text() == "ignite" && ignition["channel"].text() == "burning") << "the latest thing on record of it: it caught";
        expect(ignition["law"]["name"].text() == "burns" && ignition["law"]["at"].text() == "/laws/8") << "by a law, named and addressed";
        expect(ignition["law"]["when"].text() == "temperature >= ignition && burned < fuel && near.oxygen > 0");
        const double read = ignition["read"]["temperature"].number().value_or(0), needed = ignition["read"]["ignition"].number().value_or(0);
        expect(eq(needed, 570.0) && read >= 570.0 && read < 580.0) << std::format("what the law read, in units: {} K against {} K", read, needed);
        expect(ignition["read"]["near.oxygen"].number() == 1.0) << "the air above it";
        // Who gave it: the piece before it, alight; and that one from the one before, which the agent heated.
        const auto giver = ignition["gave"][0];
        expect(giver["cell"][0].integer() == 3 && giver["cell"][1].integer() == 3 && giver["what"].text() == "temperature" && giver["was"].text() == "wood");
        expect(giver["states"][0].text() == "burning" && giver["amount"].number().value_or(0) > 1.0) << "heat flowed from a neighbour that was burning";
        const auto before = giver["because"];
        expect(before["what"].text() == "ignite" && before["step"].integer().value_or(0) < ignition["step"].integer().value_or(0)) << "which had caught earlier";
        const auto first = before["gave"][0]["because"];
        expect(before["gave"][0]["cell"][0].integer() == 2 && first["what"].text() == "ignite" && first["step"].integer() == 1);
        expect(first["read"]["temperature"].number() == 700.0);
        expect(first["applied"]["what"].text() == "applied" && first["applied"]["channel"].text() == "temperature" && first["applied"]["set"].number() == 700.0);
        expect(first["applied"]["by"]["name"].text() == "the one who lit it" && first["applied"]["by"]["kind"].text() == "agent") << "the chain ends at who did it";
        // The air above gave the oxygen; nothing on record of it, so nothing more is said.
        const auto air = ignition["gave"][1];
        expect(air["what"].text() == "near.oxygen" && air["was"].text() == "air" && !air["because"]);
        // Its own placing is on record too: the edit whose region holds it.
        const auto placed = why["records"][1];
        expect(placed["what"].text() == "placed" && placed["material"].text() == "wood" && placed["by"]["name"].text() == "the one who lit it" && !placed["cell"]);
        // Depth 0: the record alone.
        const Answered shallow = told(session, R"({"op":"matter.explain","cell":[4,3],"depth":0})");
        expect(shallow["records"][0]["gave"][0]["cell"][0].integer() == 3 && !shallow["records"][0]["gave"][0]["because"]);
        // Within a budget: fewer records, and how many more there are.
        const Answered within = told(session, R"({"op":"matter.explain","cell":[4,3],"budget":900})");
        expect(within.text.size() <= 900u + 40u && within["records"].size() + static_cast<std::size_t>(within["more"].integer().value_or(0)) == 2u) << within.text;
    };

    "why not: a law judged for a cell as it is — wet wood at 600 K is not alight, the comparison that fails and what it read; its properties as its state leaves them"_test = [] {
        subject::World world = world_of({9, 4});
        agent::SubjectSession session = session_of(world);
        told(session, R"({"op":"matter.place","material":"stone","region":[0,3,9,1]})");
        told(session, R"({"op":"matter.place","material":"wood","region":[2,2,1,1]})");
        told(session, R"({"op":"matter.place","material":"wood","region":[6,2,1,1]})");
        told(session, R"({"op":"matter.apply","channel":"wetness","set":10,"region":[6,2,1,1]})");
        told(session, R"({"op":"matter.apply","channel":"temperature","set":600,"region":[2,2,1,1]})");
        told(session, R"({"op":"matter.apply","channel":"temperature","set":600,"region":[6,2,1,1]})");
        const Answered wet = told(session, R"({"op":"matter.explain","cell":[6,2],"law":"burns"})");
        std::println("{}", wet.text);
        expect(wet["judged"]["holds"].boolean() == false && wet["judged"]["fails"].text() == "temperature >= ignition") << "the comparison that fails, as the law writes it";
        expect(wet["judged"]["read"]["temperature"].number() == 600.0 && wet["judged"]["read"]["ignition"].number() == 970.0) << "each of wetness is 40 K more to light";
        const Answered dry = told(session, R"({"op":"matter.explain","cell":[2,2],"law":"burns"})");
        expect(dry["judged"]["holds"].boolean() == true && dry["judged"]["read"]["ignition"].number() == 570.0 && !dry["judged"]["fails"]);
        const Answered at = told(session, R"({"op":"matter.at","cell":[6,2]})");
        std::println("{}", at.text);
        expect(at["material"].text() == "wood" && at["phase"].text() == "solid" && at["channels"]["wetness"].number() == 10.0);
        expect(at["adjusted"]["ignition"]["written"].number() == 570.0 && at["adjusted"]["ignition"]["now"].number() == 970.0) << "what its wetness makes of its ignition point";
        expect(told(session, R"({"op":"matter.at","cell":[2,2]})")["adjusted"].size() == 0u);
        told(session, R"({"op":"field.step","steps":3})");
        expect(told(session, R"({"op":"matter.at","cell":[2,2]})")["channels"]["burning"].number() == 1.0 && told(session, R"({"op":"matter.at","cell":[6,2]})")["channels"]["burning"].number() == 0.0)
            << "and so it went: the dry piece burns, the wet one does not";
    };

    "what happened, by name: the events of a busy field as a stream — since a frame, of one name, in a region, the latest within a budget"_test = [] {
        subject::World world = world_of(wide);
        agent::SubjectSession session = session_of(world);
        busy(session);
        told(session, R"({"op":"field.step","steps":400})");
        const Answered all = told(session, R"({"op":"matter.events"})");
        std::map<std::string, int> names;
        for (std::size_t index = 0; index < all["events"].size(); ++index) ++names[std::string(all["events"][index]["what"].text().value_or("?"))];
        std::println("events of 400 steps: {}", names);
        expect(names["placed"] == placements && names["applied"] == applications) << "every edit";
        expect(names["ignite"] > 0 && names["melt"] > 0 && names["boil"] > 0 && names["burn_out"] > 0) << "and what the laws did, each by the name its law or channel gives it";
        expect(eq(all["more"].integer().value_or(-1), std::int64_t{0}) && eq(static_cast<std::size_t>(all["kept"].integer().value_or(0)), all["events"].size()));
        std::int64_t last = -1;
        for (std::size_t index = 0; index < all["events"].size(); ++index) {
            const std::int64_t frame = all["events"][index]["frame"].integer().value_or(-1);
            expect(frame >= last) << "oldest first";
            last = frame;
        }
        const Answered melts = told(session, R"({"op":"matter.events","name":"melt"})");
        expect(eq(static_cast<int>(melts["events"].size()), names["melt"]));
        const auto melt = melts["events"][0];
        std::println("{}", melts.text.substr(0, 700));
        expect(melt["from"].text() == "ice" && melt["to"].text() == "water" && melt["law"]["name"].text() == "melts") << "a conversion: what became what, by which law";
        const Answered late = told(session, R"({"op":"matter.events","since":200})");
        expect(late["events"].size() > 0u && late["events"].size() < all["events"].size() && late["events"][0]["frame"].integer().value_or(0) > 200);
        const Answered there = told(session, R"({"op":"matter.events","name":"ignite","region":[0,10,8,3]})");
        expect(there["events"].size() > 0u);
        for (std::size_t index = 0; index < there["events"].size(); ++index)
            expect(there["events"][index]["cell"][0].integer().value_or(99) < 8 && there["events"][index]["cell"][1].integer().value_or(0) >= 10);
        const Answered few = told(session, R"({"op":"matter.events","budget":1500})");
        expect(few.text.size() <= 1500u + 40u && few["events"].size() > 0u && few["events"].size() + static_cast<std::size_t>(few["more"].integer().value_or(0)) == all["events"].size());
        expect(few["events"][few["events"].size() - 1]["frame"].integer() == all["events"][all["events"].size() - 1]["frame"].integer()) << "within a budget the latest are kept";
    };

    "what would be: a branch run ahead says what would become of what and what would happen; the field is untouched; then it happens so"_test = [] {
        subject::World world = world_of(wide);
        agent::SubjectSession session = session_of(world);
        busy(session);
        told(session, R"({"op":"field.step","steps":40})");
        const std::uint64_t before = world.field->state_hash();
        const std::size_t on_record = world.matter->records().size();
        const std::int64_t frame = world.field->frame();
        const Answered ahead = told(session, R"({"op":"matter.branch","steps":300})");
        std::println("{}", ahead.text.substr(0, 600));
        expect(eq(world.field->state_hash(), before) && eq(world.matter->records().size(), on_record) && eq(world.field->frame(), frame)) << "a branch touches nothing";
        const auto would = counted(ahead["events"]);
        expect(would.size() >= 3u && ahead["materials"]["oil"]["now"].integer() == 3 && ahead["materials"]["oil"]["would"].integer() == 0) << "the oil alight in its basin would be gone";
        const std::string hash(ahead["hash"].text().value_or(""));
        const Answered stepped = told(session, R"({"op":"field.step","steps":300})");
        expect(stepped["hash"].text() == hash) << "the same state, when it is stepped";
        std::map<std::string, std::int64_t> did;
        const Answered since = told(session, std::format(R"({{"op":"matter.events","since":{}}})", frame));
        for (std::size_t index = 0; index < since["events"].size(); ++index) ++did[std::string(since["events"][index]["what"].text().value_or("?"))];
        expect(did == would) << std::format("what would happen is what happened: {} / {}", would, did);
        // With edits first, in the branch alone: a row of water let go at the top and everything made 50 K colder —
        // 243 K: the water would freeze where it is, every cell of it. Counted in the region asked.
        const Answered cold = told(session, R"({"op":"matter.branch","steps":50,"place":{"material":"water","region":[0,0,28,1]},"apply":{"channel":"temperature","take":50,"region":[0,0,28,14]},"region":[0,0,28,1]})");
        std::int64_t now = 0, then = 0;
        for (const auto& [name, count] : cold["materials"].members()) now += count["now"].integer().value_or(0), then += count["would"].integer().value_or(0);
        expect(cold["region"][3].integer() == 1 && eq(now, std::int64_t{28}) && eq(then, std::int64_t{28}) && !cold["materials"]["water"]) << cold.text.substr(0, 400);
        expect(cold["events"]["freeze"].integer() == 28 && cold["first"][0]["what"].text() == "freeze" && cold["first"][0]["read"]["temperature"].number() == 243.0) << cold.text.substr(0, 400);
        expect(eq(world.field->frame(), frame + 300));
    };

    "the record is the kernel's: a cell's slice steps to the field's bits, and what is on record of a step is what the reference step says happened — closed, open to a bath, and with cells moving"_test = [] {
        // Cells that stay where they are: every bit of every cell, every step.
        for (const bool open : {false, true}) {
            const matter::Program standard = *matter::standard();
            std::vector<std::uint32_t> outside = standard.fresh(1, *standard.material("air"));
            subject::Options options;
            options.lowering.moves = false;
            if (open) options.field.boundary = lattice::open_to(outside);
            subject::World world = world_of(wide, options);
            agent::SubjectSession session = session_of(world);
            busy(session);
            const std::size_t area = static_cast<std::size_t>(wide.width) * wide.height, channels = world.matter->program().channels().size();
            std::size_t compared = 0, happened = 0;
            for (int step = 0; step < 160; ++step) {
                const std::vector<std::uint32_t> input(world.field->state().cells.begin(), world.field->state().cells.end());
                world.field->step();
                const auto& now = world.field->state().cells;
                bool same = true;
                for (std::uint32_t cell = 0; cell < area && same; ++cell) {
                    const matter::Why why = world.matter->why(input, cell);
                    for (std::size_t channel = 0; channel < channels && same; ++channel, ++compared) same = why.after[channel] == now[channel * area + cell];
                    if (!same) expect(false) << std::format("{}: step {}, cell [{},{}] differs", open ? "open" : "closed", step, cell % wide.width, cell / wide.width);
                }
                if (!same) break;
                if (!open) {
                    const auto on_record = of_records(world.matter->records(), world.field->frame()), by_reference = of_events(reference_events(world.matter->program(), wide, input));
                    expect(on_record == by_reference) << std::format("step {}: {} on record, {} by the reference", step, on_record.size(), by_reference.size());
                    happened += by_reference.size();
                }
            }
            std::println("{}: {} values of 160 steps the same from a cell's slice as from the field{}", open ? "open to air" : "closed", compared,
                         open ? std::string() : std::format("; {} events, each on record", happened));
            expect(compared == 160u * area * channels);
            if (!open) expect(happened > 20u);
        }
        // Cells that move: what happened in a step is on record where it happened, though the cell has gone on.
        subject::World world = world_of(wide);
        agent::SubjectSession session = session_of(world);
        busy(session);
        std::size_t happened = 0, moved = 0;
        for (int step = 0; step < 400; ++step) {
            const std::vector<std::uint32_t> input(world.field->state().cells.begin(), world.field->state().cells.end());
            world.field->step();
            const auto on_record = of_records(world.matter->records(), world.field->frame()), by_reference = of_events(reference_events(world.matter->program(), wide, input));
            expect(on_record == by_reference) << std::format("step {}: {} on record, {} by the reference", step, on_record.size(), by_reference.size());
            happened += by_reference.size();
            for (const auto& [kind, cell, what, from, to] : by_reference)
                if (kind == 0 && world.field->state().cells[cell] != to) ++moved;
        }
        std::println("cells moving: {} events of 400 steps, each on record; {} of them of a cell that changed places in the step it became something else", happened, moved);
        expect(happened > 40u && moved > 0u);
    };

    "the record follows the field's time: a seek drops what came after, and played again the same is on record; it keeps its last so many"_test = [] {
        subject::World world = world_of(wide);
        agent::SubjectSession session = session_of(world);
        busy(session);
        told(session, R"({"op":"field.step","steps":100})");
        const std::int64_t frame = world.field->frame();
        const std::size_t then = world.matter->records().size();
        told(session, R"({"op":"field.step","steps":120})");
        const std::string first = told(session, std::format(R"({{"op":"matter.events","since":{}}})", frame)).text;
        expect(world.matter->records().size() > then);
        told(session, std::format(R"({{"op":"field.seek","frame":{}}})", frame));
        expect(eq(world.matter->records().size(), then)) << "sought back: what came after is not on record";
        expect(std::ranges::all_of(world.matter->records(), [&](const subject::Record& record) { return record.frame <= frame; }));
        told(session, R"({"op":"field.step","steps":120})");
        expect(told(session, std::format(R"({{"op":"matter.events","since":{}}})", frame)).text == first) << "played again: the same record, byte for byte";

        subject::World small = world_of(wide, {.records_kept = 24});
        agent::SubjectSession other = session_of(small);
        busy(other);
        told(other, R"({"op":"field.step","steps":400})");
        expect(small.matter->records().size() <= 24u && small.matter->records().back().frame > 100) << "the last so many, the latest among them";
    };

    "the same edits from code: a host places a material and applies a quantity, each a frame on record under its own name"_test = [] {
        subject::World world = world_of({12, 5});
        const matter::Program& program = world.matter->program();
        const openas::Actor game{openas::Kind::service, "the game", {}};
        expect(world.matter->place(*program.material("stone"), {0, 4, 12, 1}) && world.matter->place(*program.material("wood"), {2, 3, 8, 1}, game));
        expect(world.matter->apply(*program.channel("temperature"), lattice::agent::EditKind::set, 700 * 256, {2, 3, 1, 1}, game));
        expect(eq(world.field->frame(), std::int64_t{3}) && eq(world.matter->records().size(), std::size_t{3}));
        expect(!world.matter->place(matter::none, {0, 0, 1, 1}) && !world.matter->place(0, {11, 0, 2, 1}) && !world.matter->apply(0, lattice::agent::EditKind::set, 1, {0, 0, 1, 1}) &&
               !world.matter->apply(99, lattice::agent::EditKind::add, 1, {0, 0, 1, 1}) && !world.matter->apply(1, lattice::agent::EditKind::step, 1, {0, 0, 1, 1}))
            << "what is not there is refused";
        expect(eq(world.field->frame(), std::int64_t{3}));
        for (int step = 0; step < 120; ++step) world.field->step();
        agent::SubjectSession session = session_of(world);
        const Answered why = told(session, R"({"op":"matter.explain","cell":[3,3],"depth":2})");
        const auto first = why["records"][0]["gave"][0]["because"];
        expect(why["records"][0]["what"].text() == "ignite" && first["applied"]["by"]["name"].text() == "the game" && first["applied"]["by"]["kind"].text() == "service") << why.text;
        expect(why["records"][1]["what"].text() == "placed" && why["records"][1]["by"]["name"].text() == "the game");
        // The floor was the owner's: an edit with no one named.
        expect(told(session, R"({"op":"matter.explain","cell":[0,4]})")["records"][0]["by"]["kind"].text() == openas::name(lattice::agent::Field::owner().kind));
    };

    "the emergence set asked why (the plan's §9.3): for each phenomenon the record names the law expected and the neighbour whose part it was"_test = [] {
        // Every event of a name, and whether one of them is as asked.
        const auto events = [](agent::SubjectSession& session, std::string_view name) { return told(session, std::format(R"({{"op":"matter.events","name":"{}"}})", name)); };
        const auto any = [](const Answered& answer, auto&& holds) {
            for (std::size_t index = 0; index < answer["events"].size(); ++index)
                if (holds(answer["events"][index])) return true;
            return false;
        };
        // A neighbour's part: one of `key` ("gave", "took", "beside") that was `material`, in `state` when one is named.
        const auto part = [](mlibs::json::Node event, std::string_view key, std::string_view material, std::string_view state = {}) {
            for (std::size_t index = 0; index < event[key].size(); ++index) {
                const auto one = event[key][index];
                if (one["was"].text() != material) continue;
                if (state.empty()) return true;
                for (std::size_t named = 0; named < one["states"].size(); ++named)
                    if (one["states"][named].text() == state) return true;
            }
            return false;
        };
        const auto count = [](const Answered& answer) { return answer["events"].size(); };

        {  // 1 — sand falls, water finds its level: by density and phase alone. No law acts, and the record says so.
            subject::World world = world_of({16, 14});
            agent::SubjectSession session = session_of(world);
            told(session, R"({"op":"matter.place","material":"stone","region":[0,13,16,1]})");
            told(session, R"({"op":"matter.place","material":"water","region":[0,9,16,4]})");
            told(session, R"({"op":"matter.place","material":"sand","region":[6,0,3,3]})");
            told(session, R"({"op":"field.step","steps":200})");
            const Answered all = told(session, R"({"op":"matter.events"})");
            expect(eq(count(all), std::size_t{3}) && told(session, R"({"op":"matter.at","cell":[7,12]})")["material"].text() == "sand") << "the sand is at the bottom, and nothing is on record but the three placings";
        }
        {  // 2 is the case above (why a cell is alight). 3 — water puts a fire out and becomes steam.
            subject::World world = world_of({12, 8});
            agent::SubjectSession session = session_of(world, "the one with the bucket");
            told(session, R"({"op":"matter.place","material":"stone","region":[0,7,12,1]})");
            told(session, R"({"op":"matter.place","material":"wood","region":[3,6,6,1]})");
            told(session, R"({"op":"matter.apply","channel":"temperature","set":800,"region":[3,6,6,1]})");
            told(session, R"({"op":"field.step","steps":30})");
            const std::int64_t poured = told(session, R"({"op":"matter.place","material":"water","region":[3,4,6,2]})")["frame"].integer().value_or(0);
            told(session, R"({"op":"field.step","steps":300})");
            const Answered out = told(session, std::format(R"({{"op":"matter.events","name":"go_out","since":{}}})", poured));
            expect(any(out, [&](mlibs::json::Node event) {
                return event["law"]["name"].text() == "burns" && event["failed"].text() == "near.oxygen > 0" && event["read"]["near.oxygen"].number() == 0.0 && part(event, "beside", "water");
            })) << "3: a piece went out for want of air, water beside it: " << out.text.substr(0, 900);
            // Asked of the cell: the water beside it was placed, and by whom.
            const auto first = out["events"][0]["cell"];
            const Answered why = told(session, std::format(R"({{"op":"matter.explain","cell":[{},{}],"depth":1}})", first[0].integer().value_or(0), first[1].integer().value_or(0)));
            bool traced = false;
            for (std::size_t index = 0; index < why["records"].size() && !traced; ++index)
                for (std::size_t beside = 0; beside < why["records"][index]["beside"].size() && !traced; ++beside) {
                    const auto cause = why["records"][index]["beside"][beside]["because"];
                    traced = cause["what"].text() == "placed" && cause["material"].text() == "water" && cause["by"]["name"].text() == "the one with the bucket";
                }
            expect(traced) << "3: … back to who poured it: " << why.text.substr(0, 1200);
            const Answered boiled = events(session, "boil");
            expect(any(boiled, [&](mlibs::json::Node event) { return event["law"]["name"].text() == "boils" && event["from"].text() == "water" && event["to"].text() == "steam"; })) << "3: and water became steam";
        }
        {  // 4 — lava meets water: the water boils, the lava sets as stone, its heat taken by what is beside it.
            subject::World world = world_of({12, 12});
            agent::SubjectSession session = session_of(world);
            told(session, R"({"op":"matter.place","material":"stone","region":[0,11,12,1]})");
            told(session, R"({"op":"matter.place","material":"stone","region":[0,4,1,7]})");
            told(session, R"({"op":"matter.place","material":"stone","region":[11,4,1,7]})");
            told(session, R"({"op":"matter.place","material":"lava","region":[1,9,10,2]})");
            told(session, R"({"op":"matter.place","material":"water","region":[1,6,10,3]})");
            told(session, R"({"op":"field.step","steps":600})");
            const Answered set = events(session, "freeze"), boiled = events(session, "boil");
            expect(eq(count(set), std::size_t{20})) << "4: every cell of lava set";
            expect(any(set, [&](mlibs::json::Node event) { return event["law"]["name"].text() == "freezes" && event["from"].text() == "lava" && event["to"].text() == "stone" && part(event, "took", "water"); }))
                << "4: lava set as stone, its heat taken by the water: " << set.text.substr(0, 900);
            expect(count(boiled) > 10u && any(boiled, [&](mlibs::json::Node event) { return event["law"]["name"].text() == "boils" && event["read"]["temperature"].number().value_or(0) >= 373.0; }))
                << "4: and the water boiled";
        }
        {  // 5 — ice by a fire melts; water in the cold freezes — the cold an actor's doing, and named.
            subject::World world = world_of({14, 6});
            agent::SubjectSession session = session_of(world, "winter");
            told(session, R"({"op":"matter.place","material":"stone","region":[0,5,14,1]})");
            told(session, R"({"op":"matter.place","material":"wood","region":[2,4,3,1]})");
            told(session, R"({"op":"matter.apply","channel":"temperature","set":800,"region":[2,4,3,1]})");
            told(session, R"({"op":"matter.place","material":"ice","region":[5,4,4,1]})");
            told(session, R"({"op":"field.step","steps":200})");
            const Answered melted = events(session, "melt");
            expect(any(melted, [&](mlibs::json::Node event) {
                return event["cell"][0].integer() == 5 && event["law"]["name"].text() == "melts" && event["from"].text() == "ice" && event["to"].text() == "water" && part(event, "gave", "wood", "burning");
            })) << "5: the ice next to the fire melted by the heat the burning wood gave: " << melted.text.substr(0, 900);
            told(session, R"({"op":"matter.place","material":"water","region":[11,4,2,1]})");
            told(session, R"({"op":"matter.apply","channel":"temperature","set":250,"region":[11,4,2,1]})");
            told(session, R"({"op":"field.step","steps":1})");
            const Answered cold = told(session, R"({"op":"matter.explain","cell":[11,4]})");
            const auto froze = cold["records"][0];
            expect(froze["what"].text() == "freeze" && froze["law"]["name"].text() == "freezes" && froze["to"].text() == "ice" && froze["read"]["temperature"].number() == 250.0) << cold.text.substr(0, 900);
            expect(froze["applied"]["channel"].text() == "temperature" && froze["applied"]["set"].number() == 250.0 && froze["applied"]["by"]["name"].text() == "winter") << "5: … made cold by an actor, who is named";
        }
        {  // 6 — oil afloat catches and burns on the water, cell from cell; the water under it boils by its heat.
            subject::World world = world_of({14, 8});
            agent::SubjectSession session = session_of(world);
            told(session, R"({"op":"matter.place","material":"stone","region":[0,7,14,1]})");
            told(session, R"({"op":"matter.place","material":"stone","region":[0,3,1,4]})");
            told(session, R"({"op":"matter.place","material":"stone","region":[13,3,1,4]})");
            told(session, R"({"op":"matter.place","material":"water","region":[1,5,12,2]})");
            told(session, R"({"op":"matter.place","material":"oil","region":[1,4,12,1]})");
            told(session, R"({"op":"matter.apply","channel":"temperature","set":700,"region":[1,4,1,1]})");
            told(session, R"({"op":"field.step","steps":900})");
            const Answered caught = events(session, "ignite"), gone = events(session, "burn_out"), boiled = events(session, "boil");
            expect(any(caught, [&](mlibs::json::Node event) { return event["law"]["name"].text() == "burns" && event["read"]["ignition"].number() == 500.0 && part(event, "gave", "oil", "burning"); }))
                << "6: oil caught from oil alight: " << caught.text.substr(0, 900);
            expect(eq(count(gone), std::size_t{12}) && any(gone, [&](mlibs::json::Node event) { return event["law"]["name"].text() == "burns out" && event["from"].text() == "oil" && event["to"].text() == "smoke"; }))
                << "6: every cell of it burnt to smoke";
            expect(any(boiled, [&](mlibs::json::Node event) { return part(event, "gave", "oil", "burning"); })) << "6: and water under it boiled by the heat burning oil gave";
        }
        {  // 7 — metal carries heat: the wood at the far end of an iron bar catches, and it was the iron that gave it.
            subject::World world = world_of({12, 6});
            const matter::Program& program = world.matter->program();
            const openas::Actor forge{openas::Kind::program, "the forge", {}};
            expect(world.matter->place(*program.material("stone"), {0, 5, 12, 1}) && world.matter->place(*program.material("iron"), {2, 4, 6, 1}) && world.matter->place(*program.material("wood"), {8, 4, 1, 1}));
            int caught = -1;
            for (int step = 0; step < 2500 && caught < 0; ++step) {
                world.matter->apply(*program.channel("temperature"), lattice::agent::EditKind::set, 1250 * 256, {2, 4, 1, 1}, forge);
                world.field->step();
                if (world.field->state().cells[*program.channel("burning") * 72 + 4 * 12 + 8]) caught = step + 1;
            }
            agent::SubjectSession session = session_of(world);
            const Answered why = told(session, R"({"op":"matter.explain","cell":[8,4],"depth":0})");
            const auto ignition = why["records"][0];
            expect(caught > 0 && ignition["what"].text() == "ignite" && ignition["gave"][0]["cell"][0].integer() == 7 && ignition["gave"][0]["was"].text() == "iron" && ignition["gave"][0]["what"].text() == "temperature")
                << "7: caught at " << caught << ": " << why.text.substr(0, 900);
        }
        {  // 8 — gunpowder goes off: each grain from the last, three ticks apart, gone to smoke.
            subject::World world = world_of({16, 6});
            agent::SubjectSession session = session_of(world);
            told(session, R"({"op":"matter.place","material":"stone","region":[0,5,16,1]})");
            told(session, R"({"op":"matter.place","material":"gunpowder","region":[3,4,10,1]})");
            told(session, R"({"op":"matter.apply","channel":"temperature","set":900,"region":[3,4,1,1]})");
            told(session, R"({"op":"field.step","steps":80})");
            const Answered caught = events(session, "ignite"), gone = events(session, "burn_out");
            expect(eq(count(caught), std::size_t{10}) && eq(count(gone), std::size_t{10}));
            expect(caught["events"][9]["step"].integer().value_or(999) < 40) << "8: the whole trail in ticks, where wood takes hundreds";
            expect(any(caught, [&](mlibs::json::Node event) { return part(event, "gave", "gunpowder", "burning") && event["read"]["ignition"].number() == 480.0; })) << caught.text.substr(0, 900);
            expect(any(gone, [&](mlibs::json::Node event) { return event["from"].text() == "gunpowder" && event["to"].text() == "smoke" && event["law"]["name"].text() == "burns out"; }));
            // The blast: pressure, by a law that holds while a thing with a blast burns — judged for a cell, it does not hold now.
            expect(told(session, R"({"op":"matter.observe"})")["channels"][6]["name"].text() == "pressure" && told(session, R"({"op":"matter.observe"})")["channels"][6]["most"].number().value_or(0) > 100.0);
        }
        {  // 12 — a material that only says its properties: alcohol, two rows and no law. The same laws act on it, by name.
            auto definition = matter::read(matter::standard_snapshot, "standard.matter.omui.json");
            expect(fatal(definition.has_value()));
            using P = matter::Property;
            definition->materials.push_back({"alcohol_vapour",
                                             {P{"phase", "gas"}, P{"density", 1.0}, P{"thermal", 1.0}, P{"boiling", 351.0}, P{"condensed", "alcohol"}, P{"starts_at", 360.0}, P{"ignition", 400.0},
                                              P{"fuel", 40.0}, P{"heat", 90.0}, P{"burnt", "smoke"}},
                                             {}});
            definition->materials.push_back({"alcohol", {P{"phase", "liquid"}, P{"density", 790.0}, P{"thermal", 1.0}, P{"boiling", 351.0}, P{"boiled", "alcohol_vapour"}}, {}});
            auto program = matter::Program::prepare(*definition);
            expect(fatal(program.has_value()));
            auto made = subject::World::make(*program, {8, 10}, "air");
            expect(fatal(made.has_value()));
            agent::SubjectSession session = session_of(*made);
            told(session, R"({"op":"matter.place","material":"stone","region":[0,9,8,1]})");
            told(session, R"({"op":"matter.place","material":"alcohol","region":[2,8,4,1]})");
            told(session, R"({"op":"matter.apply","channel":"temperature","set":700,"region":[0,9,8,1]})");
            told(session, R"({"op":"field.step","steps":400})");
            const Answered boiled = events(session, "boil"), caught = events(session, "ignite");
            expect(any(boiled, [&](mlibs::json::Node event) { return event["from"].text() == "alcohol" && event["to"].text() == "alcohol_vapour" && event["law"]["name"].text() == "boils" && part(event, "gave", "stone"); }))
                << "12: it boils by the law water boils by, the hot stone giving the heat: " << boiled.text.substr(0, 700);
            expect(told(session, R"({"op":"matter.describe"})")["recipes"].integer() == 0 && told(session, R"({"op":"matter.describe"})")["laws"].size() == 22u) << "12: and no law was added for it";
            std::println("12: alcohol over a stone at 700 K — {} boilings in 400 steps (its vapour condenses under the cold lid and comes back), {} of its vapour caught", count(boiled), count(caught));
        }
    };

    "the engine's verbs as an agent's operations: an explosion by a wall is rubble by the law, and asked why, the sand names the law and the one who exploded it; a pour fills only gas; a heat lights wood"_test = [] {
        subject::World world = world_of({24, 16});
        auto session = session_of(world, "builder");
        told(session, R"({"op":"matter.place","material":"stone","region":[0,14,24,2]})");
        told(session, R"({"op":"matter.place","material":"stone","region":[12,6,2,8]})");
        told(session, R"({"op":"matter.explode","cell":[10,11],"radius":4,"pressure":600000,"heat":900})");
        told(session, R"({"op":"field.step","steps":3})");
        const auto shattered = told(session, R"({"op":"matter.events","name":"shatter"})");
        expect(gt(shattered["events"].size(), std::size_t{4})) << shattered.text;
        const auto first = shattered["events"][0];
        const auto why = told(session, std::format(R"({{"op":"matter.explain","cell":[{},{}],"depth":2}})", first["cell"][0].integer().value_or(0), first["cell"][1].integer().value_or(0)));
        expect(why.text.contains(R"("what":"shatter","from":"stone","to":"sand")") && why.text.contains(R"("law":{"name":"shatters")") && why.text.contains(R"("name":"builder")")) << why.text;
        // A pour: only where there is gas — the floor under it is the floor.
        const auto poured = told(session, R"({"op":"matter.pour","material":"water","cell":[4,13],"radius":2,"reach":3})");
        expect(poured["filled"].integer() == 13) << poured.text;
        expect(told(session, R"({"op":"matter.at","cell":[4,14]})")["material"].text() == "stone");
        expect(told(session, R"({"op":"matter.pour","material":"water","cell":[4,13],"radius":2,"reach":3})")["filled"].integer() == 0) << "where it poured there is no gas left";
        // A heat.
        told(session, R"({"op":"matter.place","material":"wood","region":[18,12,3,2]})");
        told(session, R"({"op":"matter.heat","cell":[19,12],"radius":1,"kelvin":900})");
        told(session, R"({"op":"field.step","steps":2})");
        expect(gt(told(session, R"({"op":"matter.events","name":"ignite"})")["events"].size(), std::size_t{0}));
        // Ill said.
        const std::array<std::pair<std::string_view, std::string_view>, 5> refused{{
            {R"({"op":"matter.explode","cell":[10,11],"radius":0,"pressure":5})", "`radius` is 1 … 64"},
            {R"({"op":"matter.explode","cell":[10,11],"radius":3})", "an explosion is a `pressure`, a `heat`, or both"},
            {R"({"op":"matter.heat","cell":[10,11],"radius":2})", "`kelvin` is a temperature"},
            {R"({"op":"matter.pour","material":"cheese","cell":[4,13],"radius":2})", "`material` is one of"},
            {R"({"op":"matter.pour","material":"water","cell":[99,13],"radius":2})", "`cell` is [x, y]"},
        }};
        for (const auto& [line, reason] : refused) {
            const Answered answer = ask(session, line);
            expect(answer.error == openas::error_code::invalid_argument && answer.message.contains(reason)) << line << " → " << answer.text;
        }
    };

    "what it refuses, and says why: a material or a channel it has not, an amount out of its range, a cell outside, a law that is not judged, a budget too small"_test = [] {
        subject::World world = world_of({12, 5});
        agent::SubjectSession session = session_of(world);
        strip(session);
        const std::uint64_t before = world.field->state_hash();
        const std::int64_t frame = world.field->frame();
        const std::vector<std::pair<std::string_view, std::string_view>> refused{
            {R"({"op":"matter.place","material":"cheese","region":[0,0,1,1]})", "`material` is one of `air`"},
            {R"({"op":"matter.place","material":"wood","region":[10,0,5,1]})", "region"},
            {R"({"op":"matter.place","material":"wood"})", "region"},
            {R"({"op":"matter.apply","channel":"mood","set":1,"region":[0,0,1,1]})", "`channel` is one of `temperature`"},
            {R"({"op":"matter.apply","channel":"material","set":1,"region":[0,0,1,1]})", "placed (matter.place), not applied"},
            {R"({"op":"matter.apply","channel":"temperature","region":[0,0,1,1]})", "one of `set`, `add`, `take`"},
            {R"({"op":"matter.apply","channel":"temperature","set":300,"add":5,"region":[0,0,1,1]})", "one of `set`, `add`, `take`"},
            {R"({"op":"matter.apply","channel":"temperature","set":-4,"region":[0,0,1,1]})", "an amount of `temperature` is 0 …"},
            {R"({"op":"matter.apply","channel":"temperature","add":1e9,"region":[0,0,1,1]})", "K"},
            {R"({"op":"matter.at","cell":[12,0]})", "`cell` is [x, y] within the field (12 × 5)"},
            {R"({"op":"matter.explain","cell":[1]})", "`cell` is [x, y]"},
            {R"({"op":"matter.explain","cell":[3,3],"depth":7})", "`depth` is 0 … 6"},
            {R"({"op":"matter.explain","cell":[3,3],"law":"heat flows"})", "a transfer is not judged"},
            {R"({"op":"matter.explain","cell":[3,3],"law":"levity"})", "`law` is one of"},
            {R"({"op":"matter.explain","cell":[3,3],"law":"gravity"})", "nor a law of motion"},
            {R"({"op":"matter.explain","cell":[3,3],"budget":20})", "the least this answers"},
            {R"({"op":"matter.observe","budget":40})", "the least this answers"},
            {R"({"op":"matter.describe","material":"cheese"})", "`material` is one of"},
            {R"({"op":"matter.describe","law":"levity"})", "`law` is one of"},
            {R"({"op":"matter.branch","steps":0})", "`steps` is 1 … 4096"},
            {R"({"op":"matter.branch","steps":5,"place":{"material":"cheese","region":[0,0,1,1]}})", "`place`: `material` is one of"},
            {R"({"op":"matter.branch","steps":5,"apply":{"channel":"temperature","region":[0,0,1,1]}})", "`apply`: one of `set`, `add`, `take`"},
        };
        for (const auto& [line, why] : refused) {
            const Answered answer = ask(session, line);
            expect(answer.error == openas::error_code::invalid_argument && answer.message.contains(why)) << line << " → " << answer.text;
        }
        expect(eq(world.field->state_hash(), before) && eq(world.field->frame(), frame)) << "none of them touched the field";
        std::println("{} requests refused, each with its reason", refused.size());
    };

    "what it is made of, said for an agent: the definition's names, one material's properties, one law as written; a region by material and in units"_test = [] {
        subject::World world = world_of({12, 5});
        agent::SubjectSession session = session_of(world);
        strip(session);
        const Answered all = told(session, R"({"op":"matter.describe"})");
        expect(all["materials"].size() == 17u && all["laws"].size() == 22u && all["recipes"].integer() == 0 && all["channels"][1]["name"].text() == "temperature" && all["channels"][1]["unit"].text() == "K");
        const Answered wood = told(session, R"({"op":"matter.describe","material":"wood"})");
        expect(wood["phase"].text() == "solid" && wood["properties"]["ignition"].number() == 570.0 && wood["properties"]["burnt"].text() == "ash" && wood["properties"]["friction"].number() == 0.45);
        const Answered burns = told(session, R"({"op":"matter.describe","law":"burns"})");
        expect(burns["kind"].text() == "reaction" && burns["flag"].text() == "burning" && burns["when"].text() == "temperature >= ignition && burned < fuel && near.oxygen > 0");
        const Answered flows = told(session, R"({"op":"matter.describe","law":"heat flows"})");
        std::println("{}", flows.text);
        expect(flows["kind"].text() == "transfer" && flows["transfer"].text() == "temperature" && flows["by"].text() == "thermal");
        const Answered seen = told(session, R"({"op":"matter.observe"})");
        expect(seen["cells"].integer() == 60 && seen["materials"]["air"].integer() == 40 && seen["materials"]["stone"].integer() == 12 && seen["materials"]["wood"].integer() == 8);
        expect(seen["channels"][0]["name"].text() == "temperature" && seen["channels"][0]["least"].number() == 293.0 && seen["channels"][0]["most"].number() == 700.0);
        expect(seen["states"]["burning"].integer() == 0);
        const Answered part = told(session, R"({"op":"matter.observe","region":[2,3,8,1],"budget":260})");
        expect(part.text.size() <= 260u + 40u && part["materials"]["wood"].integer() == 8 && part["more"].integer().value_or(0) > 0) << part.text;
    };
}};
