// omui.devtools.site — the website's page tools (moved from tools/ci/site-split.sh, site-profile.mjs and
// drive-page.mjs, D-206). The site is one page (site/): `omui build --web` packs it, `omui-devtools check site`
// holds it.
//
// split    the program cut in two by what its first screen runs (D5; Part 2 plan §4.1, LD2):
// wasm-split instruments
//          a copy, a copy of the page runs it at its first screen at each route asked (`profile`),
//          wasm-split cuts the program by the merged profile, and `omui-gen dist-toy-split` puts
//          both halves in the package (the second compiled after the first frame on the home
//          routes, before main elsewhere). Without a browser or wasm-split the page stays whole,
//          with a notice — a failure under OMUI_REQUIRE_BROWSER=1 (CI).
// profile  a packed page whose program is wasm-split's instrumented build, opened at a route,
// waited on for its first
//          frame, used as a visitor uses a first screen (the pointer across it, the page scrolled
//          to its end and back), left idle; then the program writes which of its functions ran
//          (`__write_profile`).
// drive    a page in a real browser in real time: loaded and left to run, then a script evaluated
// in it (awaited),
//          and the page captured; what it logged and threw printed.
export module omui.devtools.site;

import std;

export namespace omui::devtools::site {

// routes: the first screens profiled ("" the page's own; "/zh" … a route's), their profiles merged. names: the program's symbol
// map (Emscripten's --emit-symbol-map, beside the build's program), when given the first half said by owner
// (target/site-split/first.owners.{md,json}: what its first frame waits for, B3).
[[nodiscard]] int split(const std::filesystem::path& root, const std::filesystem::path& site, const std::vector<std::string>& routes = {""},
                        const std::filesystem::path& names = {});

struct Profile {
    std::filesystem::path site, out;
    std::string route;  // e.g. /zh
    int idle_ms = 4000;
};
[[nodiscard]] int profile(const Profile& asked);

struct Drive {
    std::string url;  // or a built page's directory, served as Pages serves it (a route's of a built site: with its site)
    std::filesystem::path out;  // the PNG
    double seconds = 6;
    int width = 1280, height = 800;
    std::string script;
    std::vector<std::string> flags;                  // none: --disable-gpu
    std::optional<std::pair<double, double>> click;  // a person's click there first
    bool no_script = false;  // the page as a reader without JavaScript has it (the script still evaluated)
};
[[nodiscard]] int drive(const Drive& asked);

// The page with its loader's decoded size for the program, `"<wasm>":<size>`, set to `size` (it
// must state it once).
[[nodiscard]] std::expected<std::string, std::string>
with_decoded_size(std::string_view page, std::string_view wasm, std::uint64_t size);

// The newest path by its version-like parts (as `sort -V | tail -1`).
[[nodiscard]] std::optional<std::filesystem::path> newest(std::vector<std::filesystem::path> paths);

}  // namespace omui::devtools::site
