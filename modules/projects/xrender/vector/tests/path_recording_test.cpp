import std;
import mlibs.geom;
import xrender.vector;
import xrender.raster;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace render = xrender;
namespace geom = mlibs::geom;
static_assert(std::same_as<render::PathView,xrender::raster::PathData>);
static_assert(std::same_as<render::PathVerb,geom::PathVerb>);
static_assert(std::same_as<render::FillRule,xrender::raster::FillRule>);
namespace { std::atomic<std::size_t> allocations{0}; }
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {
void triangle(render::MutablePathView path) {
    path.verbs[0] = render::PathVerb::move; path.verbs[1] = render::PathVerb::line;
    path.verbs[2] = render::PathVerb::line; path.verbs[3] = render::PathVerb::close;
    path.points[0] = {0, 0}; path.points[1] = {10, 0}; path.points[2] = {5, 10};
}
}
UTest cases{"render.path-recording", [] {
    "one writer records arena geometry shared by fill and stroke with owned style data"_test = [] {
        render::DisplayList list;
        unsigned calls = 0;
        const auto path = list.record_path(4, 3, [&](render::MutablePathView out) { ++calls; triangle(out); });
        expect(path.has_value()); if (!path) return;
        expect(eq(calls, 1u));
        expect(path->bounds() == geom::Rect{0, 0, 10, 10});
        expect(list.fill_path(*path, render::FillRule::nonzero, render::Paint::solid(geom::Color::white())));
        std::array<float, 2> dashes{2, 3};
        expect(list.stroke_path(*path, {.width = 2, .dashes = dashes}, render::Paint::solid(geom::Color::black())));
        dashes[0] = 99;
        const auto& fill = std::get<render::FillPath>(list.commands()[0]);
        const auto& stroke = std::get<render::StrokePath>(list.commands()[1]);
        expect(fill.path.verbs.data() == stroke.path.verbs.data());
        expect(fill.path.points.data() == stroke.path.points.data());
        expect(stroke.style.dashes[0] == 2) << "style retains its own lifetime, independent of geometry sharing";
        render::DisplayList copy;
        copy.append(list); list.clear();
        expect(std::get<render::FillPath>(copy.commands()[0]).path.points[2] == geom::Point{5, 10}) << "append owns a safe independent copy";
    };
    "foreign, cleared and moved recordings fail without appending or dereferencing expired data"_test = [] {
        render::DisplayList a, b;
        const auto first = a.record_path(4, 3, [](render::MutablePathView path) { triangle(path); });
        expect(first.has_value()); if (!first) return;
        const auto paint = render::Paint::solid(geom::Color::white());
        expect(!b.fill_path(*first, render::FillRule::nonzero, paint)); expect(b.empty());
        a.clear();
        expect(!a.fill_path(*first, render::FillRule::nonzero, paint)); expect(a.empty());
        const auto second = a.record_path(4, 3, [](render::MutablePathView path) { triangle(path); });
        expect(second.has_value()); if (!second) return;
        render::DisplayList moved(std::move(a));
        expect(!a.fill_path(*second, render::FillRule::nonzero, paint));
        expect(!moved.fill_path(*second, render::FillRule::nonzero, paint));
        const auto before_assignment = b.record_path(4, 3, [](render::MutablePathView path) { triangle(path); });
        expect(before_assignment.has_value()); if (!before_assignment) return;
        b = std::move(moved);
        expect(!b.fill_path(*before_assignment, render::FillRule::nonzero, paint));
        expect(!b.stroke_path(*first, {.width = 1}, paint));
    };
    "invalid counts, verbs and nonfinite positions are reported rather than becoming commands"_test = [] {
        render::DisplayList list;
        expect(!list.record_path(0, 0, [](render::MutablePathView path) { triangle(path); }));
        expect(!list.record_path(4, std::numeric_limits<std::size_t>::max(), [](render::MutablePathView path) { triangle(path); }));
        expect(!list.record_path(4, 3, [](render::MutablePathView path) { triangle(path); path.verbs[1] = static_cast<render::PathVerb>(255); }));
        expect(!list.record_path(4, 3, [](render::MutablePathView path) { triangle(path); path.verbs[0] = render::PathVerb::line; }));
        expect(!list.record_path(4, 3, [](render::MutablePathView path) { triangle(path); path.verbs[1] = render::PathVerb::cubic; }));
        expect(!list.record_path(4, 3, [](render::MutablePathView path) { triangle(path); path.points[0].x = std::numeric_limits<float>::infinity(); }));
        expect(list.empty());
    };
    "warm caller-owned recording and repeated painting allocate no scalar heap memory"_test = [] {
        render::DisplayList list;
        list.reserve(8);
        const auto paint = render::Paint::solid(geom::Color::white());
        auto frame = [&] {
            list.clear();
            const auto path = list.record_path(4, 3, [](render::MutablePathView path) { triangle(path); });
            return path && list.fill_path(*path, render::FillRule::nonzero, paint) && list.stroke_path(*path, {.width = 1}, paint);
        };
        expect(frame()); expect(frame());
        const auto before = allocations.load(std::memory_order_relaxed);
        bool ok = true;
        for (unsigned i = 0; i < 1000; ++i) ok = frame() && ok;
        const auto after = allocations.load(std::memory_order_relaxed);
        expect(ok); expect(eq(after, before));
    };
}};
