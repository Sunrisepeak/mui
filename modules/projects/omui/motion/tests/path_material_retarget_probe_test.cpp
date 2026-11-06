// Originally RED for normal/plus layer snapshots. The public flat sampled
// material must keep this same probe green without stranding an old shape.
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
namespace geom = mlibs::geom;
namespace {
mo::Resolver resolver() {
    return {.keys = [](std::uint32_t, std::string& key, std::string&) { key = "shape"; }};
}
mo::Options options() {
    mo::Options o; o.curve.kind = mo::Curve::Kind::tween; o.curve.duration = 1; o.curve.ease = hanim::ease::linear;
    return o;
}
render::Path triangle() {
    render::Path path; path.move_to({80, 20}).line_to({20, 140}).line_to({140, 140}).close(); return path;
}
render::Path circle() {
    render::Path path; path.add_circle({80, 80}, 60); return path;
}
render::DisplayList scene(const render::Path& path, const render::Paint& paint) {
    render::DisplayList list; list.begin_node(7); list.fill_path(path, render::FillRule::nonzero, paint); list.end_node(); return list;
}
std::uint64_t pixel_hash(const render::DisplayList& list) {
    xrender::raster::Surface surface(160, 160); render::CpuRenderer cpu;
    (void)cpu.render(list, surface, {}); return surface.hash();
}
}
int main(int argc, char** argv) {
    Suite suite{"motion.path-material-retarget-probe"};
    suite.test("interrupted mixed materials retain current paint and all subsequent paints share current geometry", [] {
        const auto a = scene(triangle(), render::Paint::solid(geom::Color{1, 0, 0, 1}));
        const std::array<render::GradientStop, 2> stops{{{0, geom::Color{0, 0, 1, 1}}, {1, geom::Color{0, 1, 1, 1}}}};
        render::Paint paint; paint.kind = render::Paint::Kind::gradient;
        paint.gradient.start = {20, 80}; paint.gradient.end = {140, 80}; paint.gradient.stops = stops;
        const auto b = scene(circle(), paint);
        const auto target = scene(triangle(), render::Paint::solid(geom::Color{0, 1, 0, 1}));
        const mo::Morph first(mo::Matter::extract(a, resolver()), mo::Matter::extract(b, resolver()), options(), 0);
        render::DisplayList before; first.draw(0.4, before);
        const mo::Morph retarget(first.snapshot(0.4), mo::Matter::extract(target, resolver()), options(), 2);
        render::DisplayList restart; retarget.draw(2, restart);
        check(pixel_hash(restart) == pixel_hash(before), "actual painted snapshot, not just shape, is continuous at retarget");
        check_eq(retarget.stats().from_only, std::size_t{0});
        render::DisplayList middle; retarget.draw(2.35, middle);
        std::optional<render::PathView> geometry;
        unsigned paths = 0;
        bool one_current_geometry = true;
        for (const auto& command : middle.commands()) if (const auto* fill = std::get_if<render::FillPath>(&command)) {
            ++paths;
            if (!geometry) geometry = fill->path;
            else one_current_geometry = one_current_geometry && std::ranges::equal(geometry->verbs, fill->path.verbs) &&
                                        std::ranges::equal(geometry->points, fill->path.points);
        }
        check_eq(paths, 1u);
        for (const auto& command : middle.commands()) if (const auto* fill = std::get_if<render::FillPath>(&command)) {
            check(fill->paint.kind == render::Paint::Kind::mix);
            check_eq(fill->paint.mix.size(), std::size_t{3});
        }
        check(one_current_geometry, "no old geometry survives as a separately fading material group");
        render::DisplayList end; retarget.draw(3, end);
        check(pixel_hash(end) == pixel_hash(target));
    });
    return suite.run(argc, argv);
}
