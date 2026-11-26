// omui.host.sdl3 drawing on a device, the GPU path installed (omui.host.sdl3.gpu) as an application's SDK
// installs it: the first frame by the CPU and by the GPU, said and held; two hosts, one's device still being made;
// the swapchain shows the GPU renderer's frame, and an effect file edited while it runs shows on the next frame; a
// picture drawn elsewhere shows in the window by both. On the machine's GPU (lavapipe in CI; required with
// OMUI_REQUIRE_GPU=1). They live here, not with the host: a slot's owner cannot depend on its provider.
import std;
import mlibs.text.harfbuzz;
import mcpplibs.os;
import omui.host.sdl3;
import omui.ui.base;  // a button in the first frame
import omui.host.sdl3.gpu;  // the GPU path, installed as an application's SDK installs it
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c{mlibs::text::harfbuzz::shaper()};
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

// A node that shows a picture of one colour, as a world's viewport does (Services::add_picture).
struct Shows {
    static constexpr std::string_view name = "test_shows";
    Services* services = nullptr;
    NodeId node{};
    mrender::raster::Surface picture{4, 4};
    std::uint64_t number = 0;
    void mount(Services& s, NodeId n) {
        services = &s, node = n;
        fill({0, 0, 255, 255});
        number = s.add_picture([this]() -> std::optional<mrender::raster::ImageView> { return picture.view(); });
    }
    ~Shows() {
        if (services) services->remove_picture(number);
    }
    void fill(mrender::raster::Rgba8 colour) {
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) picture.at(x, y) = colour;
    }
    void changed() { number = Services::picture_changed(number), services->invalidate_paint(node); }
    Size measure(MeasureContext&, std::optional<float>, std::optional<float>, mlibs::layout::Available, mlibs::layout::Available) { return {40, 40}; }
    void paint(PaintContext& ctx) const { ctx.list.external_texture(number, {0, 0, ctx.size.width, ctx.size.height}, 1, mrender::Sampling::nearest); }
};

// UI9: how far the GPU's frame of a list may be from the CPU's shown before the device took over, the largest channel
// difference on text and elsewhere (the case below says where each comes from).
constexpr int takeover_bound_text = 50;
constexpr int takeover_bound_rest = 8;

DesktopOptions offscreen(std::string app = "sdl3-test") {
    agent::install();  // the agent surface (the captures the cases read), installed as an application's SDK installs it
    host::sdl3::gpu::install();  // every host of these tests may draw on a device
    DesktopOptions o;
    o.title = "openmui test";
    o.size = {240, 160};
    o.video_driver = "offscreen";
    o.render_driver = "software";
    o.vsync = false;
    o.fonts = &fonts();
    o.app = std::move(app);
    o.agent = false;
    o.accessibility = false;  // its own test
    o.renderer = DesktopOptions::Renderer::cpu;  // the CPU frame, read back exactly
    o.follow_system = false;  // the options' look, whatever the machine's person prefers
    return o;
}

void write(const std::filesystem::path& f, std::string_view text) {
    std::error_code ec;
    const auto before = std::filesystem::exists(f) ? std::filesystem::last_write_time(f) : std::filesystem::file_time_type::min();
    std::ofstream(f, std::ios::binary) << text;
    // Later than what was loaded, whatever the clock's grain.
    if (before != std::filesystem::file_time_type::min())
        std::filesystem::last_write_time(f, std::max(std::filesystem::last_write_time(f), before + std::chrono::seconds(1)), ec);
}

}  // namespace

