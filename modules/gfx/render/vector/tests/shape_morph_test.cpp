// Independent graphics consumer of hanim: no UI Runtime, components or site.
// Geometry is prepared once; each frame is ONE current path. Materials and
// registered pixel effects consume that path through the existing renderer.
import std;
import hanim;
import openmui.geom;
import openmui.raster;
import openmui.render.vector;
import openmui.image;
import openmui.testing;
import mcpplibs.os;

using namespace omui::testing;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;
namespace image = omui::imageio;
namespace os = mcpplibs::os;

namespace {
constexpr int side = 192;
constexpr double radius = 64, center = 96, k = 0.5522847498307936;
constexpr std::array<hanim::ShapeSegment, 2> triangle{{
    hanim::ShapeSegment::line({-1, 1}), hanim::ShapeSegment::line({1, 1})}};
constexpr std::array<hanim::ShapeSegment, 4> circle{{
    hanim::ShapeSegment::cubic({k, -1}, {1, -k}, {1, 0}),
    hanim::ShapeSegment::cubic({1, k}, {k, 1}, {0, 1}),
    hanim::ShapeSegment::cubic({-k, 1}, {-1, k}, {-1, 0}),
    hanim::ShapeSegment::cubic({-1, -k}, {-k, -1}, {0, -1})}};
geom::Point placed(hanim::Vec2 p) {
    return {static_cast<float>(center + radius * p.x), static_cast<float>(center + radius * p.y)};
}
std::optional<hanim::PreparedShapeMorph> prepare() {
    auto result = hanim::PreparedShapeMorph::prepare({{0, -1}, triangle}, {{0, -1}, circle});
    check(result.has_value());
    if (!result) return std::nullopt;
    return std::move(*result);
}
render::Path original(bool disk) {
    render::Path path;
    path.move_to(placed({0, -1}));
    if (disk) for (const auto& s : circle) path.cubic_to(placed(s.control1), placed(s.control2), placed(s.end));
    else for (const auto& s : triangle) path.line_to(placed(s.end));
    path.close();
    return path;
}
render::Path sampled(const hanim::PreparedShapeMorph& plan, double t) {
    std::vector<hanim::ShapeCubic> cubics(plan.segment_count());
    const auto result = plan.sample(t, cubics);
    check(result.has_value());
    render::Path path;
    if (!result || cubics.empty()) return path;
    path.move_to(placed(cubics.front().start));
    for (const auto& c : cubics) path.cubic_to(placed(c.control1), placed(c.control2), placed(c.end));
    path.close();
    return path;
}
render::DisplayList list_of(const render::Path& path, const render::Paint& paint) {
    render::DisplayList list;
    list.begin_node(42);
    list.fill_path(path, render::FillRule::nonzero, paint);
    list.end_node();
    return list;
}
raster::Surface draw(const render::DisplayList& list, float scale = 1, render::ImageStore* images = nullptr) {
    const int dimension = static_cast<int>(side * scale);
    raster::Surface pixels(dimension, dimension);
    render::CpuRenderer renderer;
    const auto report = renderer.render(list, pixels, {images, nullptr}, {.scale = scale});
    check_eq(report.missing_images, std::size_t{0});
    check_eq(report.effects_skipped, std::size_t{0});
    return pixels;
}
raster::Surface plain(const render::Path& path, float scale = 1) {
    return draw(list_of(path, render::Paint::solid(geom::Color::white())), scale);
}
image::View view(const raster::Surface& surface) {
    return {reinterpret_cast<const std::uint8_t*>(surface.pixels().data()), surface.width(), surface.height(), surface.width() * 4};
}
const std::filesystem::path& directory() {
    // The shared test helper owns scratch-directory lifecycle; all artifact
    // content is encoded in memory and written through the public OS service.
    static const auto path = scratch_dir("hanim-shape-morph");
    return path;
}
void write_bytes(std::string_view name, const std::expected<std::vector<std::uint8_t>, std::string>& encoded) {
    check(encoded.has_value());
    if (!encoded) return;
    const auto bytes = std::string_view(reinterpret_cast<const char*>(encoded->data()), encoded->size());
    const auto written = os::fs::write_atomic(directory() / os::fs::from_utf8(name), bytes);
    check(written.has_value());
}
void save(const raster::Surface& surface, std::string_view name) { write_bytes(name, image::encode_png(view(surface))); }

// The CPU flattener permits 1/32 device pixel error per representation.
// Independently flattening an exact subdivision may thus differ by 1/16 px;
// allow 18/255 coverage (including subpixel rounding) ONLY in a one-pixel
// reference boundary band. There is no geometric/image-wide tolerance.
void endpoint_pixels(const raster::Surface& expected, const raster::Surface& actual, unsigned scale) {
    check_eq(expected.width(), actual.width());
    unsigned worst = 0, off_band = 0;
    std::uint64_t sum = 0;
    for (int y = 0; y < expected.height(); ++y) for (int x = 0; x < expected.width(); ++x) {
        const auto a = expected.at(x, y), b = actual.at(x, y);
        const auto difference = static_cast<unsigned>(std::abs(static_cast<int>(a.a) - static_cast<int>(b.a)));
        worst = std::max(worst, difference); sum += difference;
        if (!difference) continue;
        bool edge = a.a > 0 && a.a < 255;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int xx = std::clamp(x + dx, 0, expected.width() - 1);
            const int yy = std::clamp(y + dy, 0, expected.height() - 1);
            edge = edge || expected.at(xx, yy).a != a.a;
        }
        if (!edge) ++off_band;
        check(a.r == a.a && a.g == a.a && a.b == a.a, "white reference is premultiplied coverage");
        check(b.r == b.a && b.g == b.a && b.b == b.a, "sample uses one opaque white paint, not faded copies");
    }
    std::println("    endpoint @{}x: max coverage difference {}, sum {}, off boundary {}", scale, worst, sum, off_band);
    check(worst <= 18, "independent curve flattening remains within declared coverage error");
    check(off_band == 0, "fully interior and far exterior pixels stay exact");
    check(sum <= 8192u * scale, "total error stays bounded by the contour perimeter, not image area");
}

