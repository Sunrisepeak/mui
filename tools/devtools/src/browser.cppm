// omui.devtools.browser — a test program built for wasm32-emscripten (mcpp's wasm-browser profile: linked to
// suspend, -sASYNCIFY, and to report its exit) run in a real page (moved from tools/ci/browser.mjs, D-206).
// Headless Chromium opens it from a server on 127.0.0.1 (a secure context, as WebGPU asks); the page posts back what
// the program prints, line by line and in order, and its exit code. Under Asyncify a main() that suspended returns its
// code to nobody, so a suite's own summary line (mtest's `N passed, M failed, K skipped`, or `<suite>: N passed,
// M failed`) is read too. The status: 1 when the program exits non-zero, a suite failed or none said anything, the
// page failed to load it, or the time ran out; else 0.
export module omui.devtools.browser;

import std;

export namespace omui::devtools::browser {

struct Options {
    std::filesystem::path chrome;                                         // empty: found (chromium())
    std::chrono::seconds timeout{300};
    std::vector<std::string> flags;                                       // more switches for Chromium
    std::vector<std::pair<std::filesystem::path, std::string>> preloads;  // every file of a directory at a path of the program's file system
    std::filesystem::path program;                                        // PROGRAM.js
    std::vector<std::string> arguments;                                   // the program's
    bool headed = false;                                                  // a window on the display there is, not headless
};

// The program's status as above; or why it could not run.
[[nodiscard]] std::expected<int, std::string> run(const Options& options);

// A headless Chromium: OMUI_CHROME, else Playwright's headless shell under ~/.cache/ms-playwright, else
// chrome-headless-shell, google-chrome, google-chrome-stable, chromium or chromium-browser on PATH.
[[nodiscard]] std::optional<std::filesystem::path> chromium();

// A suite's summary line's failures (mtest's `N passed, M failed, K skipped`, or `<suite>: N passed, M failed`).
[[nodiscard]] std::optional<int> summary_failures(std::string_view line);

}  // namespace omui::devtools::browser
