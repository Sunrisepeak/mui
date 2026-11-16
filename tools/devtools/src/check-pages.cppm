// What the site's browser checks share (omui.devtools.check's own partition; moved from
// tools/ci/site-*.mjs, visitor-path.mjs, dist-toy-loading.mjs and page.mjs, D-206): the built site
// served as Pages serves it, a headless Chromium driven over the DevTools protocol
// (omui.devtools.cdp), and each check's report. Every check returns its report (JSON) with "ok";
// the page-side JavaScript each evaluates stays JavaScript (the allowlist's browser host JS).
module omui.devtools.check:pages;

import std;
import omui.devtools.cdp;

namespace omui::devtools::check::pages {

namespace fs = std::filesystem;
using cdp::json;

struct Context {
    fs::path root;    // the workspace: outputs under target/, budgets and references read from it
    fs::path site;    // the built site served
    fs::path chrome;  // the browser
};

// A browser for a check: launched, or the reason as an error.
inline std::expected<std::unique_ptr<cdp::Browser>, std::string>
open(const Context& context, int width, int height, std::vector<std::string> flags = {}) {
    return cdp::Browser::launch(context.chrome,
                                {.width = width, .height = height, .flags = std::move(flags)});
}

// The path of a URL (`new URL(url).pathname`).
inline std::string pathname(std::string_view url) {
    std::size_t start = 0;
    if (const std::size_t scheme = url.find("://"); scheme != std::string_view::npos) {
        start = url.find('/', scheme + 3);
        if (start == std::string_view::npos)
            return "/";
    } else if (const std::size_t colon = url.find(':'); colon != std::string_view::npos) {
        start = colon + 1;
    }
    const std::size_t end = url.find_first_of("?#", start);
    return std::string(
        url.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
}

inline std::string zero_padded(int value, int width) {
    return std::format("{:0{}}", value, width);
}

// The site's checks; each one's report, "ok" in it.
struct Perf {
    int width = 1280, height = 800;
    std::string network;  // MBIT/RTT_MS, or none
    fs::path budget;      // none: no verdict
    bool cache = true;
    fs::path request_log;
};
json perf(const Context& context, const Perf& asked);
json a11y(const Context& context);
json structure(const Context& context);
json catalog(const Context& context);
json fonts(const Context& context);
json workspace(const Context& context);
json reactions(const Context& context);
json motion(const Context& context);
json presentation(const Context& context);
json transition(const Context& context);
json loading(const Context& context);
json font_sharing(const Context& context);

}  // namespace omui::devtools::check::pages
