// UI motion's display-list adapter consumes the public hanim plan. These are
// real Matter/Morph/CPU draws, independent of components and the website.
import std;
import hanim.core;
import mlibs.geom;
import xrender.raster;
import xrender.vector;
import omui.motion;
import mlibs.testing;

using namespace mlibs::testing;
namespace mo = omui::motion;
namespace render = xrender;
namespace raster = xrender::raster;
namespace geom = mlibs::geom;
namespace { std::atomic<std::size_t> allocations{0}; }
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {
constexpr int dimension = 192;
constexpr float k = 0.5522847498307936f;
render::Path triangle() {
    render::Path p;
    p.move_to({96, 32}).line_to({32, 160}).line_to({160, 160}).close();
    return p;
}
render::Path circle() {
    render::Path p;
    p.move_to({96, 32}).cubic_to({96 + 64 * k, 32}, {160, 96 - 64 * k}, {160, 96})
     .cubic_to({160, 96 + 64 * k}, {96 + 64 * k, 160}, {96, 160})
     .cubic_to({96 - 64 * k, 160}, {32, 96 + 64 * k}, {32, 96})
     .cubic_to({32, 96 - 64 * k}, {96 - 64 * k, 32}, {96, 32}).close();
    return p;
}
render::DisplayList scene(const render::Path& path, const render::Paint& paint, float width = 0) {
    render::DisplayList list;
    list.begin_node(17);
    if (width > 0) list.stroke_path(path, {.width = width, .cap = render::LineCap::round, .join = render::LineJoin::round}, paint);
    else list.fill_path(path, render::FillRule::nonzero, paint);
    list.end_node();
    return list;
}
mo::Resolver resolver() {
    return {.keys = [](std::uint32_t id, std::string& key, std::string&) { key = std::format("shape/{}", id); }};
}
mo::Options options() {
    mo::Options result;
    result.curve.kind = mo::Curve::Kind::tween;
    result.curve.duration = 1;
    result.curve.ease = hanim::ease::linear;
    return result;
}
mo::Morph transition(const render::DisplayList& a, const render::DisplayList& b, double start = 0) {
    return {mo::Matter::extract(a, resolver()), mo::Matter::extract(b, resolver()), options(), start};
}
render::DisplayList at(const mo::Morph& morph, double t) {
    render::DisplayList list; morph.draw(t, list); return list;
}
raster::Surface draw(const render::DisplayList& list, render::ImageStore* images = nullptr) {
    raster::Surface pixels(dimension, dimension);
    render::CpuRenderer renderer;
    const auto report = renderer.render(list, pixels, {images, nullptr});
    check_eq(report.missing_images, std::size_t{0});
    return pixels;
}
std::vector<const render::FillPath*> fills(const render::DisplayList& list) {
    std::vector<const render::FillPath*> result;
    for (const auto& command : list.commands()) if (auto* path = std::get_if<render::FillPath>(&command)) result.push_back(path);
    return result;
}
void shared_geometry(const render::DisplayList& list) {
    const auto paths = fills(list);
    check_eq(paths.size(), std::size_t{1}); if (paths.size() != 1) return;
    check(paths[0]->paint.kind == render::Paint::Kind::mix);
    check_eq(paths[0]->paint.mix.size(), std::size_t{2});
    for (const auto& term : paths[0]->paint.mix) check(term.paint.kind != render::Paint::Kind::mix);
    check_eq(std::ranges::count(paths[0]->path.verbs, render::PathVerb::move), std::ptrdiff_t{1});
    check_eq(std::ranges::count(paths[0]->path.verbs, render::PathVerb::close), std::ptrdiff_t{1});
}
}

