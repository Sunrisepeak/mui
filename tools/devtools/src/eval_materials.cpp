// eval materials (moved from tools/eval/materials/controls.sh): the material a comparison set names. `controls`
// (compare set `components`): ours from the components' quality lab — each control's four states in light
// (modules/projects/omui/sdk/tests/sdk/quality_lab_test.cpp) — and Material 3's, the same controls in the same states, cells and
// background, as Google's @material/web draws them (tests/bench/competitors/compare/material3, captured by
// `bench material3`). Both land in target/compare-material/{omui,material3}/<control>.png.
module omui.devtools.eval;

import std;
import mcpplibs.os;
import mlibs.image;
import omui.devtools.bench;
import omui.devtools.run;

namespace omui::devtools::eval {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;

int materials_controls(const fs::path& root) {
    const fs::path out = root / "target/compare-material";
    std::error_code error;
    fs::create_directories(out / "omui", error);
    fs::create_directories(out / "material3" / "states", error);
    // Ours: the quality lab's pictures, as its test writes them.
    const auto lab = run::program(root, "mcpp", {"test", "-p", "omui.sdk", "quality_lab_test"},
                                  {.merged = true, .variables = {{"MCPP_JOBS", std::to_string(run::share_of_machine())}}});
    std::ofstream(out / "quality-lab.log", std::ios::binary) << (lab ? lab->out : lab.error());
    if (!lab || lab->status != 0) {
        std::cerr << std::format("omui-devtools eval materials: the quality lab failed (see {})\n", os::fs::utf8(out / "quality-lab.log"));
        return 1;
    }
    const fs::path pictures = fs::temp_directory_path() / "openmui-tests" / "quality-lab" / "controls";
    std::size_t ours = 0;
    for (const auto& entry : fs::directory_iterator(pictures, error))
        if (entry.path().extension() == ".png" && fs::copy_file(entry.path(), out / "omui" / entry.path().filename(), fs::copy_options::overwrite_existing, error)) ++ours;
    // Theirs: the page's bundle (npm's pinned packages, esbuild), then the four states of each control.
    const fs::path side = root / "tests/bench/competitors/compare/material3";
    if (const auto installed = run::program(root, "npm", {"install", "--silent", "--no-audit", "--no-fund"}, {.cwd = side, .merged = true}); !installed || installed->status != 0) {
        std::cerr << "omui-devtools eval materials: npm install failed in " << os::fs::utf8(side) << '\n' << (installed ? installed->out : installed.error());
        return 1;
    }
    if (const auto bundled = run::program(root, "esbuild",
                                          {"controls.js", "--bundle", "--loader:.woff2=dataurl", "--loader:.woff=dataurl", "--outfile=controls.bundle.js", "--log-level=warning"},
                                          {.cwd = side, .merged = true});
        !bundled || bundled->status != 0) {
        std::cerr << "omui-devtools eval materials: the esbuild bundle failed\n" << (bundled ? bundled->out : bundled.error());
        return 1;
    }
    const auto captured = bench::material3(root, out / "material3" / "states");
    if (!captured || *captured != 0) {
        std::cerr << "omui-devtools eval materials: " << (captured ? std::string("bench material3 failed") : captured.error()) << '\n';
        return 1;
    }
    // Each control's four states in a row, as ours are drawn.
    constexpr std::array states{"rest", "hover", "pressed", "focus"};
    std::set<std::string> names;
    for (const auto& entry : fs::directory_iterator(out / "material3" / "states", error)) {
        const std::string stem = os::fs::utf8(entry.path().stem());
        if (entry.path().extension() == ".png" && stem.rfind('-') != std::string::npos) names.insert(stem.substr(0, stem.rfind('-')));
    }
    for (const auto& name : names) {
        std::vector<mlibs::image::Image> cells;
        for (const char* state : states) {
            auto cell = mlibs::image::load_png(out / "material3" / "states" / std::format("{}-{}.png", name, state));
            if (!cell) {
                std::cerr << "omui-devtools eval materials: " << cell.error() << '\n';
                return 1;
            }
            cells.push_back(std::move(*cell));
        }
        const auto strip = row(cells);
        if (auto saved = mlibs::image::save_png(out / "material3" / std::format("{}.png", name), mlibs::image::view(strip)); !saved) {
            std::cerr << "omui-devtools eval materials: " << saved.error() << '\n';
            return 1;
        }
    }
    std::cout << std::format("controls: {} Material 3 strips, {} of ours, in {}\n", names.size(), ours, os::fs::utf8(fs::relative(out, root)));
    return 0;
}

}  // namespace omui::devtools::eval
