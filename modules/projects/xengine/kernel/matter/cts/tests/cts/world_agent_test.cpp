// The matter subject on a world of matter (xengine.cellular.rigid.agent's `WorldLattice`; the foundation plan's
// §6.10.1): the same questions an agent asks of a field of its own, on a world whose cells also fly and are
// bodies'. The agent contract's suite on it; why a grain of sand is where it is — it landed, from a cell it was
// thrown out of, by a blast, which someone made; what would be, by a branch of the whole world; and that being
// asked changes nothing of the world.
import std;
import mlibs.agent;
import mlibs.agent.cts;
import mlibs.control;
import mlibs.json;
import openas;
import xengine.matter.json;
import xengine.cellular.matter.agent;
import xengine.cellular.rigid.agent;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
namespace bodies = xengine::cellular::rigid;
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
Answered told(agent::SubjectSession& session, std::string_view line, std::source_location at = std::source_location::current()) {
    Answered out = ask(session, line);
    expect(fatal(out.data.has_value() && out.error.empty()), at) << line << " → " << out.text;
    return out;
}

const std::shared_ptr<const matter::Program>& standard() {
    static const std::shared_ptr<const matter::Program> program = std::make_shared<const matter::Program>(*matter::standard());
    return program;
}
std::uint32_t material(std::string_view name) { return *standard()->material(name); }
constexpr ca::Shape shape{64, 40};
constexpr std::uint32_t seed = 4;
const openas::Actor player{openas::Kind::person, "player", {}};

// A floor, a wall, a heap of sand on its left; a beam of wood on two posts of stone with a crate of iron on it.
std::unique_ptr<bodies::MatterWorld> scene() {
    auto world = *bodies::MatterWorld::make(*standard(), {.shape = shape, .tile = {16, 16}, .seed = seed, .fill = "air"});
    world->place({0, 36, 64, 4}, material("stone"));
    world->place({20, 26, 2, 10}, material("stone"));  // the wall
    world->place({8, 31, 8, 5}, material("sand"));     // the heap
    world->place({34, 28, 2, 8}, material("stone")), world->place({46, 28, 2, 8}, material("stone"));
    world->place({34, 26, 14, 2}, material("wood"));   // the beam
    world->place({39, 22, 4, 4}, material("iron"));    // the crate
    (void)world->carve({39, 22, 4, 4});
    return world;
}

// A world, its lattice and the matter subject of it: what a game holds.
struct Game {
    std::unique_ptr<bodies::MatterWorld> world = scene();
    ca::rigid_agent::WorldLattice        lattice{*world};
    subject::Matter                      matter{standard(), lattice, {.seed = seed}};
    void step(int frames = 1) {
        for (int frame = 0; frame < frames; ++frame) world->step(), lattice.stepped(*world);
    }
    [[nodiscard]] agent::SubjectSession session(std::string_view actor = "tester") { return agent::SubjectSession({&matter}, {.actor = {openas::Kind::agent, std::string(actor), {}}}); }
};

// The subject with the world it speaks of, for the contract's probe.
class Whole final : public agent::Subject {
public:
    explicit Whole(std::unique_ptr<Game> game) : game_(std::move(game)) {}
    [[nodiscard]] std::string_view name() const noexcept override { return game_->matter.name(); }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return game_->matter.interface_name(); }
    [[nodiscard]] agent::Capabilities capabilities() const noexcept override { return game_->matter.capabilities(); }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return game_->matter.operations(); }
    [[nodiscard]] std::string_view listing() const noexcept override { return game_->matter.listing(); }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return game_->matter.guide(); }
    [[nodiscard]] std::vector<mlibs::agent::Term> terms() const override { return game_->matter.terms(); }
    [[nodiscard]] std::uint64_t state_hash() const override { return game_->matter.state_hash(); }
    void operate(std::string_view operation, const agent::Request& request, agent::Answer& answer) override { game_->matter.operate(operation, request, answer); }

private:
    std::unique_ptr<Game> game_;
};

}  // namespace

