// M3.5's four signature shots (convergence plan §11), headless, on whatever
// device this machine has: "一" becoming "二" (the second bar growing out of
// the first), a stretch of the outline of "一" running round it, the
// crossbar of an "H" pulled down and let go on a spring, a ball dropped.
// Each sampled through its course — ends, middles, the spring's overshoot —
// drawn by the CPU reference and by the GPU renderer: the same pixels within
// the vector suite's tolerance. The CPU frames are kept as a strip a shot.
import std;
import mlibs.rhi;
import mlibs.geom;
import mlibs.image;
import xrender.rhi.sdl;
import xrender.raster;
import xrender.vector;
import hanim.engine;
import hanim.render.vector;
import mlibs.hash;
import mcpplibs.os;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace hanim;
namespace rhi = mlibs::rhi;
namespace geom = mlibs::geom;
namespace raster = xrender::raster;

namespace {
template <class T, class E>
T take(std::expected<T, E> result) {
    if constexpr (requires { result.error().text(); })
        expect(bool(result)) << (result ? std::string() : result.error().text());
    else
        expect(bool(result));
    if (!result) std::abort();
    if constexpr (!std::is_void_v<T>) return std::move(*result);
}
constexpr int W = 128, H = 96;

Contour box(double x, double y, double w, double h) {
    const std::array edges{ShapeSegment::line({x + w, y}), ShapeSegment::line({x + w, y + h}), ShapeSegment::line({x, y + h})};
    return take(Contour::copy({{x, y}, edges, true}));
}
// A shot: its engine, its program, the moments it is looked at.
struct Shot {
    std::string name;
    Engine engine = take(Engine::make());
    std::optional<PreparedProgram> plan;
    std::vector<double> moments;
};

std::unique_ptr<Shot> one_to_two() {
    auto shot = std::make_unique<Shot>();
    shot->name = "one-to-two";
    const auto one = take(shot->engine.create_object({{{.id = "glyph", .local = {.tx = 24, .ty = 20}, .contours = {box(0, 30, 80, 10)}, .fill = Rgba{.95f, .95f, .9f, 1}}}}));
    const auto two = take(shot->engine.create_object({{{.id = "glyph", .local = {.tx = 24, .ty = 20}, .contours = {box(8, 10, 64, 10), box(0, 46, 80, 10)}, .fill = Rgba{.95f, .95f, .9f, 1}}}}));
    shot->plan = take(shot->engine.prepare(anim::morph(one, two).duration(seconds(1)).easing(ease::in_out_cubic)));
    shot->moments = {0, .2, .4, .5, .6, .8, 1};
    return shot;
}
std::unique_ptr<Shot> trace() {
    auto shot = std::make_unique<Shot>();
    shot->name = "trace";
    const auto bar = take(shot->engine.create_object({{{.id = "bar", .local = {.tx = 24, .ty = 42}, .contours = {box(0, 0, 80, 10)}, .fill = Rgba{.95f, .95f, .9f, 1},
                                                        .stroke = PartStroke{{.9f, .2f, .15f, 1}, 3}}}}));
    shot->plan = take(shot->engine.prepare(sequence(anim::to(part(bar, "bar"), ch::trim_end = .12).duration(seconds(0)),
                                                    anim::to(part(bar, "bar"), ch::trim_offset = 1.0).duration(seconds(1)))));
    shot->moments = {0, .15, .3, .45, .6, .75, .9};
    return shot;
}
std::unique_ptr<Shot> crossbar() {
    static constexpr auto pull = ch::define<Vec2>("glyph.pull");
    auto shot = std::make_unique<Shot>();
    shot->name = "crossbar";
    const std::array<Vec2, 12> corners{{{30, 0}, {30, 45}, {70, 45}, {70, 0}, {100, 0}, {100, 100}, {70, 100}, {70, 55}, {30, 55}, {30, 100}, {0, 100}, {0, 0}}};
    std::vector<ShapeSegment> edges;
    for (const auto corner : std::span(corners).subspan(1)) edges.push_back(ShapeSegment::line(corner));
    const auto letter = take(shot->engine.create_object({{{.id = "H", .local = {.tx = 34, .ty = 10, .sx = .6, .sy = .6},
                                                           .contours = {take(Contour::copy({corners[0], edges, true}))}, .fill = Rgba{.95f, .95f, .9f, 1},
                                                           .deformers = {deform::displace(Selector::inside({35, 44}, {65, 56}, 6), pull)}}}}));
    shot->plan = take(shot->engine.prepare(sequence(anim::to(part(letter, "H"), pull = Vec2{0, 26}).duration(seconds(.3)).easing(ease::out_cubic),
                                                    anim::to(part(letter, "H"), pull = Vec2{}).spring(springs::wobbly))));
    shot->moments = {0, .3, .4, .5, .6, .8, 1.2};
    return shot;
}
std::unique_ptr<Shot> drop() {
    auto shot = std::make_unique<Shot>();
    shot->name = "drop";
    constexpr double r = 12, k = r * .5522847498307936;
    const std::array arcs{ShapeSegment::cubic({k, -r}, {r, -k}, {r, 0}), ShapeSegment::cubic({r, k}, {k, r}, {0, r}),
                          ShapeSegment::cubic({-k, r}, {-r, k}, {-r, 0}), ShapeSegment::cubic({-r, -k}, {-k, -r}, {0, -r})};
    const auto ball = take(shot->engine.create_object({{{.id = "ball", .local = {.tx = 64, .ty = 14}, .contours = {take(Contour::copy({{0, -r}, arcs, true}))},
                                                         .fill = Rgba{1, .45f, .2f, 1}, .deformers = {deform::squash({0, r})}}}}));
    shot->plan = take(shot->engine.prepare(anim::drop(part(ball, "ball"), {0, 58}, {.squash = .3})));
    const double fall = 1 / 2.45;  // a first fall, of the drop's length in falls
    shot->moments = {.2 * fall, .7 * fall, 1.03 * fall, 1.5 * fall, 2.02 * fall, 2.3 * fall, 1.4};
    return shot;
}
}  // namespace

