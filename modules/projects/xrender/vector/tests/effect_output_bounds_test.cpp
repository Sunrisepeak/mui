// Capture is the region an effect reads; output bounds are where it may
// change its source. CPU checks stay here; desktop GPU counterparts are in render/cts.
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
constexpr int W = 192, H = 144;
constexpr geom::Rect layer{64, 48, 64, 48};

struct Case {
    std::string_view name;
    std::vector<render::EffectShape> shapes;
    float smoothness = 0, thickness = 12, blur = 0;
};

std::vector<Case> cases() {
    const render::EffectShape single{layer, geom::CornerRadii::all(8)};
    const std::vector<render::EffectShape> pair{
        {{64, 48, 28, 48}, geom::CornerRadii::all(8)}, {{100, 48, 28, 48}, geom::CornerRadii::all(8)}};
    return {{"single", {single}}, {"single smooth", {single}, 12}, {"pair smooth", pair, 12},
            {"eight overlapping", std::vector<render::EffectShape>(8, single), 12},
            {"thick frosted", pair, 12, 70, 18}, {"thin frosted", {single}, 0, 4, 18}};
}

std::vector<float> params(render::EffectRef ref, const Case& c) {
    const std::array<std::pair<std::string_view, float>, 5> values{{
        {"smoothness", c.smoothness}, {"thickness", c.thickness}, {"blur", c.blur}, {"tint_a", 0.6f}, {"adaptive", 0}}};
    return render::effects().params(ref, values);
}

geom::Rect scaled(geom::Rect r, float scale) { return {r.x * scale, r.y * scale, r.width * scale, r.height * scale}; }

geom::Rect geometry(const Case& c) {
    float left = layer.x, top = layer.y, right = layer.right(), bottom = layer.bottom();
    for (const auto& shape : c.shapes) {
        left = std::min(left, shape.rect.x);
        top = std::min(top, shape.rect.y);
        right = std::max(right, shape.rect.right());
        bottom = std::max(bottom, shape.rect.bottom());
    }
    return {left, top, right - left, bottom - top};
}

bool inside(geom::Rect r, geom::Point p, float reach) {
    return p.x >= r.x - reach && p.x < r.right() + reach && p.y >= r.y - reach && p.y < r.bottom() + reach;
}

void compare(std::span<const raster::Rgba8> source, std::span<const raster::Rgba8> result,
             const Case& c, float scale, float reach) {
    const int width = static_cast<int>(static_cast<float>(W) * scale);
    const int height = static_cast<int>(static_cast<float>(H) * scale);
    const auto bounds = scaled(geometry(c), scale);
    int outside = 0, escaped = 0, changed = 0, beyond_geometry = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const auto i = static_cast<std::size_t>(y * width + x);
            const bool different = source[i] != result[i];
            const geom::Point p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            // One logical pixel is a conservative allowance for the CPU and
            // shader's half-device-pixel glass coverage ramp.
            if (!inside(bounds, p, (reach + 1) * scale)) {
                ++outside;
                escaped += different;
            }
            changed += different;
            if (!inside(bounds, p, scale)) beyond_geometry += different;
        }
    expect(outside > 1000) << std::format("{} has substantial outside-source evidence", c.name);
    expect(escaped == 0) << std::format("{} scale {}: {} pixels outside output reach + AA differ from input", c.name, scale, escaped);
    expect(changed > 50) << std::format("{} actually runs the effect", c.name);
    if (c.name == "eight overlapping")
        expect(beyond_geometry > 0) << "smooth unions really generate pixels beyond geometry; zero reach would be false";
}

}  // namespace

UTest effect_output_bounds{"render.effect_output_bounds", [] {

    "declared output reach is independent of capture; undeclared effects fall back conservatively"_test = [] {
        auto& registry = render::effects();
        const auto glass = registry.find("liquid_glass");
        expect(glass.has_value());
        if (!glass) return;
        for (const auto& c : cases()) {
            const auto p = params(*glass, c);
            const auto output = registry.output_reach(*glass, p);
            expect(output && *output == c.smoothness) << std::format("{} only smoothness expands writes", c.name);
            expect(registry.reach(*glass, p) == c.thickness * 2 + c.blur + c.smoothness) << "capture retains refraction and blur";
        }
        auto p = registry.params(*glass, {});
        p[10] = -12;
        expect(registry.output_reach(*glass, p) == 12.0f) << "direct parameter callers get conservative absolute reach";
        render::EffectRegistry custom;
        render::EffectInfo external;
        external.name = "external_capture_only";
        external.reach = [](std::span<const float>) { return 17.0f; };
        const auto ref = custom.add(std::move(external));
        expect(custom.output_reach(ref, {}) == 17.0f) << "external effects need no new declaration to keep old capture bounds";
        expect(!custom.output_reach(render::EffectRef{}, {})) << "unknown effect has unknown bounds";
        expect(!registry.output_reach(*glass, {})) << "incomplete parameters cannot prove bounds";
    };

    "CPU glass preserves every input pixel outside declared output plus AA"_test = [] {
        const auto ref = render::effects().find("liquid_glass");
        expect(ref.has_value());
        if (!ref) return;
        const auto* info = render::effects().info(*ref);
        for (const auto& c : cases()) for (float scale : {1.0f, 2.0f}) {
            const int width = static_cast<int>(static_cast<float>(W) * scale);
            const int height = static_cast<int>(static_cast<float>(H) * scale);
            raster::Surface source(width, height), target(width, height), backdrop(width, height);
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                // Premultiplied, nonempty source both inside and outside the
                // shapes: an empty transparent target cannot pass by accident.
                source.at(x, y) = {static_cast<std::uint8_t>(x % 64), static_cast<std::uint8_t>(y % 64), 30, 100};
                backdrop.at(x, y) = {static_cast<std::uint8_t>(80 + x % 128), 60, static_cast<std::uint8_t>(80 + y % 128), 255};
            }
            auto shapes = c.shapes;
            for (auto& shape : shapes) {
                shape.rect = scaled(shape.rect, scale);
                shape.radii = geom::CornerRadii::all(8 * scale);
            }
            const auto p = params(*ref, c);
            render::run_cpu_effect(*info, {source, target, p, scale, scaled(layer, scale), &backdrop, shapes}, nullptr);
            compare(source.pixels(), target.pixels(), c, scale, *render::effects().output_reach(*ref, p));
        }
    };
}};