int main(int argc, char** argv) {
    Suite suite{"motion.path-morph"};
    suite.test("public point path changes real pixels and retains exact endpoints and interrupted pose", [] {
        const auto paint = render::Paint::solid(geom::Color::white());
        const auto a = scene(triangle(), paint), b = scene(circle(), paint);
        auto curved_options = options(); curved_options.vertex_path = hanim::PointPath::bend(.35);
        const mo::Morph curved(mo::Matter::extract(a, resolver()), mo::Matter::extract(b, resolver()), curved_options, 0);
        const auto plain = transition(a, b);
        check(curved.path_morphs()[0].mode == mo::PathMorphMode::prepared);
        check(draw(at(curved, 0)).hash() == draw(a).hash());
        check(draw(at(curved, 1)).hash() == draw(b).hash());
        const auto actual = draw(at(curved, .37)).hash();
        check(actual != draw(at(plain, .37)).hash(), "process effect changes coverage, not just metadata");
        const mo::Morph interrupted(curved.snapshot(.37), mo::Matter::extract(a, resolver()), curved_options, 2);
        check(draw(at(interrupted, 2)).hash() == actual, "new path starts at the actual curved pose");
        check(draw(at(interrupted, 3)).hash() == draw(a).hash());
        for (double time : {1., .7, .1, 0.}) (void)at(curved, time);
        check(draw(at(curved, .37)).hash() == actual);
        render::DisplayList out; curved.draw(.5, out); out.clear(); curved.draw(.6, out);
        const auto before = allocations.load();
        for (unsigned i = 0; i < 100; ++i) { out.clear(); curved.draw(.37, out); }
        const auto after = allocations.load(); check_eq(before, after);
    });
    suite.test("compatible open paths share the operator and invalid modifiers have an observable fallback", [] {
        render::Path a_path, b_path;
        a_path.move_to({20, 20}).line_to({80, 20}); b_path.move_to({20, 20}).line_to({80, 80});
        const auto paint = render::Paint::solid(geom::Color::white());
        const auto a = scene(a_path, paint, 2), b = scene(b_path, paint, 2);
        auto policy = options(); policy.vertex_path = hanim::PointPath::bend(.25);
        const mo::Morph curved(mo::Matter::extract(a, resolver()), mo::Matter::extract(b, resolver()), policy, 0);
        check_eq(curved.stats().paths_compatible, std::size_t{1});
        const auto middle = at(curved, .5); bool seen = false;
        for (const auto& command : middle.commands()) if (auto* stroke = std::get_if<render::StrokePath>(&command)) {
            seen = true; check(stroke->path.points[0] == geom::Point{20, 20});
            check(stroke->path.points[1] == geom::Point{65, 50});
        }
        check(seen);
        policy.vertex_path.control1.x = std::numeric_limits<double>::infinity();
        const mo::Morph invalid(mo::Matter::extract(a, resolver()), mo::Matter::extract(b, resolver()), policy, 0);
        check_eq(invalid.stats().paths_unsupported, std::size_t{1});
        check(invalid.path_morphs()[0].geometry_error == hanim::ShapeMorphError::invalid_options);
        render::Path huge_a, huge_b;
        huge_a.move_to({0, 0}).line_to({1e38f, 1e38f}); huge_b.move_to({0, 0}).line_to({-1e38f, 1e38f});
        const auto large_a = scene(huge_a, paint, 2), large_b = scene(huge_b, paint, 2);
        policy.vertex_path = hanim::PointPath::bend(8);
        const mo::Morph overflow(mo::Matter::extract(large_a, resolver()), mo::Matter::extract(large_b, resolver()), policy, 0);
        check_eq(overflow.stats().paths_unsupported, std::size_t{1});
        check(overflow.path_morphs()[0].geometry_error == hanim::ShapeMorphError::numerical_range);
    });
    suite.test("fill triangle to circle prepares hanim correspondence and retains exact original endpoint pixels", [] {
        const auto paint = render::Paint::solid(geom::Color::white());
        const auto a = scene(triangle(), paint), b = scene(circle(), paint);
        const auto morph = transition(a, b);
        check_eq(morph.stats().paths_prepared, std::size_t{1});
        check_eq(morph.stats().paths_unsupported, std::size_t{0});
        check_eq(morph.path_morphs().size(), std::size_t{1});
        check(morph.path_morphs()[0].mode == mo::PathMorphMode::prepared);
        check(morph.path_morphs()[0].material_mode == mo::PathMaterialMode::interpolated);
        check(draw(at(morph, 0)).hash() == draw(a).hash());
        check(draw(at(morph, 1)).hash() == draw(b).hash());
        const auto middle = at(morph, 0.5);
        const auto paths = fills(middle);
        check_eq(paths.size(), std::size_t{1}); if (paths.empty()) return;
        check_eq(std::ranges::count(paths[0]->path.verbs, render::PathVerb::move), std::ptrdiff_t{1});
        check(std::ranges::count(paths[0]->path.verbs, render::PathVerb::cubic) >= 4);
        check(paths[0]->paint.color.a == 1);
        const auto pa = draw(a), pb = draw(b), pm = draw(middle);
        unsigned not_fade = 0;
        for (int y = 0; y < dimension; ++y) for (int x = 0; x < dimension; ++x) {
            const int average = (static_cast<int>(pa.at(x, y).a) + pb.at(x, y).a) / 2;
            not_fade += std::abs(static_cast<int>(pm.at(x, y).a) - average) > 64 ? 1u : 0u;
        }
        check(not_fade > 200, "actual pixels are a moving single opaque shape, not a two-endpoint fade");
    });

    suite.test("stroke paths use the same prepared geometry while width and endpoint pixels remain correct", [] {
        const auto paint = render::Paint::solid(geom::Color::white());
        const auto a = scene(triangle(), paint, 2), b = scene(circle(), paint, 6);
        const auto morph = transition(a, b);
        check_eq(morph.stats().paths_prepared, std::size_t{1});
        check(draw(at(morph, 0)).hash() == draw(a).hash());
        check(draw(at(morph, 1)).hash() == draw(b).hash());
        const auto middle = at(morph, 0.5);
        std::size_t count = 0;
        for (const auto& command : middle.commands()) if (const auto* path = std::get_if<render::StrokePath>(&command)) {
            ++count; check(path->style.width == 4); check(path->style.join == render::LineJoin::round);
            check(std::ranges::count(path->path.verbs, render::PathVerb::cubic) >= 4);
        }
        check_eq(count, std::size_t{1});
        check(draw(middle).hash() != draw(a).hash());
    });

    suite.test("incompatible solid and gradient paints mix on the current geometry without losing opaque interior", [] {
        const auto a = scene(triangle(), render::Paint::solid(geom::Color{1, 0, 0, 1}));
        const std::array<render::GradientStop, 2> stops{{{0, geom::Color{0, 0, 1, 1}}, {1, geom::Color{0, 1, 1, 1}}}};
        render::Paint gradient; gradient.kind = render::Paint::Kind::gradient;
        gradient.gradient.start = {32, 96}; gradient.gradient.end = {160, 96}; gradient.gradient.stops = stops;
        const auto b = scene(circle(), gradient);
        const auto morph = transition(a, b);
        check(morph.path_morphs()[0].material_mode == mo::PathMaterialMode::prepared_mix);
        const auto middle = at(morph, 0.5);
        shared_geometry(middle);
        const auto image = draw(middle);
        const auto color = image.at(96, 96);
        check(color.a >= 254, "once-only coverage does not create a .75-alpha hole");
        check(color.r > 100 && color.b > 100, "both incompatible paints actually contribute");
        check(draw(at(morph, 0)).hash() == draw(a).hash());
        check(draw(at(morph, 1)).hash() == draw(b).hash());
    });

    suite.test("different materials on the same image do not cancel geometry morph or silently switch lighting", [] {
        render::ImageStore images;
        const auto albedo = images.add(4, 4, std::vector<raster::Rgba8>(16, raster::Rgba8{255, 255, 255, 255}));
        const auto normal = images.add(4, 4, std::vector<raster::Rgba8>(16, raster::Rgba8{128, 128, 255, 255}));
        render::Material warm, cool;
        warm.normal = normal; warm.light = {0, 0}; warm.light_z = 1; warm.bands = 0; warm.rim_strength = 0;
        warm.key = {1, 0.1f, 0.1f, 1}; cool = warm; cool.key = {0.1f, 0.1f, 1, 1};
        render::Paint paint; paint.kind = render::Paint::Kind::image; paint.image = albedo;
        paint.image_transform = geom::Affine::scale(48, 48); paint.color = geom::Color::white();
        paint.material = &warm; const auto a = scene(triangle(), paint);
        paint.material = &cool; const auto b = scene(circle(), paint);
        const auto morph = transition(a, b);
        check_eq(morph.stats().paths_prepared, std::size_t{1});
        check(morph.path_morphs()[0].material_mode == mo::PathMaterialMode::prepared_mix);
        const auto middle = at(morph, 0.5); shared_geometry(middle);
        const auto color = draw(middle, &images).at(96, 96);
        check(color.r > 100 && color.b > 100 && color.g < 50 && color.a >= 254);
    });

    suite.test("arbitrary seek and reverse are deterministic and a solid snapshot retarget starts on actual current geometry", [] {
        const auto paint = render::Paint::solid(geom::Color::white());
        const auto a = scene(triangle(), paint), b = scene(circle(), paint);
        const auto morph = transition(a, b);
        const auto before = at(morph, 0.37);
        const auto expected = draw(before).hash();
        for (double time : {1.0, 0.9, 0.2, 0.8, 0.0}) (void)at(morph, time);
        check(draw(at(morph, 0.37)).hash() == expected);
        const mo::Morph interrupted(morph.snapshot(0.37), mo::Matter::extract(a, resolver()), options(), 2);
        check_eq(interrupted.stats().paths_prepared, std::size_t{1});
        check_eq(interrupted.stats().from_only, std::size_t{0});
        check(draw(at(interrupted, 2)).hash() == expected, "retarget starts at the actual sampled shape, not a nearest endpoint");
        check_eq(fills(at(interrupted, 2.3)).size(), std::size_t{1});
        check(draw(at(interrupted, 3)).hash() == draw(a).hash());
    });

    suite.test("repeated mixed-material retargets keep one geometry and a flat bounded material", [] {
        const auto a = scene(triangle(), render::Paint::solid(geom::Color{1, 0, 0, 1}));
        const std::array<render::GradientStop, 2> stops{{{0, geom::Color{0, 0, 1, 1}}, {1, geom::Color{0, 1, 1, 1}}}};
        render::Paint gradient; gradient.kind = render::Paint::Kind::gradient;
        gradient.gradient.start = {32, 96}; gradient.gradient.end = {160, 96}; gradient.gradient.stops = stops;
        const auto b = scene(circle(), gradient);
        auto current = std::make_unique<mo::Morph>(transition(a, b));
        for (unsigned i = 0; i < 5; ++i) {
            const double now = current->start() + 0.37;
            const auto before = draw(at(*current, now)).hash();
            const auto target = scene(i % 2 ? circle() : triangle(), render::Paint::solid({0.1f * static_cast<float>(i + 1), 0.7f, 0.2f, 1}));
            auto next = std::make_unique<mo::Morph>(current->snapshot(now), mo::Matter::extract(target, resolver()), options(), now);
            check_eq(next->stats().from_only, std::size_t{0});
            check(draw(at(*next, now)).hash() == before);
            const auto middle = at(*next, now + 0.25);
            const auto paths = fills(middle); check_eq(paths.size(), std::size_t{1});
            if (!paths.empty()) {
                check(paths[0]->paint.kind == render::Paint::Kind::mix);
                check(paths[0]->paint.mix.size() <= 7);
                for (const auto& term : paths[0]->paint.mix) check(term.paint.kind != render::Paint::Kind::mix);
            }
            check(next->path_morphs()[0].material_mode == mo::PathMaterialMode::prepared_mix);
            check(!next->path_morphs()[0].material_error);
            current = std::move(next);
        }
    });

    suite.test("legacy compatible open paths retain interpolation and unsupported new topology is diagnosed", [] {
        render::Path line, bent;
        line.move_to({20, 20}).line_to({80, 20}); bent.move_to({20, 20}).line_to({80, 80});
        const auto paint = render::Paint::solid(geom::Color::white());
        const auto compatible = transition(scene(line, paint, 2), scene(bent, paint, 2));
        check_eq(compatible.stats().paths_compatible, std::size_t{1});
        const auto middle = at(compatible, 0.5);
        for (const auto& c : middle.commands()) if (const auto* stroke = std::get_if<render::StrokePath>(&c)) check(stroke->path.points[1] == geom::Point{80, 50});
        const auto unsupported = transition(scene(line, paint, 2), scene(circle(), paint, 2));
        check_eq(unsupported.stats().paths_prepared, std::size_t{0});
        check_eq(unsupported.stats().paths_unsupported, std::size_t{1});
        check(unsupported.path_morphs()[0].mode == mo::PathMorphMode::unsupported);
        check(unsupported.path_morphs()[0].issue == mo::PathMorphIssue::unsupported_contours,
              "legacy fallback is not reported as successful general shape morph");
    });

    suite.test("warm prepared path draws do not allocate per-frame geometry or mutate the plan", [] {
        const auto paint = render::Paint::solid(geom::Color::white());
        const auto a = scene(triangle(), paint), b = scene(circle(), paint);
        const auto morph = transition(a, b);
        render::DisplayList out; out.reserve(64);
        morph.draw(0.5, out); out.clear(); morph.draw(0.6, out);
        const auto count = allocations.load(std::memory_order_relaxed);
        for (unsigned i = 0; i < 1000; ++i) {
            out.clear(); morph.draw(0.1 + static_cast<double>(i % 801) / 1000, out);
        }
        const auto after = allocations.load(std::memory_order_relaxed);
        check_eq(after, count);
        check_eq(morph.stats().paths_prepared, std::size_t{1});
    });

    suite.test(threads ? "concurrent draws use independent caller-owned recording buffers"
                       : "threadless draws use independent caller-owned recording buffers", [] {
        const auto paint = render::Paint::solid(geom::Color::white());
        const auto a = scene(triangle(), paint), b = scene(circle(), paint);
        const auto morph = transition(a, b);
        const auto expected = at(morph, 0.37).hash();
        std::atomic<unsigned> wrong{0};
        auto run = [&] {
            render::DisplayList out;
            for (unsigned i = 0; i < 100; ++i) {
                out.clear(); morph.draw(0.37, out);
                if (out.hash() != expected) wrong.fetch_add(1, std::memory_order_relaxed);
            }
        };
        if constexpr (threads) { std::jthread first(run), second(run); }
        else { run(); run(); }
        check_eq(wrong.load(), 0u);
    });
    return suite.run(argc, argv);
}
