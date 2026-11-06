// dist-toy: a staged wasm32-emscripten program becomes a Toy package — a
// small index.html, the program and the fonts under content-hashed names,
// only file types Toy keeps, the licences as text it keeps, and a note
// beside it of every file's size; what is not a Toy package is refused.
import std;
import openmui.gen.dist_toy;
import mlibs.testing;
import mlibs.text;
import nlohmann.json;

using namespace mlibs::testing;
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

// A small no-post SFNT fixture. Basic loader tests need real directory /
// checksum structure, not a renderable typeface; coverage tests below use
// actual fonts. The 84-byte fixture stays below their tiny-face threshold.
std::string loader_font(std::uint32_t revision) {
    std::string font(84, '\0');
    const auto set = [&](std::size_t at, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) font[at + i] = static_cast<char>(value >> (24 - i * 8));
    };
    const auto sum = [](std::string_view bytes) {
        std::uint32_t result = 0;
        for (std::size_t i = 0; i < bytes.size(); i += 4) {
            std::uint32_t word = 0;
            for (std::size_t k = 0; k < 4; ++k) word = word << 8 | (i + k < bytes.size() ? static_cast<unsigned char>(bytes[i + k]) : 0);
            result += word;
        }
        return result;
    };
    set(0, 0x00010000u); set(4, 0x00010010u); // 1 table, searchRange 16; selector/shift 0.
    font.replace(12, 4, "head"); set(20, 28); set(24, 54);
    set(28, 0x00010000u); set(32, revision); set(40, 0x5f0f3cf5u);
    font[46] = 3; font[47] = static_cast<char>(0xe8); // unitsPerEm 1000.
    set(16, sum(std::string_view(font).substr(28, 54)));
    set(36, 0xB1B0AFBAu - sum(font));
    return font;
}