struct MaterialRig {
    render::ImageStore images;
    render::ImageRef albedo, normal;
    MaterialRig() {
        albedo = images.add(4, 4, std::vector<raster::Rgba8>(16, raster::Rgba8{255, 255, 255, 255}));
        normal = images.add(4, 4, std::vector<raster::Rgba8>(16, raster::Rgba8{128, 128, 255, 255}));
    }
    render::DisplayList frame(const render::Path& path, double t, bool tone = false, bool blur = false) {
        render::Material material;
        material.normal = normal;
        material.light = {0, 0}; material.light_z = 1;
        material.bands = 0; material.rim_strength = 0;
        const auto key = hanim::lerp(hanim::Rgba{1, 0.2f, 0.1f, 1}, hanim::Rgba{0.1f, 0.3f, 1, 1}, t);
        material.key = {key.r, key.g, key.b, key.a};
        render::Paint paint;
        paint.kind = render::Paint::Kind::image;
        paint.color = geom::Color::white(); paint.image = albedo;
        paint.image_transform = geom::Affine::scale(static_cast<float>(side) / 4, static_cast<float>(side) / 4);
        paint.material = &material;
        render::DisplayList list;
        list.begin_node(42);
        if (blur) {
            const auto effect = render::effects().find("blur");
            check(effect.has_value());
            if (effect) {
                const std::array<std::pair<std::string_view, float>, 1> values{{{"radius", 4}}};
                list.push_layer(1, render::BlendMode::normal, *effect, render::effects().params(*effect, values), path.bounds());
            }
        }
        if (tone) {
            const auto effect = render::effects().find("tone");
            check(effect.has_value());
            if (effect) {
                const std::array<std::pair<std::string_view, float>, 4> values{{
                    {"color_r", 1}, {"color_g", 1}, {"color_b", 1}, {"amount", 1}}};
                list.push_layer(1, render::BlendMode::normal, *effect, render::effects().params(*effect, values), path.bounds());
            }
        }
        list.fill_path(path, render::FillRule::nonzero, paint);
        if (tone) list.pop_layer();
        if (blur) list.pop_layer();
        list.end_node();
        return list;
    }
};

} // namespace

