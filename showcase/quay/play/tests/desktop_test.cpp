// Quay in a real window (SDL3: offscreen, or OMUI_GPU_VIDEO_DRIVER's) whose frames a device draws (lavapipe in CI, the real GPU by
// omui-devtools gpu --real): the harbour is drawn on that same device and shown as it is — no pixels read back or sent
// again — and the window's frame shows it where the harbour's node is. With OMUI_REQUIRE_GPU=1 a missing device
// fails; otherwise it is said and the case passes vacuously. The window's frame is kept for a person to look at.
import std;
import omui;
import omui.host.sdl3;
import omui.showcase.quay.window;
import mlibs.image;
import mcpplibs.os;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection made = [] {
        mlibs::text::FontCollection collection;
        (void)collection.add_defaults();
        return collection;
    }();
    return made;
}

}  // namespace

UTest cases{"quay.desktop", [] {
    "the harbour in a window drawn by a device: drawn on that device and shown as it is, where its node is"_test = [] {
        omui::DesktopOptions options;
        options.title = "Quay";
        options.size = {960, 600};
        // The window's video driver as every device test takes it: macOS CI's is cocoa (omui-devtools gpu), since
        // SDL's offscreen driver gives Metal no view and so no device (red 98).
        options.video_driver = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER").value_or("offscreen");
        options.vsync = false;
        options.fonts = &fonts();
        options.agent = false;
        options.accessibility = false;
        options.renderer = omui::DesktopOptions::Renderer::gpu;
        options.theme = omui::Theme::dark_theme();
        if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *library;
        omui::Desktop desktop(options);
        const auto opened = desktop.open();
        if (!opened || !desktop.gpu()) {
            const std::string why = opened ? std::string("the window draws on the CPU") : opened.error().text();
            if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", why);
            else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", why);
            return;
        }
        const omui::AppSpec spec = quay::window::app({.hearing = quay::window::Hearing::silent});
        spec.setup(desktop.runtime());
        desktop.mount("main", spec.view());
        // Half a second of the game on the window's own clock, and 30 frames shown at least (a software device
        // draws a frame in a good part of a second).
        const auto began = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - began < std::chrono::milliseconds(500) || desktop.frames_shown() < 30) expect(desktop.step());
        const omui::NodeId node = desktop.runtime().find("main/harbour");
        const auto* harbour = desktop.runtime().component<quay::window::Harbour>(node);
        expect(fatal(harbour != nullptr && harbour->game() != nullptr));
        expect(harbour->drawn_on_device()) << "the host draws on a device: the harbour is drawn on it";

        const auto frame = desktop.window_pixels();
        expect(fatal(frame.has_value())) << (frame ? "" : frame.error().text());
        // Where the harbour's node is the world shows: lamps brighter than anything the interface draws, and the
        // quay's greys — many levels, where an empty node would be one colour.
        const omui::Rect box = desktop.runtime().bounds(node);
        const float scale = desktop.scale();
        std::set<std::uint32_t> colours;
        int brightest = 0;
        for (int y = static_cast<int>(box.y * scale); y < static_cast<int>((box.y + box.height) * scale) && y < frame->height(); y += 2)
            for (int x = static_cast<int>(box.x * scale); x < static_cast<int>((box.x + box.width) * scale) && x < frame->width(); x += 2) {
                const auto pixel = frame->at(x, y);
                colours.insert((static_cast<std::uint32_t>(pixel.r) << 16) | (static_cast<std::uint32_t>(pixel.g) << 8) | pixel.b);
                brightest = std::max({brightest, static_cast<int>(pixel.r), static_cast<int>(pixel.g), static_cast<int>(pixel.b)});
            }
        const std::filesystem::path file = std::filesystem::temp_directory_path() / "openmui-tests" / "quay" / "quay-window.png";
        std::filesystem::create_directories(file.parent_path());
        const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(&frame->at(0, 0)), frame->width(), frame->height(), frame->width() * 4, mlibs::image::Alpha::premultiplied};
        expect(mlibs::image::save_png(file, view).has_value());
        std::println("in a window on {}: {} steps, {} frames shown; the harbour's node {} × {} — {} colours, the brightest {}; {}", "the device", harbour->game()->state().frame,
                     desktop.frames_shown(), box.width, box.height, colours.size(), brightest, file.generic_string());
        expect(gt(colours.size(), std::size_t{200})) << "the world, not an empty node";
        expect(gt(brightest, 200)) << "a lamp's globe";
        expect(ge(harbour->game()->state().frame, 20u)) << "the game ran on the window's clock";
    };
}};
