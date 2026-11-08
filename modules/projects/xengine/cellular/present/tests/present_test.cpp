// A field on screen as one image: every pixel the palette's colour of its
// cell, after every step; only the changed tiles recoloured and named, so a
// renderer redraws only their rows — and the incremental frame is the full
// one, byte for byte; one draw command however large the field.
import std;
import xengine.cellular;
import xengine.cellular.present;
import mlibs.testing;

using namespace mlibs::testing;
namespace ca = xengine::cellular;
namespace geom = mlibs::geom;
namespace raster = xrender::raster;

namespace {
// A spark that spreads along a path (channel 0: path, 1: burning 0 none, 1 burning, 2 burnt; 2: age).
std::vector<ca::Rule> rules() {
    using C = ca::Condition;
    using W = ca::WriteKind;
    return {{{C::self({1, {1, 1}}), C::self({2, {3, 0xffffffffu}})}, {{1, W::set, 2}}},
            {{C::self({1, {1, 1}})}, {{2, W::add_saturating, 1}}},
            {{C::self({0, {1, 1}}), C::self({1, {0, 0}}), C::neighbors({1, {1, 1}}, ca::cardinal, {1, 4})}, {{1, W::set, 1}, {2, W::set, 0}}}};
}
ca::Palette palette() {
    return {{{{{1, {1, 1}}}, geom::Color::hex(0xff7a1a)}, {{{1, {2, 2}}}, geom::Color::hex(0x3a2a20)}, {{{0, {1, 1}}}, geom::Color::hex(0x8fa3b8)}},
            geom::Color::hex(0x101820)};
}
std::vector<std::uint32_t> field(ca::Shape shape) {
    const std::size_t area = static_cast<std::size_t>(shape.width) * shape.height;
    std::vector<std::uint32_t> cells(area * 3, 0);
    for (std::uint32_t x = 2; x + 2 < shape.width; ++x) cells[(shape.height / 2) * shape.width + x] = 1;  // a path across
    for (std::uint32_t y = 2; y + 2 < shape.height; ++y) cells[y * shape.width + shape.width / 3] = 1;    // and one down
    cells[area + (shape.height / 2) * shape.width + 2] = 1;                                                 // lit at its left end
    return cells;
}
bool image_is(const xrender::ImageStore& store, const ca::FieldPresenter& view, std::span<const std::uint32_t> cells) {
    const raster::Surface* image = store.get(view.image());
    if (!image) return false;
    for (std::uint32_t y = 0; y < view.shape().height; ++y)
        for (std::uint32_t x = 0; x < view.shape().width; ++x) {
            const auto want = raster::premultiply(view.color(cells, x, y));
            const auto got = image->at(static_cast<int>(x), static_cast<int>(y));
            if (got.r != want.r || got.g != want.g || got.b != want.b || got.a != want.a) return false;
        }
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    Suite s{"simulation.cellular.present"};

    s.test("every pixel is its cell's colour after every step; only the changed tiles are recoloured and named", [] {
        const ca::Shape shape{100, 70};
        const auto rule_list = rules();
        const auto program = ca::Program::prepare(3, rule_list);
        check(program.has_value());
        if (!program) return;
        xrender::ImageStore store;
        auto view = ca::FieldPresenter::make(store, shape, 3, palette(), {16, 16});
        check(view.has_value(), view ? "" : view.error());
        if (!view) return;
        auto cells = field(shape), next = cells;
        const auto first = view->present(cells);
        check(first && first->whole && first->tiles == 7 * 5, "the first present: all 35 tiles");
        check(image_is(store, *view, cells), "the first picture");
        std::vector<std::uint8_t> mask(35);
        for (int step = 0; step < 60; ++step) {
            check(program->step(shape, cells, next, {.tile = {16, 16}, .changed_tiles = mask}).has_value());
            cells.swap(next);
            const auto before = store.revision(view->image());
            const auto report = view->present(cells, mask);
            const auto changed = static_cast<std::size_t>(std::ranges::count(mask, 1));
            check(report && report->tiles == changed, std::format("step {}: {} tiles recoloured, {} changed", step, report ? report->tiles : 0, changed));
            check(store.revision(view->image()) == before + (changed ? 1 : 0), "one update when something changed, none otherwise");
            std::vector<geom::PixelRect> rects;
            check(!changed || (store.changed_since(view->image(), before, rects) &&
                               std::accumulate(rects.begin(), rects.end(), 0, [](int sum, const geom::PixelRect& r) { return sum + r.width * r.height; }) <=
                                   static_cast<int>(changed) * 256),
                  "the update names the changed tiles' cells only");
            check(image_is(store, *view, cells), std::format("step {}: the picture", step));
        }
        // Comparing presents finds the same tiles.
        auto other = ca::FieldPresenter::make(store, shape, 3, palette(), {16, 16});
        auto replay = field(shape), replay_next = replay;
        (void)other->present(replay);
        for (int step = 0; step < 20; ++step) {
            check(program->step(shape, replay, replay_next, {.tile = {16, 16}, .changed_tiles = mask}).has_value());
            replay.swap(replay_next);
            const auto report = other->present(replay);
            check(report && report->tiles == static_cast<std::size_t>(std::ranges::count(mask, 1)), "found by comparison: the mask's tiles");
        }
        check(image_is(store, *other, replay), "and the same picture");
    });

    s.test("drawn: one command; redrawing only the damaged rows gives the full frame byte for byte", [] {
        const ca::Shape shape{120, 90};
        const auto rule_list = rules();
        const auto program = ca::Program::prepare(3, rule_list);
        xrender::ImageStore store;
        auto view = ca::FieldPresenter::make(store, shape, 3, palette());
        auto cells = field(shape), next = cells;
        std::vector<std::uint8_t> mask(ca::tiles(shape, {32, 32}).width * ca::tiles(shape, {32, 32}).height);
        (void)view->present(cells);
        constexpr int W = 480, H = 360;
        const xrender::Resources resources{&store, nullptr};
        xrender::DisplayList list;
        const auto scene = [&] {
            list.clear();
            list.fill_rect({0, 0, W, H}, geom::Color::hex(0x0b1016));
            view->draw(list, {0, 0, W, H});  // four pixels a cell
        };
        scene();
        check_eq(list.commands().size(), std::size_t{2});
        raster::Surface incremental(W, H);
        xrender::CpuRenderer renderer;
        xrender::DamageTracker tracker;
        (void)renderer.render(list, incremental, resources, {.rows = tracker.update(list, W, H, 1, resources)});
        std::size_t most = 0;
        for (int step = 0; step < 40; ++step) {
            check(program->step(shape, cells, next, {.tile = {32, 32}, .changed_tiles = mask}).has_value());
            cells.swap(next);
            (void)view->present(cells, mask);
            scene();
            const auto spans = tracker.update(list, W, H, 1, resources);
            std::size_t rows = 0;
            for (auto r : spans) rows += static_cast<std::size_t>(r.bottom - r.top);
            most = std::max(most, rows);
            (void)renderer.render(list, incremental, resources, {.rows = spans});
            raster::Surface full(W, H);
            xrender::CpuRenderer fresh;
            (void)fresh.render(list, full, resources);
            check(std::ranges::equal(incremental.bytes(), full.bytes()), std::format("step {}: the incremental frame is the full one", step));
        }
        check(most > 0 && most < static_cast<std::size_t>(H), std::format("at most {} of {} rows redrawn a step", most, H));
    });

    s.test("a snapshot holding the image keeps its pixels; a new palette recolours everything; bad input is refused", [] {
        const ca::Shape shape{40, 30};
        xrender::ImageStore store;
        auto view = ca::FieldPresenter::make(store, shape, 3, palette(), {8, 8});
        auto cells = field(shape);
        (void)view->present(cells);
        const auto held = store.shared(view->image());
        const std::vector<raster::Rgba8> kept(held->pixels().begin(), held->pixels().end());
        cells[3 * 40 + 3] = 1;
        (void)view->present(cells);
        cells[5 * 40 + 30] = 1;
        (void)view->present(cells);
        check(std::ranges::equal(held->pixels(), kept), "what another thread holds never changes");
        check(image_is(store, *view, cells), "and the image is current");
        auto bright = palette();
        bright.fallback = geom::Color::white();
        check(view->recolor(bright).has_value());
        const auto report = view->present(cells);
        check(report && report->whole && report->tiles == 5 * 4, "every tile recoloured");
        check(image_is(store, *view, cells), "in the new palette");
        check(!view->present(std::span(cells).first(10)), "too few values");
        check(!view->present(cells, std::vector<std::uint8_t>(3)), "a mask of the wrong size");
        check(!ca::FieldPresenter::make(store, shape, 1, palette()), "a swatch testing a channel the field has not");
        check(!ca::FieldPresenter::make(store, {0, 4}, 3, palette()), "an empty field");
        const auto images = store.size();
        {
            auto gone = ca::FieldPresenter::make(store, shape, 3, palette());
            check_eq(store.size(), images + 1);
            gone->release();
        }
        check_eq(store.size(), images);
        { auto outlives = ca::FieldPresenter::make(store, shape, 3, palette()); }  // never touches the store when it goes
        check_eq(store.size(), images + 1);
    });

    return s.run(argc, argv);
}