int main(int argc, char** argv) {
    Suite suite{"render.shape-morph"};
    suite.test("original line triangle and cubic circle endpoints survive actual CPU rasterization at 1x and 2x", [] {
        const auto plan = prepare(); if (!plan) return;
        for (unsigned scale : {1u, 2u}) for (bool disk : {false, true}) {
            const auto reference = plain(original(disk), static_cast<float>(scale));
            const auto actual = plain(sampled(*plan, disk ? 1 : 0), static_cast<float>(scale));
            endpoint_pixels(reference, actual, scale);
            save(actual, std::format("{}@{}x.png", disk ? "circle" : "triangle", scale));
            if (!disk) {
                check(actual.at(95 * static_cast<int>(scale), 34 * static_cast<int>(scale)).a > 200, "top sharp corner retains filled pixels");
                check(actual.at(34 * static_cast<int>(scale), 158 * static_cast<int>(scale)).a > 200, "left sharp corner survives");
                check(actual.at(157 * static_cast<int>(scale), 158 * static_cast<int>(scale)).a > 200, "right sharp corner survives");
            }
        }
    });

    suite.test("middle is one current closed path whose pixels are not a two-image crossfade", [] {
        const auto plan = prepare(); if (!plan) return;
        const auto path = sampled(*plan, 0.5);
        check_eq(std::ranges::count(path.verbs(), render::PathVerb::move), std::ptrdiff_t{1});
        check_eq(std::ranges::count(path.verbs(), render::PathVerb::close), std::ptrdiff_t{1});
        check_eq(std::ranges::count(path.verbs(), render::PathVerb::cubic), static_cast<std::ptrdiff_t>(plan->segment_count()));
        const auto list = list_of(path, render::Paint::solid(geom::Color::white()));
        std::size_t paths = 0, images = 0, layers = 0;
        for (const auto& command : list.commands()) {
            paths += std::holds_alternative<render::FillPath>(command) ? 1u : 0u;
            images += std::holds_alternative<render::DrawImage>(command) ? 1u : 0u;
            layers += std::holds_alternative<render::PushLayer>(command) ? 1u : 0u;
        }
        check_eq(paths, std::size_t{1}); check_eq(images, std::size_t{0}); check_eq(layers, std::size_t{0});
        const auto a = plain(original(false)), b = plain(original(true)), middle = draw(list);
        unsigned different_from_fade = 0;
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            const int average = (static_cast<int>(a.at(x, y).a) + b.at(x, y).a) / 2;
            if (std::abs(static_cast<int>(middle.at(x, y).a) - average) > 64) ++different_from_fade;
        }
        check(different_from_fade > 200, "a geometric boundary moves; endpoint-exclusive regions are not half-transparent");
        check(middle.at(96, 96).a == 255, "shared interior is opaque");
        check(middle.hash() != a.hash() && middle.hash() != b.hash());
        save(middle, "middle-geometry.png");
    });

    suite.test("real Material key-light parameters change one current geometry without changing coverage", [] {
        const auto plan = prepare(); if (!plan) return;
        const auto path = sampled(*plan, 0.5);
        MaterialRig rig;
        const auto warm = draw(rig.frame(path, 0), 1, &rig.images);
        const auto mid = draw(rig.frame(path, 0.5), 1, &rig.images);
        const auto cool = draw(rig.frame(path, 1), 1, &rig.images);
        const auto coverage = plain(path);
        check(warm.hash() != mid.hash() && cool.hash() != mid.hash() && warm.hash() != cool.hash());
        check(warm.at(96, 96).r > warm.at(96, 96).b + 150);
        check(cool.at(96, 96).b > cool.at(96, 96).r + 150);
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            const auto alpha = coverage.at(x, y).a;
            check(warm.at(x, y).a == alpha && mid.at(x, y).a == alpha && cool.at(x, y).a == alpha,
                  "material processing reuses the same current shape mask");
        }
        save(warm, "middle-material-warm.png"); save(cool, "middle-material-cool.png");
    });

    suite.test("registered tone and blur compose around the current material-painted contour", [] {
        const auto plan = prepare(); if (!plan) return;
        const auto path = sampled(*plan, 0.5);
        MaterialRig rig;
        const auto bare = draw(rig.frame(path, 0), 1, &rig.images);
        const auto toned = draw(rig.frame(path, 0, true), 1, &rig.images);
        const auto chain = draw(rig.frame(path, 0, true, true), 1, &rig.images);
        check(toned.hash() != bare.hash(), "tone changes material RGB");
        check(chain.hash() != toned.hash(), "blur acts on the already toned pixels");
        const auto gray = toned.at(96, 96);
        check(gray.r == gray.g && gray.g == gray.b && gray.a == 255);
        unsigned outside = 0;
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
            outside += bare.at(x, y).a == 0 && chain.at(x, y).a > 0 ? 1u : 0u;
        check(outside > 50, "blur coverage really extends beyond the current path, not just metadata");
        save(chain, "middle-material-tone-blur.png");
    });

    suite.test("seek and reverse frame order reproduce pixels and emit reviewable actual frames", [] {
        const auto plan = prepare(); if (!plan) return;
        MaterialRig rig;
        std::vector<raster::Surface> frames;
        std::vector<std::uint64_t> hashes;
        for (unsigned i = 0; i <= 12; ++i) {
            const double t = static_cast<double>(i) / 12;
            frames.push_back(draw(rig.frame(sampled(*plan, t), t), 1, &rig.images));
            hashes.push_back(frames.back().hash());
        }
        for (unsigned i : {12u, 0u, 8u, 3u, 11u, 1u, 6u, 4u, 9u, 2u, 10u, 5u, 7u}) {
            const double t = static_cast<double>(i) / 12;
            const auto again = draw(rig.frame(sampled(*plan, t), t), 1, &rig.images);
            check(again.hash() == hashes[i], "arbitrary time is independent of playback history");
        }
        for (unsigned i = 12; i > 0; --i) {
            const double t = static_cast<double>(i - 1) / 12;
            frames.push_back(draw(rig.frame(sampled(*plan, t), t), 1, &rig.images));
            check(frames.back().hash() == hashes[i - 1], "reverse ends at the original first frame");
        }
        raster::Surface strip(side * 5, side);
        for (unsigned tile = 0; tile < 5; ++tile) for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) strip.row(y)[static_cast<int>(tile) * side + x] = frames[tile * 3].at(x, y);
        save(strip, "triangle-circle-material-strip.png");
        std::vector<image::View> views;
        for (const auto& frame : frames) views.push_back(view(frame));
        write_bytes("triangle-circle-material.webp", image::encode_animated_webp(views, {.delay_num = 1, .delay_den = 12, .lossless = true}));
        std::println("    actual CPU shape/material media: {}", os::fs::utf8(directory()));
    });
    return suite.run(argc, argv);
}
