// A missing outline does not imply a missing glyph: spaces have a valid,
// empty coverage mask. Keep unsupported bitmap-only fallback explicit.
import std;
import openmui.geom;
import openmui.raster;
import openmui.render.vector;
import openmui.testing;

using namespace omui::testing;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;

namespace {
enum class Kind { blank, missing, bitmap, color };
enum class Placement { translation, scaled_glyph, rotated_list };

class Glyphs final : public render::GlyphSource {
public:
    explicit Glyphs(Kind kind) : kind_(kind), color_(4, 4) {
        bitmap_.bounds = {-2, -4, 4, 4}; bitmap_.values.assign(16, 255);
        color_.clear({64, 128, 192, 255});
    }
    render::GlyphMask mask(render::FontRef, std::uint32_t, float, geom::Point) override {
        ++mask_calls;
        if (kind_ == Kind::blank) return {&blank_, nullptr, {}};
        if (kind_ == Kind::bitmap) return {&bitmap_, nullptr, {}};
        if (kind_ == Kind::color) return {nullptr, &color_, {0, -4, 4, 4}};
        return {};
    }
    raster::PathData outline(render::FontRef, std::uint32_t) override { ++outline_calls; return {}; }
    int mask_calls = 0, outline_calls = 0;

private:
    Kind kind_;
    raster::Coverage blank_, bitmap_;
    raster::Surface color_;
};

struct Result {
    render::RenderReport report;
    std::uint64_t pixels;
    int masks, outlines;
};

Result draw(Kind kind, Placement placement) {
    Glyphs glyphs(kind);
    render::DisplayList list;
    const std::array<render::Glyph, 1> run{{{1, {0, 0}}}};
    const std::array<render::GlyphStyle, 1> style{{{geom::Affine::scale(0.9f, 0.9f)}}};
    if (placement == Placement::rotated_list)
        list.push_transform(geom::Affine::translate(40, 0) * geom::Affine::rotation(0, 1));
    list.draw_glyphs({1}, 12, {12, 20}, run, render::Paint::solid(geom::Color::black()),
                     placement == Placement::scaled_glyph ? std::span<const render::GlyphStyle>(style) : std::span<const render::GlyphStyle>{});
    if (placement == Placement::rotated_list) list.pop_transform();
    raster::Surface surface(64, 64);
    render::CpuRenderer cpu;
    const auto report = cpu.render(list, surface, {nullptr, &glyphs}, {.background = geom::Color::white()});
    check(glyphs.mask_calls > 0, "the fixture resolves a real GlyphSource mask");
    check(placement == Placement::translation ? glyphs.outline_calls == 0 : glyphs.outline_calls > 0,
          "the chosen transform exercises the intended mask or outline fallback branch");
    return {report, surface.hash(), glyphs.mask_calls, glyphs.outline_calls};
}

std::uint64_t untouched() {
    raster::Surface surface(64, 64); surface.clear({255, 255, 255, 255}); return surface.hash();
}
constexpr std::array placements{Placement::translation, Placement::scaled_glyph, Placement::rotated_list};
}  // namespace

int main(int argc, char** argv) {
    Suite suite("render.blank-glyph");
    suite.test("a valid empty coverage is a blank glyph through translation, glyph scaling and list rotation", [] {
        for (const auto placement : placements) {
            const auto result = draw(Kind::blank, placement);
            check(result.report.missing_glyphs == 0, "a valid blank has no missing-glyph diagnostic");
            check(result.pixels == untouched(), "a blank does not change any pixel");
        }
    });
    suite.test("a null mask and empty outline still report a genuinely missing glyph", [] {
        for (const auto placement : placements) {
            const auto result = draw(Kind::missing, placement);
            check_eq(result.report.missing_glyphs, std::size_t{1});
            check_eq(result.pixels, untouched());
        }
    });
    suite.test("a nonempty coverage-only bitmap is drawn normally and remains explicitly unsupported without a transformed outline", [] {
        for (const auto placement : placements) {
            const auto result = draw(Kind::bitmap, placement);
            if (placement == Placement::translation) {
                check_eq(result.report.missing_glyphs, std::size_t{0});
                check(result.pixels != untouched(), "the ordinary bitmap mask actually paints");
            } else {
                check(result.report.missing_glyphs == 1, "a nonempty unsupported bitmap must not be silently accepted as whitespace");
                check(result.pixels == untouched(), "the diagnostic fix does not add a new bitmap transformation feature");
            }
        }
    });
    suite.test("color image fallback still paints with no missing-glyph diagnostic under every placement", [] {
        for (const auto placement : placements) {
            const auto result = draw(Kind::color, placement);
            check_eq(result.report.missing_glyphs, std::size_t{0});
            check(result.pixels != untouched(), "the existing color fallback really paints");
        }
    });
    return suite.run(argc, argv);
}
