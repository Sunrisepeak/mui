// `omui::run` on the desktop: a window (openmui.host.sdl3).
module openmui;

import std;
import openmui.core;
import openmui.view;
import openmui.host.sdl3;
import :app;
import :shown;

namespace omui::detail {

int shown(const AppSpec& app, const RunOptions& o, const Prepare& prepare) {
    DesktopOptions d;
    d.title = app.title;
    d.size = o.size.value_or(app.size);
    d.app = app.id;
    d.agent = o.agent;
    d.agent_directory = o.agent_dir;
    d.theme = o.dark ? Theme::dark_theme() : app.theme;
    d.effects = app.effects;
    d.reduce_transparency = o.reduce_transparency;
    if (o.gpu) d.renderer = *o.gpu ? DesktopOptions::Renderer::gpu : DesktopOptions::Renderer::cpu;
    else if (app.renderer == "cpu") d.renderer = DesktopOptions::Renderer::cpu;
    else if (app.renderer == "gpu") d.renderer = DesktopOptions::Renderer::gpu;
    const std::string_view mode = o.window.empty() ? std::string_view(app.window) : std::string_view(o.window);
    d.frame = mode == "framework" ? DesktopOptions::Frame::framework : DesktopOptions::Frame::native;
    Desktop desktop(d);
    if (auto r = desktop.open(); !r) {
        error("no window ({}); `--headless` runs without a display", r.error());
        return 1;
    }
    desktop.runtime().set_debug(o.debug);
    const auto kept = prepare(desktop.runtime());
    return desktop.run();
}

std::vector<std::string> host_locales() { return preferred_locales(); }

}  // namespace omui::detail
