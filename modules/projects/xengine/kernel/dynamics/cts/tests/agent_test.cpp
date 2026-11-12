// A soft body as a subject of the agent kernel: the agent contract's suite on
// it — what it says it cannot do yet reported so, never passed — and the body
// read, pushed, stepped, sought and branched through the same session an
// agent has.
import std;
import mlibs.agent.cts;
import mlibs.json;
import xengine.dynamics.xpbd;
import xengine.dynamics.agent;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace dynamics = xengine::dynamics;

namespace {

// An octagon about (100, 250), a floor at y = 300 (the page's y grows downwards).
dynamics::SoftBody octagon() {
    const std::array<std::array<double, 2>, 8> outline{{{120, 250}, {114, 264}, {100, 270}, {86, 264}, {80, 250}, {86, 236}, {100, 230}, {114, 236}}};
    const std::array ground{dynamics::Plane{.normal_x = 0, .normal_y = -1, .offset = -300}};
    auto body = dynamics::SoftBody::ring(outline, {}, ground);
    expect(fatal(body.has_value()));
    return std::move(*body);
}

// Dropped and left to come to rest on the floor: 240 steps, the same each time.
std::unique_ptr<dynamics::agent::Body> resting() {
    auto body = std::make_unique<dynamics::agent::Body>(octagon());
    for (int step = 0; step < 240; ++step) body->step();
    return body;
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

UTest cases{"dynamics.agent", [] {
    "the agent contract on a body: what it has passes, what it says it has not is unsupported"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> { return resting(); },
            .read = R"({"op":"body.observe"})",
            .change = R"({"op":"body.step","steps":4})",
            .undo = R"({"op":"body.seek","frame":240})",
            .preview = R"({"op":"body.branch","steps":30,"kick":[0,-400]})",
            .script = {R"({"op":"body.push","kick":[120,-380]})", R"({"op":"body.step","steps":30})", R"({"op":"body.observe"})", R"({"op":"body.push","walk":40})",
                       R"({"op":"body.step","steps":60})", R"({"op":"body.particles"})"},
            .budgeted = R"({"op":"body.observe","budget":360})",
            .budget = 360,
        };
        contract::declare(probe, {.subject = "xengine.dynamics.agent"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.source.chain", "agent.actor.recorded", "agent.causes.explained"})
            << "causes and the source of what it puts out: not yet, and said so";
    };

    "a body at rest is read as it is: on the floor, its contacts named; a kick lifts it, and it comes down again"_test = [] {
        auto body = resting();
        agent::SubjectSession session({body.get()});
        const auto seen = ask(session, R"({"op":"body.observe"})");
        expect(seen["frame"].integer() == 240);
        expect(seen["bounds"][3].number().value_or(0) > 299 && seen["bounds"][3].number().value_or(0) < 301) << "its lowest point on the floor at 300";
        expect(seen["contacts"].size() >= 1u && seen["contacts"][0]["on"].text() == "plane") << "it touches the floor";
        const auto brief = ask(session, R"({"op":"body.observe","budget":360})");
        expect(brief["contacts"].size() + static_cast<std::size_t>(brief["more"].integer().value_or(0)) == seen["contacts"].size()) << "what is left out is counted";
        expect(ask(session, R"({"op":"body.contacts"})").data.has_value()) << "the same in words";

        const auto pushed = ask(session, R"({"op":"body.push","kick":[0,-400]})");
        expect(pushed["effective"].integer() == 241) << "the next step shows it";
        (void)ask(session, R"({"op":"body.step","steps":12})");
        const auto up = ask(session, R"({"op":"body.observe"})");
        expect(up["centre"][1].number().value_or(999) < seen["centre"][1].number().value_or(0) - 10) << "kicked upwards, it is off the floor";
        expect(eq(up["contacts"].size(), std::size_t{0}));
        // Read step by step, as an agent would: a body that bounces touches the floor on some steps and not on others.
        bool landed = false;
        for (int step = 0; step < 480 && !landed; ++step) {
            (void)ask(session, R"({"op":"body.step"})");
            landed = ask(session, R"({"op":"body.observe"})")["contacts"].size() >= 1u;
        }
        expect(landed) << "and comes down again";
        expect(ask(session, R"({"op":"body.push","kick":[1]})").error == "invalid-argument");
        expect(ask(session, R"({"op":"body.step","steps":0})").error == "invalid-argument");
    };

    "time is the body's: a frame kept is reached again exactly; what would be is asked without touching it"_test = [] {
        auto body = resting();
        agent::SubjectSession session({body.get()});
        const std::uint64_t at_rest = body->state_hash();
        const auto future = ask(session, R"({"op":"body.branch","steps":20,"kick":[60,-300]})");
        expect(future["frame"].integer() == 260);
        expect(eq(body->state_hash(), at_rest) && eq(body->frame(), std::int64_t{240})) << "the body itself did not move";

        (void)ask(session, R"({"op":"body.push","kick":[60,-300]})");
        const auto played = ask(session, R"({"op":"body.step","steps":20})");
        expect(played["hash"].text() == future["hash"].text()) << "the same steps, played";
        const auto back = ask(session, R"({"op":"body.seek","frame":240})");
        expect(back["frame"].integer() == 240 && eq(body->state_hash(), at_rest)) << "sought back: the same bits";
        expect(ask(session, R"({"op":"body.seek","frame":99999})").error == "stale");
    };
}};
