// The stage as a subject of the agent kernel: the agent contract's suite on
// it — what it says it cannot do yet reported so, never passed — and a
// program played by name, sought, paused, reversed and released through the
// same session an agent has.
import std;
import hanim.agent;
import mlibs.agent.cts;
import mlibs.json;
import mcpplibs.mtest;

using namespace hanim;
using namespace mcpplibs::mtest;
namespace contract = mlibs::agent::cts;
using mlibs::cts::Status;

namespace {

template <class T, class E>
T take(std::expected<T, E> result) {
    expect(fatal(bool(result)));
    if constexpr (!std::is_void_v<T>) return std::move(*result);
}
Contour square() {
    const std::array edges{ShapeSegment::line({10, 0}), ShapeSegment::line({10, 10}), ShapeSegment::line({0, 10})};
    return take(Contour::copy({{0, 0}, edges, true}));
}
// A card on an engine: what a program holds before any agent asks.
struct Scene {
    Engine    engine = take(Engine::make());
    ObjectRef card = take(engine.create_object({{{.id = "card", .contours = {square()}, .fill = Rgba{1, 1, 1, 1}}}}));
    [[nodiscard]] Program slide() const { return anim::to(card, PropertyTargets{.position = Vec2{200, 50}}).duration(seconds(1)); }
};
// The scene first (a base before the subject that reads it), then the stage over its engine: a slide offered,
// and one already playing.
struct Shown final : Scene, agent::Stage {
    PlaybackRef first;
    Shown() : agent::Stage(engine) {
        first = take(engine.play(slide()));
        ObjectRef other = take(engine.create_object({{{.id = "tag", .contours = {square()}, .fill = Rgba{1, 0, 0, 1}}}}));
        offer("rise", anim::to(other, PropertyTargets{.position = Vec2{0, -80}}).duration(seconds(2)));
    }
};

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};
Answered ask(mlibs::agent::SubjectSession& session, std::string_view line) {
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

UTest cases{"hanim.agent", [] {
    "the agent contract on the stage: what it has passes, what it says it has not is unsupported"_test = [] {
        PlaybackRef playing;
        {
            Shown first;
            playing = first.first;
        }
        const std::string playback = std::format(R"("playback":{{"slot":{},"generation":{}}})", playing.slot, playing.generation);
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<mlibs::agent::Subject> { return std::make_unique<Shown>(); },
            .read = R"({"op":"animation.list"})",
            .change = std::format(R"({{"op":"animation.seek",{},"position":0.4}})", playback),
            .undo = std::format(R"({{"op":"animation.seek",{},"position":0}})", playback),
            .script = {std::format(R"({{"op":"animation.seek-progress",{},"progress":0.5}})", playback), std::format(R"({{"op":"animation.inspect",{}}})", playback),
                       std::format(R"({{"op":"animation.pause",{}}})", playback), std::format(R"({{"op":"animation.rate",{},"rate":2}})", playback),
                       std::format(R"({{"op":"animation.seek",{},"position":0.25}})", playback), R"({"op":"animation.play","program":"rise"})", R"({"op":"animation.list"})",
                       std::format(R"({{"op":"animation.reverse",{}}})", playback), R"({"op":"animation.list"})"},
        };
        const auto report = contract::run(probe, {.subject = "hanim.agent"});
        std::println("{}", report.text());
        expect(report.ok()) << report.text();
        std::vector<std::string> unsupported;
        for (const auto& outcome : report.outcomes)
            if (outcome.status == Status::unsupported) unsupported.push_back(outcome.id);
        expect(unsupported == std::vector<std::string>{"agent.preview.untouched", "agent.source.chain", "agent.actor.recorded", "agent.observe.budget", "agent.causes.explained"})
            << "a preview, causes, the source of a value, a budget: not yet, and said so";
    };

    "a program played by name is sought to a pose, held, turned back and let go"_test = [] {
        Shown stage;
        mlibs::agent::SubjectSession session({&stage});
        const auto prepared = ask(session, R"({"op":"animation.prepare","program":"rise"})");
        expect(prepared["duration"].number() == 2.0);
        const auto played = ask(session, R"({"op":"animation.play","program":"rise"})");
        const std::string playback = std::format(R"("playback":{})", played["playback"].dump());
        expect(eq(ask(session, R"({"op":"animation.list"})")["playbacks"].size(), std::size_t{2})) << "the slide already playing, and the rise";

        const auto half = ask(session, std::format(R"({{"op":"animation.seek-progress",{},"progress":0.5}})", playback));
        expect(half["position"].number() == 1.0 && half["progress"].number() == 0.5) << "halfway through two seconds";
        expect(ask(session, std::format(R"({{"op":"animation.pause",{}}})", playback))["state"].text() == "paused");
        // Time passing leaves what is paused where it is: the owner's clock, not a request.
        expect(stage.engine.advance(seconds(0.5)).has_value());
        expect(ask(session, std::format(R"({{"op":"animation.inspect",{}}})", playback))["position"].number() == 1.0);
        expect(ask(session, std::format(R"({{"op":"animation.resume",{}}})", playback))["state"].text() == "playing");
        expect(stage.engine.advance(seconds(0.5)).has_value());
        expect(ask(session, std::format(R"({{"op":"animation.inspect",{}}})", playback))["position"].number() == 1.5);

        expect(ask(session, std::format(R"({{"op":"animation.release",{}}})", playback))["released"].boolean() == true);
        expect(ask(session, std::format(R"({{"op":"animation.inspect",{}}})", playback)).error == "stale") << "released, its handle is stale";
        expect(ask(session, R"({"op":"animation.play","program":"fall"})").error == "not-found") << "a program the stage does not offer";
        expect(ask(session, R"({"op":"animation.seek","playback":{"slot":0},"position":1})").error == "invalid-argument");
    };
}};
