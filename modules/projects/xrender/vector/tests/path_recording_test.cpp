import std;
import openmui.geom;
import openmui.render.vector;
import xrender.raster;
import openmui.testing;

using namespace omui::testing;
namespace render = omui::render;
namespace geom = omui::geom;
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
int main(int argc, char** argv) {
    Suite suite{"render.path-recording"};
    suite.test("one writer records arena geometry shared by fill and stroke with owned style data", [] {
        render::DisplayList list;
        unsigned calls = 0;
        const auto path = list.record_path(4, 3, [&](render::MutablePathView out) { ++calls; triangle(out); });
        check(path.has_value()); if (!path) return;
        check_eq(calls, 1u);
        check(path->bounds() == geom::Rect{0, 0, 10, 10});
        check(list.fill_path(*path, render::FillRule::nonzero, render::Paint::solid(geom::Color::white())));
        std::array<float, 2> dashes{2, 3};
        check(list.stroke_path(*path, {.width = 2, .dashes = dashes}, render::Paint::solid(geom::Color::black())));
        dashes[0] = 99;
        const auto& fill = std::get<render::FillPath>(list.commands()[0]);
        const auto& stroke = std::get<render::StrokePath>(list.commands()[1]);
        check(fill.path.verbs.data() == stroke.path.verbs.data());
        check(fill.path.points.data() == stroke.path.points.data());
        check(stroke.style.dashes[0] == 2, "style retains its own lifetime, independent of geometry sharing");
        render::DisplayList copy;
        copy.append(list); list.clear();
        check(std::get<render::FillPath>(copy.commands()[0]).path.points[2] == geom::Point{5, 10}, "append owns a safe independent copy");
    });
    suite.test("foreign, cleared and moved recordings fail without appending or dereferencing expired data", [] {
        render::DisplayList a, b;
        const auto first = a.record_path(4, 3, [](render::MutablePathView path) { triangle(path); });
        check(first.has_value()); if (!first) return;
        const auto paint = render::Paint::solid(geom::Color::white());
        check(!b.fill_path(*first, render::FillRule::nonzero, paint)); check(b.empty());
        a.clear();
        check(!a.fill_path(*first, render::FillRule::nonzero, paint)); check(a.empty());
        const auto second = a.record_path(4, 3, [](render::MutablePathView path) { triangle(path); });
        check(second.has_value()); if (!second) return;
        render::DisplayList moved(std::move(a));
        check(!a.fill_path(*second, render::FillRule::nonzero, paint));
        check(!moved.fill_path(*second, render::FillRule::nonzero, paint));
        const auto before_assignment = b.record_path(4, 3, [](render::MutablePathView path) { triangle(path); });
        check(before_assignment.has_value()); if (!before_assignment) return;
        b = std::move(moved);
        check(!b.fill_path(*before_assignment, render::FillRule::nonzero, paint));
        check(!b.stroke_path(*first, {.width = 1}, paint));
    });
    suite.test("invalid counts, verbs and nonfinite positions are reported rather than becoming commands", [] {
        render::DisplayList list;
        check(!list.record_path(0, 0, [](render::MutablePathView path) { triangle(path); }));
        check(!list.record_path(4, std::numeric_limits<std::size_t>::max(), [](render::MutablePathView path) { triangle(path); }));
        check(!list.record_path(4, 3, [](render::MutablePathView path) { triangle(path); path.verbs[1] = static_cast<render::PathVerb>(255); }));
        check(!list.record_path(4, 3, [](render::MutablePathView path) { triangle(path); path.verbs[0] = render::PathVerb::line; }));
        check(!list.record_path(4, 3, [](render::MutablePathView path) { triangle(path); path.verbs[1] = render::PathVerb::cubic; }));
        check(!list.record_path(4, 3, [](render::MutablePathView path) { triangle(path); path.points[0].x = std::numeric_limits<float>::infinity(); }));
        check(list.empty());
    });
    suite.test("warm caller-owned recording and repeated painting allocate no scalar heap memory", [] {
        render::DisplayList list;
        list.reserve(8);
        const auto paint = render::Paint::solid(geom::Color::white());
        auto frame = [&] {
            list.clear();
            const auto path = list.record_path(4, 3, [](render::MutablePathView path) { triangle(path); });
            return path && list.fill_path(*path, render::FillRule::nonzero, paint) && list.stroke_path(*path, {.width = 1}, paint);
        };
        check(frame()); check(frame());
        const auto before = allocations.load(std::memory_order_relaxed);
        bool ok = true;
        for (unsigned i = 0; i < 1000; ++i) ok = frame() && ok;
        const auto after = allocations.load(std::memory_order_relaxed);
        check(ok); check_eq(after, before);
    });
    return suite.run(argc, argv);
}
