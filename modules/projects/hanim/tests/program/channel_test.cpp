// Typed channels (L2): ch::position/rotation/scale/fill write a part's own
// fields exactly as the property targets they lower to; any other channel
// (`ch::define<T>("app.glow")`) is a value of the part, sampled into
// FrameView::values, going on from where it is across a sequence, refused
// when two writers overlap or a relative write names a built-in it cannot.
import std;
import hanim.engine;
import mlibs.error;
import mcpplibs.mtest;

using namespace hanim;
using namespace mcpplibs::mtest;

namespace {
inline constexpr auto glow = ch::define<double>("app.glow");
inline constexpr auto drift = ch::define<Vec2>("app.drift", {1, 1});
inline constexpr auto glow_pair = ch::define<Vec2>("app.glow");  // the same name, other lanes

ObjectDescription shape(double tx) {
    const auto contour = Contour::copy({{0, -1}, std::vector<ShapeSegment>{ShapeSegment::line({-1, 1}), ShapeSegment::line({1, 1})}, true});
    return {{{.id = "group"}, {.id = "outline", .parent = "group", .local = {.tx = tx}, .contours = {*contour}, .fill = Rgba{1, 0, 0, 1}}}};
}
struct Fixture {
    Engine    engine = *Engine::make();
    ObjectRef a = *engine.create_object(shape(0));
};
struct Frames {
    std::vector<FramePart> parts;
    std::vector<ShapeCubic> cubics;
    std::vector<double> values;
    std::vector<float> effects;
    explicit Frames(const PreparedProgram& p)
        : parts(p.tracks()[0].requirements.parts), cubics(p.tracks()[0].requirements.cubics), values(p.tracks()[0].requirements.values),
          effects(p.tracks()[0].max_effect_parameters) {}
    FrameView at(const PreparedProgram& p, double t) {
        const auto s = p.sample(0, seconds(t), {parts, cubics, values}, effects);
        expect(s.has_value()) << (s ? "" : s.error().text());
        return s ? s->geometry : FrameView{};
    }
};
PreparedProgram take(const std::expected<PreparedProgram, EngineFailure>& r) {
    expect(r.has_value()) << (r ? std::string() : r.error().text());
    if (!r) std::abort();
    return *r;
}
std::size_t index_of(const FrameView& f, std::string_view id) {
    for (std::size_t i = 0; i < f.parts.size(); ++i)
        if (f.parts[i].info.id == id) return i;
    return f.parts.size();
}
}  // namespace

