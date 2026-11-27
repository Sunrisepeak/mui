// The README media (moved from tools/ci/readme-media.sh): what the showcases' and the site's tests draw (a test's
// scratch "readme-<name>"), copied into their media/ directories, or the committed files held to it.
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

std::string package_of(const fs::path& manifest) {
    try {  // toml++ reports a malformed file by throwing (this tool is built with exceptions; the lint reads the same way)
        const toml::table data = toml::parse(mcpplibs::os::fs::read(manifest).value_or(""));
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
            if (mcpplibs::os::fs::read(drawn).value_or("") != mcpplibs::os::fs::read(committed).value_or("") || !fs::exists(drawn))
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
        const fs::path drawn = scratch / ("readme-" + show);
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
    }
    if (failures == 0) std::cout << (check_only ? "media: the committed files are what the tests draw\n" : "media: copied\n");
    return failures == 0 ? 0 : 1;
}

}  // namespace omui::devtools::check
