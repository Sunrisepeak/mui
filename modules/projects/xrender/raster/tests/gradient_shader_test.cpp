// Shader setup is a real warm-path allocation contract, measured directly;
// no time threshold and no renderer allocations can hide a temporary stop copy.
import std;
import mlibs.geom;
import xrender.raster;
import mcpplibs.mtest;
using namespace mcpplibs::mtest;
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
constexpr std::array<raster::Stop, 4> ordered{{
    {0, mlibs::geom::Color{1, 0, 0, 1}}, {0.4f, mlibs::geom::Color{0, 1, 0, 0.5f}},
    {0.7f, mlibs::geom::Color{1, 1, 0, 0.8f}}, {1, mlibs::geom::Color{0, 0, 1, 1}}}};
constexpr std::array<raster::Stop, 4> shuffled{{ordered[2], ordered[0], ordered[3], ordered[1]}};
raster::Paint gradient(std::span<const raster::Stop> stops) {
    raster::Paint paint; paint.kind = raster::Paint::Kind::gradient;
    paint.start = {0, 0}; paint.end = {32, 0}; paint.stops = stops; return paint;
}
}
UTest cases{"raster.gradient-setup", [] {
    "unsorted stops produce the same full premultiplied lookup table as ordered input"_test = [] {
        const auto a = gradient(ordered), b = gradient(shuffled);
        const raster::Shader sa(a), sb(b);
        expect(std::ranges::equal(sa.table(), sb.table()));
        expect(sa.table().front() == raster::Rgba8{255, 0, 0, 255});
        expect(sa.table().back() == raster::Rgba8{0, 0, 255, 255});
    };
    "duplicate offsets retain stable hard-stop ordering and empty gradients remain transparent"_test = [] {
        const std::array<raster::Stop, 4> stops{{
            {1, mlibs::geom::Color{0, 0, 1, 1}}, {0.5f, mlibs::geom::Color{1, 0, 0, 1}},
            {0, mlibs::geom::Color{1, 1, 1, 1}}, {0.5f, mlibs::geom::Color{0, 1, 0, 1}}}};
        auto sorted = stops; std::ranges::stable_sort(sorted, {}, &raster::Stop::offset);
        const auto a = gradient(stops), b = gradient(sorted);
        const raster::Shader sa(a), sb(b);
        expect(std::ranges::equal(sa.table(), sb.table()));
        expect(sa.table()[127].r > 250 && sa.table()[128].g > 250) << "hard stop changes from first red to last green";
        const auto empty = gradient({}); const raster::Shader clear(empty);
        expect(std::ranges::all_of(clear.table(), [](auto p) { return p == raster::Rgba8{}; }));
    };
    "both ordered and shuffled gradient setup avoid per-frame scalar allocations"_test = [] {
        auto a = gradient(ordered), b = gradient(shuffled);
        const auto before = allocations.load(std::memory_order_relaxed);
        bool valid = true;
        for (unsigned i = 0; i < 1000; ++i) {
            const raster::Shader shader(i % 2 ? a : b);
            valid = valid && shader.table()[0].r == 255;
        }
        const auto after = allocations.load(std::memory_order_relaxed);
        expect(valid); expect(eq(after, before));
    };
    "caller-owned mixed gradient setup and shading retain their warmed storage"_test = [] {
        const auto a = gradient(ordered), b = gradient(shuffled);
        const std::array<raster::WeightedPaint, 2> terms{{{a, 0.37f}, {b, 0.63f}}};
        raster::Paint paint; paint.kind = raster::Paint::Kind::mix; paint.mix = terms;
        raster::MixShader shader;
        std::array<raster::Rgba8, 32> output;
        expect(shader.reset(paint)); shader.shade(0, 0, 32, output.data());
        const auto before = allocations.load(std::memory_order_relaxed);
        bool okay = true;
        for (unsigned i = 0; i < 1000; ++i) {
            okay = shader.reset(paint) && okay;
            shader.shade(0, 0, 32, output.data());
        }
        const auto after = allocations.load(std::memory_order_relaxed);
        expect(okay); expect(eq(after, before));
    };
}};
