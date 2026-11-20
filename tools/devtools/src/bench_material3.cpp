// Material 3's controls in four states (moved from
// tests/bench/competitors/compare/material3/capture.mjs, D-206): at rest, under the pointer,
// pressed, keyboard-focused — each a 220×84 cell at 2× on the same background as ours
// (modules/projects/omui/sdk/tests/sdk/quality_lab_test.cpp): the competitor's side of the controls' side-by-side
// review (D-135). The page (controls.html and its esbuild bundle of @material/web, the competitor's
// own runtime) is served from its directory; each state is captured to OUT/<control>-<state>.png.
module omui.devtools.bench;

import std;
import mcpplibs.os;
import omui.devtools.browser;
import omui.devtools.cdp;

namespace omui::devtools::bench {

namespace fs = std::filesystem;

std::expected<int, std::string> material3(const fs::path& root, const fs::path& out) {
    const auto chrome = browser::chromium();
    if (!chrome)
        return std::unexpected("no headless Chromium (OMUI_CHROME)");
    const fs::path side = root / "tests/bench/competitors/compare/material3";
    if (!fs::is_regular_file(side / "controls.bundle.js"))
        return std::unexpected(
            "no controls.bundle.js beside controls.html (npm install, then the esbuild bundle)");
    std::error_code error;
    fs::create_directories(out, error);
    auto server = cdp::Server::start(cdp::pages(side));
    if (!server)
        return std::unexpected(server.error());
    auto launched = cdp::Browser::launch(*chrome, {.width = 220, .height = 84});
    if (!launched)
        return std::unexpected(launched.error());
    cdp::Script script;
    script.page = launched->get();
    script.call("Emulation.setDeviceMetricsOverride",
                {{"width", 220}, {"height", 84}, {"deviceScaleFactor", 2}, {"mobile", false}});
    const auto open = [&](const std::string& name) {
        script.call("Page.navigate", {{"url", (*server)->url() + "controls.html?control=" + name}});
        for (int attempt = 0; attempt < 100 && !script.thrown() &&
                              !cdp::truthy(script.value("!!window.controlReady"));
             ++attempt)
            script.sleep(100);
        script.sleep(400);
    };
    open("primary");
    const cdp::json names = script.value("window.controlNames");
    for (const auto& entry : names.is_array() ? names : cdp::json::array()) {
        const std::string name = cdp::text(entry);
        for (const std::string state : {"rest", "hover", "pressed", "focus"}) {
            open(name);
            const auto box = cdp::box(script.value(
                "(() => { const r = document.getElementById('it').getBoundingClientRect(); return "
                "[r.x, r.y, r.width, r.height]; })()"));
            if (!box)
                script.raise("no #it on the page for " + name);
            if (script.thrown())
                break;
            // A slider is pressed at its thumb (60 %), the rest at their middle.
            const double x = name == "slider" ? (*box)[0] + 10 + ((*box)[2] - 20) * 0.6
                                              : (*box)[0] + (*box)[2] / 2,
                         y = (*box)[1] + (*box)[3] / 2;
            if (state == "hover" || state == "pressed")
                script.call("Input.dispatchMouseEvent",
                            {{"type", "mouseMoved"}, {"x", x}, {"y", y}});
            if (state == "pressed")
                script.call("Input.dispatchMouseEvent", {{"type", "mousePressed"},
                                                         {"x", x},
                                                         {"y", y},
                                                         {"button", "left"},
                                                         {"buttons", 1},
                                                         {"clickCount", 1}});
            if (state == "focus")
                for (const std::string type : {"keyDown", "keyUp"})
                    script.call("Input.dispatchKeyEvent", {{"type", type},
                                                           {"key", "Tab"},
                                                           {"code", "Tab"},
                                                           {"windowsVirtualKeyCode", 9}});
            script.sleep(800);  // ours settle 0.8 s
            script
                .screenshot(out / std::format("{}-{}.png", name, state),
                            {{"format", "png"},
                             {"clip",
                              {{"x", 0}, {"y", 0}, {"width", 220}, {"height", 84}, {"scale", 1}}}});
            if (state == "pressed")
                script.call("Input.dispatchMouseEvent", {{"type", "mouseReleased"},
                                                         {"x", x},
                                                         {"y", y},
                                                         {"button", "left"},
                                                         {"buttons", 0},
                                                         {"clickCount", 1}});
        }
        if (script.thrown())
            break;
        std::cout << "material3: " << name << '\n' << std::flush;
    }
    (*launched)->close();
    (*server)->close();
    if (const auto failure = script.take())
        return std::unexpected(*failure);
    return 0;
}

}  // namespace omui::devtools::bench
