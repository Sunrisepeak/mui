// A field on screen as one image: every pixel the palette's colour of its
// cell, after every step; only the changed tiles recoloured and named, so a
// renderer redraws only their rows — and the incremental frame is the full
// one, byte for byte; one draw command however large the field.
import std;
import xengine.cellular;
import xengine.cellular.present;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
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
// The same field by a shade: the path's colour by its row, uneven; what has burnt charred; what burns, by its
// age, aflame — and that is the field's light.
ca::Shade shade() {
    ca::Shade out;
    out.base = {0xff20180b, 0xffb8a38f};  // straight 0xAABBGGRR
    out.grain = {0, 24};
    out.tints.push_back({.channel = 1, .from = 1, .to = 2, .color = 0xe0101418});
    out.tints.push_back({.channel = 2, .from = 0, .to_by_row = {0, 3}, .color = 0xff28b4ff, .emits = true});
    return out;
}
bool images_are(const xrender::ImageStore& store, const ca::FieldPresenter& view, std::span<const std::uint32_t> cells, const ca::Shade& by) {
    const raster::Surface* image = store.get(view.image());
    const raster::Surface* light = store.get(view.light());
    if (!image || !light) return false;
    const std::size_t area = static_cast<std::size_t>(view.shape().width) * view.shape().height;
    for (std::uint32_t y = 0; y < view.shape().height; ++y)
        for (std::uint32_t x = 0; x < view.shape().width; ++x) {
            const ca::Shaded want = ca::shade_of(by, cells, area, static_cast<std::size_t>(y) * view.shape().width + x);
            if (std::bit_cast<std::uint32_t>(image->at(static_cast<int>(x), static_cast<int>(y))) != want.pixel) return false;
            if (std::bit_cast<std::uint32_t>(light->at(static_cast<int>(x), static_cast<int>(y))) != want.light) return false;
            if (view.shaded(cells, x, y) != want) return false;
        }
    return true;
}
}  // namespace

