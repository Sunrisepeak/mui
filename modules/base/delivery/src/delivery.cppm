// mlibs.delivery — a built directory as a static host sends it (0.1.0 plan RL7 §3.2, ST6).
//
// A static host compresses what it serves by its kind: text (HTML, Markdown, JSON, scripts, styles, SVG, XML,
// WebAssembly) goes out as gzip, pictures and fonts already packed go out as they are. So what a visitor
// downloads is each file's gzip size where its kind is compressed and its size where not; a budget is over that.
// `measure` reads a directory into a manifest — every file, both sizes, the sums — and holds it to budgets, each a
// pattern over paths, a measure, and whether each file or their sum is held:
//
//     const auto manifest = mlibs::delivery::measure("dist", std::array{
//         mlibs::delivery::Budget{"a page readable", "**/index.html", mlibs::delivery::Measure::sent, mlibs::delivery::Over::each, 100'000}});
//     if (!manifest->within()) …   // the verdict names the file and what it weighs
//
// The manifest is data (`delivery@1`): what a tool prints, a check holds, a deployment records.
export module mlibs.delivery;

import std;

export namespace mlibs::delivery {

// `bytes` as one gzip member (level 1 to 9; a host's default is 6, a build's measure 9).
[[nodiscard]] std::string gzip(std::string_view bytes, int level = 9);
// A gzip member's bytes again, or why not.
[[nodiscard]] std::expected<std::string, std::string> gunzip(std::string_view bytes);

// Whether a static host sends a file of this extension compressed (".html", ".wasm"; lower case).
[[nodiscard]] bool compressed_kind(std::string_view extension) noexcept;

// `*` any run within a directory, `**` any run across them, `?` one character: "**/index.html", "*.wasm".
[[nodiscard]] bool matches(std::string_view pattern, std::string_view path) noexcept;

struct File {
    std::string    path;        // from the directory, `/` between its parts
    std::uintmax_t bytes = 0;   // as stored
    std::uintmax_t gzip = 0;    // as one gzip member, level 9
    bool           compressed = false;  // a static host sends it compressed
    // What a visitor downloads of it.
    [[nodiscard]] std::uintmax_t sent() const noexcept { return compressed ? gzip : bytes; }
};

enum class Measure : std::uint8_t { bytes, gzip, sent };
enum class Over : std::uint8_t { each, sum };

struct Budget {
    std::string    name;     // "a page readable"
    std::string    pattern;  // the files it is over
    Measure        measure = Measure::sent;
    Over           over = Over::each;
    std::uintmax_t most = 0;
};

// One file measured from its bytes (what a build has in hand as it writes).
[[nodiscard]] File measured(std::string path, std::string_view bytes);

struct Verdict {
    Budget         budget;
    std::uintmax_t found = 0;  // the largest file's (each) or the sum
    std::string    where;      // the largest file; empty for a sum or where no file matched
    std::size_t    files = 0;  // how many it is over
    bool           within = true;
};

struct Manifest {
    std::vector<File>    files;     // by path
    std::uintmax_t       bytes = 0, gzip = 0, sent = 0;
    std::vector<Verdict> verdicts;
    [[nodiscard]] bool within() const noexcept;
};

// The directory's files, measured and held to the budgets; or why it could not be read.
[[nodiscard]] std::expected<Manifest, std::string> measure(const std::filesystem::path& directory, std::span<const Budget> budgets = {});
// Files already known (a build's own list), held to budgets.
[[nodiscard]] Manifest held(std::vector<File> files, std::span<const Budget> budgets);

// The manifest as its snapshot (`delivery@1`).
[[nodiscard]] std::string to_json(const Manifest& manifest);

[[nodiscard]] constexpr std::string_view name(Measure measure) noexcept { return measure == Measure::bytes ? "bytes" : measure == Measure::gzip ? "gzip" : "sent"; }
[[nodiscard]] constexpr std::string_view name(Over over) noexcept { return over == Over::each ? "each" : "sum"; }

}  // namespace mlibs::delivery
