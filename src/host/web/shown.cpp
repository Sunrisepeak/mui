// `omui::run` on the Web: the page's canvas (openmui.host.web). The page's
// loop takes over and main does not return; the host lives as long as the page.
module openmui;

import std;
import openmui.core;
import openmui.view;
import openmui.host.web;
import :app;
import :shown;

namespace omui::detail {

int shown(const AppSpec& app, const RunOptions& o, const Prepare& prepare) {
    WebOptions w;
    w.title = app.title;
    w.app = app.id;
    w.size = o.size.value_or(app.size);
    w.theme = o.dark ? Theme::dark_theme() : app.theme;
    // The person's settings, as every page reads them.
    w.reduce_transparency = o.reduce_transparency || page_prefers("(prefers-reduced-transparency: reduce)");
    w.reduce_motion = page_prefers("(prefers-reduced-motion: reduce)");
    // The page's ?renderer= (the person's choice: choose_renderer), else the options, else the application's.
    const std::string asked = page_query("renderer");
    if (asked == "cpu") w.renderer = Renderer::cpu;
    else if (asked == "gpu") w.renderer = Renderer::gpu;
    else if (asked == "webgpu") w.renderer = Renderer::webgpu;
    else if (asked == "webgl") w.renderer = Renderer::webgl;
    else if (asked == "automatic") w.renderer = Renderer::automatic;
    else if (o.gpu) w.renderer = *o.gpu ? Renderer::gpu : Renderer::cpu;
    else if (app.renderer == "cpu") w.renderer = Renderer::cpu;
    else if (app.renderer == "gpu") w.renderer = Renderer::gpu;
    static Web* page = nullptr;            // the page's: never destroyed
    static std::shared_ptr<void> kept;     // what the application keeps while it is shown
    page = new Web(std::move(w));
    page->runtime().set_debug(o.debug);
    kept = prepare(page->runtime());
    return page->run();
}

std::vector<std::string> host_locales() { return browser_locales(); }

}  // namespace omui::detail

namespace omui {

std::expected<void, std::string> choose_renderer(std::string_view name) {
    if (name != "automatic" && name != "gpu" && name != "cpu" && name != "webgpu" && name != "webgl")
        return std::unexpected(std::format("`{}` is not a renderer (automatic, gpu, cpu, webgpu, webgl)", name));
    if (!reload_page_with("renderer", name == "automatic" ? std::string_view{} : name)) return std::unexpected("no page to load again");
    return {};
}

}  // namespace omui
