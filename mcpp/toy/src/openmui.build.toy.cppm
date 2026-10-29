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

struct toy_options {
    std::string              target;                 // the program target
    std::string              title;                  // the page's <title>
    std::vector<std::string> fonts;                  // openmui.text's default fonts by stem (Inter, NotoSansSC); empty: all
    std::string              background = "#000000"; // behind the canvas while it loads
    bool                     sdk = true;             // the page loads Toy's SDK (window.toy)
    std::string              out_dir = std::string(mcpp::out_dir());
};

// Declares the format (always: so `--pkg-format` can name what is
// available) and, when the pack asks for it, the edge that makes the package.
bool dist_toy(toy_options opt);

}  // namespace openmui::build
