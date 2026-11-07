// Representations drawn through the shared renderer: a stroked part's trim
// channels run a red stretch of the outline of "一" once round it like a
// train, a time function sampled through its whole course; a deformer bends
// the crossbar of an "H" down and back.
import std;
import hanim.engine;
import hanim.render.vector;
import xrender.raster;
import mlibs.testing;

using namespace hanim;
using namespace hanim::vector_render;
using namespace mlibs::testing;
namespace render = xrender;
namespace geom = mlibs::geom;
namespace raster = xrender::raster;

namespace {
template <class T, class E>
T take(std::expected<T, E> result) {
    check(bool(result));
    if (!result)
        std::abort();
    if constexpr (!std::is_void_v<T>)
        return std::move(*result);
}
struct Storage {
    render::DisplayList staging;
    std::vector<geom::Affine> world;
    std::vector<float> parameters;
    std::vector<geom::Rect> layers, bounds;
    std::vector<std::size_t> members, order, stack;
    explicit Storage(ProgramVectorRequirements needed)
        : world(needed.buffers.world_transforms),
          parameters(needed.buffers.backend_parameters),
          layers(needed.buffers.effect_layers),
          bounds(needed.tracks + needed.scopes),
          members(needed.scopes),
          order(needed.tracks),
          stack(needed.scope_depth) {}
    ProgramVectorScratch get() {
        return {{&staging, world, parameters, layers}, bounds, members, order, stack};
    }
};
// The bar of "一": 64 × 8 at (16, 28), its outline 144 long, starting at its top-left corner.
ObjectRef bar(Engine& engine) {
    const std::array edges{ShapeSegment::line({64, 0}), ShapeSegment::line({64, 8}),
                           ShapeSegment::line({0, 8})};
    return take(engine.create_object({{{.id = "bar",
                                        .local = {.tx = 16, .ty = 28},
                                        .contours = {take(Contour::copy({{}, edges, true}))},
                                        .fill = Rgba{1, 1, 1, 1},
                                        .stroke = PartStroke{{1, 0, 0, 1}, 3}}}}));
}
struct Centroid {
    double x = 0, y = 0;
    std::size_t count = 0;
};
Centroid red(const raster::Surface& pixels) {
    Centroid found;
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x)
            if (const auto& pixel = pixels.at(x, y);
                pixel.r > 150 && pixel.g < 100 && pixel.b < 100) {
                found.x += x + .5;
                found.y += y + .5;
                ++found.count;
            }
    if (found.count) {
        found.x /= static_cast<double>(found.count);
        found.y /= static_cast<double>(found.count);
    }
    return found;
}
}  // namespace

