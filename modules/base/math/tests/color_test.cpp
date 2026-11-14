import std;
import mlibs.math;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace color = mlibs::math::color;

UTest cases{"math.color", [] {
    "sRGB luminance keeps endpoints, gamma breakpoint and channel weights"_test = [] {
        expect(near(color::relative_luminance(0, 0, 0), 0.0, 1e-15));
        expect(near(color::relative_luminance(1, 1, 1), 1.0, 1e-15));
        expect(near(color::relative_luminance(1, 0, 0), 0.2126, 1e-15));
        expect(near(color::relative_luminance(0, 1, 0), 0.7152, 1e-15));
        expect(near(color::relative_luminance(0, 0, 1), 0.0722, 1e-15));
        expect(near(color::relative_luminance(0.5, 0.5, 0.5), 0.21404114048223255, 1e-14));
        expect(near(color::relative_luminance(0.04045, 0.04045, 0.04045), 0.04045 / 12.92, 1e-15));
        double previous = -1;
        for (int i = 0; i <= 255; ++i) {
            const double c = static_cast<double>(i) / 255;
            const double luminance = color::relative_luminance(c, c, c);
            expect(luminance > previous); previous = luminance;
        }
    };
    "contrast is symmetric, normalized, and distinguishes the observed low-contrast frame"_test = [] {
        expect(near(color::contrast_ratio(0, 1), 21.0, 1e-14));
        expect(near(color::contrast_ratio(0.25, 0.25), 1.0, 1e-15));
        expect(near(color::contrast_ratio(0.2, 0.7), color::contrast_ratio(0.7, 0.2), 1e-15));
        expect(near(color::relative_luminance(-1, 2, 0), 0.7152, 1e-15));
        const auto ink = color::relative_luminance(136.0 / 255, 137.0 / 255, 140.0 / 255);
        const auto paper = color::relative_luminance(136.0 / 255, 139.0 / 255, 174.0 / 255);
        expect(color::contrast_ratio(ink, paper) > 1.0 && color::contrast_ratio(ink, paper) < 1.1);
    };

    "a shade computes an item's colour (the lattice's cells, a cloud's particles): a row's base, its grain, tints along a channel's range, and the light"_test = [] {
        // Three channels: the key, heat, burned. Colours are straight 0xAABBGGRR.
        color::Shade shade;
        shade.base = {0x00000000, 0xff204080, 0x80ff0000};
        shade.fallback = 0x40ffffff;
        shade.tints.push_back({.channel = 1, .from = 100, .to = 300, .color = 0xc00000ff, .emits = true});
        shade.tints.push_back({.channel = 2, .from = 0, .to_by_row = {0, 8, 0}, .color = 0xff000000});
        expect(fatal(color::check(shade, 3).has_value()));
        const std::size_t area = 5;
        //                               plain  hot   charred  hot, half clear  no such row
        std::vector<std::uint32_t> cells{1,     1,    1,       2,               9,
                                         0,     200,  0,       300,             0,
                                         0,     0,    4,       7,               5};
        // Worked by hand (xengine.cellular's CONTRACT.md states the arithmetic):
        //   plain    the base, opaque: itself.
        //   hot      share (200 − 100) · 256 / 200 = 128, amount 128 · 192 / 255 = 96:
        //            red (128 · 160 + 255 · 96 + 128) >> 8 = 176, green 40, blue 20; light red (255 · 96 + 128) >> 8 = 96.
        //   charred  share 4 · 256 / 8 = 128, all of black's 255: halfway to black — 64, 32, 16.
        //   hot, half clear  amount 256 · 192 / 255 = 192: red 191, blue 64, alpha (128 · 64 + 255 · 192 + 128) >> 8 = 223,
        //            premultiplied 167, 0, 56; its row's burned range is none, so 7 burned does nothing; light 191.
        //   no such row  the fallback, premultiplied; a tint by row has no row for it.
        const std::array<color::Shaded, 5> expected{{{0xff204080, 0}, {0xff1428b0, 0x60000060}, {0xff102040, 0}, {0xdf3800a7, 0xbf0000bf}, {0x40404040, 0}}};
        for (std::size_t index = 0; index < area; ++index) {
            const color::Shaded shaded = color::shade_of(shade, cells, area, index);
            expect(eq(shaded.pixel, expected[index].pixel)) << std::format("cell {}: pixel {:08x}, by hand {:08x}", index, shaded.pixel, expected[index].pixel);
            expect(eq(shaded.light, expected[index].light)) << std::format("cell {}: light {:08x}, by hand {:08x}", index, shaded.light, expected[index].light);
        }
        // A run is the cells one by one; without a place for the light, the pixels alone.
        std::array<std::uint32_t, 3> pixels{}, light{};
        color::shade_run(shade, cells, area, 1, pixels, light);
        for (std::size_t offset = 0; offset < 3; ++offset) expect(color::Shaded{pixels[offset], light[offset]} == expected[1 + offset]);
        std::array<std::uint32_t, 5> alone{};
        color::shade_run(shade, cells, area, 0, alone, {});
        for (std::size_t index = 0; index < area; ++index) expect(eq(alone[index], expected[index].pixel));

        // The less, the more: frost from 273 down to 173, at most half white over black.
        color::Shade frost;
        frost.base = {0xff000000};
        frost.tints.push_back({.channel = 1, .from = 273, .to = 173, .color = 0x80ffffff});
        const auto at = [&](std::uint32_t temperature) {
            const std::array<std::uint32_t, 2> cell{0, temperature};
            return color::shade_of(frost, cell, 1, 0).pixel;
        };
        expect(eq(at(300), 0xff000000u)) << "warmer than where it begins: none";
        expect(eq(at(273), 0xff000000u));
        expect(eq(at(223), 0xff404040u)) << "halfway: share 128, amount 128 · 128 / 255 = 64";
        expect(eq(at(173), 0xff808080u));
        expect(eq(at(0), 0xff808080u)) << "colder than where it ends: all of it, no more";

        // Grain: lighter or darker by the cell's place — by up to the row's grain, the same cell the same every time.
        color::Shade grained;
        grained.base = {0xff808080, 0xff808080};
        grained.grain = {64, 0};
        std::vector<std::uint32_t> field(256, 0);
        std::set<std::uint32_t> seen;
        for (std::size_t index = 0; index < field.size(); ++index) {
            const std::uint32_t pixel = color::shade_of(grained, field, field.size(), index).pixel;
            const int roll = static_cast<int>(mlibs::math::mix32(static_cast<std::uint32_t>(index)) & 0xff) - 128;
            const auto level = static_cast<std::uint32_t>(128 + roll * 64 / 128);
            expect(eq(pixel, 0xff000000u | level | level << 8 | level << 16)) << std::format("cell {}", index);
            expect(level >= 64 && level <= 191);
            expect(eq(color::shade_of(grained, field, field.size(), index).pixel, pixel));
            seen.insert(pixel);
        }
        expect(seen.size() > 32u) << std::format("{} levels among 256 cells", seen.size());
        std::ranges::fill(field, 1u);
        expect(eq(color::shade_of(grained, field, field.size(), 77).pixel, 0xff808080u)) << "a row without grain is flat";

        // What numbers cannot be shaded by.
        const auto refused = [](color::Shade bad, std::string_view part) {
            const auto checked = color::check(bad, 3);
            expect(!checked.has_value() && checked.error().contains(part)) << (checked ? std::string("accepted") : checked.error());
        };
        color::Shade bad = shade;
        bad.key = 3;
        refused(bad, "key is channel 3");
        bad = shade;
        bad.base.clear();
        refused(bad, "a row at least");
        bad = shade;
        bad.grain = {1, 2};
        refused(bad, "grain for 2 rows");
        bad = shade;
        bad.tints[0].channel = 5;
        refused(bad, "tint 0 reads channel 5");
        bad = shade;
        bad.tints[1].to_by_row = {1};
        refused(bad, "tint 1 has an end for 1 rows");
    };
}};