struct Tree {
    fs::path root = fs::temp_directory_path() / std::format("omui-dist-toy-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    Tree() {
        put(root / "stage" / "bin" / "game.js", "var Module; /* the launcher */");
        put(root / "stage" / "bin" / "game.wasm", std::string("\0asm\1\0\0\0", 8));
        put(root / "stage" / "licenses" / "compat.zlib" / "LICENSE", "zlib");
        put(root / "fonts" / "Inter.ttf", loader_font(1));
        put(root / "fonts" / "NotoSansSC.ttf", loader_font(2));
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

    s.test("resolved canonical and legacy text packages deliver real default fonts", [] {
        Tree t;
        fs::copy_file(mlibs::text::FontCollection::default_directory() / "Inter.ttf", t.root / "fonts" / "Inter.ttf",
                      fs::copy_options::overwrite_existing);
        for (const std::string owner : {"mlibs", "openmui"}) {
            const auto package = [&](std::string_view ns, const fs::path& dir) {
                return nlohmann::json{{"package", {{"namespace", ns}, {"name", "text"}}}, {"manifest_dir", dir.generic_string()}};
            };
            const nlohmann::json graph{{"packages", {package("unrelated", t.root / "absent"), package(owner, t.root)}}};
            auto p = t.package();
            p.fonts_dir.clear();
            p.graph = t.root / "graph.json";
            p.out = t.root / "out" / owner;
            put(p.graph, graph.dump());
            auto result = gen::package_for_toy(p);
            check(result.has_value(), result ? owner : result.error());
            if (!result) continue;
            std::size_t fonts = 0;
            for (const auto& asset : result->files) {
                if (!asset.path.ends_with(".ttf")) continue;
                ++fonts;
                mlibs::text::FontCollection faces;
                const auto face = faces.add_file(p.out / asset.path);
                check(face.has_value(), "the packaged asset is a loadable font");
                if (face) check(faces.covers(*face, U'A'), "actual Latin glyph retained");
            }
            check_eq(fonts, std::size_t{1});
            put(p.graph, nlohmann::json{{"packages", {package("unrelated", t.root)}}}.dump());
            check(!gen::package_for_toy(p).has_value(), "unrelated text package is not the font owner");
        }
    });

    s.test("files fetched once the program is drawn: hashed assets, listed with where they go, written by the page after it starts", [] {
        Tree t;
        put(t.root / "art" / "b.webp", "b picture");
        put(t.root / "art" / "a.webp", "a picture");
        put(t.root / "art" / "notes.txt", "not a picture");
        auto p = t.package();
        p.later = {{t.root / "art", ".webp", "/later/fighters-art"}};
        auto r = gen::package_for_toy(p);
        check(r.has_value(), r ? "" : r.error());
        if (!r) return;
        const std::string index = read(t.root / "out" / "game" / "index.html");
        const auto a = index.find("[\"/later/fighters-art/a.webp\", \"assets/"), b = index.find("[\"/later/fighters-art/b.webp\", \"assets/");
        check(a != std::string::npos && b != std::string::npos && a < b, "both pictures, in order, each with its asset");
        check(!index.contains("notes.txt"), "only the suffix asked for");
        check(index.contains("window.omuiFirstFrame = omuiFetchLater;") && index.contains("FS.writeFile('/later/.ready', '')"),
              "fetched once the program's first frame is shown, .ready last");
        check(std::ranges::count_if(r->files, [](const gen::ToyFile& f) { return f.path.ends_with(".webp"); }) == 2);
        p.later = {{t.root / "art", ".webp", "/elsewhere"}};
        check(!gen::package_for_toy(p).has_value(), "only under /later/");
    });

    s.test("a font for some languages: before main only when the page opens in one, else once the program is drawn; a program that follows its route is not reloaded", [] {
        Tree t;
        auto p = t.package();
        p.font_languages = {{"Inter", {"zh"}}};
        auto r = gen::package_for_toy(p);
        check(r.has_value(), r ? "" : r.error());
        if (!r) return;
        const std::string index = read(t.root / "out" / "game" / "index.html");
        check(index.contains("[\"Inter.ttf\", \"assets/") && index.contains("[\"zh\"]]"), "its languages listed with it");
        check(index.contains("omuiWants(f[2])") && index.contains("'/later/fonts/' + f[0]"), "else in /later/fonts");
        check(index.contains("if (!window.omuiFollowsRoute) location.reload();"), "reloaded on a new hash only when the program does not follow its route");
    });

    s.test("the file types Toy keeps", [] {
        check(gen::toy_keeps(".wasm") && gen::toy_keeps(".ttf") && gen::toy_keeps(".webp") && gen::toy_keeps(".md"));
        check(!gen::toy_keeps(".txt") && !gen::toy_keeps(".mjs") && !gen::toy_keeps(".map") && !gen::toy_keeps(""));
    });

    s.test("initial font coverage is a separate hashed asset, selected only while the full font is deferred", [] {
        Tree t;
        fs::copy_file(mlibs::text::FontCollection::default_directory() / "NotoSansSC.ttf", t.root / "fonts" / "NotoSansSC.ttf", fs::copy_options::overwrite_existing);
        auto p = t.package();
        p.fonts = {"Inter", "NotoSansSC"};
        p.font_languages = {{"NotoSansSC", {"zh"}}};
        p.font_initial = {{"NotoSansSC", "中文"}};
        auto r = gen::package_for_toy(p);
        check(r.has_value(), r ? "" : r.error());
        if (!r) return;
        const auto index = read(p.out / "index.html");
        const auto entry = index.find("[\"NotoSansSC.ttf\"");
        check(entry != std::string::npos && index.substr(entry, index.find('\n', entry) - entry).contains("[\"zh\"], \"assets/"));
        check(index.contains("omuiFontRecord('initial-font:' + f[0], f[3], f[0])") && index.contains("var startup = deferred ? initial : full") &&
              index.contains("(deferred && font[3] ? 'initial-font:' : 'font:') + name") && index.contains("window.omuiFontLanguage"),
              "both native startup and DOM consumers select the actual locale-dependent initial/full source");
        check(index.contains("FS.symlink('/fonts/' + name, alias)") && index.contains("FS.unlink('/fonts/.partial')"), "later fonts refresh the canonical source, then clear readiness");
        std::optional<fs::path> tiny;
        for (const auto& f : r->files) if (f.path.ends_with(".ttf") && f.size > 100 && f.size < 1000000) tiny = p.out / f.path;
        check(tiny.has_value(), "a small, separately addressable initial font");
        if (tiny) {
            mlibs::text::FontCollection fonts;
            auto face = fonts.add_file(*tiny);
            check(face && fonts.covers(*face, U'中') && !fonts.covers(*face, U'龙'));
        }
        put(t.root / "only.txt", "A");
        p.subsets = {{"NotoSansSC", {t.root / "only.txt"}}};
        const auto subset = gen::package_for_toy(p);
        check(subset.has_value(), subset ? "" : subset.error());
        int full_faces = 0;
        if (subset) for (const auto& f : subset->files) if (f.path.ends_with(".ttf")) {
            mlibs::text::FontCollection fonts;
            const auto face = fonts.add_file(p.out / f.path);
            if (face && fonts.covers(*face, U'A')) {
                ++full_faces;
                check(fonts.covers(*face, U'中') && fonts.covers(*face, U'文') && !fonts.covers(*face, U'龙'),
                      "the full subset includes declared initial characters even when its source text does not");
            }
        }
        check(full_faces == 1);
        p.font_initial = {{"Missing", "中文"}};
        check(!gen::package_for_toy(p), "an unselected initial font is a configuration error");
        p.font_initial = {{"NotoSansSC", "中文"}}; p.font_languages.clear();
        check(gen::package_for_toy(p).has_value(), "initial coverage without language rules defers the full font for every language");
        p.font_initial = {{"NotoSansSC", ""}};
        check(!gen::package_for_toy(p), "initial coverage remains nonempty");
        p.font_initial = {{"NotoSansSC", "中文"}, {"NotoSansSC", "文"}};
        check(!gen::package_for_toy(p), "duplicate initial coverage remains a configuration error");
    });

    s.test("decoded asset sizes and an executable loading fixture accompany the actual package", [] {
        Tree t;
        fs::copy_file(mlibs::text::FontCollection::default_directory() / "NotoSansSC.ttf", t.root / "fonts" / "NotoSansSC.ttf", fs::copy_options::overwrite_existing);
        put(t.root / "words.txt", "A readable page 中文 龙");
        put(t.root / "art" / "later.webp", "deferred picture bytes");
        auto p = t.package(); p.sdk = false; p.out = scratch_dir("dist-toy-loading");
        p.fonts = {"Inter", "NotoSansSC"}; p.font_languages = {{"NotoSansSC", {"zh"}}}; p.font_initial = {{"NotoSansSC", "中文"}};
        p.subsets = {{"NotoSansSC", {t.root / "words.txt"}}}; p.later = {{t.root / "art", ".webp", "/later/art"}};
        const auto result = gen::package_for_toy(p); check(result.has_value(), result ? "" : result.error()); if (!result) return;
        const auto html = read(p.out / "index.html");
        check(html.contains("var omuiAssetBytes = {") && html.contains("window.omuiLoad = {") && html.contains("new CustomEvent('omui-load'"));
        check(html.contains("this.focus(); });\n</script>\n<script src=\""), "the inline loader closes before the external launcher script");
        check(html.contains("fontBytes: omuiFontBytes") && html.contains("omuiFontRecords[url]") && html.contains("record.buffer = null"),
              "explicit DOM font consumers share URL transfers and release installed byte buffers");
        check(html.contains("basis: 'decoded'") && html.contains("WebAssembly.instantiateStreaming(body.response, imports)"));
        check(html.contains("headers.delete('Content-Length')") && html.contains("headers.delete('Content-Encoding')"));
        check(html.contains("runtimeInitialized: omuiInitialized, presented: omuiPresented") && html.contains("omuiPublish('presented')"));
        check(html.contains("omuiPlanFonts();\n    var job = omuiJob(omuiWasmAsset") && html.contains("if (omuiPreRan) return; omuiPlanFonts(); omuiPreRan = true;"),
              "font plans follow bootstrap and are rechecked before ordinary startup installation");
        check(html.contains("setTimeout(function () { omuiFetchLater(); }, 4000)") && !html.contains("window.omuiFirstFrame = null;"), "the timer never impersonates a native presentation");
        for (const auto& file : result->files) if (file.path.ends_with(".ttf") || file.path.ends_with(".wasm") || file.path.ends_with(".webp"))
            check(html.contains(std::format("\"{}\":{}", file.path, file.size)), file.path);
        std::println("dist-toy loading fixture: {}", (p.out / "index.html").string());

        // Keep the existing mixed-language fixture intact and add a real
        // Inter package for the all-language first-paint typography branch.
        fs::copy_file(mlibs::text::FontCollection::default_directory() / "Inter.ttf", t.root / "fonts" / "Inter.ttf", fs::copy_options::overwrite_existing);
        auto initial = t.package(); initial.sdk = false; initial.out = p.out / "all-language-initial"; initial.font_initial = {{"Inter", "omui"}};
        const auto packed = gen::package_for_toy(initial); check(packed.has_value(), packed ? "" : packed.error());
        bool tiny_face = false, full_face = false;
        if (packed) for (const auto& file : packed->files) if (file.path.ends_with(".ttf")) {
            mlibs::text::FontCollection faces; const auto face = faces.add_file(initial.out / file.path); check(face.has_value());
            if (face) {
                const bool full = faces.covers(*face, U'Z');
                tiny_face = tiny_face || (faces.covers(*face, U'o') && !full); full_face = full_face || full;
            }
        }
        check(tiny_face && full_face, "real Inter initial coverage and the complete face both travel in the package");
        if (packed) check(read(initial.out / "index.html").contains("[], \"assets/"), "the initial asset needs no artificial language rule");

        // Different selected basenames can have identical content hashes.
        // They share a transfer but each must exist under its own FS name.
        fs::copy_file(t.root / "fonts" / "Inter.ttf", t.root / "fonts" / "InterAlias.ttf");
        auto aliases = t.package(); aliases.sdk = false; aliases.out = p.out / "shared-font-url"; aliases.fonts = {"Inter", "InterAlias"};
        const auto shared = gen::package_for_toy(aliases); check(shared.has_value(), shared ? "" : shared.error());
        if (shared) {
            check(std::ranges::count_if(shared->files, [](const auto& file) { return file.path.ends_with(".ttf"); }) == 1,
                  "two basenames package as one actual content-hashed TTF asset");
            check(read(aliases.out / "index.html").contains("InterAlias.ttf"), "the shared-content fixture retains the second logical font name");
        }
    });

    return s.run(argc, argv);
}
