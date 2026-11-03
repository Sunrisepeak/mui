module openmui.gen.dist_toy;

import std;
import openmui.hash;
import nlohmann.json;
import openmui.gen.font_subset;

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
    return std::format("{:012x}", omui::hash::fnv1a(bytes) & 0xffffffffffffull);
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
std::string page(const ToyPackage& p, std::string_view js, std::string_view wasm, std::span<const std::pair<std::string, std::string>> fonts,
                 std::span<const std::pair<std::string, std::string>> later, std::span<const std::pair<std::string, std::string>> initial) {
    std::string font_list, later_list;
    for (const auto& [name, file] : fonts) {
        // Its languages, if it is for some: ["zh"].
        std::string langs;
        const std::string stem = name.substr(0, name.rfind('.'));
        if (const auto fl = std::ranges::find(p.font_languages, stem, &std::pair<std::string, std::vector<std::string>>::first); fl != p.font_languages.end())
            for (const std::string& l : fl->second) langs += std::format("{}{}", langs.empty() ? "" : ", ", js_string(l));
        const auto first = std::ranges::find(initial, stem, &std::pair<std::string, std::string>::first);
        font_list += std::format("{}[{}, {}, [{}]{}]", font_list.empty() ? "" : ", ", js_string(name), js_string(file), langs,
                                 first == initial.end() ? "" : ", " + js_string(first->second));
    }
    for (const auto& [path, file] : later) later_list += std::format("{}[{}, {}]", later_list.empty() ? "" : ",\n  ", js_string(path), js_string(file));
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
    html += std::format("var omuiLater = [{}];\n", later_list);
    html += "var omuiFontPending = 0;\n";
    html += R"(var omuiStatus = document.getElementById('status');
// Hash routes are the program's flags: #/training runs it with --training.
function omuiArguments() {
  return (document.URL.split('#')[1] || '').replace(/^\//, '').split('/').filter(function (s) { return /^[A-Za-z0-9_-]+$/.test(s); })
    .map(function (s) { return '--' + s; });
}
// Whether the page opens in one of these languages: its route's first part (#/zh/…), or the browser's first language.
function omuiWants(langs) {
  // A prerendered application can state the language it actually opens in.
  if (typeof window.omuiFontLanguage === 'string') {
    var chosen = window.omuiFontLanguage.toLowerCase();
    return langs.some(function (l) { return chosen === l || chosen.indexOf(l + '-') === 0; });
  }
  var route = (document.URL.split('#')[1] || '').replace(/^\//, '').split('/')[0];
  var first = ((navigator.languages && navigator.languages[0]) || navigator.language || '').toLowerCase();
  return langs.some(function (l) { return route === l || first === l || first.indexOf(l + '-') === 0; });
}
var Module = {
  canvas: document.getElementById('canvas'),
  arguments: omuiArguments(),
  locateFile: function (path) { return /\.wasm$/.test(path) ? omuiWasm : path; },
  print: function (text) { console.log(text); },
  printErr: function (text) { console.warn(text); },
  preRun: [function () {
    // The fonts the program draws with, in its /fonts before main — but a
    // font for some languages only when the page opens in one of them; else
    // it comes once the program is drawn (omuiFetchLater, /later/fonts).
    FS.mkdir('/fonts');
    omuiFonts.forEach(function (f) {
      var file = f[1];
      if (f[2].length && !omuiWants(f[2])) {
        omuiLater.push(['/later/fonts/' + f[0], file]);
        ++omuiFontPending;
        FS.writeFile('/fonts/.partial', '');
        if (!f[3]) return;
        file = f[3];
      }
      Module.addRunDependency('font ' + f[0]);
      fetch(file).then(function (r) { return r.arrayBuffer(); }).then(function (b) {
        FS.writeFile('/fonts/' + f[0], new Uint8Array(b));
        Module.removeRunDependency('font ' + f[0]);
      });
    });
  }],
  onRuntimeInitialized: function () {
    omuiStatus.style.display = 'none';
    Module.canvas.focus();
    // What comes after the first frame starts once it is shown (the program says so), so it never slows it; or in a while, whatever.
    window.omuiFirstFrame = omuiFetchLater;
    setTimeout(function () { var f = window.omuiFirstFrame; window.omuiFirstFrame = null; if (f) f(); }, 4000);
  },
  setStatus: function (text) { if (text) omuiStatus.textContent = text; },
};
// What the program takes once it is drawn: fetched now, written into its
// file system, /later/.ready last (the program looks for it).
function omuiFetchLater() {
  var left = omuiLater.length;
  if (!left) return;
  omuiLater.forEach(function (f) {
    fetch(f[1]).then(function (r) { return r.arrayBuffer(); }).then(function (b) {
      FS.mkdirTree(f[0].substring(0, f[0].lastIndexOf('/')));
      if (f[0].indexOf('/later/fonts/') === 0) {
        // Future font collections use /fonts too; the alias avoids a second copy in MEMFS.
        var canonical = '/fonts/' + f[0].substring('/later/fonts/'.length);
        FS.writeFile(canonical, new Uint8Array(b));
        FS.symlink(canonical, f[0]);
        if (--omuiFontPending === 0) FS.unlink('/fonts/.partial');
      } else FS.writeFile(f[0], new Uint8Array(b));
      if (--left === 0) FS.writeFile('/later/.ready', '');
    });
  });
}
if (typeof WebAssembly !== 'object') omuiStatus.textContent = 'WebAssembly is needed';
// A new hash route: the program started again with its flags — unless it follows its route itself (it has set one: Back and Forward move it).
window.addEventListener('hashchange', function () { if (!window.omuiFollowsRoute) location.reload(); });
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
    for (const auto& [font, text] : p.font_initial) {
        const auto lang = std::ranges::find(p.font_languages, font, &std::pair<std::string, std::vector<std::string>>::first);
        if (text.empty() || std::ranges::find(stems, font) == stems.end() || lang == p.font_languages.end() || lang->second.empty())
            return std::unexpected(std::format("--font-initial {} needs nonempty text, a selected font and --font-languages", font));
        if (std::ranges::count(p.font_initial, font, &std::pair<std::string, std::string>::first) != 1)
            return std::unexpected(std::format("--font-initial {} is repeated", font));
    }
    std::vector<std::pair<std::string, std::string>> fonts, initial;
    for (const std::string& stem : stems) {
        auto bytes = read(fonts_dir / (stem + ".ttf"));
        if (!bytes) return std::unexpected(std::format("font {}: {}", stem, bytes.error()));
        const auto first = std::ranges::find(p.font_initial, stem, &std::pair<std::string, std::string>::first);
        if (first != p.font_initial.end()) {
            std::set<char32_t> keep;
            characters_of(first->second, keep);
            auto cut = subset_font(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()), keep);
            if (!cut) return std::unexpected(std::format("initial font {}: {}", stem, cut.error()));
            const std::string_view data(reinterpret_cast<const char*>(cut->data()), cut->size());
            const std::string asset = std::format("assets/{}.ttf", content_hash(data));
            if (auto w = write(p.out / asset, data); !w) return std::unexpected(w.error());
            initial.emplace_back(stem, asset);
            std::println("dist-toy: {} initial glyphs: {} bytes", stem, cut->size());
        }
        // Cut down to the characters the program's text holds, where asked.
        if (const auto sub = std::ranges::find(p.subsets, stem, &ToyPackage::Subset::font); sub != p.subsets.end()) {
            std::set<char32_t> keep;
            always_kept(keep);
            if (first != p.font_initial.end()) characters_of(first->second, keep);
            if (auto common = read(fonts_dir / (stem + ".common.txt"))) characters_of(*common, keep);
            static constexpr std::array<std::string_view, 7> text_kinds{".md", ".cpp", ".cppm", ".toml", ".json", ".txt", ".js"};
            auto take = [&](const fs::path& f) {
                if (std::ranges::find(text_kinds, utf8(f.extension())) == text_kinds.end()) return;
                if (auto t = read(f)) characters_of(*t, keep);
            };
            for (const fs::path& t : sub->text) {
                if (fs::is_directory(t, ec)) {
                    for (const auto& e : fs::recursive_directory_iterator(t, ec))
                        if (e.is_regular_file()) take(e.path());
                } else if (fs::is_regular_file(t, ec)) {
                    take(t);
                } else {
                    return std::unexpected(std::format("--subset {}: {} is neither a file nor a directory", stem, utf8(t)));
                }
            }
            FontSubsetReport r;
            auto cut = subset_font(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()), keep, &r);
            if (!cut) return std::unexpected(std::format("font {}: {}", stem, cut.error()));
            bytes->assign(reinterpret_cast<const char*>(cut->data()), cut->size());
            std::println("dist-toy: {} cut to {} of its {} glyphs: {} → {} bytes", stem, r.kept, r.glyphs, r.bytes_before, r.bytes_after);
        }
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

    // The files fetched once the program is drawn, each a hashed asset.
    std::vector<std::pair<std::string, std::string>> later;
    for (const ToyPackage::Later& l : p.later) {
        if (!l.mount.starts_with("/later/")) return std::unexpected(std::format("later files go under /later/ ({})", l.mount));
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(l.dir, ec))
            if (e.is_regular_file() && utf8(e.path().filename()).ends_with(l.suffix)) files.push_back(e.path());
        if (ec) return std::unexpected(std::format("{}: {}", utf8(l.dir), ec.message()));
        std::ranges::sort(files);
        for (const fs::path& f : files) {
            auto bytes = read(f);
            if (!bytes) return std::unexpected(bytes.error());
            const std::string asset = std::format("assets/{}{}", content_hash(*bytes), utf8(f.extension()));
            if (auto w = write(p.out / asset, *bytes); !w) return std::unexpected(w.error());
            later.emplace_back(std::format("{}/{}", l.mount, utf8(f.filename())), asset);
        }
    }
    const std::string index = page(p, js_name, wasm_name, fonts, later, initial);
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
                                "                         [--fonts-dir <dir>] [--graph <file>] [--background #rrggbb] [--no-sdk]\n"
                                "                         [--later <dir>|<suffix>|</later/mount>]... [--subset <font>=<path>,<path>...]...\n"
                                "                         [--font-languages <font>=<lang>,<lang>...]... [--font-initial <font>=<UTF-8 text>]...");
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
        else if (a == "--subset" && (v = value())) {  // <font>=<path>,<path>…
            const auto eq = v->find('=');
            if (eq == std::string::npos) {
                std::println(std::cerr, "omui-gen dist-toy: --subset is <font>=<path>,<path>…");
                return 2;
            }
            ToyPackage::Subset s{v->substr(0, eq), {}};
            for (std::size_t at = eq + 1; at <= v->size();) {
                const std::size_t end = std::min(v->find(',', at), v->size());
                if (end > at) s.text.emplace_back(v->substr(at, end - at));
                at = end + 1;
            }
            p.subsets.push_back(std::move(s));
        }
        else if (a == "--font-initial" && (v = value())) {
            const auto eq = v->find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 == v->size()) return usage();
            p.font_initial.emplace_back(v->substr(0, eq), v->substr(eq + 1));
        }
        else if (a == "--font-languages" && (v = value())) {  // <font>=<lang>,<lang>…
            const auto eq = v->find('=');
            if (eq == std::string::npos) return usage();
            std::vector<std::string> langs;
            for (std::size_t at = eq + 1; at <= v->size();) {
                const std::size_t end = std::min(v->find(',', at), v->size());
                if (end > at) langs.emplace_back(v->substr(at, end - at));
                at = end + 1;
            }
            p.font_languages.emplace_back(v->substr(0, eq), std::move(langs));
        }
        else if (a == "--later" && (v = value())) {  // <dir>|<suffix>|<mount>
            const auto bar1 = v->find('|'), bar2 = v->find('|', bar1 + 1);
            if (bar1 == std::string::npos || bar2 == std::string::npos) return usage();
            p.later.push_back({v->substr(0, bar1), v->substr(bar1 + 1, bar2 - bar1 - 1), v->substr(bar2 + 1)});
        }
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
