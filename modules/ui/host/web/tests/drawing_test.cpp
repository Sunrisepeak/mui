// Who draws a page's frames (WebOptions::renderer): without a page, the CPU; in
// a page that has the device asked for — the browser's half of the Web row
// (tools/ci/browser.sh runs this program with --webgpu, then with --webgl) —
// that device, and its frame of a page of the framework's own boxes, text and
// a picture within the GPU renderer's tolerances of the CPU's reference
// (rounded corners: the GPU covers the true arc, the CPU flattens it within
// 1/32 pixel, so edge pixels may differ by a few levels).
import std;
import openmui.host.web;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

Renderer wanted = Renderer::automatic;  // --webgpu, --webgl
bool     offscreen = false;              // --offscreen: drawn, not shown (a browser whose GPU cannot present)

View page(Runtime& rt) {
    // A picture: a gradient with a checker, stored in the runtime's images.
    raster::Surface pic(96, 64);
    for (int y = 0; y < pic.height(); ++y)
        for (int x = 0; x < pic.width(); ++x)
            pic.row(y)[x] = raster::Rgba8{static_cast<std::uint8_t>(x * 255 / 96), static_cast<std::uint8_t>(y * 255 / 64),
                                          static_cast<std::uint8_t>(((x / 8 + y / 8) % 2) * 180), 255};
    const auto ref = rt.images().add(std::make_shared<const raster::Surface>(std::move(pic)));
    const std::string source = std::format("image:{}", ref.id);
    return column({.padding = 12, .gap = 8, .background = "background"},
                  {text({.text = "Drawn on the page's GPU", .style = "heading"}),
                   row({.gap = 8},
                       {box({.width = 60, .height = 36, .background = "accent", .radius = 10}),
                        box({.width = 60, .height = 36, .background = "surface", .border_color = "border", .border_width = 2, .radius = 18}),
                        image({.source = source, .alt = "a gradient", .width = 72, .height = 48, .radius = 6})}),
                   text({.text = "The CPU's pixels are the reference; a GPU's are within its tolerances. 中文也一样。", .style = "body"})});
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<char*> args(argv, argv + argc);
    if (std::erase_if(args, [](const char* a) { return std::string_view(a) == "--webgpu"; }) > 0) wanted = Renderer::webgpu;
    if (std::erase_if(args, [](const char* a) { return std::string_view(a) == "--webgl"; }) > 0) wanted = Renderer::webgl;
    offscreen = std::erase_if(args, [](const char* a) { return std::string_view(a) == "--offscreen"; }) > 0;
    Suite s{"host.web.drawing"};

    s.test("without a page the CPU draws, and what is read back is its frame", [] {
        if (in_browser()) return;
        Web w({.size = {320, 200}, .renderer = Renderer::automatic});
        check_eq(std::string(w.renderer()), std::string("cpu"));
        w.mount("main", page(w.runtime()));
        (void)w.step();
        auto f = w.read_frame();
        check(f.has_value() && f->hash() == w.pixels().hash(), "the CPU's frame");
    });

    s.test("in a page with the device asked for, it draws: its frame within the renderer's tolerances of the CPU's", [] {
        if (!in_browser() || wanted == Renderer::automatic) return;
        const std::string_view name = wanted == Renderer::webgpu ? "webgpu" : "webgl";
        Web w({.renderer = wanted, .present = !offscreen});
        check_eq(std::string(w.renderer()), std::string(name));
        if (w.renderer() != name) return;
        w.mount("main", page(w.runtime()));
        (void)w.step();
        (void)w.step();
        auto gpu = w.read_frame();
        check(gpu.has_value(), gpu ? "" : gpu.error());
        if (!gpu) return;
        // The CPU's reference: the same list, size and scale.
        Runtime& rt = w.runtime();
        typeset::GlyphCache glyphs(rt.fonts());
        raster::Surface ref(gpu->width(), gpu->height());
        render::CpuRenderer cpu;
        (void)cpu.render(rt.display_list(), ref, {&rt.images(), &glyphs}, {.scale = rt.scale(), .background = Color::white()});
        // Its text drawn (the page gave it its fonts): dark pixels on the light page.
        std::size_t ink = 0;
        for (int y = 0; y < ref.height(); ++y)
            for (int x = 0; x < ref.width(); ++x)
                if (const raster::Rgba8 c = ref.at(x, y); c.r < 100 && c.g < 100 && c.b < 120) ++ink;
        check(ink > 800, std::format("text drawn: {} dark pixels", ink));
        int worst = 0;
        std::size_t off = 0;
        for (int y = 0; y < ref.height(); ++y)
            for (int x = 0; x < ref.width(); ++x) {
                const raster::Rgba8 c = ref.at(x, y), g = gpu->at(x, y);
                const int d = std::max({std::abs(c.r - g.r), std::abs(c.g - g.g), std::abs(c.b - g.b), std::abs(c.a - g.a)});
                worst = std::max(worst, d);
                if (d > 1) ++off;
            }
        const std::size_t pixels = static_cast<std::size_t>(ref.width()) * static_cast<std::size_t>(ref.height());
        std::println("    {} {}{}×{}: max diff {}, {} of {} pixels off by more than 1 ({} pixels of text)", name, offscreen ? "offscreen " : "", ref.width(), ref.height(), worst,
                     off, pixels, ink);
        check(worst <= 10, std::format("max diff {}", worst));
        check(off * 100 <= pixels, std::format("{} pixels off", off));
        check(w.frames_shown() >= 1, "shown");
    });

    return s.run(static_cast<int>(args.size()), args.data());
}
