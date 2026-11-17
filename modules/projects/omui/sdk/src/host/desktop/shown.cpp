// `omui::run` on the desktop: a window (openmui.host.sdl3).
module omui;

import std;
import mlibs.error;
import omui.core;
import omui.view;
import omui.host.sdl3;
import :app;
import :shown;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::error;
}

namespace omui::hosting {

int shown(const AppSpec& app, const RunOptions& o, const Prepare& prepare) {
    DesktopOptions d;
    d.title = app.title;
    d.size = o.size.value_or(app.size);
    d.app = app.id;
    d.agent = o.agent;
    d.agent_input = o.agent_input.value_or(app.agent_input);
    d.agent_directory = o.agent_dir;
    d.accessibility = o.accessibility && app.accessibility;
    d.theme = o.dark ? Theme::dark_theme() : app.theme;
    d.effects = app.effects;
    d.effect_data = app.effect_data;
    d.reduce_transparency = o.reduce_transparency;
    if (o.gpu) d.renderer = *o.gpu ? DesktopOptions::Renderer::gpu : DesktopOptions::Renderer::cpu;
    else if (app.renderer == "cpu") d.renderer = DesktopOptions::Renderer::cpu;
    else if (app.renderer == "gpu") d.renderer = DesktopOptions::Renderer::gpu;
    const std::string_view mode = o.window.empty() ? std::string_view(app.window) : std::string_view(o.window);
    d.frame = mode == "framework" ? DesktopOptions::Frame::framework : DesktopOptions::Frame::native;
    const auto asked = std::chrono::steady_clock::now();
    Desktop desktop(d);
    if (auto r = desktop.open(); !r) {
        error("no window ({}); `--headless` runs without a display", r.error());
        return 1;
    }
    desktop.runtime().set_debug(o.debug);
    const auto kept = prepare(desktop.runtime());
    if (o.frames) {
        // --frames N: N frames stepped, as a headless run steps them, and as many more as it takes for the first to
        // be shown (within ten seconds); it says when the first was shown (RC3.6: the first frame's time).
        const auto deadline = asked + std::chrono::seconds(10);
        std::optional<double> first;
        for (int stepped = 0; stepped < *o.frames || (!first && std::chrono::steady_clock::now() < deadline); ++stepped) {
            if (!desktop.step(false)) break;
            if (!first && desktop.frames_shown() > 0)
                first = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - asked).count();
        }
        if (!first) {
            error("--frames: no frame was shown within ten seconds");
            return 1;
        }
        std::println(std::cerr, "first frame shown {:.1f} ms after the window was asked for", *first);
        return 0;
    }
    return desktop.run();
}

std::vector<std::string> host_locales() { return preferred_locales(); }

}  // namespace omui::hosting

namespace omui {

std::expected<void, Failure> choose_renderer(std::string_view name) {
    if (name != "automatic" && name != "gpu" && name != "cpu")
        return std::unexpected(Failure{mlibs::error::Kind::invalid_argument, std::format("`{}` is not a renderer (automatic, gpu, cpu)", name)});
    return std::unexpected(name == "automatic" ? std::string("the next start chooses (without --gpu or --cpu)")
                                               : std::format("the next start draws with the {} (--{})", name == "gpu" ? "GPU" : "CPU", name));
}

}  // namespace omui
