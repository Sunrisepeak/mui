module openmui.gen.dist_toy;

import std;
import nlohmann.json;

namespace omui::gen {

namespace {

namespace fs = std::filesystem;

std::string utf8(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::expected<std::string, std::string> read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot read", utf8(file)));
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::expected<void, std::string> write(const fs::path& file, std::string_view bytes) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return std::unexpected(std::format("{}: cannot write", utf8(file)));
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) return std::unexpected(std::format("{}: cannot write", utf8(file)));
    return {};
}

// A content hash for file names: FNV-1a, 64 bits, as 12 hex digits.
std::string content_hash(std::string_view bytes) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const unsigned char c : bytes) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return std::format("{:012x}", h & 0xffffffffffffull);
}

// openmui.text's directory of default fonts, from mcpp's resolved graph.
std::optional<fs::path> text_fonts(const fs::path& graph) {
    if (graph.empty()) return std::nullopt;
    auto text = read(graph);
    if (!text) return std::nullopt;
    try {
        const auto g = nlohmann::json::parse(*text);
        for (const auto& p : g.at("packages")) {
            const auto& id = p.at("package");
            if (id.value("namespace", "") == "openmui" && id.value("name", "") == "text") return fs::path(p.value("manifest_dir", "")) / "fonts";
        }
    } catch (const nlohmann::json::exception&) {
    }
    return std::nullopt;
}

std::string escape_html(std::string_view s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string js_string(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '<') {
            out += "\\u003c";  // never `</script>` inside the page's script
            continue;
        }
        out += c;
    }
    return out + "\"";
}

// The page: a canvas filling the frame, a word while it loads, and the
// loader — the program's arguments from the hash route, its /fonts filled
// before main, its .wasm by its hashed name.
std::string page(const ToyPackage& p, std::string_view js, std::string_view wasm, std::span<const std::pair<std::string, std::string>> fonts) {
    std::string font_list;
    for (const auto& [name, file] : fonts) font_list += std::format("{}[{}, {}]", font_list.empty() ? "" : ", ", js_string(name), js_string(file));
    std::string html;
    html += "<!doctype html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover\">\n";
    html += std::format("<title>{}</title>\n", escape_html(p.title.empty() ? p.target : p.title));
    html += std::format(
        "<style>html,body{{margin:0;height:100%;overflow:hidden;background:{0};touch-action:none;-webkit-user-select:none;user-select:none}}"
        "#canvas{{display:block;width:100vw;height:100vh;outline:none}}"
        "#status{{position:fixed;inset:0;display:flex;align-items:center;justify-content:center;color:#888;font:14px system-ui,sans-serif;pointer-events:none}}</style>\n",
        escape_html(p.background));
    // Toy's SDK (window.toy): Bilibili's own script, loaded by the page itself (Toy's guide).
    if (p.sdk) html += "<script src=\"//s1.hdslb.com/bfs/seed/toy/app/sdk/toy-sdk.js\"></script>\n";
    html += "</head>\n<body>\n<canvas id=\"canvas\" tabindex=\"0\"></canvas>\n<div id=\"status\">…</div>\n<script>\n";
    html += std::format("var omuiFonts = [{}];\n", font_list);
    html += std::format("var omuiWasm = {};\n", js_string(wasm));
    html += R"(var omuiStatus = document.getElementById('status');
// Hash routes are the program's flags: #/training runs it with --training.
function omuiArguments() {
  return (document.URL.split('#')[1] || '').replace(/^\//, '').split('/').filter(function (s) { return /^[A-Za-z0-9_-]+$/.test(s); })
    .map(function (s) { return '--' + s; });
}
var Module = {
  canvas: document.getElementById('canvas'),
  arguments: omuiArguments(),
  locateFile: function (path) { return /\.wasm$/.test(path) ? omuiWasm : path; },
  print: function (text) { console.log(text); },
  printErr: function (text) { console.warn(text); },
  preRun: [function () {
    // The fonts the program draws with, in its /fonts before main.
    FS.mkdir('/fonts');
    omuiFonts.forEach(function (f) {
      Module.addRunDependency('font ' + f[0]);
      fetch(f[1]).then(function (r) { return r.arrayBuffer(); }).then(function (b) {
        FS.writeFile('/fonts/' + f[0], new Uint8Array(b));
        Module.removeRunDependency('font ' + f[0]);
      });
    });
  }],
  onRuntimeInitialized: function () { omuiStatus.style.display = 'none'; Module.canvas.focus(); },
  setStatus: function (text) { if (text) omuiStatus.textContent = text; },
};
if (typeof WebAssembly !== 'object') omuiStatus.textContent = 'WebAssembly is needed';
window.addEventListener('hashchange', function () { location.reload(); });
document.getElementById('canvas').addEventListener('pointerdown', function () { this.focus(); });
</script>
)";
    html += std::format("<script src=\"{}\"></script>\n</body>\n</html>\n", js);
    return html;
}

}  // namespace

