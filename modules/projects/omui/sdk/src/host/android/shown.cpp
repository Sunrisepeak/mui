// `omui::run` on Android: the window the platform's NativeActivity gives
// (omui.host.android), drawn by the framework.
module omui;

import std;
import mlibs.error;
import omui.core;
import omui.view;
import omui.host.android;
import :app;
import :shown;

namespace omui::hosting {

int shown(const AppSpec& app, const RunOptions& o, const Prepare& prepare) {
    AndroidOptions a;
    a.app = app.id;
    a.theme = app.theme;  // --dark is already in it (omui::run)
    AndroidHost phone(a);
    phone.runtime().set_debug(o.debug);
    const auto kept = prepare(phone.runtime());
    return phone.run();
}

std::vector<std::string> host_locales() { return {}; }
std::string host_document() { return {}; }
std::string host_document(std::string_view) { return {}; }

int terminal(const AppSpec&, const RunOptions&, const Prepare&) {
    std::println(std::cerr, "--terminal: this platform has no terminal host (omui.host.terminal runs on Linux, macOS and Windows)");
    return 2;
}

}  // namespace omui::hosting

namespace omui {

std::expected<void, Failure> choose_renderer(std::string_view name) {
    if (name != "automatic" && name != "cpu" && name != "gpu") return std::unexpected(Failure{mlibs::error::Kind::invalid_argument, std::format("`{}` is not a renderer (automatic, gpu, cpu)", name)});
    return std::unexpected(std::string("on Android the frames are drawn on the CPU"));
}

}  // namespace omui
