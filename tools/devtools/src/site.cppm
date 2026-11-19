// omui.devtools.site — the website's build steps and its page tools (moved from tools/ci/site.sh,
// site-split.sh, site-profile.mjs and drive-page.mjs, D-206).
//
// build    omui.site built for wasm32-emscripten and packed as a static page (dist-toy's package
// without Toy's SDK —
//          a Pages subpath serves it as it is), copied to target/site/ (what Pages uploads, and
//          what a person serves to look at it); then, by the same program built natively, the
//          openings (`omui-site --openings`: the pictures and geometry the page shows while it
//          loads), its places pre-rendered into the page
//          (`--prerender`: the documents as md4c-html writes them, in a section a reader without
//          JavaScript and a search engine read, hidden when the program starts) and what an agent
//          needs to operate it
//          (`--agent-guide`: agent.md, .well-known/openmui-agent.json, the page's link); then
//          split; then verified.
// verify   the page carries what it must (the loader, the static places, the fonts, the canvas, the
// agent's link and
//          files, no Toy SDK) and its sizes are said.
// split    the program cut in two by what its first screen runs (D5; Part 2 plan §4.1, LD2):
// wasm-split instruments
//          a copy, a copy of the page runs it at its first screen in each language (`profile`),
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

[[nodiscard]] int build(const std::filesystem::path& root);
[[nodiscard]] int verify(const std::filesystem::path& site);
[[nodiscard]] int split(const std::filesystem::path& root, const std::filesystem::path& site);

struct Profile {
    std::filesystem::path site, out;
    std::string route;  // e.g. /zh
    int idle_ms = 4000;
};
[[nodiscard]] int profile(const Profile& asked);

struct Drive {
    std::string url;  // or a built page's directory, served as Pages serves it
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
