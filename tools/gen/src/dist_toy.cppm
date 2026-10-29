// openmui.gen.dist_toy — a program built for wasm32-emscripten, packaged for
// Bilibili Toy (D-84 §6.5): what `mcpp pack --pkg-format dist-toy` runs
// (openmui.build.toy's dist_toy(), the format's provider).
//
//   omui-gen dist-toy <staged tree> --target <name> --out <dir> [--title T]
//                     [--fonts Inter,NotoSansSC | all] [--fonts-dir <dir>]
//                     [--graph <mcpp graph file>] [--background #rrggbb] [--no-sdk]
//
// The package (Toy's rules, from github.com/bilibili/toy at ae156ec — its
// skill's content checklist and toy_doctor.py — and the publishing guide's
// FAQ): a small `index.html` (at most 1 MiB, Toy's hard limit) that loads
// everything else by relative paths; the program as `<name>-<hash>.js` and
// `<name>-<hash>.wasm`, the fonts as `assets/<hash>.ttf` (content hashes:
// what has not changed is not fetched again); only the file types Toy
// keeps; hash routes: `#/training` is the program's `--training`. The page
// fills the program's /fonts before `main` and loads the Toy SDK
// (`window.toy`). Toy has no manifest file: what `toy create` asks (title,
// slug, cover) is written beside the package, in `<name>.toy.omui.json`,
// with every file's size.
export module openmui.gen.dist_toy;

import std;

export namespace omui::gen {

struct ToyPackage {
    std::filesystem::path stage;          // `mcpp pack`'s staged tree: bin/<target>.js, .wasm, licenses/
    std::string           target;
    std::filesystem::path out;            // the package's directory (emptied first)
    std::string           title;
    std::vector<std::string> fonts;       // font file stems of openmui.text's defaults; empty: all of them
    std::filesystem::path fonts_dir;      // empty: openmui.text's, found through `graph`
    std::filesystem::path graph;          // mcpp's resolved graph (graph_file()), may be empty
    std::string           background = "#000000";
    bool                  sdk = true;     // load the Toy SDK in the page
};

struct ToyFile {
    std::string   path;   // relative to the package
    std::uintmax_t size = 0;
};

struct ToyReport {
    std::vector<ToyFile> files;
    std::uintmax_t       total = 0;
    std::uintmax_t       index = 0;  // index.html's size
};

inline constexpr std::uintmax_t toy_index_limit = 1024 * 1024;  // Toy: the entry index.html, unpacked

// The file types Toy keeps (anything else is silently left out of a package).
[[nodiscard]] bool toy_keeps(std::string_view extension) noexcept;

[[nodiscard]] std::expected<ToyReport, std::string> package_for_toy(const ToyPackage& p);

// The command line's `dist-toy` (argv[1]); the exit code.
int dist_toy_main(int argc, char** argv);

}  // namespace omui::gen
