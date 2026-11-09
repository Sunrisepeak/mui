import std;
import mlibs.geom;
import xrender.raster;
import xrender.vector;
import mcpplibs.mtest;
using namespace mcpplibs::mtest;
namespace render = xrender;
namespace raster = xrender::raster;
namespace geom = mlibs::geom;
namespace {
constexpr int W = 96, H = 80;
render::Path shape() {
    render::Path path;
    path.move_to({48.25f, 6.5f}).quad_to({91.5f, 12.25f}, {87.75f, 65.5f})
        .line_to({12.25f, 71.25f}).cubic_to({4.5f, 52.75f}, {5.25f, 20.5f}, {48.25f, 6.5f}).close();
    return path;
}
raster::Surface draw(const render::Paint& paint, bool field = false, render::ImageStore* images = nullptr) {
    render::DisplayList list;
    if (field) list.fill_rect({0, 0, W, H}, paint);
    else list.fill_path(shape(), render::FillRule::nonzero, paint);
    raster::Surface pixels(W, H); render::CpuRenderer cpu;
    const auto report = cpu.render(list, pixels, {images, nullptr});
    expect(eq(report.missing_images, std::size_t{0})); expect(eq(report.invalid_paints, std::size_t{0}));
    return pixels;
}
void reference(const render::Paint& a, const render::Paint& b, double t, render::ImageStore* images = nullptr) {
    const auto plan = render::PreparedPaintMix::prepare(a, b); expect(plan.has_value()); if (!plan) return;
    render::DisplayList list;
    const auto mix = plan->record(t, list); expect(mix.has_value()); if (!mix) return;
    const auto actual = draw(*mix, false, images);
    const auto pa = draw(a, true, images), pb = draw(b, true, images);
    const auto coverage = draw(render::Paint::solid(geom::Color::white()));
    unsigned wrong = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const auto p = pa.at(x, y), q = pb.at(x, y), c = actual.at(x, y);
        auto channel = [&](std::uint8_t aa, std::uint8_t bb) {
            const auto mixed = static_cast<std::uint32_t>(static_cast<float>(aa) * static_cast<float>(1 - t) + static_cast<float>(bb) * static_cast<float>(t) + 0.5f);
            return static_cast<std::uint8_t>(raster::mul255(mixed, coverage.at(x, y).a));
        };
        const raster::Rgba8 expected{channel(p.r, q.r), channel(p.g, q.g), channel(p.b, q.b), channel(p.a, q.a)};
        wrong += expected == c ? 0u : 1u;
        expect(c.r <= c.a && c.g <= c.a && c.b <= c.a);
    }
    expect(eq(wrong, 0u));
}
}
UTest cases{"render.paint-mix-cpu", [] {
    "premultiplied translucent materials mix before the current path coverage exactly once"_test = [] {
        reference(render::Paint::solid({1, 0, 0, 0.3f}), render::Paint::solid({0, 0, 1, 0.7f}), 0.37);
        const auto white = render::Paint::solid(geom::Color::white());
        reference(white, white, 0.5);
    };
    "gradient and normal-lit image leaves reuse the existing shaders under one geometry"_test = [] {
        const std::array<render::GradientStop, 3> stops{{{0, geom::Color{1, 0, 0, 0.4f}}, {0.4f, geom::Color{0, 1, 0, 0.8f}}, {1, geom::Color{0, 0, 1, 1}}}};
        render::Paint gradient; gradient.kind = render::Paint::Kind::gradient;
        gradient.gradient.start = {0, 0}; gradient.gradient.end = {W, H}; gradient.gradient.stops = stops;
        render::ImageStore images;
        const auto picture = images.add(2, 2, std::vector<raster::Rgba8>{{180, 100, 50, 255}, {60, 120, 200, 255}, {100, 70, 30, 128}, {20, 60, 120, 128}});
        const auto normal = images.add(2, 2, std::vector<raster::Rgba8>(4, raster::Rgba8{128, 128, 255, 255}));
        render::Material material; material.normal = normal; material.key = {0.8f, 0.4f, 1, 1};
        render::Paint image; image.kind = render::Paint::Kind::image; image.image = picture;
        image.color = {1, 1, 1, 0.7f}; image.material = &material;
        image.image_transform = geom::Affine::translate(3, 5) * geom::Affine::scale(40, 30);
        reference(gradient, image, 0.25, &images);
        reference(gradient, image, 0.75, &images);
    };
    "equal material mix preserves clipping, stroke geometry and pixel provenance"_test = [] {
        const auto leaf = render::Paint::solid(geom::Color{0.2f, 0.5f, 0.9f, 0.6f});
        const auto plan = render::PreparedPaintMix::prepare(leaf, leaf); expect(plan.has_value()); if (!plan) return;
        auto build = [&](render::DisplayList& list, bool mix) {
            list.begin_node(21); list.push_clip({10.25f, 5.5f, 60.5f, 60.25f}, geom::CornerRadii::all(9));
            list.push_transform(geom::Affine::translate(1.5f, 2.25f));
            auto paint = mix ? plan->record(0.42, list) : std::expected<render::Paint, render::PaintMixError>(leaf);
            expect(paint.has_value()); if (paint) list.stroke_path(shape(), {.width = 5.5f, .join = render::LineJoin::round}, *paint);
            list.pop_transform(); list.pop_clip(); list.end_node();
        };
        render::DisplayList a, b; build(a, false); build(b, true);
        raster::Surface plain(W, H), mixed(W, H); render::CpuRenderer ca, cb;
        (void)ca.render(a, plain, {}, {.object_ids = true}); (void)cb.render(b, mixed, {}, {.object_ids = true});
        expect(std::ranges::equal(plain.bytes(), mixed.bytes()));
        expect(std::ranges::equal(ca.object_ids(), cb.object_ids()));
    };
    "invalid raw mixtures remain observable as renderer diagnostics"_test = [] {
        render::Paint invalid; invalid.kind = render::Paint::Kind::mix;
        render::DisplayList list; list.fill_rect({0, 0, W, H}, invalid);
        raster::Surface pixels(W, H); render::CpuRenderer renderer;
        const auto report = renderer.render(list, pixels, {});
        expect(eq(report.invalid_paints, std::size_t{1}));
        expect(pixels.at(30, 30) == raster::Rgba8{});
    };
}};
