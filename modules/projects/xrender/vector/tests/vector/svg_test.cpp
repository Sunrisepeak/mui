// A display list as SVG: every kind of command written as SVG's own element
// or, where SVG cannot say it the same way, as a marked island of the CPU
// reference's pixels; glyph outlines defined once; node and part markers as
// marked groups; the same bytes every time (pinned on every platform).
import std;
import mlibs.geom;
import mlibs.image;
import mlibs.hash;
import xrender.raster;
import xrender.vector;
import xrender.vector.svg;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace geom = mlibs::geom;

namespace {

// Glyph 1 is a square a little inside its em, glyph 2 a blank (a space).
class SquareGlyphs final : public xrender::GlyphSource {
public:
    xrender::GlyphMask mask(xrender::FontRef, std::uint32_t, float, geom::Point) override { return {}; }
    xrender::raster::PathData outline(xrender::FontRef, std::uint32_t glyph) override {
        if (glyph != 1) return {};
        return {verbs_, points_};
    }

private:
    std::array<geom::PathVerb, 5> verbs_{geom::PathVerb::move, geom::PathVerb::line, geom::PathVerb::line, geom::PathVerb::line, geom::PathVerb::close};
    std::array<geom::Point, 4> points_{{{0.1f, -0.7f}, {0.6f, -0.7f}, {0.6f, 0}, {0.1f, 0}}};
};

std::expected<std::vector<std::uint8_t>, std::string> png(std::span<const std::uint8_t> rgba, int width, int height) {
    return mlibs::image::encode_png({rgba.data(), width, height, width * 4, mlibs::image::Alpha::premultiplied});
}

std::size_t count(std::string_view text, std::string_view what) {
    std::size_t found = 0;
    for (std::size_t at = text.find(what); at != std::string_view::npos; at = text.find(what, at + what.size())) ++found;
    return found;
}

struct Scene {
    xrender::DisplayList list;
    xrender::ImageStore images;
    SquareGlyphs glyphs;
    std::array<xrender::GradientStop, 2> stops{{{0, geom::Color::hex(0x2563eb)}, {1, geom::Color::hex(0xf97316)}}};
    std::array<float, 2> dashes{6, 3};
    std::array<xrender::Glyph, 3> run{{{1, {0, 0}}, {2, {10, 0}}, {1, {20, 0}}}};
    std::array<xrender::GlyphStyle, 3> styles{};

    Scene() {
        auto& l = list;
        l.begin_node(7);
        l.fill_rect({0, 0, 200, 120}, geom::Color::hex(0xf8fafc));
        l.box_shadow({20, 20, 60, 40}, xrender::CornerRadii::all(8), {0, 4}, 12, 2, geom::Color{0, 0, 0, 0.3f});
        l.fill_round_rect({20, 20, 60, 40}, xrender::CornerRadii::all(8), xrender::Paint::solid(geom::Color::hex(0x0ea5e9)));
        l.stroke_round_rect({20, 20, 60, 40}, xrender::CornerRadii::all(8), 2, xrender::Paint::solid(geom::Color{1, 1, 1, 0.5f}));
        xrender::Paint gradient;
        gradient.kind = xrender::Paint::Kind::gradient;
        gradient.gradient.start = {100, 20};
        gradient.gradient.end = {180, 60};
        gradient.gradient.stops = stops;
        xrender::Path wave;
        wave.move_to({100, 30}).cubic_to({120, 10}, {160, 50}, {180, 30}).line_to({180, 60}).line_to({100, 60}).close();
        l.fill_path(wave, geom::FillRule::nonzero, gradient);
        l.push_clip({90, 70, 100, 40});
        l.push_transform(geom::Affine::translate(140, 90) * geom::Affine::rotation(0.8f, 0.6f));
        xrender::Path line;
        line.move_to({-40, 0}).line_to({40, 0});
        l.stroke_path(line, {.width = 4, .cap = xrender::LineCap::round, .dashes = dashes}, xrender::Paint::solid(geom::Color::hex(0x16a34a)));
        l.pop_transform();
        l.pop_clip();
        l.push_layer(0.5f, xrender::BlendMode::multiply);
        l.fill_rect({30, 70, 40, 30}, geom::Color::hex(0xdc2626));
        l.pop_layer();
        xrender::PartFrame part{.role = "label", .id = "title"};
        l.begin_part(part);
        styles[2].opacity = 0.5f;
        l.draw_glyphs(xrender::FontRef{3}, 16, {24, 110}, run, xrender::Paint::solid(geom::Color::hex(0x111827)), styles);
        l.end_part();
        // A conic paint: no SVG of its own, so pixels.
        xrender::Paint conic = gradient;
        conic.gradient.kind = xrender::GradientKind::conic;
        l.fill_rect({160, 90, 30, 25}, conic);
        // A picture, drawn twice: defined once.
        std::vector<xrender::raster::Rgba8> pixels(4 * 4, {255, 0, 0, 255});
        pixels[5] = {0, 0, 255, 255};
        const auto image = images.add(4, 4, pixels);
        l.draw_image(image, {0, 0, 4, 4}, {120, 95, 8, 8}, xrender::Sampling::nearest);
        l.draw_image(image, {0, 0, 2, 2}, {132, 95, 8, 8}, xrender::Sampling::linear, 0.5f);
        l.end_node();
    }
    xrender::Resources resources() { return {&images, &glyphs}; }
};

}  // namespace

