// The smallest application's distribution (implementation plan B4, D-214): example 01 packed for this machine as a
// directory, and what it carries held — Inter alone of the fonts (every other script is a pack it did not take, at
// most 0.9 MB of fonts), and no window system's library (SDL opens the system's at run time: neither linked nor
// carried). Its size said; B's other tasks hold the program's.
module omui.devtools.check;

import std;
import mcpplibs.os;
import omui.devtools.run;
import :support;

namespace omui::devtools::check {

using namespace support;
namespace fs = std::filesystem;

int distribution(const fs::path& root, const std::vector<std::string>& profile_args) {
    // mcpp packs a member into its own target/dist; what was there before is not this run's.
    const fs::path dist = root / "examples" / "01-hello-im" / "target" / "dist";
    std::error_code error;
    fs::remove_all(dist, error);
    std::vector<std::string> args{"pack", "-p", "omui.example.hello-im", "--pkg-format", "dir"};
    args.insert(args.end(), profile_args.begin(), profile_args.end());
    const auto ran = run::program(root, "mcpp", args, {.merged = true});
    if (!ran || ran->status != 0) return failed(std::format("example 01 was not packed: {}", ran ? ran->out.substr(ran->out.size() > 2000 ? ran->out.size() - 2000 : 0) : ran.error()));
    std::optional<fs::path> tree;
    for (const auto& entry : fs::directory_iterator(dist, error))
        if (entry.is_directory(error) && mcpplibs::os::fs::utf8(entry.path().filename()).starts_with("hello-im-")) tree = entry.path();
    if (!tree) return failed(std::format("example 01 was packed, but {} holds no tree", mcpplibs::os::fs::utf8(dist)));
    std::uintmax_t total = 0, fonts = 0;
    std::vector<std::string> font_files, window_libraries;
    for (auto at = fs::recursive_directory_iterator(*tree, error); at != fs::recursive_directory_iterator(); at.increment(error)) {
        if (error || !at->is_regular_file(error)) continue;
        const std::uintmax_t bytes = at->file_size(error);
        total += bytes;
        const std::string name = mcpplibs::os::fs::utf8(at->path().filename());
        if (at->path().parent_path().filename() == "fonts" && at->path().extension() == ".ttf") {
            fonts += bytes;
            font_files.push_back(name);
        }
        for (const std::string_view window : {"libX", "libxcb", "libwayland", "libxkbcommon", "libdecor"})
            if (name.starts_with(window)) window_libraries.push_back(name);
    }
    std::ranges::sort(font_files);
    std::cout << std::format("distribution: {} — {} bytes; fonts {} bytes ({})\n", mcpplibs::os::fs::utf8(*tree), total, fonts,
                             font_files.empty() ? std::string("none") : std::accumulate(std::next(font_files.begin()), font_files.end(), font_files.front(),
                                                                                         [](std::string all, const std::string& one) { return all + ", " + one; }))
              << std::flush;
    if (font_files != std::vector<std::string>{"Inter.ttf"}) return failed("example 01 carries a font other than Inter: a pack it did not take (D-214)");
    if (fonts > 900'000) return failed(std::format("example 01's fonts are {} bytes, over the plan's 0.9 MB", fonts));
    if (!window_libraries.empty()) return failed(std::format("example 01 carries a window system's library ({}): the system has it", window_libraries.front()));
    return 0;
}

}  // namespace omui::devtools::check