UTest cases{"simulation.cellular.present", [] {

    "every pixel is its cell's colour after every step; only the changed tiles are recoloured and named"_test = [] {
        const ca::Shape shape{100, 70};
        const auto rule_list = rules();
        const auto program = ca::Program::prepare(3, rule_list);
        expect(program.has_value());
        if (!program) return;
        xrender::ImageStore store;
        auto view = ca::FieldPresenter::make(store, shape, 3, palette(), {16, 16});
        expect(view.has_value()) << (view ? "" : view.error());
        if (!view) return;
        auto cells = field(shape), next = cells;
        const auto first = view->present(cells);
        expect(first && first->whole && first->tiles == 7 * 5) << "the first present: all 35 tiles";
        expect(image_is(store, *view, cells)) << "the first picture";
        std::vector<std::uint8_t> mask(35);
        for (int step = 0; step < 60; ++step) {
            expect(program->step(shape, cells, next, {.tile = {16, 16}, .changed_tiles = mask}).has_value());
            cells.swap(next);
            const auto before = store.revision(view->image());
            const auto report = view->present(cells, mask);
            const auto changed = static_cast<std::size_t>(std::ranges::count(mask, 1));
            expect(report && report->tiles == changed) << std::format("step {}: {} tiles recoloured, {} changed", step, report ? report->tiles : 0, changed);
            expect(store.revision(view->image()) == before + (changed ? 1 : 0)) << "one update when something changed, none otherwise";
            std::vector<geom::PixelRect> rects;
            expect(!changed || (store.changed_since(view->image(), before, rects) &&
                               std::accumulate(rects.begin(), rects.end(), 0, [](int sum, const geom::PixelRect& r) { return sum + r.width * r.height; }) <=
                                   static_cast<int>(changed) * 256)) << "the update names the changed tiles' cells only";
            expect(image_is(store, *view, cells)) << std::format("step {}: the picture", step);
        }
        // Comparing presents finds the same tiles.
        auto other = ca::FieldPresenter::make(store, shape, 3, palette(), {16, 16});
        auto replay = field(shape), replay_next = replay;
        (void)other->present(replay);
        for (int step = 0; step < 20; ++step) {
            expect(program->step(shape, replay, replay_next, {.tile = {16, 16}, .changed_tiles = mask}).has_value());
            replay.swap(replay_next);
            const auto report = other->present(replay);
            expect(report && report->tiles == static_cast<std::size_t>(std::ranges::count(mask, 1))) << "found by comparison: the mask's tiles";
        }
        expect(image_is(store, *other, replay)) << "and the same picture";
    };

    "drawn: one command; redrawing only the damaged rows gives the full frame byte for byte"_test = [] {
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
        expect(eq(list.commands().size(), std::size_t{2}));
        raster::Surface incremental(W, H);
        xrender::CpuRenderer renderer;
        xrender::DamageTracker tracker;
        (void)renderer.render(list, incremental, resources, {.rows = tracker.update(list, W, H, 1, resources)});
        std::size_t most = 0;
        for (int step = 0; step < 40; ++step) {
            expect(program->step(shape, cells, next, {.tile = {32, 32}, .changed_tiles = mask}).has_value());
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
            expect(std::ranges::equal(incremental.bytes(), full.bytes())) << std::format("step {}: the incremental frame is the full one", step);
        }
        expect(most > 0 && most < static_cast<std::size_t>(H)) << std::format("at most {} of {} rows redrawn a step", most, H);
    };

    "by a shade: every pixel is its cell's shade and its light after every step, by the changed tiles; the glow is drawn from the light"_test = [] {
        const ca::Shape shape{100, 70};
        const auto rule_list = rules();
        const auto program = ca::Program::prepare(3, rule_list);
        expect(fatal(program.has_value()));
        xrender::ImageStore store;
        const ca::Shade by = shade();
        auto view = ca::FieldPresenter::make(store, shape, 3, by, {16, 16});
        expect(fatal(view.has_value())) << (view ? "" : view.error());
        expect(view->glows() && eq(store.size(), std::size_t{2})) << "the field and its light";
        auto cells = field(shape), next = cells;
        const auto first = view->present(cells);
        expect(first && first->whole && first->tiles == 7 * 5);
        expect(images_are(store, *view, cells, by)) << "the first picture";
        std::vector<std::uint8_t> mask(35);
        std::size_t lit = 0;
        for (int step = 0; step < 60; ++step) {
            expect(program->step(shape, cells, next, {.tile = {16, 16}, .changed_tiles = mask}).has_value());
            cells.swap(next);
            const auto before = store.revision(view->image());
            const auto before_light = store.revision(view->light());
            const auto report = view->present(cells, mask);
            const auto changed = static_cast<std::size_t>(std::ranges::count(mask, 1));
            expect(report && report->tiles == changed) << std::format("step {}: {} tiles recoloured, {} changed", step, report ? report->tiles : 0, changed);
            expect(store.revision(view->image()) == before + (changed ? 1 : 0) && store.revision(view->light()) == before_light + (changed ? 1 : 0))
                << "the two images are updated together, when something changed";
            expect(images_are(store, *view, cells, by)) << std::format("step {}: the picture and the light", step);
            const raster::Surface* light = store.get(view->light());
            lit += static_cast<std::size_t>(std::ranges::count_if(light->pixels(), [](raster::Rgba8 pixel) { return pixel.a != 0; }));
        }
        expect(lit > 60u) << std::format("{} lit cells over 60 steps: what burns is the light", lit);

        // On screen: the field, then its light blurred and added. A cell beside the fire is brighter for the glow;
        // far from it nothing changes; and redrawing only the damaged rows gives the full frame byte for byte.
        constexpr int W = 400, H = 280;
        const xrender::Resources resources{&store, nullptr};
        xrender::DisplayList list;
        const auto scene = [&](bool glow) {
            list.clear();
            list.fill_rect({0, 0, W, H}, geom::Color::hex(0x0b1016));
            view->draw(list, {0, 0, W, H});  // four pixels a cell
            if (glow) view->draw_glow(list, {0, 0, W, H}, 6);
        };
        scene(true);
        expect(eq(list.commands().size(), std::size_t{5})) << "the background, the field, and a layer with the light in it";
        raster::Surface plain(W, H), glowing(W, H);
        {
            scene(false);
            xrender::CpuRenderer renderer;
            (void)renderer.render(list, plain, resources);
            scene(true);
            xrender::CpuRenderer again;
            (void)again.render(list, glowing, resources);
        }
        std::size_t brighter = 0, darker = 0, unlit_brighter = 0;
        const raster::Surface* light = store.get(view->light());
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const auto without = plain.at(x, y), with = glowing.at(x, y);
                const int gain = (with.r - without.r) + (with.g - without.g) + (with.b - without.b);
                if (gain > 0) ++brighter;
                if (with.r < without.r || with.g < without.g || with.b < without.b) ++darker;
                if (gain > 0 && light->at(x / 4, y / 4).a == 0) ++unlit_brighter;
            }
        expect(eq(darker, std::size_t{0})) << "light is added: nothing is darker for it";
        expect(brighter > 0u && unlit_brighter > 0u) << std::format("{} pixels brighter, {} of them in cells that give no light themselves", brighter, unlit_brighter);
        expect(brighter < static_cast<std::size_t>(W) * H / 4) << std::format("{} of {} pixels: the glow stays near the fire", brighter, W * H);

        raster::Surface incremental(W, H);
        xrender::CpuRenderer renderer;
        xrender::DamageTracker tracker;
        scene(true);
        (void)renderer.render(list, incremental, resources, {.rows = tracker.update(list, W, H, 1, resources)});
        for (int step = 0; step < 12; ++step) {
            expect(program->step(shape, cells, next, {.tile = {16, 16}, .changed_tiles = mask}).has_value());
            cells.swap(next);
            (void)view->present(cells, mask);
            scene(true);
            (void)renderer.render(list, incremental, resources, {.rows = tracker.update(list, W, H, 1, resources)});
            raster::Surface full(W, H);
            xrender::CpuRenderer fresh;
            (void)fresh.render(list, full, resources);
            expect(std::ranges::equal(incremental.bytes(), full.bytes())) << std::format("step {}: the incremental frame with its glow is the full one", step);
        }

        // Another pack: the look changes and the cells do not.
        ca::Shade night = by;
        night.base = {0xff000000, 0xff404040};
        expect(view->reshade(night).has_value());
        const auto report = view->present(cells);
        expect(report && report->whole && report->tiles == 7 * 5) << "every tile recoloured";
        expect(images_are(store, *view, cells, night)) << "in the new shade";
        ca::Shade dark = by;
        for (auto& tint : dark.tints) tint.emits = false;
        expect(!view->reshade(dark)) << "a shade without light for a presenter with one";
        expect(!view->recolor(palette())) << "a palette for a presenter by a shade";
        ca::Shade bad = by;
        bad.tints[0].channel = 3;
        expect(!view->reshade(bad) && !ca::FieldPresenter::make(store, shape, 3, bad)) << "a tint reading a channel the field has not";
        auto flat = ca::FieldPresenter::make(store, shape, 3, palette());
        expect(!flat->reshade(by)) << "a shade for a presenter by a palette";
        expect(!flat->glows() && eq(flat->light().id, 0u) && flat->shaded(cells, 1, 1) == ca::Shaded{});
        // Without light, one image; released, both are gone.
        const auto images = store.size();
        auto unlit = ca::FieldPresenter::make(store, shape, 3, dark);
        expect(unlit && !unlit->glows() && eq(store.size(), images + 1));
        unlit->release();
        view->release();
        expect(eq(store.size(), images - 2));
    };

    "a snapshot holding the image keeps its pixels; a new palette recolours everything; bad input is refused"_test = [] {
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
        expect(std::ranges::equal(held->pixels(), kept)) << "what another thread holds never changes";
        expect(image_is(store, *view, cells)) << "and the image is current";
        auto bright = palette();
        bright.fallback = geom::Color::white();
        expect(view->recolor(bright).has_value());
        const auto report = view->present(cells);
        expect(report && report->whole && report->tiles == 5 * 4) << "every tile recoloured";
        expect(image_is(store, *view, cells)) << "in the new palette";
        expect(!view->present(std::span(cells).first(10))) << "too few values";
        expect(!view->present(cells, std::vector<std::uint8_t>(3))) << "a mask of the wrong size";
        expect(!ca::FieldPresenter::make(store, shape, 1, palette())) << "a swatch testing a channel the field has not";
        expect(!ca::FieldPresenter::make(store, {0, 4}, 3, palette())) << "an empty field";
        const auto images = store.size();
        {
            auto gone = ca::FieldPresenter::make(store, shape, 3, palette());
            expect(eq(store.size(), images + 1));
            gone->release();
        }
        expect(eq(store.size(), images));
        { auto outlives = ca::FieldPresenter::make(store, shape, 3, palette()); }  // never touches the store when it goes
        expect(eq(store.size(), images + 1));
    };
}};