UTest cases{"hanim.channel", [] {

    "the built-in channels lower to the property targets they name"_test = [] {
        Fixture f;
        const auto typed_plan = f.engine.prepare(anim::to(part(f.a, "outline"), ch::position = {4, 2}, ch::rotation = .5, ch::fill = Rgba{0, 0, 1, 1}).duration(seconds(2)));
        const auto plain_plan = f.engine.prepare(anim::to(f.a, {.position = Vec2{4, 2}, .rotation = .5, .fill = Rgba{0, 0, 1, 1}, .part = "outline"}).duration(seconds(2)));
        const auto typed = take(typed_plan), plain = take(plain_plan);
        Frames x(typed), y(plain);
        for (const double t : {0.0, .5, 1.3, 2.0}) {
            const auto a = x.at(typed, t), b = y.at(plain, t);
            expect(a.parts.size() == b.parts.size() && a.values.empty()) << "same parts, no values";
            for (std::size_t i = 0; i < a.parts.size(); ++i)
                expect(a.parts[i].local == b.parts[i].local && a.parts[i].fill == b.parts[i].fill) << std::format("part {} at {}", i, t);
        }
        const auto relative = take(f.engine.prepare(anim::by(f.a, ch::position = {1, 0}, ch::scale = {2, 2}).duration(seconds(1))));
        Frames r(relative);
        const auto end = r.at(relative, 1);
        expect(end.parts[0].local.tx == 1 && end.parts[0].local.sx == 2) << "by: position adds, scale multiplies";
    };

    "an application's channel is a value of the part, from its rest, sampled in its lanes"_test = [] {
        Fixture f;
        const auto p = take(f.engine.prepare(anim::to(f.a, glow = 1.0, drift = Vec2{3, -1}).duration(seconds(2))));
        expect(eq(p.tracks()[0].requirements.values, std::size_t{3}));
        Frames x(p);
        const auto root = 0;
        for (const auto& [t, g, dx] : std::array<std::tuple<double, double, double>, 3>{{{0, 0, 1}, {1, .5, 2}, {2, 1, 3}}}) {
            const auto frame = x.at(p, t);
            expect(eq(*frame.value(root, glow), g));
            expect(eq(frame.value(root, drift)->x, dx));
        }
        expect(!x.at(p, 1).value(1, glow)) << "the outline has no glow: only the part written has the channel";
        expect(!x.at(p, 1).value(root, glow_pair)) << "a channel is its name and its lanes";
    };

    "parts are targeted by id; a sequence goes on from where the channel is"_test = [] {
        Fixture f;
        const auto p = *f.engine.prepare(sequence(anim::to(part(f.a, "outline"), glow = 1.0, ch::rotation = 1.0).duration(seconds(1)),
                                                  anim::by(part(f.a, "outline"), glow = -.5).duration(seconds(1))));
        Frames x(p);
        const auto mid = x.at(p, 1.5);
        const auto outline = index_of(mid, "outline");
        expect(outline < mid.parts.size() && mid.parts[outline].local.rotate == 1) << "the outline turned; the group did not";
        expect(mid.parts[index_of(mid, "group")].local.rotate == 0) << "the group is as it was";
        expect(eq(*mid.value(outline, glow), .75));
        expect(eq(*x.at(p, 2).value(outline, glow), .5));
        expect(eq(*x.at(p, 0).value(outline, glow), 0.0));
    };

    "overlapping writers of a channel, a relative fill and a renamed lane count are refused"_test = [] {
        Fixture f;
        const auto overlap = f.engine.prepare(parallel(anim::to(f.a, glow = 1.0), anim::to(f.a, glow = 2.0)));
        expect(!overlap && overlap.error().kind() == mlibs::error::Kind::refused) << (overlap ? "accepted" : overlap.error().text());
        const auto twice = f.engine.prepare(anim::to(f.a, glow = 1.0, glow = 2.0));
        expect(!twice && twice.error().caused_by(MorphError::property_conflict)) << (twice ? "accepted" : twice.error().text());
        const auto fill = f.engine.prepare(anim::by(f.a, ch::fill = Rgba{0, 0, 0, 1}));
        expect(!fill && fill.error().kind() == mlibs::error::Kind::invalid_argument) << (fill ? "accepted" : fill.error().text());
        const auto lanes = f.engine.prepare(sequence(anim::to(f.a, glow = 1.0), anim::to(f.a, glow_pair = Vec2{1, 1})));
        expect(!lanes && lanes.error().caused_by(MorphError::invalid_property)) << (lanes ? "accepted" : lanes.error().text());
        const auto nan = f.engine.prepare(anim::to(f.a, glow = std::numeric_limits<double>::quiet_NaN()));
        expect(!nan && nan.error().kind() == mlibs::error::Kind::invalid_argument) << "a non-finite value is refused";
    };

    "a spring leaf plays a spring's course: for as long as it settles, each lane its oscillator"_test = [] {
        Fixture f;
        const auto p = take(f.engine.prepare(anim::spring(f.a, springs::wobbly, ch::position = {10, 0}, glow = 2.0)));
        const Oscillator o(0, 0, 1, springs::wobbly, {1e-4, 1e-3});
        expect(eq(p.duration().count(), o.settle_time()));
        Frames x(p);
        bool over = false;
        for (const double t : {.05, .1, .2, .4}) {
            const auto frame = x.at(p, t);
            expect(eq(frame.parts[0].local.tx, 10 * o.position(t)));
            expect(eq(*frame.value(0, glow), 2 * o.position(t)));
            over = over || frame.parts[0].local.tx > 10;
        }
        expect(over) << "the wobbly spring overshoots its target";
        const auto end = x.at(p, p.duration().count());
        expect(end.parts[0].local.tx == 10 && *end.value(0, glow) == 2) << "settled: exactly at the targets";
        const auto timed = take(f.engine.prepare(anim::to(f.a, ch::position = {10, 0}).spring(springs::snappy)));
        expect(eq(timed.duration().count(), spring_ease(springs::snappy).settle()));
    };

    "a value written earlier holds through a later segment that writes another channel"_test = [] {
        Fixture f;
        const auto plan = take(f.engine.prepare(sequence(anim::to(part(f.a, "outline"), glow = .5).duration(seconds(0)),
                                                         anim::to(part(f.a, "outline"), drift = Vec2{3, 3}).duration(seconds(1)))));
        Frames frames(plan);
        const auto frame = frames.at(plan, .5);
        const auto outline = index_of(frame, "outline");
        expect(frame.value(outline, glow) == std::optional<double>(.5)) << "the glow written first is where it was left";
        expect(frame.value(outline, drift) == std::optional<Vec2>(Vec2{2, 2})) << "the drift halfway from its rest";
    };

    "the engine publishes a playback's values with its frame"_test = [] {
        Fixture f;
        const auto ref = f.engine.play(anim::to(f.a, glow = 1.0).duration(seconds(1)));
        expect(ref.has_value() && f.engine.advance(seconds(.25)).has_value());
        const auto frame = f.engine.frame(*ref);
        expect(frame.has_value() && frame->geometry.value(0, glow) == std::optional<double>(.25)) << "a quarter of the way at a quarter of the time";
    };
}};
