// Who draws a page's frames (WebOptions::renderer): without a page, the CPU; in
// a page that has the device asked for — the browser's half of the Web row
// (omui-devtools check browser runs this program with --webgpu, then with --webgl) —
// that device, and its frame of a page of the framework's own boxes, text and
// a picture within the GPU renderer's tolerances of the CPU's reference
// (rounded corners: the GPU covers the true arc, the CPU flattens it within
// 1/32 pixel, so edge pixels may differ by a few levels). A frame shown has its
// text from outlines, each glyph where it is; its reference is the CPU's masks
// rasterised there (ExactGlyphs). A frame kept for read_frame has every glyph's
// mask, and is the reference's bytes (--offscreen).
import std;
import omui.host.web;
import mcpplibs.mtest;
import mrender.text;
import mrender.raster;
import mrender.vector;
import mlibs.geom;
import mlibs.text.harfbuzz;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

Renderer wanted = Renderer::automatic;  // --webgpu, --webgl
bool     offscreen = false;              // --offscreen: drawn, not shown (a browser whose GPU cannot present)

// The reference for a frame shown: the CPU's glyph masks, each rasterised at the exact fraction of a pixel its glyph
// is at — what an outline covered on the device is. mrender.text's cache keeps a mask for each of four phases, the
// nearest (the last takes [7/8, 1) too, so a glyph is up to a quarter pixel from its place: 58 levels of this page's
// text), which is the reference for a frame kept for read_frame, and not for one that draws each glyph where it is.
class ExactGlyphs final : public mrender::GlyphSource {
public:
    explicit ExactGlyphs(mrender::text::GlyphCache& cache) : cache_(cache) {}
    mrender::GlyphMask mask(mrender::FontRef font, std::uint32_t glyph, float size, mlibs::geom::Point fraction) override {
        mrender::GlyphMask coloured = cache_.mask(font, glyph, size, fraction);
        if (coloured.color) return coloured;  // a picture of a glyph is placed by its cache
        const auto key = std::tuple(font.id, glyph, size, fraction.x, fraction.y);
        auto found = masks_.find(key);
        if (found == masks_.end()) {
            auto made = std::make_unique<mrender::raster::Coverage>();
            mrender::raster::fill_coverage(cache_.outline(font, glyph), mlibs::geom::Affine::translate(fraction.x, fraction.y) * mlibs::geom::Affine::scale(size, size),
                                           mrender::raster::FillRule::nonzero, {-4096, -4096, 8192, 8192}, *made);
            found = masks_.emplace(key, std::move(made)).first;
        }
        return {found->second.get(), nullptr, {}};
    }
    mrender::raster::PathData outline(mrender::FontRef font, std::uint32_t glyph) override { return cache_.outline(font, glyph); }

private:
    mrender::text::GlyphCache& cache_;
    std::map<std::tuple<std::uint32_t, std::uint32_t, float, float, float>, std::unique_ptr<mrender::raster::Coverage>> masks_;
};