int main(int argc, char** argv) {
    Suite suite{"hanim.stroke"};
    suite.test("a trimmed stretch of the outline runs once round it, wherever it is sampled", [] {
        Engine engine = take(Engine::make());
        const auto glyph = bar(engine);
        // A tenth of the outline, carried round by the offset in one second.
        const auto plan = take(
            engine.prepare(sequence(anim::to(glyph, ch::trim_end = .1).duration(seconds(0)),
                                    anim::to(glyph, ch::trim_offset = 1.0).duration(seconds(1)))));
        const auto playback = take(engine.play(plan));
        const auto renderer = take(PreparedVectorProgram::bind(plan));
        Storage storage(renderer.requirements());
        const auto draw_at = [&](double time) {
            take(engine.seek(playback, seconds(time)));
            render::DisplayList output;
            take(
                renderer.record(take(engine.program_frame(playback)).scene, output, storage.get()));
            raster::Surface pixels(96, 64);
            render::CpuRenderer cpu;
            const auto report = cpu.render(output, pixels, {});
            check(output.balanced() && !report.invalid_paints);
            return pixels;
        };
        // Where the middle of the stretch is: along the top, down the right
        // end and back along the bottom.
        struct Expected {
            double time, x, y;
        };
        const std::array course{Expected{0, 23.2, 28}, Expected{.25, 59.2, 28},
                                Expected{.5, 72.8, 36}, Expected{.75, 36.8, 36}};
        std::size_t first_count = 0;
        for (const auto& [time, x, y] : course) {
            const auto found = red(draw_at(time));
            check(found.count > 20, std::format("a red stretch at {}s", time));
            check(std::abs(found.x - x) < 3 && std::abs(found.y - y) < 3,
                  std::format("at {}s the stretch is at ({:.1f}, {:.1f}), expected ({}, {})", time,
                              found.x, found.y, x, y));
            if (!first_count)
                first_count = found.count;
            check(found.count * 2 > first_count && found.count < first_count * 2,
                  "the stretch keeps its length round the course");
        }
        const auto start = draw_at(0), again = draw_at(.5), lap = draw_at(1), back = draw_at(.5);
        check(lap.pixels().size() == start.pixels().size() &&
                  std::ranges::equal(lap.pixels(), start.pixels(),
                                     [](const auto& left, const auto& right) {
                                         return left.r == right.r && left.g == right.g &&
                                                left.b == right.b && left.a == right.a;
                                     }),
              "one lap ends where it began");
        check(std::ranges::equal(again.pixels(), back.pixels(),
                                 [](const auto& left, const auto& right) {
                                     return left.r == right.r && left.g == right.g &&
                                            left.b == right.b && left.a == right.a;
                                 }),
              "seeking back draws the same frame: a function of time");
    });
    suite.test("a deformer bends the crossbar of an H down, drawn, and lets it back", [] {
        static constexpr auto pull = ch::define<Vec2>("glyph.pull");
        Engine engine = take(Engine::make());
        const std::array<Vec2, 12> corners{{{30, 0},
                                            {30, 45},
                                            {70, 45},
                                            {70, 0},
                                            {100, 0},
                                            {100, 100},
                                            {70, 100},
                                            {70, 55},
                                            {30, 55},
                                            {30, 100},
                                            {0, 100},
                                            {0, 0}}};
        std::vector<ShapeSegment> edges;
        for (const auto corner : std::span(corners).subspan(1))
            edges.push_back(ShapeSegment::line(corner));
        const auto letter = take(engine.create_object(
            {{{.id = "H",
               .local = {.tx = 8, .ty = 8, .sx = .5, .sy = .5},
               .contours = {take(Contour::copy({corners[0], edges, true}))},
               .fill = Rgba{1, 1, 1, 1},
               .deformers = {deform::displace(Selector::inside({35, 44}, {65, 56}, 6), pull)}}}}));
        const auto plan = take(engine.prepare(
            sequence(anim::to(part(letter, "H"), pull = Vec2{0, 20}).duration(seconds(0)),
                     anim::to(part(letter, "H"), pull = Vec2{}).duration(seconds(1)))));
        const auto playback = take(engine.play(plan));
        const auto renderer = take(PreparedVectorProgram::bind(plan));
        Storage storage(renderer.requirements());
        const auto lit_below_bar = [&](double time) {
            take(engine.seek(playback, seconds(time)));
            render::DisplayList output;
            take(
                renderer.record(take(engine.program_frame(playback)).scene, output, storage.get()));
            raster::Surface pixels(64, 64);
            render::CpuRenderer cpu;
            (void)cpu.render(output, pixels, {});
            return pixels.at(33, 39).r > 128;  // (50, 62) in the letter: below the bar at rest
        };
        check(lit_below_bar(0), "pulled: the bar bends down over (50, 62)");
        check(!lit_below_bar(1), "let back: below the bar is empty again");
    });

    suite.test("a dropped ball drawn: round in the air, wide and flat on the ground, never through "
               "it",
               [] {
                   Engine engine = take(Engine::make());
                   constexpr double k = 10 * .5522847498307936;
                   const std::array arcs{ShapeSegment::cubic({k, -10}, {10, -k}, {10, 0}),
                                         ShapeSegment::cubic({10, k}, {k, 10}, {0, 10}),
                                         ShapeSegment::cubic({-k, 10}, {-10, k}, {-10, 0}),
                                         ShapeSegment::cubic({-10, -k}, {-k, -10}, {0, -10})};
                   const auto ball = take(engine.create_object(
                       {{{.id = "ball",
                          .local = {.tx = 32, .ty = 12},
                          .contours = {take(Contour::copy({{0, -10}, arcs, true}))},
                          .fill = Rgba{1, 1, 1, 1},
                          .deformers = {deform::squash({0, 10})}}}}));
                   const auto plan = take(
                       engine.prepare(anim::drop(part(ball, "ball"), {0, 40}, {.squash = .3})));
                   const auto playback = take(engine.play(plan));
                   const auto renderer = take(PreparedVectorProgram::bind(plan));
                   Storage storage(renderer.requirements());
                   // The lit span of each row: the ball's width there, and its lowest lit row.
                   const auto drawn = [&](double time) {
                       take(engine.seek(playback, seconds(time)));
                       render::DisplayList output;
                       take(renderer.record(take(engine.program_frame(playback)).scene, output,
                                            storage.get()));
                       raster::Surface pixels(64, 72);
                       render::CpuRenderer cpu;
                       (void)cpu.render(output, pixels, {});
                       int widest = 0, lowest = 0;
                       for (int y = 0; y < pixels.height(); ++y) {
                           int lit = 0;
                           for (int x = 0; x < pixels.width(); ++x)
                               lit += pixels.at(x, y).r > 128;
                           widest = std::max(widest, lit);
                           if (lit)
                               lowest = y;
                       }
                       return std::pair{widest, lowest};
                   };
                   const auto [air_width, air_low] = drawn(.1 / 2.45);
                   const auto [ground_width, ground_low] = drawn(1.05 / 2.45);
                   check(air_width <= 20 && air_low < 61,
                         std::format("round in the air: {} wide", air_width));
                   check(ground_width >= 24,
                         std::format("flattened on the ground: {} wide", ground_width));
                   check(ground_low <= 61,
                         std::format("its lowest row {} on the ground at 62", ground_low));
               });

    return suite.run(argc, argv);
}