bool toy_keeps(std::string_view extension) noexcept {
    // The publishing guide's FAQ, verbatim: the file types a Toy package keeps.
    static constexpr std::array<std::string_view, 32> kept{
        ".html", ".htm", ".css", ".js", ".json", ".wasm", ".data", ".md", ".csv", ".tsv", ".png", ".jpg",
        ".jpeg", ".gif", ".svg", ".webp", ".ico", ".woff2", ".woff", ".ttf", ".eot", ".mp3", ".wav", ".ogg",
        ".m4a", ".mp4", ".webm", ".atlas", ".ani", ".part", ".nani", ".unityweb"};
    return std::ranges::find(kept, extension) != kept.end();
}

std::expected<ToyReport, std::string> package_for_toy(const ToyPackage& p) {
    std::error_code ec;
    const fs::path bin = p.stage / "bin";
    const fs::path js_in = bin / (p.target + ".js"), wasm_in = bin / (p.target + ".wasm");
    auto js = read(js_in);
    if (!js) return std::unexpected(std::format("{} (dist-toy packages a wasm32-emscripten program: pack with --target wasm32-emscripten)", js.error()));
    auto wasm = read(wasm_in);
    if (!wasm) return std::unexpected(wasm.error());
    if (fs::exists(bin / (p.target + ".data"), ec))
        return std::unexpected("the program carries a .data file (--preload-file): dist-toy gives the page's files as hashed assets instead");

    fs::remove_all(p.out, ec);
    fs::create_directories(p.out / "assets", ec);
    if (ec) return std::unexpected(std::format("{}: {}", utf8(p.out), ec.message()));

    const std::string js_name = std::format("{}-{}.js", p.target, content_hash(*js));
    const std::string wasm_name = std::format("{}-{}.wasm", p.target, content_hash(*wasm));
    if (auto w = write(p.out / js_name, *js); !w) return std::unexpected(w.error());
    if (auto w = write(p.out / wasm_name, *wasm); !w) return std::unexpected(w.error());

    // The fonts: openmui.text's defaults the program asks for, each a hashed asset.
    fs::path fonts_dir = p.fonts_dir;
    if (fonts_dir.empty()) fonts_dir = text_fonts(p.graph).value_or(fs::path());
    if (fonts_dir.empty() || !fs::is_directory(fonts_dir, ec))
        return std::unexpected("openmui.text's fonts were not found (give --fonts-dir, or pack through mcpp so the graph names them)");
    std::vector<std::string> stems = p.fonts;
    if (stems.empty() || (stems.size() == 1 && stems[0] == "all")) {
        stems.clear();
        for (const auto& e : fs::directory_iterator(fonts_dir, ec))
            if (e.path().extension() == ".ttf") stems.push_back(utf8(e.path().stem()));
        std::ranges::sort(stems);
    }
    std::vector<std::pair<std::string, std::string>> fonts;
    for (const std::string& stem : stems) {
        auto bytes = read(fonts_dir / (stem + ".ttf"));
        if (!bytes) return std::unexpected(std::format("font {}: {}", stem, bytes.error()));
        const std::string asset = std::format("assets/{}.ttf", content_hash(*bytes));
        if (auto w = write(p.out / asset, *bytes); !w) return std::unexpected(w.error());
        fonts.emplace_back(stem + ".ttf", asset);
    }

    // The licences travel with what they cover: the program's (staged by
    // mcpp) and the fonts', as text Toy keeps (.md).
    auto keep_licence = [&](const fs::path& from, const fs::path& to) -> std::expected<void, std::string> {
        auto bytes = read(from);
        if (!bytes) return std::unexpected(bytes.error());
        fs::path dest = to;
        if (!toy_keeps(utf8(dest.extension()))) dest += ".md";
        return write(dest, *bytes);
    };
    if (fs::is_directory(p.stage / "licenses", ec))
        for (const auto& e : fs::recursive_directory_iterator(p.stage / "licenses", ec))
            if (e.is_regular_file()) {
                if (auto r = keep_licence(e.path(), p.out / "licenses" / fs::relative(e.path(), p.stage / "licenses", ec)); !r) return std::unexpected(r.error());
            }
    for (const auto& e : fs::directory_iterator(fonts_dir, ec)) {
        const std::string name = utf8(e.path().filename());
        if (name.starts_with("OFL") || name.starts_with("LICENSE"))
            if (auto r = keep_licence(e.path(), p.out / "licenses" / "fonts" / name); !r) return std::unexpected(r.error());
    }

    const std::string index = page(p, js_name, wasm_name, fonts);
    if (auto w = write(p.out / "index.html", index); !w) return std::unexpected(w.error());

    ToyReport report;
    for (const auto& e : fs::recursive_directory_iterator(p.out, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string rel = utf8(fs::relative(e.path(), p.out, ec));
        if (!toy_keeps(utf8(e.path().extension())))
            return std::unexpected(std::format("{}: a file type Toy does not keep", rel));
        report.files.push_back({rel, e.file_size()});
        report.total += e.file_size();
    }
    std::ranges::sort(report.files, {}, &ToyFile::path);
    report.index = index.size();
    if (report.index > toy_index_limit)
        return std::unexpected(std::format("index.html is {} bytes: Toy takes at most {}", report.index, toy_index_limit));

    // What `toy create` asks, and what is in the package, beside it (Toy has no manifest file).
    nlohmann::ordered_json about;
    about["kind"] = "toy-package";
    about["target"] = p.target;
    about["title"] = p.title;
    about["entry"] = "index.html";
    about["index_bytes"] = report.index;
    about["index_limit"] = toy_index_limit;
    about["total_bytes"] = report.total;
    about["publish"] = std::format("toy create --title \"{}\" (the slug, the cover and the category are the maintainer's; see `toy create --help-json`)", p.title);
    auto& list = about["files"] = nlohmann::ordered_json::array();
    for (const ToyFile& f : report.files) list.push_back({{"path", f.path}, {"bytes", f.size}});
    fs::path side = p.out;
    side += ".toy.omui.json";
    if (auto w = write(side, about.dump(2) + "\n"); !w) return std::unexpected(w.error());
    return report;
}

int dist_toy_main(int argc, char** argv) {
    auto usage = [] {
        std::println(std::cerr, "usage: omui-gen dist-toy <staged tree> --target <name> --out <dir> [--title T] [--fonts a,b|all]\n"
                                "                         [--fonts-dir <dir>] [--graph <file>] [--background #rrggbb] [--no-sdk]");
        return 2;
    };
    ToyPackage p;
    for (int i = 2; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto value = [&]() -> std::optional<std::string> {
            if (i + 1 >= argc) return std::nullopt;
            return std::string(argv[++i]);
        };
        std::optional<std::string> v;
        if (a == "--target" && (v = value())) p.target = *v;
        else if (a == "--out" && (v = value())) p.out = *v;
        else if (a == "--title" && (v = value())) p.title = *v;
        else if (a == "--fonts-dir" && (v = value())) p.fonts_dir = *v;
        else if (a == "--graph" && (v = value())) p.graph = *v;
        else if (a == "--background" && (v = value())) p.background = *v;
        else if (a == "--fonts" && (v = value())) {
            for (std::size_t from = 0; from <= v->size();) {
                const std::size_t comma = std::min(v->find(',', from), v->size());
                if (comma > from) p.fonts.push_back(v->substr(from, comma - from));
                from = comma + 1;
            }
        } else if (a == "--no-sdk") p.sdk = false;
        else if (!a.starts_with("--") && p.stage.empty()) p.stage = a;
        else return usage();
    }
    if (p.stage.empty() || p.target.empty() || p.out.empty()) return usage();
    auto r = package_for_toy(p);
    if (!r) {
        std::println(std::cerr, "dist-toy: {}", r.error());
        return 1;
    }
    std::println("dist-toy: {} — {} files, {} bytes (index.html {} of {})", utf8(p.out), r->files.size(), r->total, r->index, toy_index_limit);
    return 0;
}

}  // namespace omui::gen