static UTest cases{"matter.world-agent", [] {
    "the agent contract on the matter of a world: what it has passes; time is the game's, and said so"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> {
                auto game = std::make_unique<Game>();
                game->step(10);
                game->lattice.by(player);
                (void)game->world->blast({12, 33}, 5.0f, 22.0f);
                game->step(140);
                return std::make_unique<Whole>(std::move(game));
            },
            .read = R"({"op":"matter.observe"})",
            .change = R"({"op":"matter.place","material":"water","region":[5,0,2,1]})",
            .preview = R"({"op":"matter.branch","steps":60,"place":{"material":"water","region":[2,0,8,2]}})",
            .explain = R"({"op":"matter.explain","cell":[5,0]})",
            .source = R"({"op":"matter.explain","cell":[5,0]})",
            .budgeted = R"({"op":"matter.observe","budget":320})",
            .budget = 320,
        };
        contract::declare(probe, {.subject = "xengine.cellular.rigid.agent"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.undo.returns", "agent.time.replays"}) << std::format("{}", unsupported);
    };

    "why a grain of sand is beyond the wall: it landed there, from the cell it was thrown out of, by a blast, which the player made"_test = [] {
        Game game;
        agent::SubjectSession session = game.session();
        game.step(20);
        game.lattice.by(player);
        const std::uint32_t threw = game.world->blast({12, 33}, 5.0f, 22.0f);
        expect(fatal(threw > 10u));
        game.lattice.by(ca::agent::Field::owner());  // what follows is no longer the player's
        game.step(200);
        // What is on record: the blast once, each cell thrown, each landed.
        std::size_t blasts = 0, thrown = 0, landed = 0;
        for (const subject::Record& record : game.matter.records()) {
            blasts += record.kind == subject::RecordKind::blast;
            thrown += record.kind == subject::RecordKind::thrown;
            landed += record.kind == subject::RecordKind::landed;
        }
        expect(eq(blasts, std::size_t{1}) && eq(thrown, std::size_t{threw}) && eq(landed, std::size_t{threw})) << std::format("{} blasts, {} thrown, {} landed of {}", blasts, thrown, landed, threw);
        // A grain at rest beyond the wall, on the floor: where it is now, which need not be where it landed.
        std::optional<std::uint32_t> rests;
        for (std::uint32_t at = 22; at < 34 && !rests; ++at)
            for (std::uint32_t up = 35; up > 20 && !rests; --up)
                if (game.world->cells()[up * shape.width + at] == material("sand")) rests = up * shape.width + at;
        expect(fatal(rests.has_value())) << "sand went over the wall";
        const std::uint32_t x = *rests % shape.width, y = *rests / shape.width;
        const Answered why = told(session, std::format(R"({{"op":"matter.explain","cell":[{},{}],"depth":3}})", x, y));
        std::println("{}", why.text);
        expect(why["now"]["material"].text() == "sand");
        const auto landing = why["records"][0];
        std::println("asked of [{}, {}]; it landed at [{}, {}]", x, y, landing["cell"][0].integer().value_or(-1), landing["cell"][1].integer().value_or(-1));
        expect(landing["what"].text() == "landed" && landing["material"].text() == "sand" && landing["thrown_at"].integer() == 21);
        const auto from = landing["from"];
        expect(from[0].integer().value_or(99) < 20 && from[1].integer().value_or(0) >= 28) << "from the heap, this side of the wall";
        const auto throwing = landing["because"];
        expect(throwing["what"].text() == "thrown" && throwing["by"].text() == "blast" && throwing["frame"].integer() == 21);
        expect(throwing["cell"][0].integer() == from[0].integer() && throwing["cell"][1].integer() == from[1].integer());
        const auto blast = throwing["because"];
        expect(blast["what"].text() == "blast" && blast["by"]["name"].text() == "player" && blast["by"]["kind"].text() == "person") << "and whose it was";
        // The same by name, as events.
        const Answered events = told(session, R"({"op":"matter.events","name":"landed","region":[22,0,42,40]})");
        expect(events["events"].size() >= 1u);
    };

    "what the laws do in a world is on record as in a field: the beam lit by an agent burns, and its cells leave the body it was"_test = [] {
        Game game;
        agent::SubjectSession session = game.session("the one who lit it");
        const auto beam = game.world->carve({34, 26, 14, 2});  // the beam a body
        const auto crate = std::as_const(*game.world).bodies().owner(40, 23);
        expect(fatal(beam.has_value() && beam->size() == 1u && crate.has_value()));
        const auto number = [](auto body) { return (static_cast<std::int64_t>(body.generation) << 32) | static_cast<std::uint32_t>(body.index); };
        game.step(5);
        told(session, R"({"op":"matter.apply","channel":"temperature","set":900,"region":[40,26,2,2]})");
        game.step(1500);
        const Answered events = told(session, R"({"op":"matter.events","name":"ignite","region":[40,26,2,2]})");
        expect(events["events"].size() >= 1u) << "the laws judged on the world's lattice: it caught";
        const auto first = events["events"][0];
        const Answered why = told(session, std::format(R"({{"op":"matter.explain","cell":[{},{}],"depth":2}})", first["cell"][0].integer().value_or(0), first["cell"][1].integer().value_or(0)));
        bool applied = false;
        for (std::size_t index = 0; index < why["records"].size(); ++index) {
            const auto record = why["records"][index];
            applied |= record["what"].text() == "applied" && record["by"]["name"].text() == "the one who lit it";
            applied |= record["applied"]["by"]["name"].text() == "the one who lit it";
        }
        std::println("{}", why.text);
        expect(applied) << "back to the hand that applied the heat";
        const std::size_t left = static_cast<std::size_t>(std::ranges::count(game.matter.records(), subject::RecordKind::left, &subject::Record::kind));
        expect(left > 0u) << "cells burned out of the body are said to have left it";
        // Why the crate is on the floor: it came loose of the beam it stood on, whose cells had left it, burned out.
        std::optional<std::uint32_t> of_crate;
        for (std::uint32_t at = 0; at < shape.width * shape.height && !of_crate; ++at)
            if (std::as_const(*game.world).bodies().owner(at % shape.width, at / shape.width) == crate) of_crate = at;
        expect(fatal(of_crate.has_value())) << "the crate is somewhere";
        expect(*of_crate / shape.width > 28u) << std::format("and lower than it stood: a cell of it at row {}", *of_crate / shape.width);
        const Answered fell = told(session, std::format(R"({{"op":"matter.explain","cell":[{},{}],"depth":3}})", *of_crate % shape.width, *of_crate / shape.width));
        std::println("{}", fell.text);
        expect(fell["body"]["id"].integer() == number(*crate));
        expect(fatal(fell["body"]["records"].size() >= 1u)) << "its coming loose is on record";
        const auto first_drop = fell["body"]["records"][fell["body"]["records"].size() - 1];
        expect(first_drop["what"].text() == "dropped" && first_drop["support"].integer() == number((*beam)[0])) << "it had stood on the beam";
        const auto gave_way = first_drop["because"][0];
        expect(gave_way["what"].text() == "left" && gave_way["material"].text() == "wood" && gave_way["because"]["what"].text() == "burn_out") << "whose cells had burned out of it";
        expect(gave_way["frame"].integer().value_or(1 << 30) <= first_drop["frame"].integer().value_or(0));
    };

    "what would be is a branch of the whole world: nothing here is touched, and then it happens so"_test = [] {
        Game game;
        agent::SubjectSession session = game.session();
        game.step(20);
        (void)game.world->blast({12, 33}, 5.0f, 22.0f);
        game.step(3);  // sand in flight
        const std::uint64_t before = game.world->state_hash();
        const std::size_t on_record = game.matter.records().size();
        const Answered ahead = told(session, R"({"op":"matter.branch","steps":120})");
        expect(eq(game.world->state_hash(), before) && eq(game.matter.records().size(), on_record) && eq(game.world->frames(), std::uint64_t{23})) << "a branch touches nothing";
        game.step(120);
        expect(ahead["hash"].text() == std::format("{:016x}", game.world->state_hash())) << "what flew landed in the branch as it then did here";
    };

    "being asked is no part of the world: the same hash each frame as a world no one asks; going back drops what was on record after"_test = [] {
        Game game;
        agent::SubjectSession session = game.session();
        auto plain = scene();
        for (int frame = 0; frame < 120; ++frame) {
            if (frame == 20) (void)plain->blast({12, 33}, 5.0f, 22.0f), (void)game.world->blast({12, 33}, 5.0f, 22.0f);
            if (frame == 60) told(session, R"({"op":"matter.observe"})"), told(session, R"({"op":"matter.explain","cell":[30,35]})"), told(session, R"({"op":"matter.branch","steps":20})");
            plain->step(), game.step();
            if (plain->state_hash() != game.world->state_hash()) {
                expect(false) << std::format("frame {}: the world asked about parts from the other", frame + 1);
                break;
            }
        }
        // Back to frame 10, as a game's timeline would: an image restored.
        auto early = scene();
        for (int frame = 0; frame < 10; ++frame) early->step();
        expect(fatal(game.world->restore(early->save()).has_value()));
        game.lattice.rewound(*game.world);
        expect(std::ranges::all_of(game.matter.records(), [](const subject::Record& record) { return record.frame <= 10; })) << "what happened after frame 10 is another branch's";
        game.step(30);
        expect(eq(game.world->frames(), std::uint64_t{40}));
    };
}};
