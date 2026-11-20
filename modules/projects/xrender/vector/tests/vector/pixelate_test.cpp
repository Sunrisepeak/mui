// Pixel units and an exact off endpoint, through the public registry and renderer.
import std;
import mlibs.geom;
import xrender.raster;
import xrender.vector;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {
using Params = std::vector<std::pair<std::string_view, float>>;
xrender::raster::Surface source() {
    xrender::raster::Surface out(24, 18);
    for (int y = 0; y < out.height(); ++y) for (int x = 0; x < out.width(); ++x) {
        const auto alpha = static_cast<std::uint8_t>(128 + (x * 7 + y * 3) % 128);
        out.at(x, y) = {static_cast<std::uint8_t>((x * 17) % alpha), static_cast<std::uint8_t>((y * 23) % alpha), 31, alpha};
    }
    return out;
}
xrender::raster::Surface filtered(const xrender::raster::Surface& input, Params params, float scale = 1, mlibs::geom::Rect bounds = {0, 0, 24, 18}) {
    const auto ref = *xrender::effects().find("pixelate");
    const auto p = xrender::effects().params(ref, params);
    xrender::raster::Surface result(input.width(), input.height());
    xrender::run_cpu_effect(*xrender::effects().info(ref), {input, result, p, scale, bounds}, nullptr);
    return result;
}
xrender::DisplayList scene(float amount, bool effect = true) {
    xrender::DisplayList out;
    out.fill_rect({0, 0, 120, 90}, mlibs::geom::Color::hex(0x122030));
    out.push_clip({8.25f, 6.5f, 93, 70});
    // Keep the same group opacity/blend in the reference. The identity hook
    // must bypass filtering without dropping either or skipping the child.
    const auto ref = *xrender::effects().find("pixelate");
    const auto p = xrender::effects().params(ref, Params{{"size", 7}, {"amount", amount}, {"origin_x", -2}, {"origin_y", 3}});
    out.push_layer(.75f, xrender::BlendMode::multiply, effect ? ref : xrender::EffectRef{}, effect ? std::span<const float>(p) : std::span<const float>{}, {9, 7, 90, 67});
    for (int i = 0; i < 18; ++i)
        out.fill_rect({8 + static_cast<float>(i) * 5, 9 + static_cast<float>(i % 3), 3.5f, 59}, mlibs::geom::Color{.2f, .7f, .9f, .6f});
    const auto tone = *xrender::effects().find("tone");
    const auto tp = xrender::effects().params(tone, Params{{"amount", .6f}});
    out.push_layer(1, xrender::BlendMode::normal, tone, tp, {35, 25, 30, 25});
    out.fill_rect({35, 25, 30, 25}, mlibs::geom::Color::hex(0xf05030));
    out.pop_layer(); out.pop_layer(); out.pop_clip();
    out.fill_rect({105, 5, 8, 60}, mlibs::geom::Color::white());
    return out;
}
xrender::raster::Surface draw(const xrender::DisplayList& list, float scale) {
    xrender::raster::Surface out(static_cast<int>(120 * scale), static_cast<int>(90 * scale));
    xrender::CpuRenderer renderer;
    const auto report = renderer.render(list, out, {}, {.scale = scale});
    expect(report.effects_skipped == 0 && report.invalid_paints == 0 && list.balanced());
    return out;
}
}

static UTest cases{"render.pixelate", [] {
    "zero strength is identity at fractional and high DPI while one logical pixel is not"_test = [] {
        const auto input = source();
        for (float scale : {.5f, 1.f, 1.25f, 2.f}) {
            expect(filtered(input, {{"size", 9}, {"amount", 0}}, scale).hash() == input.hash());
            expect(filtered(input, {{"size", 1}, {"device_pixels", 1}}, scale).hash() == input.hash());
        }
        expect(filtered(input, {{"size", 1}}, 2).hash() != input.hash()) << "a logical pixel spans multiple device samples";
        expect(filtered(input, {{"size", 4}}, 1.25f).hash() == filtered(input, {{"size", 5}, {"device_pixels", 1}}, 2).hash());
    };
    "partial strength mixes premultiplied source and blocks without losing alpha"_test = [] {
        const auto input = source();
        const auto full = filtered(input, {{"size", 6}});
        const auto half = filtered(input, {{"size", 6}, {"amount", .5f}});
        expect(half.hash() != input.hash() && half.hash() != full.hash());
        for (int y = 0; y < input.height(); ++y) for (int x = 0; x < input.width(); ++x) {
            const auto a = input.at(x, y), b = full.at(x, y), p = half.at(x, y);
            expect(p.r <= p.a && p.g <= p.a && p.b <= p.a);
            expect(std::abs(2 * p.r - a.r - b.r) <= 2 && std::abs(2 * p.a - a.a - b.a) <= 2);
        }
    };
    "grid offset uses the selected unit and a whole cell preserves phase"_test = [] {
        const auto input = source();
        const auto base = filtered(input, {{"size", 6}});
        expect(filtered(input, {{"size", 6}, {"origin_x", 6}, {"origin_y", -6}}).hash() == base.hash());
        expect(filtered(input, {{"size", 6}, {"origin_x", 2}}).hash() != base.hash());
        expect(filtered(input, {{"size", 3}, {"origin_x", -1}, {"origin_y", 2}}, 2).hash() ==
              filtered(input, {{"size", 6}, {"origin_x", -2}, {"origin_y", 4}, {"device_pixels", 1}}, 2).hash());
    };
    "real renderer identity retains nested effects clips blending and outside pixels"_test = [] {
        for (float scale : {1.f, 1.25f, 2.f}) {
            const auto plain = draw(scene(0, false), scale);
            const auto zero = draw(scene(0), scale);
            const auto full = draw(scene(1), scale);
            expect(plain.hash() == zero.hash()) << "the declared identity is an ordinary layer, even with nested effects";
            expect(full.hash() != zero.hash());
            for (int y = 0; y < plain.height(); ++y) for (int x = static_cast<int>(104 * scale); x < plain.width(); ++x)
                expect(plain.at(x, y) == full.at(x, y)) << "unrelated pixels stay unchanged";
        }
    };
    "viewport cropping retains the full grid neighbourhood at its edges"_test = [] {
        for (float scale : {1.f, 1.25f, 2.f}) {
            const auto list = scene(1);
            const auto whole = draw(list, scale);
            xrender::raster::Surface tile(48, 32); xrender::CpuRenderer renderer;
            xrender::CpuOptions options; options.transform = mlibs::geom::Affine::translate(-37, -22) * mlibs::geom::Affine::scale(scale, scale);
            const auto report = renderer.render(list, tile, {}, options); expect(report.effects_skipped == 0);
            bool equal = true;
            for (int y = 0; y < tile.height(); ++y) for (int x = 0; x < tile.width(); ++x)
                equal = equal && tile.at(x, y) == whole.at(x + 37, y + 22);
            expect(equal) << std::format("DPI {}: an independently cropped viewport is the same region of the full render", scale);
        }
    };
}};