UTest cases{"host.sdl3.gpu.host", [] {

    "the first frame: from the host made to its first frame shown, by the CPU, by the GPU window the machine chooses (the CPU's frame first, UI9) and by the GPU insisted on — said, and held (RC3.6)"_test = [] {
        using R = DesktopOptions::Renderer;
        for (const R renderer : {R::cpu, R::automatic, R::gpu}) {
            const bool on_gpu = renderer == R::gpu;
            DesktopOptions o = offscreen(renderer == R::cpu ? "sdl3-first-cpu" : on_gpu ? "sdl3-first-gpu" : "sdl3-first-automatic");
            if (renderer != R::cpu) {
                o.renderer = renderer;
                if (renderer == R::automatic) o.render_driver.clear();  // the window's own framebuffer, as an application's
                if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
                if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
            }
            const auto asked = std::chrono::steady_clock::now();
            Desktop d(o);
            auto opened = d.open();
            if (!opened) {
                if (on_gpu && mcpplibs::os::env::get("OMUI_REQUIRE_GPU") != "1") {
                    std::println("  no GPU for the window here ({})", opened.error());
                    continue;
                }
                expect(false) << opened.error().text();
                continue;
            }
            d.mount("main", column({.padding = 20}, {text({.id = "hello", .text = "Hello"}), button({.id = "go", .text = "Go"})}));
            while (d.frames_shown() == 0 && std::chrono::steady_clock::now() - asked < std::chrono::seconds(20)) (void)d.step(false);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - asked).count();
            std::println("  first frame shown by the {} {:.1f} ms after the host was made", renderer == R::cpu ? "CPU" : on_gpu ? "GPU" : d.gpu() ? "GPU (chosen)" : "CPU (chosen)", ms);
            expect(d.frames_shown() > 0) << "shown at all";
            // Held where CI runs it (shared runners, an unoptimised build, a software device): a regression of the start,
            // not the figure the platform table gives (measured on the machine, RC3.6). The bounds from CI's own figures
            // (run 44061517307): the CPU 8.0 ms Linux x64, 11.4 arm, 14.1 Windows, 206 macOS; the GPU 378, 489, 2265 (WARP),
            // 1548 ms. About 7× the CPU's worst (macOS' window server apart, 5×), the GPU's 1.3× Windows' WARP. The
            // window the machine chooses is the CPU's until its device takes over (UI9), so held to the CPU's — where a
            // window keeps a framebuffer of its own (macOS' does not: there it waits for the device, as insisted).
            const double bound = on_gpu || (renderer == R::automatic && mcpplibs::os::is_macos) ? 3000.0 : (mcpplibs::os::is_macos ? 1000.0 : 100.0);
            expect(lt(ms, bound)) << std::format("the first frame took {:.0f} ms (at most {:.0f})", ms, bound);
        }
    };

    "UI9: the GPU window's first frames are the CPU's while its device is made; the device takes over when ready without drawing the same picture again, and draws it within the pinned bound of the CPU's"_test = [] {
        DesktopOptions o = offscreen("sdl3-takeover");
        o.renderer = DesktopOptions::Renderer::automatic;
        o.render_driver.clear();  // the window's own framebuffer, as an application's
        if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
        const auto asked = std::chrono::steady_clock::now();
        Desktop d(o);
        auto opened = d.open();
        expect(fatal(opened.has_value())) << (opened ? "" : opened.error().text());
        auto colour = signal(std::string("#2563eb"));
        d.mount("main", column({.padding = 20, .gap = 8},
                               {text({.id = "hello", .text = "Hello, world"}), button({.id = "go", .text = "Go"}),
                                box({.id = "swatch", .width = 40, .height = 40,
                                     .background = Prop<Fill>(Source{std::nullopt, [colour] { return Value(colour.get()); }, {}}), .radius = 8})}));
        expect(d.step(false));
        const double first = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - asked).count();
        expect(fatal(d.frames_shown() > 0)) << "a frame at once";
        if (!d.gpu_pending()) {
            std::println("  {} at the first frame ({:.1f} ms): nothing to take over here", d.gpu() ? "the device was ready" : "no device is being made", first);
            return;
        }
        expect(!d.gpu()) << "the first frame is the CPU's while the device is made";
        auto cpu = d.window_pixels();
        expect(fatal(cpu.has_value())) << (cpu ? "" : cpu.error().text());
        const std::size_t shown = d.frames_shown();
        // The device's thread wakes the loop when it is done: steps that sleep until an event take it over.
        while (d.gpu_pending() && std::chrono::steady_clock::now() - asked < std::chrono::seconds(20)) (void)d.step(true);
        const double taken = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - asked).count();
        expect(!d.gpu_pending()) << "taken, or let go, within 20 s";
        if (!d.gpu()) {
            std::println("  first frame by the CPU at {:.1f} ms; no device took over (none here, or a software one: the CPU draws on)", first);
            return;
        }
        std::println("  first frame by the CPU at {:.1f} ms; the device took over at {:.1f} ms", first, taken);
        expect(eq(d.frames_shown(), shown)) << "the same picture is not drawn again at the takeover";
        auto still = d.window_pixels();
        expect(still.has_value() && still->pixels().size() == cpu->pixels().size() &&
               std::ranges::equal(still->pixels(), cpu->pixels(), [](const auto& a, const auto& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }))
            << "what is shown is still the CPU's frame";
        // The same list drawn by the GPU: changed, and changed back.
        colour.set("#dc2626");
        expect(d.step(false));
        colour.set("#2563eb");
        expect(d.step(false));
        expect(eq(d.frames_shown(), shown + 2)) << "the GPU draws from the first change on";
        auto gpu = d.window_pixels();
        expect(fatal(gpu.has_value())) << (gpu ? "" : gpu.error().text());
        expect(fatal(gpu->width() == cpu->width() && gpu->height() == cpu->height()));
        // Text against the rest: the CPU places a glyph's mask at a quarter pixel (mrender.text's phases), the device
        // covers its outline where it is (glyph_masks_a_live_frame) — an edge moves at most √2/8 pixel across itself, 45
        // levels of a full-contrast edge, and the outline's coverage is within 5 of the mask's (mrender's CTS,
        // render.vector.glyphs-outlined): 50 on text. Elsewhere the CTS's own: 8 on a rounded edge (the CPU's chords
        // within 1/32 pixel), a level on the rest. Measured on the RTX 4080: 43 on text, 2 elsewhere.
        const float scale = d.scale();
        std::vector<Rect> text_areas;
        for (const std::string_view node : {"main/hello", "main/go"}) {
            const Rect area = d.runtime().drawn_bounds(d.runtime().find(node));
            text_areas.push_back({area.x * scale - 1, area.y * scale - 1, area.width * scale + 2, area.height * scale + 2});
        }
        int worst_text = 0, worst_rest = 0;
        std::size_t off = 0;
        for (int y = 0; y < cpu->height(); ++y)
            for (int x = 0; x < cpu->width(); ++x) {
                const auto a = cpu->at(x, y), b = gpu->at(x, y);
                const int diff = std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b), std::abs(a.a - b.a)});
                const bool text = std::ranges::any_of(text_areas, [&](const Rect& r) {
                    return static_cast<float>(x) >= r.x && static_cast<float>(x) < r.x + r.width && static_cast<float>(y) >= r.y && static_cast<float>(y) < r.y + r.height;
                });
                (text ? worst_text : worst_rest) = std::max(text ? worst_text : worst_rest, diff);
                off += diff > 1 ? 1 : 0;
            }
        std::println("  the CPU's frame against the GPU's of the same list: max diff {} on text, {} elsewhere; {} pixels off by more than 1 ({:.2f} %)",
                     worst_text, worst_rest, off, 100.0 * static_cast<double>(off) / static_cast<double>(cpu->width() * cpu->height()));
        expect(le(worst_text, takeover_bound_text)) << std::format("text: {} (at most {})", worst_text, takeover_bound_text);
        expect(le(worst_rest, takeover_bound_rest)) << std::format("elsewhere: {} (at most {})", worst_rest, takeover_bound_rest);
    };

    "UI9: a window that keeps moving is taken over too — a second after its device is ready, not at rest, which never comes"_test = [] {
        DesktopOptions o = offscreen("sdl3-takeover-moving");
        o.renderer = DesktopOptions::Renderer::automatic;
        o.render_driver.clear();
        if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
        Desktop d(o);
        expect(fatal(d.open().has_value()));
        // A swatch that changes every frame, and frames wanted at 60 a second: the loop never rests.
        auto colour = signal(std::string("#2563eb"));
        d.mount("main", column({.padding = 20}, {box({.id = "swatch", .width = 40, .height = 40,
                                                      .background = Prop<Fill>(Source{std::nullopt, [colour] { return Value(colour.get()); }, {}})})}));
        auto lease = d.runtime().demand_frames({.min_hz = 60, .max_hz = 60, .preferred_hz = 60, .reason = "test"});
        bool red = false;
        const auto change = [&] {
            red = !red;
            colour.set(red ? "#dc2626" : "#2563eb");
        };
        expect(d.step(false));
        if (!d.gpu_pending()) {
            std::println("  no device is being made here: nothing to take over");
            return;
        }
        const auto from = std::chrono::steady_clock::now();
        const std::size_t first = d.frames_shown();
        while (d.gpu_pending() && std::chrono::steady_clock::now() - from < std::chrono::seconds(20)) {
            change();
            (void)d.step(true);
        }
        const double taken = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
        expect(!d.gpu_pending()) << "taken, or let go, within 20 s";
        if (!d.gpu()) {
            std::println("  no device took over (none here, or a software one): the CPU draws on");
            return;
        }
        const std::size_t by_cpu = d.frames_shown() - first;
        std::println("  moving all along: the device took over {:.0f} ms after the first frame; the CPU showed {} frames before", taken, by_cpu);
        expect(gt(by_cpu, std::size_t{10})) << "the CPU drew the frames meanwhile";
        const std::size_t shown = d.frames_shown();
        for (int i = 0; i < 10; ++i) {
            change();
            (void)d.step(true);
        }
        expect(eq(d.frames_shown(), shown + 10)) << "and the GPU draws them after";
    };

    "no device to be made: the CPU's frames go into the window's own framebuffer — shown, read back exactly, at the rate the loop wants"_test = [] {
        DesktopOptions o = offscreen("sdl3-framebuffer");
        o.renderer = DesktopOptions::Renderer::automatic;
        o.render_driver.clear();                    // no renderer asked for by name
        o.vulkan_library = "/no/such/libvulkan.so";  // no device made
        o.vsync = true;                             // asked; the framebuffer shows a frame at once, so the loop holds the rate
        Desktop d(o);
        auto opened = d.open();
        expect(opened.has_value()) << (opened ? "" : opened.error().text());
        if (!opened) return;
        if (d.gpu()) {
            std::println("  a device made here without Vulkan (the platform's own API): the framebuffer is not used");
            return;
        }
        auto color = signal(std::string("#ff0000"));
        d.mount("main", column({.padding = 20}, {box({.id = "swatch", .width = 40, .height = 40, .background = Prop<Fill>(Source{std::nullopt, [color] { return Value(color.get()); }, {}})})}));
        expect(d.step(false));
        auto px = d.window_pixels();
        expect(px.has_value()) << (px ? "" : px.error().text());
        if (!px) return;
        const int at = static_cast<int>(40 * d.scale());
        expect(px->width() == static_cast<int>(240 * d.scale()) && px->at(at, at).r == 255 && px->at(at, at).g == 0 && px->at(5, 5).b == 255)
            << std::format("the swatch {} {} {}", px->at(at, at).r, px->at(at, at).g, px->at(at, at).b);
        color.set("#0000ff");
        expect(d.step(false));
        auto blue = d.window_pixels();
        expect(blue && blue->at(at, at).b == 255 && blue->at(at, at).r == 0) << "the next frame shown there";
        // Thirty frames wanted at 60 a second take about half a second: none shown faster than the display's rate.
        auto lease = d.runtime().demand_frames({.min_hz = 30, .max_hz = 60, .preferred_hz = 60, .reason = "test"});
        expect(d.step(false));
        const auto from = std::chrono::steady_clock::now();
        for (int i = 0; i < 30; ++i) expect(d.step(true));
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
        std::println("  30 frames at 60 Hz into the framebuffer: {:.3f} s", took);
        expect(took > 0.45 && took < 3.0) << std::format("paced: {:.3f} s", took);
    };

    "two hosts: one's device still being made while another opens a window and ends — SDL stays up for the first, which draws"_test = [] {
        DesktopOptions o = offscreen("sdl3-held-gpu");
        o.renderer = DesktopOptions::Renderer::gpu;
        if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
        Desktop first(o);  // its device begun on a thread of its own, SDL held for it
        {
            Desktop second(offscreen("sdl3-held-cpu"));
            expect(fatal(second.open().has_value()));
            second.mount("main", text({.text = "the other"}));
            (void)second.step(false);
        }  // its window, the only one open, closed: SDL is still held by the first
        auto opened = first.open();
        if (!opened) {
            if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") != "1") {
                std::println("  no GPU for the window here ({})", opened.error());
                return;
            }
            expect(false) << opened.error().text();
            return;
        }
        first.mount("main", column({.padding = 20}, {text({.id = "hello", .text = "Hello"})}));
        const auto asked = std::chrono::steady_clock::now();
        while (first.frames_shown() == 0 && std::chrono::steady_clock::now() - asked < std::chrono::seconds(20)) (void)first.step(false);
        expect(first.gpu() && first.frames_shown() > 0) << "the first host's frame shown on its device (not a fallback)";
    };

    "on the GPU: the swapchain shows the frame, and an effect file edited while it runs shows on the next frame"_test = [] {
        const auto dir = std::filesystem::temp_directory_path() / "openmui-host-effects";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        write(dir / "invert.effect.xdef.json", R"({"$format": "effect@1", "effect": {"name": "invert", "doc": "The colours turned over."}})");
        const std::string invert = R"(import openmui_effect;
[shader("fragment")]
float4 invert_fs(EffectPixel px) : SV_Target {
    const uint4 s = source_texel(at(px));
    return out8(uint4(s.a - s.r, s.a - s.g, s.a - s.b, s.a));
}
)";
        write(dir / "invert.slang", invert);
        DesktopOptions o = offscreen("sdl3-gpu");
        o.renderer = DesktopOptions::Renderer::gpu;
        o.effects = dir;
        if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
        Desktop d(o);
        auto opened = d.open();
        if (!opened) {
            if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") expect(false) << std::format("OMUI_REQUIRE_GPU=1: {}", opened.error());
            else std::println("  no GPU for the window here ({})", opened.error());
            return;
        }
        expect(d.gpu()) << "the GPU draws";
        expect(d.effects() && d.effects()->names() == std::vector<std::string>{"invert"}) << "the effect file loaded with the window";
        d.mount("main", column({.padding = 20}, {box({.id = "swatch", .width = 40, .height = 40, .background = Fill(Color::hex(0xff0000))})}));
        expect(d.step());
        expect(eq(d.frames_shown(), std::size_t{1}));
        const auto pixel = [&](int x, int y) -> std::optional<mrender::raster::Rgba8> {
            auto px = d.window_pixels();
            expect(px.has_value()) << (px ? "" : px.error().text());
            if (!px) return std::nullopt;
            return px->at(static_cast<int>(x * d.scale()), static_cast<int>(y * d.scale()));
        };
        if (auto at = pixel(40, 40)) expect(at->r == 255 && at->g == 0 && at->b == 0) << std::format("red: {} {} {}", at->r, at->g, at->b);
        if (auto bg = pixel(5, 5)) expect(bg->r == 255 && bg->g == 255 && bg->b == 255) << "the background";

        // The node under the program's effect.
        Value fx;
        fx.set("name", "invert");
        d.runtime().set(d.runtime().find("main/swatch"), "pixel_effect", fx);
        d.step();
        if (auto at = pixel(40, 40)) expect(at->r == 0 && at->g == 255 && at->b == 255) << std::format("inverted: {} {} {}", at->r, at->g, at->b);

        // The shader edited while the program runs: on the next frame. Three
        // edits (green, blue, green): the latency is the fastest of them — a
        // CI machine busy compiling other members adds its own time to some.
        std::vector<std::chrono::milliseconds> took;
        for (const auto& [code, want] : std::array<std::pair<std::string_view, mrender::raster::Rgba8>, 3>{{
                 {"uint4(0, s.a, 0, s.a)", {0, 255, 0, 255}}, {"uint4(0, 0, s.a, s.a)", {0, 0, 255, 255}}, {"uint4(0, s.a, 0, s.a)", {0, 255, 0, 255}}}}) {
            std::string edited = invert;
            edited.replace(edited.find("uint4(s.a - s.r, s.a - s.g, s.a - s.b, s.a)"), std::string_view("uint4(s.a - s.r, s.a - s.g, s.a - s.b, s.a)").size(), code);
            write(dir / "invert.slang", edited);
            const auto from = std::chrono::steady_clock::now();
            d.step();
            took.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - from));
            if (auto at = pixel(40, 40)) expect(at->r == want.r && at->g == want.g && at->b == want.b) << std::format("edited: {} {} {}", at->r, at->g, at->b);
        }
        std::println("  from the saved shader to the frame showing it: {} ms, {} ms, {} ms", took[0].count(), took[1].count(), took[2].count());
        expect(std::ranges::min(took) < std::chrono::milliseconds(500)) << "M4 ③: within 500 ms";
        std::filesystem::remove_all(dir, ec);
    };

    "a picture drawn elsewhere shows in the window, by the CPU and on the GPU: a node names it, the host has its pixels; changed, it shows changed"_test = [] {
        for (const bool on_gpu : {false, true}) {
            DesktopOptions o = offscreen(on_gpu ? "sdl3-shown-gpu" : "sdl3-shown-cpu");
            if (on_gpu) {
                o.renderer = DesktopOptions::Renderer::gpu;
                if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
                if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
            }
            Desktop d(o);
            auto opened = d.open();
            if (!opened) {
                if (on_gpu && mcpplibs::os::env::get("OMUI_REQUIRE_GPU") != "1") {
                    std::println("  no GPU for the window here ({})", opened.error());
                    continue;
                }
                expect(false) << opened.error();
                continue;
            }
            Element shows;
            shows.type = &component_type<Shows>();
            shows.id = "world";
            d.mount("main", column({.padding = 20}, {std::move(shows)}));
            expect(d.step());
            const auto pixel = [&](int x, int y) -> mrender::raster::Rgba8 {
                auto px = d.window_pixels();
                expect(px.has_value()) << (px ? "" : px.error().text());
                return px ? px->at(static_cast<int>(x * d.scale()), static_cast<int>(y * d.scale())) : mrender::raster::Rgba8{};
            };
            const auto first = pixel(40, 40), outside = pixel(5, 5);
            expect(first.r == 0 && first.g == 0 && first.b == 255) << std::format("{}: blue, the picture's: {} {} {}", on_gpu ? "gpu" : "cpu", first.r, first.g, first.b);
            expect(outside.r == 255 && outside.g == 255 && outside.b == 255) << "and not outside its node";
            auto* component = d.runtime().component<Shows>(d.runtime().find("main/world"));
            expect(fatal(component != nullptr));
            component->fill({255, 0, 0, 255});
            component->changed();
            d.step();
            const auto second = pixel(40, 40);
            expect(second.r == 255 && second.g == 0 && second.b == 0) << std::format("{}: red once it changed: {} {} {}", on_gpu ? "gpu" : "cpu", second.r, second.g, second.b);
        }
    };
}};
