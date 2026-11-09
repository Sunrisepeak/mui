// hanim on the preparation contract (mlibs.cts:prepare): a program prepares
// the same way twice and samples the same frames; an invalid one is refused
// as invalid-argument, one beyond its budget as refused; and an engine that
// refuses to play a program beyond its budget publishes none of it.
import std;
import hanim.engine;
import mlibs.cts;
import mlibs.hash;
import mcpplibs.mtest;

using namespace hanim;
using namespace mcpplibs::mtest;
namespace cts = mlibs::cts;

namespace {
ObjectDescription shape(bool circle, double tx) {
    constexpr double k = 0.5522847498307936;
    const auto edges = circle ? std::vector<ShapeSegment>{ShapeSegment::cubic({k, -1}, {1, -k}, {1, 0}), ShapeSegment::cubic({1, k}, {k, 1}, {0, 1}),
                                                          ShapeSegment::cubic({-k, 1}, {-1, k}, {-1, 0}), ShapeSegment::cubic({-1, -k}, {-k, -1}, {0, -1})}
                              : std::vector<ShapeSegment>{ShapeSegment::line({-1, 1}), ShapeSegment::line({1, 1})};
    const auto contour = Contour::copy({{0, -1}, edges, true});
    return {{{.id = "group"}, {.id = "outline", .parent = "group", .local = {.tx = tx}, .contours = {*contour}, .fill = Rgba{1, 0, 0, 1}}}};
}

// What the engine holds: what a refusal must leave as it was.
std::string engine_state(const Engine& engine) {
    std::vector<PlaybackInfo> infos(engine.size());
    const auto listed = engine.list(infos);
    std::string out = std::format("{} playbacks", engine.size());
    if (listed)
        for (const auto& i : infos) out += std::format("; {}:{} at {}", i.ref.slot, i.ref.generation, i.position.count());
    return out;
}

// Every track sampled at its start, a quarter, half way and its end: the
// hash of every part's transform and fill and every cubic.
std::string frames(const PreparedProgram& p) noexcept {
    mlibs::hash::Fnv1a h;
    const double d = p.duration().count();
    for (std::size_t t = 0; t < p.tracks().size(); ++t) {
        const auto& track = p.tracks()[t];
        std::vector<FramePart> parts(track.requirements.parts);
        std::vector<ShapeCubic> cubics(track.requirements.cubics);
        std::vector<float> effects(track.max_effect_parameters);
        for (const double at : {0.0, d / 4, d / 2, d}) {
            const auto s = p.sample(t, seconds(at), {parts, cubics}, effects);
            if (!s) {
                h.str(describe(s.error().code).name);
                continue;
            }
            for (const auto& part : s->geometry.parts) {
                for (const double v : lanes<Transform2D>::to(part.local)) h.f64(v);
                if (part.fill) h.f32(part.fill->r).f32(part.fill->g).f32(part.fill->b).f32(part.fill->a);
            }
            for (const auto& c : s->geometry.cubics)
                for (const auto& q : {c.start, c.control1, c.control2, c.end}) h.f64(q.x).f64(q.y);
            for (std::size_t e = 0; e < s->effect_parameters; ++e) h.f32(effects[e]);
        }
    }
    return std::format("{:016x}", h.value());
}

// Preparation: Engine::prepare resolves the live objects; the engine is not changed.
struct Programs {
    struct Description {
        Program       program;
        ProgramLimits limits;
    };
    Engine    engine;
    ObjectRef a, b;

    auto prepare(const Description& d) { return engine.prepare(d.program, d.limits); }
    std::string fingerprint(const PreparedProgram& p) {
        return std::format("{} tracks, {} segments, {} s, {}", p.tracks().size(), p.segments().size(), p.duration().count(), frames(p));
    }
    std::string state() { return engine_state(engine); }
    std::string execute(const PreparedProgram& p) noexcept { return frames(p); }
};

// Publication: Engine::play admits a program against the engine's totals. (A
// play binds its objects, so the same one cannot be played twice at once: the
// suite checks refusals here, acceptance on Programs.)
struct Plays {
    using Description = Program;
    Engine                 engine;
    std::vector<ObjectRef> from, to;

    auto prepare(const Program& p) { return engine.play(p); }
    std::string fingerprint(PlaybackRef r) {
        const auto info = engine.inspect(r);
        return info ? std::format("{} s, {}", info->duration.count(), engine.plan(r) ? frames(*engine.plan(r)) : "no plan") : "gone";
    }
    std::string state() { return engine_state(engine); }
};
}  // namespace

UTest cases{"hanim.cts.prepare", [] {

    "a program prepares the same way twice; refusals by kind leave the engine as it was"_test = [] {
        Programs p{*Engine::make()};
        p.a = *p.engine.create_object(shape(false, 0));
        p.b = *p.engine.create_object(shape(true, 40));
        std::vector<Program> many;
        for (int i = 0; i < 12; ++i) many.push_back(anim::move_to(p.a, {static_cast<double>(i), 0}));
        cts::Suite<Programs> suite{"hanim.program.prepare", "1"};
        cts::prepare::add(suite, "hanim.program@1",
                          {.valid = {{"morph", {anim::morph(p.a, p.b).duration(seconds(2))}},
                                     {"sequence", {sequence(anim::move_to(p.a, {4, 0}), anim::rotate_to(p.a, 1))}},
                                     {"scoped-effect", {composite(anim::morph(p.a, p.b)).with_effect(fx::blur(0, 8))}}},
                           .invalid = {{"empty", {Program{}}},
                                       {"negative-time", {anim::move_through(p.a, {{seconds(-1), {1, 0}}})}}},
                           .over_budget = {{"nodes", {sequence(std::span<const Program>(many)), {.max_nodes = 4}}},
                                           {"effects", {composite(anim::morph(p.a, p.b)).with_effect(fx::blur(0, 1)).with_effect(fx::blur(0, 2)),
                                                        {.effects = {.max_effects = 1}}}}}});
        const auto report = suite.run(p, {}, {}, {.subject = "hanim.engine"});
        expect(report.ok()) << report.text();
        expect(eq(report.count(cts::Status::passed), std::size_t{7}));
    };

    "an engine refusing a program beyond its totals publishes none of it"_test = [] {
        Plays p{*Engine::make({.max_total_parts = 64})};
        for (int i = 0; i < 40; ++i) {
            p.from.push_back(*p.engine.create_object(shape(false, i)));
            p.to.push_back(*p.engine.create_object(shape(true, i)));
        }
        // Two playing already, half way: what a refusal must leave as it was.
        const auto first = p.engine.play(anim::morph(p.from[0], p.to[0]));
        const auto second = p.engine.play(parallel(anim::morph(p.from[1], p.to[1]), anim::morph(p.from[2], p.to[2])));
        expect(first && second && p.engine.advance(seconds(.25)).has_value());
        std::vector<Program> all;
        for (std::size_t i = 3; i < 40; ++i) all.push_back(anim::morph(p.from[i], p.to[i]));
        cts::Suite<Plays> suite{"hanim.engine.play", "1"};
        cts::prepare::add(suite, "hanim.engine@1",
                          {.invalid = {{"negative-time", anim::move_through(p.from[3], {{seconds(-1), {1, 0}}})}, {"empty", Program{}}},
                           .over_budget = {{"parts", parallel(std::span<const Program>(all))}}});
        const auto report = suite.run(p, {}, {}, {.subject = "hanim.engine"});
        expect(report.ok()) << report.text();
        expect(eq(report.count(cts::Status::passed), std::size_t{3}));
        expect(eq(p.engine.size(), std::size_t{2}));
    };
}};
