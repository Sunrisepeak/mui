// openmui.gen.dist_toy — a program built for wasm32-emscripten, packaged for
// Bilibili Toy (D-84 §6.5): what `mcpp pack --pkg-format dist-toy` runs
// (openmui.build.toy's dist_toy(), the format's provider).
//
//   omui-gen dist-toy <staged tree> --target <name> --out <dir> [--title T]
//                     [--fonts Inter,NotoSansSC | all] [--fonts-dir <dir>]
//                     [--graph <mcpp graph file>] [--background #rrggbb] [--no-sdk]
//                     [--later <dir>|<suffix>|</later/mount>]… [--subset <font>=<path>,<path>…]…
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
//
// Generated pages expose window.omuiLoad and synchronous `omui-load` events
// (detail is a data-only snapshot, version 1). Byte counts are decoded bodies
// of managed wasm/fonts/later assets; they exclude the launcher script and
// reset per asset attempt. Unknown total/ratio is null. `startup` excludes
// deferred assets, while the top-level count includes them. The presented
// phase is emitted only from the native first-frame hook, never the fallback
// timer. retry(id?) retries failed assets without initializing main twice;
// a fatal runtime/launcher error instead returns reason `reload_required`.
// fontBytes(basename) explicitly starts a shared decoded font transfer before
// wasm/preRun. It returns a Promise of {id, url, buffer: ArrayBuffer, release()}.
// Each lease owns a mutable copy, isolated from pending native FS bytes.
// release is idempotent and clears lease.buffer; callers must release after
// FontFace.load finishes. Native FS installation shares the transfer. Late
// calls use the installed full face when available, otherwise startup coverage;
// id/url always describe the returned bytes. Errors have code font_not_found,
// font_deferred, font_fetch_failed, or reload_required (network errors also
// expose id/url/retryable). retry(id) then a new fontBytes call recovers errors.
// No DOM consumer means font downloads still begin only in preRun. Byte caches
// are released after FS installation and the last outstanding DOM lease.
// Multiple basenames sharing one content URL have independent FS/dependency
// installations and one decoded-byte progress entry (with names[]). Retrying
// its physical error.id resumes every currently eligible installation, even
// when the failed basename differs from the one naming the shared URL. A started
// font no longer required after a preRun locale replan is reported as retired:
// its transferState/error history remains, but it is excluded from the current
// plan totals, current error, completion barrier, and automatic retry selection.
export module omui.gen.dist_toy;

import std;
import mcpplibs.cmdline;

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
    // Files fetched once the program is drawn: a directory's files with a
    // suffix, written under `mount` (/later/…) in its file system.
    struct Later {
        std::filesystem::path dir;
        std::string           suffix, mount;
    };
    std::vector<Later>    later;
    // A font cut down to the characters these files hold (text: .md, .cpp,
    // .cppm, .toml, .json, .txt, .js — a directory's, recursively), with its
    // `<stem>.common.txt` beside it (characters people type) and the
    // punctuation and Latin every page has (font_subset.cppm).
    struct Subset {
        std::string                        font;  // a stem of `fonts`
        std::vector<std::filesystem::path> text;
    };
    std::vector<Subset>   subsets;
    // A font for some languages (NotoSansSC: zh): before main only when the
    // page opens in one of them (its route's first part, or the browser's
    // first language); else fetched once the program is drawn, into
    // /later/fonts, where the Web host takes it.
    std::vector<std::pair<std::string, std::vector<std::string>>> font_languages;
    // Tiny initial coverage when a font is deferred. Literal UTF-8; full-language
    // visits fetch only the regular asset. Without font_languages this always
    // uses initial coverage before main and defers the full face until the
    // first frame, for every language. Mounted under the same /fonts basename.
    std::vector<std::pair<std::string, std::string>> font_initial;
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

// The command line's `dist-toy`, as omui-gen parsed it; the exit code.
int dist_toy_main(const mcpplibs::cmdline::ParsedArgs& args);

// A package's program cut in two by wasm-split (tools/ci/site-split.sh): the
// first half (the functions a profiled first screen ran) replaces the
// package's .wasm, the second half lands beside it as
// `<target>-<hash>.deferred.wasm`, and the page's `var omuiDeferred` says
// where it is, its size and the routes (their first part; "" the bare page)
// whose first screen the first half covers — there the second half is
// compiled after the first frame, elsewhere main waits for it.
struct ToySplit {
    std::filesystem::path    dir;       // the package (its index.html)
    std::filesystem::path    primary;   // wasm-split's -o1
    std::filesystem::path    deferred;  // wasm-split's -o2
    std::vector<std::string> lazy;      // routes; omui-gen's default: "", "home"
};

[[nodiscard]] std::expected<ToyReport, std::string> adopt_split(const ToySplit& split);

// The command line's `dist-toy-split`, as omui-gen parsed it; the exit code.
int dist_toy_split_main(const mcpplibs::cmdline::ParsedArgs& args);

}  // namespace omui::gen