UTest cases{"xrender.sdl.signature", [] {
    "the four signature shots: CPU and GPU draw the same frames through each course"_test = [] {
        xrender::rhi::sdl::Options o;
        o.formats = xrender::gpu_shader_formats();
        if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
        auto created = xrender::rhi::sdl::create(o);
        if (!created) {
            if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", created.error().message);
            return;
        }
        rhi::Device& device = **created;
        auto gpu = take(xrender::GpuRenderer::create(device));
        auto target = device.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, W, H, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "signature"});
        expect(target.has_value());
        if (!target) return;
        const auto dir = scratch_dir("hanim-signature");
        const geom::Color ground = geom::Color::hex(0x1b2433);
        std::vector<std::unique_ptr<Shot>> shots;
        shots.push_back(one_to_two());
        shots.push_back(trace());
        shots.push_back(crossbar());
        shots.push_back(drop());
        for (auto& shot : shots) {
            const auto playback = take(shot->engine.play(*shot->plan));
            take(shot->engine.pause(playback));
            const auto drawing = take(hanim::vector_render::PreparedVectorProgram::bind(*shot->plan));
            const auto needs = drawing.requirements();
            xrender::DisplayList staging;
            std::vector<geom::Affine> world(needs.buffers.world_transforms);
            std::vector<float> parameters(needs.buffers.backend_parameters);
            std::vector<geom::Rect> layers(needs.buffers.effect_layers), bounds(needs.tracks + needs.scopes);
            std::vector<std::size_t> members(needs.scopes), order(needs.tracks), stack(needs.scope_depth);
            raster::Surface strip(W * static_cast<int>(shot->moments.size()), H);
            std::uint64_t previous = 0;
            std::size_t distinct = 0;
            for (std::size_t k = 0; k < shot->moments.size(); ++k) {
                take(shot->engine.seek(playback, seconds(shot->moments[k])));
                xrender::DisplayList list;
                take(drawing.record(take(shot->engine.program_frame(playback)).scene, list, {{&staging, world, parameters, layers}, bounds, members, order, stack}));
                raster::Surface cpu(W, H);
                xrender::CpuRenderer reference;
                (void)reference.render(list, cpu, {}, {.background = ground});
                const auto report = gpu.render(list, *target, {}, {.background = ground});
                expect(report.has_value()) << (report ? "" : report.error().message);
                const auto bytes = device.read(*target, rhi::Region{0, 0, W, H});
                expect(bytes.has_value());
                if (!bytes) continue;
                int worst = 0;
                std::size_t off = 0;
                for (std::size_t p = 0; p < cpu.pixels().size(); ++p) {
                    const auto& c = cpu.pixels()[p];
                    const auto* g = reinterpret_cast<const std::uint8_t*>(bytes->data()) + p * 4;
                    const int diff = std::max({std::abs(c.r - g[0]), std::abs(c.g - g[1]), std::abs(c.b - g[2]), std::abs(c.a - g[3])});
                    worst = std::max(worst, diff);
                    off += diff > 1;
                }
                expect(worst <= 2 && off <= cpu.pixels().size() / 500) << std::format("{} at {:.3f} s: the GPU differs by up to {} ({} pixels off by more than 1)", shot->name, shot->moments[k], worst, off);
                for (int y = 0; y < H; ++y) std::copy_n(cpu.row(y), W, strip.row(y) + static_cast<int>(k) * W);
                // The frame's own pixels: does the shot move between moments?
                const std::uint64_t h = mlibs::hash::Fnv1a{}.bytes(std::as_bytes(cpu.pixels())).value();
                distinct += h != previous;
                previous = h;
            }
            expect(distinct >= shot->moments.size() - 1) << std::format("{}: its frames change through the course ({} of {})", shot->name, distinct, shot->moments.size());
            const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(strip.pixels().data()), strip.width(), H, strip.width() * 4, mlibs::image::Alpha::premultiplied};
            expect(mlibs::image::save_png(dir / (shot->name + ".strip.png"), view).has_value()) << "the strip kept";
        }
        std::println("the four shots' strips in {}", dir.string());
        device.destroy(*target);
    };
}};