static UTest cases{"render.svg", [] {

    "every kind of command as SVG's own element; what it cannot say, a marked island; the same bytes"_test = [] {
        Scene scene;
        const auto resources = scene.resources();
        const auto picture = xrender::svg::write(scene.list, resources, {.view = {0, 0, 200, 120}, .node_name = [](std::uint32_t node) {
            return node == 7 ? std::string("main/card") : std::string{};
        }, .encode_png = png});
        expect(picture.has_value()) << (picture ? "" : picture.error());
        if (!picture) return;
        const std::string_view text = picture->text;
        expect(text.starts_with(R"(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="120" viewBox="0 0 200 120">)"));
        expect(text.contains(R"(data-node="main/card")")) << "the node marker named";
        expect(text.contains(R"(data-part="label" data-part-id="title")")) << "the part marker";
        expect(text.contains("<linearGradient") && text.contains(R"(stop-color="#2563eb")")) << "the gradient";
        expect(text.contains(R"(stroke-dasharray="6 3")") && text.contains(R"(stroke-linecap="round")")) << "the dashed stroke";
        expect(text.contains("<clipPath") && text.contains(R"svg(transform="matrix(0.8 0.6 -0.6 0.8 140 90)")svg")) << "clip and transform";
        expect(text.contains(R"svg(opacity="0.5" style="mix-blend-mode:multiply")svg")) << "the layer";
        expect(text.contains("feGaussianBlur") && text.contains("<mask")) << "the outer shadow: blurred, outside the box";
        expect(text.contains(R"(fill-rule="evenodd")")) << "the rounded rectangle's stroke as a ring";
        // Glyph 1 twice, defined once; the blank glyph not at all.
        expect(eq(count(text, R"(<path id="g3_1")"), std::size_t{1}));
        expect(eq(count(text, R"(href="#g3_1")"), std::size_t{2}));
        expect(!text.contains("g3_2")) << "a blank glyph draws nothing";
        expect(text.contains(R"(fill="#111827" fill-opacity="0.5")")) << "a glyph's own opacity";
        // The picture defined once, shown twice, nearest kept.
        expect(eq(count(text, R"(<image id="i)"), std::size_t{1}));
        expect(eq(count(text, R"(<use href="#i)"), std::size_t{2}));
        expect(text.contains("image-rendering:pixelated"));
        // The conic paint as pixels, and said why.
        expect(eq(picture->report.islands.size(), std::size_t{1}));
        expect(picture->report.islands.front().why == xrender::svg::Why::paint);
        expect(text.contains(R"(data-island="paint")"));
        expect(eq(picture->report.glyphs, std::size_t{2}));
        expect(eq(picture->report.images, std::size_t{2}));
        expect(eq(picture->report.unencoded, std::size_t{0}));
        // Written twice, the same bytes; pinned on every platform.
        const auto again = xrender::svg::write(scene.list, resources, {.view = {0, 0, 200, 120}, .node_name = [](std::uint32_t node) {
            return node == 7 ? std::string("main/card") : std::string{};
        }, .encode_png = png});
        expect(again && again->text == picture->text) << "deterministic";
        mlibs::hash::Fnv1a hash;
        hash.text(picture->text);
        std::println("    svg {} bytes, {}", picture->text.size(), hash.hex());
        // Beside the CPU reference's pixels of the same list, to look at.
        const auto dir = scratch_dir("render-svg");
        std::ofstream(dir / "scene.svg", std::ios::binary) << picture->text;
        xrender::raster::Surface surface(400, 240);
        xrender::CpuRenderer cpu;
        (void)cpu.render(scene.list, surface, resources, {.scale = 2});
        expect(mlibs::image::save_png(dir / "scene@2x.png", {surface.bytes().data(), 400, 240, 1600, mlibs::image::Alpha::premultiplied}).has_value());
    };

    "an effect layer is one island drawn by the CPU reference; without an encoder, pixels are left out and counted"_test = [] {
        xrender::DisplayList list;
        list.fill_rect({0, 0, 64, 64}, geom::Color::hex(0x334155));
        const auto ref = *xrender::effects().find("pixelate");
        const auto params = xrender::effects().params(ref, std::vector<std::pair<std::string_view, float>>{{"size", 8}, {"amount", 1}});
        list.push_layer(1, xrender::BlendMode::normal, ref, params, {8, 8, 48, 48});
        list.fill_rect({8, 8, 48, 48}, geom::Color::hex(0xfacc15));
        list.fill_rect({20, 20, 10, 30}, geom::Color::hex(0x7c3aed));
        list.pop_layer();
        list.fill_rect({0, 60, 64, 4}, geom::Color::hex(0xffffff));
        const auto picture = xrender::svg::write(list, {}, {.view = {0, 0, 64, 64}, .encode_png = png});
        expect(picture.has_value());
        if (!picture) return;
        expect(eq(picture->report.islands.size(), std::size_t{1}));
        expect(picture->report.islands.front().why == xrender::svg::Why::effect);
        expect(eq(picture->report.islands.front().command, std::size_t{1}));
        expect(eq(picture->report.islands.front().effect, std::string("pixelate")));
        expect(picture->text.contains(R"svg(data-island="effect" data-effect="pixelate")svg"));
        // What follows the layer is vectors again.
        expect(picture->text.find(R"(fill="#ffffff")") > picture->text.find("data-island"));
        const auto bare = xrender::svg::write(list, {}, {.view = {0, 0, 64, 64}});
        expect(bare && bare->report.unencoded == 1 && !bare->text.contains("data-island"));
    };

    "the opacity effect is SVG's group opacity, not pixels"_test = [] {
        xrender::DisplayList list;
        const auto ref = *xrender::effects().find("opacity");
        const auto params = xrender::effects().params(ref, std::vector<std::pair<std::string_view, float>>{{"amount", 0.5f}});
        list.push_layer(0.5f, xrender::BlendMode::normal, ref, params, {0, 0, 32, 32});
        list.fill_rect({4, 4, 24, 24}, geom::Color::hex(0x0ea5e9));
        list.pop_layer();
        const auto picture = xrender::svg::write(list, {}, {.view = {0, 0, 32, 32}, .encode_png = png});
        expect(picture && picture->report.islands.empty() && picture->text.contains(R"(<g opacity="0.25">)")) << (picture ? picture->text : picture.error());
    };

    "a trimmed stroke is a dash one contour long, each contour its own, the start wrapping"_test = [] {
        xrender::DisplayList list;
        xrender::Path two;
        two.move_to({2, 2}).line_to({30, 2}).move_to({2, 20}).line_to({30, 20});
        list.stroke_path(two, {.width = 2, .trim_start = 0.25f, .trim_end = 0.75f, .trim_offset = 0.5f}, xrender::Paint::solid(geom::Color::hex(0x111827)));
        const auto picture = xrender::svg::write(list, {}, {.view = {0, 0, 32, 32}});
        expect(picture.has_value());
        if (!picture) return;
        expect(eq(count(picture->text, R"(stroke-dasharray="14 14" stroke-dashoffset="-21")"), std::size_t{2}));
        expect(picture->report.islands.empty());
    };

    "pushes and pops that do not nest are refused; a picture needs a size"_test = [] {
        xrender::DisplayList list;
        list.push_clip({0, 0, 10, 10});
        list.push_transform(geom::Affine::translate(1, 1));
        list.pop_clip();
        list.pop_transform();
        expect(!xrender::svg::write(list, {}, {.view = {0, 0, 10, 10}}).has_value());
        expect(!xrender::svg::write(xrender::DisplayList{}, {}, {}).has_value());
    };
}};
