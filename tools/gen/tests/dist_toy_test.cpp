// dist-toy: a staged wasm32-emscripten program becomes a Toy package — a
// small index.html, the program and the fonts under content-hashed names,
// only file types Toy keeps, the licences as text it keeps, and a note
// beside it of every file's size; what is not a Toy package is refused.
import std;
import openmui.gen.dist_toy;
import openmui.testing;

using namespace omui::testing;
namespace gen = omui::gen;
namespace fs = std::filesystem;

namespace {

void put(const fs::path& file, std::string_view bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << bytes;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

struct Tree {
    fs::path root = fs::temp_directory_path() / std::format("omui-dist-toy-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    Tree() {
        put(root / "stage" / "bin" / "game.js", "var Module; /* the launcher */");
        put(root / "stage" / "bin" / "game.wasm", std::string("\0asm\1\0\0\0", 8));
        put(root / "stage" / "licenses" / "compat.zlib" / "LICENSE", "zlib");
        put(root / "fonts" / "Inter.ttf", "inter bytes");
        put(root / "fonts" / "NotoSansSC.ttf", "noto bytes");
        put(root / "fonts" / "OFL-inter.txt", "ofl");
    }
    ~Tree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    gen::ToyPackage package() const {
        return {.stage = root / "stage", .target = "game", .out = root / "out" / "game", .title = "<Game>", .fonts = {"Inter"}, .fonts_dir = root / "fonts"};
    }
};

}  // namespace

int main(int argc, char** argv) {
    Suite s{"gen.dist_toy"};

    s.test("a Toy package: index.html, hashed names, the fonts asked for, licences Toy keeps, the note beside it", [] {
        Tree t;
        auto r = gen::package_for_toy(t.package());
        check(r.has_value(), r ? "" : r.error());
        if (!r) return;
        const fs::path out = t.root / "out" / "game";
        const std::string index = read(out / "index.html");
        check(r->index == index.size() && r->index < gen::toy_index_limit);
        check(index.contains("<title>&lt;Game&gt;</title>"), "the title, escaped");
        check(index.contains("toy-sdk.js"), "the page loads Toy's SDK");
        check(!index.contains("location.hash"), "no URL mutation for Toy's doctor to warn of");
        std::vector<std::string> names;
        for (const gen::ToyFile& f : r->files) names.push_back(f.path);
        auto has = [&](auto pred) { return std::ranges::any_of(names, pred); };
        check(has([](const std::string& n) { return n.starts_with("game-") && n.ends_with(".js") && n.size() == 20; }), std::format("{}", names));
        check(has([](const std::string& n) { return n.starts_with("game-") && n.ends_with(".wasm"); }));
        check(std::ranges::count_if(names, [](const std::string& n) { return n.starts_with("assets/") && n.ends_with(".ttf"); }) == 1, "Inter only");
        check(has([](const std::string& n) { return n == "licenses/compat.zlib/LICENSE.md"; }), "a licence without an extension, as .md");
        check(has([](const std::string& n) { return n == "licenses/fonts/OFL-inter.txt.md"; }));
        for (const std::string& n : names) check(gen::toy_keeps(fs::path(n).extension().string()), n);
        const std::string note = read(t.root / "out" / "game.toy.omui.json");
        check(note.contains("\"index_bytes\"") && note.contains("\"total_bytes\""));
        // The same input, the same names.
        auto again = gen::package_for_toy(t.package());
        check(again && again->files.size() == r->files.size() && again->files[0].path == r->files[0].path);
    });

    s.test("refused: no program, a .data file, a font that is not there", [] {
        Tree t;
        auto p = t.package();
        p.target = "other";
        check(!gen::package_for_toy(p).has_value(), "no other.js");
        p = t.package();
        p.fonts = {"Missing"};
        check(!gen::package_for_toy(p).has_value());
        put(t.root / "stage" / "bin" / "game.data", "preloaded");
        check(!gen::package_for_toy(t.package()).has_value(), "--preload-file's .data is not how a Toy page gets its files");
    });

    s.test("the file types Toy keeps", [] {
        check(gen::toy_keeps(".wasm") && gen::toy_keeps(".ttf") && gen::toy_keeps(".webp") && gen::toy_keeps(".md"));
        check(!gen::toy_keeps(".txt") && !gen::toy_keeps(".mjs") && !gen::toy_keeps(".map") && !gen::toy_keeps(""));
    });

    return s.run(argc, argv);
}
