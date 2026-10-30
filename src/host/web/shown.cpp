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
    w.reduce_transparency = o.reduce_transparency;
    if (o.gpu) w.renderer = *o.gpu ? Renderer::gpu : Renderer::cpu;
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
