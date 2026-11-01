// openmui.build.toy — the build plugin for Bilibili Toy (D-84 §6.5): the pack
// format `dist-toy` (mcpp's pack_format mechanism). A program built for
// wasm32-emscripten becomes a Toy package — a small index.html, the program
// and the fonts under content-hashed names, relative paths, hash routes as
// the program's flags (omui-gen dist-toy says the rest). Without Toy's SDK
// it is a plain static page (the site's).
//
//     // build.mcpp
//     openmui::build::dist_toy({.target = "omui-fight", .title = "格斗"});
//
//     mcpp pack -p <package> --target wasm32-emscripten --release --pkg-format dist-toy
//
// The package lands in <out_dir>/toy/<target>/, what is in it beside it in
// <target>.toy.omui.json. Checking it with Toy's toy_doctor.py is
// tools/ci/toy.sh's; publishing is the maintainer's (`toy create`).
export module openmui.build.toy;

import std;
import mcpp;
export import openmui.build.plan;

export namespace openmui::build {

// Files the page fetches once the program is drawn (not before it starts):
// each of `dir`'s files ending in `suffix`, a hashed asset, written to
// `mount` (under /later/) in the program's file system — /later/.ready
// once all are there.
struct later_files {
    std::string dir;     // absolute
    std::string suffix;  // ".webp"
    std::string mount;   // "/later/fighters-art"
};

// A font cut down to the characters these text files and directories hold
// (absolute paths; directories recursively), with the characters people type
// (openmui.text's `<stem>.common.txt`) and the punctuation and Latin every page has.
struct font_subset {
    std::string              font;  // "NotoSansSC"
    std::vector<std::string> text;
};

struct toy_options {
    std::string              target;                 // the program target
    std::string              title;                  // the page's <title>
    std::vector<std::string> fonts;                  // openmui.text's default fonts by stem (Inter, NotoSansSC); empty: all
    std::string              background = "#000000"; // behind the canvas while it loads
    bool                     sdk = true;             // the page loads Toy's SDK (window.toy)
    std::vector<later_files> later;                  // fetched after the program is drawn
    std::vector<font_subset> subsets;                // fonts cut down to the program's characters
    // Fonts for some languages ({"NotoSansSC", {"zh"}}): before the program
    // starts only when the page opens in one; else once it is drawn.
    std::vector<std::pair<std::string, std::vector<std::string>>> font_languages;
    std::string              out_dir = std::string(mcpp::out_dir());
};

// Declares the format (always: so `--pkg-format` can name what is
// available) and, when the pack asks for it, the edge that makes the package.
bool dist_toy(toy_options opt);

}  // namespace openmui::build
