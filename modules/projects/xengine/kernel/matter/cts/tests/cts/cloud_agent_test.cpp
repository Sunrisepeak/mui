// xengine.particles.agent: a cloud of particles as a subject of the agent kernel: the agent contract's suite on it — what it says it cannot
// do yet reported so, never passed — and sparks emitted, stepped onto a floor, listed, found near a place, sought
// back to bit for bit and asked about ahead, through the same session an agent has.
import std;
import mlibs.agent.cts;
import mlibs.json;
import xengine.particles.agent;
import xengine.matter.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace particles = xengine::particles;
namespace matter = xengine::matter;
namespace geom = mlibs::geom;

namespace {

std::shared_ptr<const matter::Program> standard() {
    static const std::shared_ptr<const matter::Program> program = [] {
        auto made = matter::standard();
        expect(fatal(made.has_value()));
        return std::make_shared<const matter::Program>(std::move(*made));
    }();
    return program;
}

// A floor of 10 × 10 m at y = 0, kept for every scene.
const std::array<particles::Surface, 1>& floor_world() {
    static const geom::Mesh floor = geom::sheet(10, 10, 2, 2);
    static const geom::Bvh tree = geom::bvh_of(floor);
    static const std::array<particles::Surface, 1> world{{{.mesh = &floor, .tree = &tree, .place = mlibs::math::Mat4{}}}};
    return world;
}

// An empty cloud in air at 293 K over the floor.
std::unique_ptr<particles::agent::Scene> scene() {
    auto cloud = particles::Cloud::make(standard(), {.ambient = *standard()->material("air"), .ambient_kelvin = 293});
    expect(fatal(cloud.has_value()));
    return std::make_unique<particles::agent::Scene>(std::move(*cloud), floor_world());
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

constexpr std::string_view sparks = R"({"op":"particles.emit","at":[0,1,0],"velocity":[0,3,0],"material":"iron","kelvin":1500,"count":20,"spread":1,"life":5})";

}  // namespace

UTest cases{"xengine.particles.agent", [] {
    "the agent contract on a cloud: what it has passes, what it says it has not is unsupported"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> {
                auto made = scene();
                agent::SubjectSession session({made.get()});
                (void)ask(session, sparks);
                (void)ask(session, R"({"op":"particles.step","steps":10})");
                return made;
            },
            .read = R"({"op":"particles.observe"})",
            .change = R"({"op":"particles.step","steps":4})",
            .undo = R"({"op":"particles.seek","frame":10})",
            .preview = R"({"op":"particles.branch","steps":30,"at":[0,1,0],"material":"stone","count":5})",
            .script = {std::string(sparks), R"({"op":"particles.step","steps":30})", R"({"op":"particles.observe"})", R"({"op":"particles.list","most":4})"},
            .budgeted = R"({"op":"particles.observe","budget":200})",
            .budget = 200,
        };
        contract::declare(probe, {.subject = "xengine.particles.agent"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.source.chain", "agent.actor.recorded", "agent.causes.explained"})
            << "causes and the source of what it puts out: not yet, and said so";
    };

    "sparks emitted fly, strike the floor (said fastest first), cool, and are listed and found near where they are"_test = [] {
        auto made = scene();
        agent::SubjectSession session({made.get()});
        const auto emitted = ask(session, sparks);
        expect(emitted["effective"].integer() == 1 && emitted["count"].integer() == 20) << "the next step emits them";
        bool struck = false;
        double fastest = 0;
        for (int step = 0; step < 90 && !struck; ++step) {
            (void)ask(session, R"({"op":"particles.step"})");
            const auto seen = ask(session, R"({"op":"particles.observe"})");
            if (seen["struck"].size() > 0) {
                struck = true, fastest = seen["struck"][0]["speed"].number().value_or(0);
                for (std::size_t index = 1; index < seen["struck"].size(); ++index) expect(seen["struck"][index]["speed"].number().value_or(0) <= fastest);
                expect(seen["materials"]["iron"].integer() == 20);
            }
        }
        std::println("the first sparks struck the floor at {:.2f} m/s, frame {}", fastest, made->frame());
        expect(struck && fastest > 3.0);
        const auto listed = ask(session, R"({"op":"particles.list","most":4})");
        expect(listed["list"].size() == 4u && listed["more"].integer() == 16);
        const double kelvin = listed["list"][0]["kelvin"].number().value_or(0);
        expect(kelvin < 1500.0 && kelvin > 293.0) << "cooling in the air: " << kelvin;
        expect(listed["list"][0]["material"].text() == std::optional<std::string_view>("iron"));
        const auto near = ask(session, R"({"op":"particles.near","at":[0,0,0],"radius":20})");
        expect(near["found"].integer() == 20);
        for (std::size_t index = 1; index < near["near"].size(); ++index) {
            const auto distance = [&](std::size_t which) {
                double sum = 0;
                for (std::size_t axis = 0; axis < 3; ++axis) sum += std::pow(near["near"][which]["at"][axis].number().value_or(0), 2);
                return sum;
            };
            expect(distance(index) >= distance(index - 1)) << "the nearest first";
        }
        expect(ask(session, R"({"op":"particles.emit","at":[0,1,0],"material":"unobtainium"})").error == "invalid-argument");
        expect(ask(session, R"({"op":"particles.emit","at":[0,1],"material":"iron"})").error == "invalid-argument");
        expect(ask(session, R"({"op":"particles.emit","at":[0,1,0],"material":"iron","count":0})").error == "invalid-argument");
        expect(ask(session, R"({"op":"particles.list","most":0})").error == "invalid-argument");
        expect(ask(session, R"({"op":"particles.step","steps":0})").error == "invalid-argument");
    };

    "time is the cloud's: a frame kept is reached again bit for bit, emissions and spread and all; what would be is asked without touching it"_test = [] {
        auto made = scene();
        agent::SubjectSession session({made.get()});
        (void)ask(session, sparks);
        (void)ask(session, R"({"op":"particles.step","steps":70})");
        const std::uint64_t at_70 = made->state_hash();
        (void)ask(session, R"({"op":"particles.emit","at":[1,2,0],"velocity":[-1,0,0],"material":"stone","count":7,"spread":0.5})");
        (void)ask(session, R"({"op":"particles.step","steps":50})");
        const std::uint64_t at_120 = made->state_hash();
        // Past one copy (every 60) and back: the copy at 60, the emission at 70 played again.
        expect(ask(session, R"({"op":"particles.seek","frame":70})").error.empty());
        expect(eq(made->state_hash(), at_70));
        (void)ask(session, R"({"op":"particles.emit","at":[1,2,0],"velocity":[-1,0,0],"material":"stone","count":7,"spread":0.5})");
        (void)ask(session, R"({"op":"particles.step","steps":50})");
        expect(eq(made->state_hash(), at_120)) << "the same emission at the same frame: the same spread, the same bits";
        // Branch: the future of an emission, and the cloud untouched.
        const std::uint64_t before = made->state_hash();
        const auto ahead = ask(session, R"({"op":"particles.branch","steps":30,"at":[0,1,0],"material":"stone","count":5})");
        expect(eq(made->state_hash(), before) && ahead["materials"]["stone"].integer() == 5);
        expect(ask(session, R"({"op":"particles.seek","frame":9999})").error == "stale");
    };
}};
