// The README media (moved from tools/ci/readme-media.sh): what the showcases' and the site's tests draw, copied
// into their media/ directories, or the committed files held to it.
module omui.devtools.check;

import std;
import mcpplibs.os;
import tomlplusplus;
import :support;

namespace omui::devtools::check {

using namespace support;
namespace fs = std::filesystem;

namespace {

// Where the tests draw (mlibs.testing's scratch directory: the system's temporary directory, openmui-tests).
fs::path scratch_dir() { return fs::temp_directory_path() / "openmui-tests"; }

// (drawn, published) — the site's media beyond its brand directory: stills of its layout, catalogue and release
// workspace tests, and a chosen few of its review tests (one desktop and one phone animation, both galleries'
// stills at both canvas widths). The workspace moved off Home; its published names are kept.
std::vector<std::pair<std::string, std::string>> site_media() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const std::string_view name : {"home-en-desktop", "docs-en-desktop", "explore-en-desktop", "hello-en-desktop", "docs-zh-phone", "features-zh-phone"})
        out.emplace_back(std::format("site-structure/{}.png", name), std::format("structure-{}.png", name));
    for (const std::string_view name : {"overview-en-1280", "button-en-1280", "text_box-en-1280", "data_grid-en-1280", "overview-zh-390", "button-zh-390",
                                        "text_box-zh-390", "code_editor-zh-390"})
        out.emplace_back(std::format("site-catalog/{}.png", name), std::format("catalog-{}.png", name));
    for (const std::string_view name : {"home-en-1280", "home-zh-390", "technical-en-1280", "technical-zh-390", "edited-zh", "motion-list", "motion-between", "motion-cards"}) {
        std::string generated(name);
        if (generated.starts_with("home-")) generated.replace(0, 5, "release-");
        out.emplace_back(std::format("site-workspace/{}.png", generated), std::format("workspace-{}.png", name));
    }
    for (const auto& [generated, published] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"home-landing/home-en-desktop.png", "landing-home-en-desktop.png"},
             {"home-landing/home-zh-phone.png", "landing-home-zh-phone.png"},
             {"gallery-redesign/workspace-en-desktop.png", "components-workspace-en-desktop.png"},
             {"gallery-redesign/workspace-zh-phone.png", "components-workspace-zh-phone.png"},
             {"gallery-redesign/options-zh-phone.png", "components-options-zh-phone.png"},
             {"motion-gallery/basic-gallery-700.png", "motion-basic-gallery-700.png"},
             {"motion-gallery/shape-path-middle.png", "motion-shape-path-middle.png"},
             {"motion-gallery/basic-gallery-300.png", "motion-basic-gallery-300.png"},
             {"motion-gallery/creative-gallery-700.png", "motion-creative-gallery-700.png"},
             {"motion-gallery/creative-gallery-300.png", "motion-creative-gallery-300.png"},
             {"motion-gallery/basic-player-700.webp", "motion-basic-player-700.webp"},
             {"motion-gallery/creative-player-300.webp", "motion-creative-player-300.webp"},
             {"creative-motion/palette-useful-zh-300.png", "creative-palette-zh-300.png"},
             {"feature-examples/glass-session-refracted.png", "style-glass-session.png"},
             {"feature-examples/local-pixel-chart.png", "style-local-pixel-chart.png"}})
        out.emplace_back(generated, published);
    return out;
}

std::string package_of(const fs::path& manifest) {
    try {  // toml++ reports a malformed file by throwing (this tool is built with exceptions; the lint reads the same way)
        const toml::table data = toml::parse(read_text(manifest).value_or(""));
        return data["package"]["namespace"].value_or(std::string{}) + "." + data["package"]["name"].value_or(std::string{});
    } catch (const toml::parse_error&) {
        return {};
    }
}

}  // namespace

int media(const fs::path& root, std::vector<std::string> shows, bool check_only) {
    std::error_code error;
    if (shows.empty()) {
        std::vector<std::string> found;
        for (const auto& entry : fs::directory_iterator(root / "showcase", error))
            if (fs::is_regular_file(entry.path() / "mcpp.toml")) found.push_back(mcpplibs::os::fs::utf8(entry.path().filename()));
        std::ranges::sort(found);
        shows = std::move(found);
        shows.push_back("site");
    }
    const fs::path scratch = scratch_dir();
    int failures = 0;
    const auto refuse = [&](std::string message) {
        std::cout << "::error::" << message << '\n';
        ++failures;
    };
    // One file: copied, or held to what was drawn.
    const auto take = [&](const fs::path& drawn, const fs::path& committed, std::string_view whose) {
        if (check_only) {
            if (read_text(drawn) != read_text(committed) || !fs::exists(drawn))
                refuse(std::format("{} is not what the {} draw: run omui-devtools media {}", mcpplibs::os::fs::generic_utf8(fs::relative(committed, root)), whose,
                                   committed.parent_path().parent_path().filename() == "showcase" ? "" : "site"));
            return;
        }
        fs::create_directories(committed.parent_path(), error);
        fs::copy_file(drawn, committed, fs::copy_options::overwrite_existing, error);
    };
    for (const auto& show : shows) {
        const bool site = show == "site";
        const fs::path folder = site ? root / "site" : root / "showcase" / show;
        const fs::path drawn = site ? scratch / "brand" : scratch / ("readme-" + show);
        if (!check_only) {
            const std::string package = package_of(folder / "mcpp.toml");
            std::cout << std::format("media: {} ({})\n", show, package) << std::flush;
            const auto [status, said] = mcpp(root, {"test", "-p", package});
            if (status != 0) {
                std::cout << said.substr(said.size() > 3000 ? said.size() - 3000 : 0);
                refuse(std::format("{}'s tests failed", package));
                continue;
            }
        }
        if (!fs::is_directory(drawn) || fs::is_empty(drawn, error)) {
            refuse(std::format("media: {} drew nothing (a test writing scratch_dir(\"readme-{}\"))", show, show));
            continue;
        }
        for (const auto& entry : fs::directory_iterator(drawn, error)) take(entry.path(), folder / "media" / entry.path().filename(), "tests");
        if (!site) continue;
        for (const auto& [generated, published] : site_media()) {
            const fs::path source = scratch / generated;
            if (!fs::exists(source)) {
                refuse(std::format("media: missing {} (run the site's native tests)", mcpplibs::os::fs::utf8(source)));
                continue;
            }
            if (source.extension() == ".webp" && fs::file_size(source, error) > 1024 * 1024) {
                refuse(std::format("{} exceeds the 1 MiB README animation budget", mcpplibs::os::fs::utf8(source)));
                continue;
            }
            take(source, folder / "media" / published, "site's tests");
        }
    }
    if (failures == 0) std::cout << (check_only ? "media: the committed files are what the tests draw\n" : "media: copied\n");
    return failures == 0 ? 0 : 1;
}

}  // namespace omui::devtools::check
