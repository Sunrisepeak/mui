// `omui::run` on the desktop: a window (omui.host.sdl3).
module omui;

import std;
import mcpplibs.os;
import mlibs.compose;
import mlibs.error;
import mlibs.trace;
import omui.core;
import omui.view;
import omui.host.sdl3;
import omui.host.terminal;
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
    d.position = o.position;
    d.app = app.id;
    d.agent = o.agent;
    d.agent_input = o.agent_input.value_or(app.agent_input);
    d.agent_directory = o.agent_dir;
    d.accessibility = o.accessibility && app.accessibility;
    d.theme = app.theme;  // --dark is already in it (omui::run)
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
    std::optional<mlibs::trace::Span> preparing{std::in_place, "start.application", "app"};  // its setup: its views, its scripts
    const auto kept = prepare(desktop.runtime());
    preparing.reset();
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
        // --png FILE: the frame on screen, read back from the window (a headless run writes its own) — once what runs
        // beside the runtime (scripts) is ready, as a headless run waits for it, within ten seconds; then its next
        // frames drawn.
        if (!o.png.empty()) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (app.ready && !app.ready() && std::chrono::steady_clock::now() < until) {
                if (!desktop.step(false)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            for (int settled = 0; settled < 3; ++settled)
                if (!desktop.step(false)) break;
            auto png = desktop.runtime().capture_png();
            if (!png) {
                error("--png: the window's frame: {}", png.error().message);
                return 1;
            }
            std::ofstream out(o.png, std::ios::binary);
            if (!out.write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()))) {
                error("--png: cannot write {}", mcpplibs::os::fs::utf8(o.png));
                return 1;
            }
        }
        return 0;
    }
    return desktop.run();
}

int terminal(const AppSpec& app, const RunOptions& o, const Prepare& prepare) {
    // The terminal is a capability this program is built with (the feature `terminal`): without it, said, with the way.
    const omui::terminal::ShowService* show = mlibs::compose::process().get(omui::terminal::show_slot);
    if (!show) {
        error("--terminal: {}", mlibs::compose::process().missing(omui::terminal::show_slot.info).message);
        return 2;
    }
    TerminalOptions t;
    t.app = app.id;
    t.theme = app.theme;
    t.agent = o.agent;
    t.agent_input = o.agent_input.value_or(app.agent_input);
    t.endpoint_directory = o.agent_dir;
    t.endpoint_name = o.agent_name;
    return show->run(std::move(t), prepare);
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
