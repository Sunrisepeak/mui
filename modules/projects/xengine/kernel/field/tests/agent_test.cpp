// The air as a subject of the agent kernel (xengine.field.agent; the M6.5 design's T5.5): the agent contract's
// suite on it — what it says it cannot do reported so, never passed — and a fire lit through the same session an
// agent has: held hot, stepped, an updraft read over it, why the cell above is warm (heat flowed in from the
// fire's cell, by `heat flows`), a branch that leaves the air untouched, a frame returned to exactly.
import std;
import mlibs.agent;
import mlibs.agent.cts;
import mlibs.json;
import xengine.field;
import xengine.field.agent;
import xengine.matter.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace field = xengine::field;
namespace matter = xengine::matter;

namespace {

// The standard definition, prepared once.
const matter::Program& program() {
    static const matter::Program prepared = *matter::standard();
    return prepared;
}

// Air covered 4 m about (0, 3, 0), a fire's cell at (0.5, 0.5, 0.5) held at 900 K (as air.set with hold does),
// stepped `steps` times.
std::unique_ptr<field::agent::Air> air_over_a_fire(int steps) {
    auto made = field::Field::make(program(), {.air = *program().material("air")});
    made->cover({0, 3, 0}, 4);
    const std::uint32_t temperature = *program().channel("temperature");
    const auto hot = static_cast<std::uint32_t>(std::lround(std::ldexp(900.0, static_cast<int>(program().channels()[temperature].fraction))));
    auto air = std::make_unique<field::agent::Air>(std::move(*made));
    air->hold({*air->state().cell_at({0.5f, 0.5f, 0.5f}), temperature, hot});
    for (int step = 0; step < steps; ++step) air->step();
    return air;
}

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error, text;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};
Answered ask(agent::SubjectSession& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        expect(fatal(parsed.has_value())) << record;
        const auto kind = parsed->root()["kind"].text();
        if (kind == "error") out.error = parsed->root()["code"].text().value_or(""), out.text = record;
        if (kind == "data" && !out.data) out.data.emplace(std::move(*parsed)), out.text = record;
    });
    return out;
}

}  // namespace

UTest cases{"field.air-agent", [] {
    "the agent contract on the air: what it has passes; the source of what it puts out it says it has not"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> { return air_over_a_fire(20); },
            .read = R"({"op":"air.observe"})",
            .change = R"({"op":"air.step","steps":5})",
            .undo = R"({"op":"air.seek","frame":20})",
            .preview = R"({"op":"air.branch","steps":30})",
            .script = {R"({"op":"air.set","at":[3.5,0.5,0.5],"channel":"wetness","value":40})", R"({"op":"air.step","steps":20})"},
            .explain = R"({"op":"air.explain","at":[0.5,1.5,0.5]})",
            .budgeted = R"({"op":"air.observe","budget":420})",
            .budget = 420,
        };
        contract::declare(probe, {.subject = "xengine.field.agent"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.source.chain"}) << std::format("{}", unsupported);
    };

    "a fire lit through the session: the air above it rises, the cell above is warm by `heat flows` from the fire's cell, a branch leaves the air as it was, a frame is reached again exactly"_test = [] {
        auto made = field::Field::make(program(), {.air = *program().material("air")});
        expect(fatal(made.has_value()));
        made->cover({0, 3, 0}, 4);
        field::agent::Air air(std::move(*made));
        agent::SubjectSession session({&air});

        const auto lit = ask(session, R"({"op":"air.set","at":[0.5,0.5,0.5],"channel":"temperature","value":900,"hold":true})");
        expect(fatal(lit.data.has_value())) << lit.text;
        expect(eq(*lit["effective"].integer(), std::int64_t{1}));
        expect(!ask(session, R"({"op":"air.step","steps":180})").data->root()["data"]["failed"]) << "the air stepped";

        const auto above = ask(session, R"({"op":"air.at","at":[0.5,3.5,0.5]})");
        expect(fatal(above.data.has_value())) << above.text;
        const auto wind = above["wind"].items();
        expect(fatal(wind.size() == 3u));
        expect(gt(*wind[1].number(), 0.3)) << "an updraft over the fire: " << above.text;

        // Why the cell over the fire is warm: heat in from the fire's cell (its centre), by `heat flows`.
        const auto why = ask(session, R"({"op":"air.explain","at":[0.5,1.5,0.5]})");
        expect(fatal(why.data.has_value())) << why.text;
        bool from_fire = false;
        for (const auto& flow : why["flows"].items()) {
            const auto from = flow["beside_at"].items();
            from_fire = from_fire || (flow["law"].text() == "heat flows" && flow["direction"].text() == "in" && from.size() == 3u && *from[1].number() == 0.5);
        }
        expect(from_fire) << why.text;
        expect(why.text.contains("\"op\":\"air.step\"") && !why.text.contains("\"op\":\"air.set\"")) << "who stepped it; nothing was set in this cell: " << why.text;
        const auto fire = ask(session, R"({"op":"air.explain","at":[0.5,0.5,0.5]})");
        expect(fire.text.contains("\"op\":\"air.set\"") && fire.text.contains("\"hold\":true")) << "who lit the fire, held: " << fire.text;

        // A branch says what would be and touches nothing.
        const std::uint64_t before = air.state_hash();
        const auto ahead = ask(session, R"({"op":"air.branch","steps":60,"set":{"at":[-2.5,0.5,0.5],"channel":"temperature","value":900}})");
        expect(fatal(ahead.data.has_value())) << ahead.text;
        expect(eq(air.state_hash(), before)) << "the air untouched by a branch";
        expect(eq(*ahead["frame"].integer(), air.frame() + 60));

        // A frame returned to is the same bits; a refusal says why and what to do.
        const auto at_60 = ask(session, R"({"op":"air.seek","frame":60})");
        expect(fatal(at_60.data.has_value())) << at_60.text;
        const std::string hash_60 = *at_60["hash"].text();
        (void)ask(session, R"({"op":"air.step","steps":30})");
        expect(eq(*ask(session, R"({"op":"air.seek","frame":60})")["hash"].text(), hash_60)) << "the same frame, the same bits";
        expect(eq(ask(session, R"({"op":"air.set","at":[40,0,0],"channel":"temperature","value":900})").error, std::string("invalid-argument")));
        expect(eq(ask(session, R"({"op":"air.set","at":[0.5,0.5,0.5],"channel":"colour","value":1})").error, std::string("invalid-argument")));
    };
}};