View page(Runtime& rt) {
    // A picture: a gradient with a checker, stored in the runtime's images.
    mrender::raster::Surface pic(96, 64);
    for (int y = 0; y < pic.height(); ++y)
        for (int x = 0; x < pic.width(); ++x)
            pic.row(y)[x] = mrender::raster::Rgba8{static_cast<std::uint8_t>(x * 255 / 96), static_cast<std::uint8_t>(y * 255 / 64),
                                          static_cast<std::uint8_t>(((x / 8 + y / 8) % 2) * 180), 255};
    const auto ref = rt.images().add(std::make_shared<const mrender::raster::Surface>(std::move(pic)));
    const std::string source = std::format("image:{}", ref.id);
    return column({.padding = 12, .gap = 8, .background = "background"},
                  {text({.id = "title", .text = "Drawn on the page's GPU", .style = "heading"}),
                   row({.gap = 8},
                       {box({.width = 60, .height = 36, .background = "accent", .radius = 10}),
                        box({.width = 60, .height = 36, .background = "surface", .border_color = "border", .border_width = 2, .radius = 18}),
                        image({.source = source, .alt = "a gradient", .width = 72, .height = 48, .radius = 6})}),
                   text({.id = "words", .text = "The CPU's pixels are the reference; a GPU's are within its tolerances. 中文也一样。", .style = "body"})});
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<char*> args(argv, argv + argc);
    if (std::erase_if(args, [](const char* a) { return std::string_view(a) == "--webgpu"; }) > 0) wanted = Renderer::webgpu;
    if (std::erase_if(args, [](const char* a) { return std::string_view(a) == "--webgl"; }) > 0) wanted = Renderer::webgl;
    offscreen = std::erase_if(args, [](const char* a) { return std::string_view(a) == "--offscreen"; }) > 0;
    // The test is the program: it installs the shaper the page's words are drawn with (mlibs.text's slot; the host
    // installs none — a program's root chooses its text measure).
    static_cast<void>(mlibs::text::harfbuzz::install());

    "without a page the CPU draws, and what is read back is its frame"_test = [] {
        if (in_browser()) return;
        Web w({.size = {320, 200}, .renderer = Renderer::automatic});
        expect(eq(std::string(w.renderer()), std::string("cpu")));
        w.mount("main", page(w.runtime()));
        (void)w.step();
        auto f = w.read_frame();
        expect(f.has_value() && f->hash() == w.pixels().hash()) << "the CPU's frame";
    };

    "in a page with the device asked for, it draws: its frame within the renderer's tolerances of the CPU's"_test = [] {
        if (!in_browser() || wanted == Renderer::automatic) return;
        const std::string_view name = wanted == Renderer::webgpu ? "webgpu" : "webgl";
        Web w({.renderer = wanted, .present = !offscreen});
        expect(eq(std::string(w.renderer()), std::string(name)));
        if (w.renderer() != name) return;
        w.mount("main", page(w.runtime()));
        (void)w.step();
        (void)w.step();
        auto gpu = w.read_frame();
        expect(gpu.has_value()) << (gpu ? "" : gpu.error().text());
        if (!gpu) return;
        // The CPU's reference: the same list, size and scale. A frame shown is one of many (the page's frames follow one
        // another, as a window's): its text is its outlines' (mrender::glyph_masks_a_live_frame), each glyph where it
        // is, so its reference has each glyph's mask where it is (ExactGlyphs), and the outline's coverage is within
        // 5 levels of the mask's (mrender's CTS, render.vector.glyphs-outlined). A frame kept for read_frame
        // (--offscreen) has every glyph's mask, the reference's bytes at its four phases.
        Runtime& rt = w.runtime();
        mrender::text::GlyphCache glyphs(rt.fonts());
        ExactGlyphs exact(glyphs);
        mrender::raster::Surface ref(gpu->width(), gpu->height());
        mrender::CpuRenderer cpu;
        (void)cpu.render(rt.display_list(), ref, {&rt.images(), offscreen ? static_cast<mrender::GlyphSource*>(&glyphs) : &exact}, {.scale = rt.scale(), .background = Color::white()});
        // Its text drawn (the page gave it its fonts): dark pixels on the light page.
        std::size_t ink = 0;
        for (int y = 0; y < ref.height(); ++y)
            for (int x = 0; x < ref.width(); ++x)
                if (const mrender::raster::Rgba8 c = ref.at(x, y); c.r < 100 && c.g < 100 && c.b < 120) ++ink;
        expect(ink > 800) << std::format("text drawn: {} dark pixels", ink);
        const float scale = rt.scale();
        std::vector<Rect> text_areas;
        for (const std::string_view node : {"main/title", "main/words"}) {
            const Rect area = rt.drawn_bounds(rt.find(node));
            text_areas.push_back({area.x * scale - 1, area.y * scale - 1, area.width * scale + 2, area.height * scale + 2});
        }
        int worst = 0, worst_text = 0;
        std::size_t off = 0;
        for (int y = 0; y < ref.height(); ++y)
            for (int x = 0; x < ref.width(); ++x) {
                const mrender::raster::Rgba8 c = ref.at(x, y), g = gpu->at(x, y);
                const int d = std::max({std::abs(c.r - g.r), std::abs(c.g - g.g), std::abs(c.b - g.b), std::abs(c.a - g.a)});
                const bool on_text = std::ranges::any_of(text_areas, [&](const Rect& r) {
                    return static_cast<float>(x) >= r.x && static_cast<float>(x) < r.x + r.width && static_cast<float>(y) >= r.y && static_cast<float>(y) < r.y + r.height;
                });
                (on_text ? worst_text : worst) = std::max(on_text ? worst_text : worst, d);
                if (d > 1) ++off;
            }
        const std::size_t pixels = static_cast<std::size_t>(ref.width()) * static_cast<std::size_t>(ref.height());
        std::println("    {} {}{}×{}: max diff {} on text, {} elsewhere; {} of {} pixels off by more than 1 ({} pixels of text)", name, offscreen ? "offscreen " : "",
                     ref.width(), ref.height(), worst_text, worst, off, pixels, ink);
        expect(worst_text <= 10) << std::format("text: max diff {}", worst_text);
        expect(worst <= 10) << std::format("elsewhere: max diff {}", worst);
        expect(off * 100 <= pixels) << std::format("{} pixels off", off);
        expect(w.frames_shown() >= 1) << "shown";
    };

    return mcpplibs::mtest::run(static_cast<int>(args.size()), args.data());
}
