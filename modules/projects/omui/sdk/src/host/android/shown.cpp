// `omui::run` on Android: the window the platform's NativeActivity gives
// (openmui.host.android), drawn by the framework.
module omui;

import std;
import omui.core;
import omui.view;
import omui.host.android;
import :app;
import :shown;

namespace omui::hosting {

int shown(const AppSpec& app, const RunOptions& o, const Prepare& prepare) {
    AndroidOptions a;
    a.app = app.id;
    a.theme = o.dark ? Theme::dark_theme() : app.theme;
    AndroidHost phone(a);
    phone.runtime().set_debug(o.debug);
    const auto kept = prepare(phone.runtime());
    return phone.run();
}

std::vector<std::string> host_locales() { return {}; }

}  // namespace omui::hosting

namespace omui {

std::expected<void, std::string> choose_renderer(std::string_view name) {
    if (name != "automatic" && name != "cpu" && name != "gpu") return std::unexpected(std::format("`{}` is not a renderer (automatic, gpu, cpu)", name));
    return std::unexpected(std::string("on Android the frames are drawn on the CPU"));
}

}  // namespace omui
