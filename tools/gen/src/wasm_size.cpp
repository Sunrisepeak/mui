// Reading a WebAssembly binary's sections, code bodies and name section.
module omui.gen.wasm_size;

import std;

namespace omui::gen {

namespace {

struct Reader {
    std::span<const std::uint8_t> b;
    std::size_t                   at = 0;
    bool                          ok = true;

    [[nodiscard]] bool done() const noexcept { return at >= b.size(); }
    std::uint8_t byte() {
        if (at >= b.size()) {
            ok = false;
            return 0;
        }
        return b[at++];
    }
    std::uint64_t leb() {
        std::uint64_t r = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const std::uint8_t x = byte();
            r |= static_cast<std::uint64_t>(x & 0x7f) << shift;
            if (!(x & 0x80) || !ok) return r;
        }
        ok = false;
        return r;
    }
    std::string_view name() {
        const std::size_t n = static_cast<std::size_t>(leb());
        if (!ok || at + n > b.size()) {
            ok = false;
            return {};
        }
        const std::string_view s(reinterpret_cast<const char*>(b.data() + at), n);
        at += n;
        return s;
    }
};

constexpr std::array<std::string_view, 13> section_names{"custom", "type",    "import", "function", "table", "memory",   "global",
                                                         "export", "start",   "element", "code",    "data",  "datacount"};

// A source name at `i` (<length><identifier>), moving past it; empty when there is none.
std::string_view source_name(std::string_view s, std::size_t& i) {
    std::size_t n = 0, j = i;
    while (j < s.size() && s[j] >= '0' && s[j] <= '9') n = n * 10 + static_cast<std::size_t>(s[j++] - '0');
    if (j == i || j + n > s.size()) return {};
    i = j + n;
    return s.substr(j, n);
}

// C libraries by their symbols' prefixes.
std::string_view library_of(std::string_view s) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 26> prefixes{{
        {"hb_", "harfbuzz"},        {"_hb_", "harfbuzz"},     {"FT_", "freetype"},        {"ft_", "freetype"},       {"af_", "freetype"},
        {"tt_", "freetype"},        {"cff_", "freetype"},     {"ps_", "freetype"},        {"t1_", "freetype"},       {"sfnt_", "freetype"},
        {"md_", "md4c"},            {"md4c", "md4c"},         {"png_", "libpng"},         {"WebP", "libwebp"},       {"VP8", "libwebp"},
        {"lua", "lua"},             {"JS_", "quickjs"},       {"SDL_", "SDL"},            {"stbi", "stb"},           {"emscripten_", "emscripten"},
        {"__cxa_", "c++ runtime"},  {"__cxx", "c++ runtime"}, {"__gxx", "c++ runtime"},   {"_Unwind", "c++ runtime"}, {"dynCall", "emscripten"},
        {"__", "libc"},
    }};
    for (const auto& [p, lib] : prefixes)
        if (s.starts_with(p)) return lib;
    return {};
}

std::string escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\', out += c;
        else if (static_cast<unsigned char>(c) < 0x20) out += std::format("\\u{:04x}", static_cast<int>(c));
        else out += c;
    }
    return out;
}

}  // namespace

// A demangled name's owner (what an analysis build's name section holds: "void omui::Runtime@openmui.view::frame(…)").
std::string demangled_owner(std::string_view s, int depth) {
    std::string name(s);
    for (std::size_t at; (at = name.find("(anonymous namespace)")) != std::string::npos;) name.replace(at, 21, "{anonymous}");
    std::string_view q = name;
    // A return type before it ("void std::…", "bool omui::…"): one word, then the qualified name.
    if (const auto sp = q.find(' '); sp != std::string_view::npos && q.substr(0, sp).find_first_of(":(<") == std::string_view::npos &&
                                     q.find("::") != std::string_view::npos && q.find("::") > sp && q.find('(') > sp)
        q.remove_prefix(sp + 1);
    const std::string_view head = q.substr(0, q.find('('));
    // The module it is attached to: after '@', to the first character no module name has.
    if (const auto at = head.find('@'); at != std::string_view::npos) {
        std::size_t end = at + 1;
        while (end < head.size() && (std::isalnum(static_cast<unsigned char>(head[end])) || head[end] == '_' || head[end] == '.')) ++end;
        if (end > at + 1) return std::string(head.substr(at + 1, end - at - 1));
    }
    // Its namespaces, outside any template's arguments.
    std::vector<std::string_view> parts;
    int angle = 0;
    std::size_t from = 0;
    for (std::size_t i = 0; i < head.size(); ++i) {
        if (head[i] == '<') ++angle;
        else if (head[i] == '>') --angle;
        else if (angle == 0 && head.substr(i, 2) == "::") {
            parts.push_back(head.substr(from, i - from));
            from = i + 2;
            ++i;
        }
    }
    if (parts.empty()) {  // a C function, or C++ in the global namespace: a library by its name or its arguments' types
        if (const auto lib = library_of(head); !lib.empty()) return std::string(lib);
        if (const auto args = q.find('('); args != std::string_view::npos)
            if (const auto lib = library_of(q.substr(args + 1)); !lib.empty()) return std::string(lib);
        if (q.find('(') != std::string_view::npos) return "(global)";
        return "(C) " + std::string(head.substr(0, std::min(head.find('_', 1), std::size_t{24})));
    }
    // The C++ libraries by their namespaces.
    constexpr std::array<std::pair<std::string_view, std::string_view>, 9> libraries{{
        {"OT", "harfbuzz"}, {"CFF", "harfbuzz"}, {"AAT", "harfbuzz"}, {"graph", "harfbuzz"}, {"toml", "tomlplusplus"},
        {"nlohmann", "nlohmann.json"}, {"tf", "taskflow"}, {"__cxxabiv1", "c++ runtime"}, {"std", "std"},
    }};
    for (const auto& [ns, lib] : libraries)
        if (parts.front() == ns) return std::string(lib);
    std::string out;
    for (int i = 0; i < depth && i < static_cast<int>(parts.size()); ++i) out += (out.empty() ? "" : "::") + std::string(parts[static_cast<std::size_t>(i)]);
    return out;
}

std::string owner_of(std::string_view s, int depth) {
    if (!s.starts_with("_Z")) {
        if (s.find_first_of(":( ") != std::string_view::npos) return demangled_owner(s, depth);
        if (const auto lib = library_of(s); !lib.empty()) return std::string(lib);
        return "(C) " + std::string(s.substr(0, std::min(s.find('_', 1), std::size_t{24})));
    }
    std::size_t i = 2;
    if (i < s.size() && s[i] == 'L') ++i;  // internal linkage
    const bool nested = i < s.size() && s[i] == 'N';
    if (nested) {
        ++i;
        while (i < s.size() && (s[i] == 'r' || s[i] == 'V' || s[i] == 'K' || s[i] == 'R' || s[i] == 'O')) ++i;
    }
    std::string module, scope;
    int parts = 0;
    for (;;) {
        if (i >= s.size()) break;
        const char c = s[i];
        if (c == 'W') {  // a module's part: W <source-name>, or WP <source-name> (a partition)
            ++i;
            const bool partition = i < s.size() && s[i] == 'P';
            if (partition) ++i;
            const auto part = source_name(s, i);
            if (part.empty()) break;
            if (!partition) module += (module.empty() ? "" : ".") + std::string(part);
            continue;
        }
        if (c == 'S' && i + 1 < s.size() && s[i + 1] >= 'a' && s[i + 1] <= 'z') {  // St, Sa, Sb, Ss…: the standard library's
            if (parts < depth) scope += (scope.empty() ? "" : "::") + std::string("std"), ++parts;
            break;
        }
        if (c == 'L') {
            ++i;
            continue;
        }
        if (c < '0' || c > '9') break;
        const auto part = source_name(s, i);
        if (part.empty()) break;
        if (!nested) {  // a name in the global namespace (or a module's)
            if (scope.empty()) scope = "(global)";
            break;
        }
        if (parts < depth) scope += (scope.empty() ? "" : "::") + std::string(part), ++parts;
        else break;
    }
    if (!module.empty()) return module;
    return scope.empty() ? std::string("(unknown)") : scope;
}

std::expected<WasmSizeReport, std::string> wasm_size(std::span<const std::uint8_t> wasm, std::size_t top) {
    if (wasm.size() < 8 || !std::equal(wasm.begin(), wasm.begin() + 4, std::array<std::uint8_t, 4>{0x00, 0x61, 0x73, 0x6d}.begin()))
        return std::unexpected("not a WebAssembly binary (no \\0asm)");
    WasmSizeReport r;
    r.total = wasm.size();
    r.shipped = 8;
    Reader in{wasm, 8};
    std::size_t imported_functions = 0;
    std::vector<std::size_t> bodies;  // each defined function's body size
    std::map<std::size_t, std::string> names;  // by function index (imports first)
    while (!in.done()) {
        const std::size_t begin = in.at;
        const std::uint8_t id = in.byte();
        const std::size_t size = static_cast<std::size_t>(in.leb());
        const std::size_t start = in.at;
        if (!in.ok || start + size > wasm.size()) return std::unexpected(std::format("a section ({}) runs past the end of the file", id));
        Reader sec{wasm.subspan(start, size), 0};
        std::string name = id < section_names.size() ? std::string(section_names[id]) : std::format("section {}", id);
        if (id == 0) {
            const std::string_view custom = sec.name();
            name = std::format("custom:{}", custom);
            if (custom == "name") {
                // Its subsections: 1 is the functions' names.
                while (!sec.done() && sec.ok) {
                    const std::uint8_t sub = sec.byte();
                    const std::size_t len = static_cast<std::size_t>(sec.leb());
                    const std::size_t end = sec.at + len;
                    if (sub == 1) {
                        const std::size_t count = static_cast<std::size_t>(sec.leb());
                        for (std::size_t k = 0; k < count && sec.ok; ++k) {
                            const std::size_t index = static_cast<std::size_t>(sec.leb());
                            names[index] = std::string(sec.name());
                        }
                    }
                    sec.at = end;
                }
            }
        } else if (id == 2) {  // imports: the functions' count comes first in the index space
            const std::size_t count = static_cast<std::size_t>(sec.leb());
            for (std::size_t k = 0; k < count && sec.ok; ++k) {
                (void)sec.name(), (void)sec.name();
                const std::uint8_t kind = sec.byte();
                switch (kind) {
                    case 0: (void)sec.leb(), ++imported_functions; break;  // a function: its type
                    case 1: (void)sec.byte(); if (const auto f = sec.leb(); f & 1) (void)sec.leb(), (void)sec.leb(); else (void)sec.leb(); break;
                    case 2: if (const auto f = sec.leb(); f & 1) (void)sec.leb(), (void)sec.leb(); else (void)sec.leb(); break;
                    case 3: (void)sec.byte(), (void)sec.byte(); break;
                    case 4: (void)sec.byte(), (void)sec.leb(); break;  // a tag
                    default: return std::unexpected(std::format("an import of an unknown kind ({})", kind));
                }
            }
        } else if (id == 10) {
            const std::size_t count = static_cast<std::size_t>(sec.leb());
            for (std::size_t k = 0; k < count && sec.ok; ++k) {
                const std::size_t body = static_cast<std::size_t>(sec.leb());
                bodies.push_back(body);
                sec.at += body;
            }
            r.code = size;
        } else if (id == 11) {
            r.data = size;
        }
        if (!sec.ok) return std::unexpected(std::format("the {} section cannot be read", name));
        if (id != 0) r.shipped += start + size - begin;  // its id, its size and its bytes
        auto it = std::ranges::find(r.sections, name, &WasmSizeEntry::name);
        if (it == r.sections.end()) r.sections.push_back({name, size, 0});
        else it->bytes += size;
        in.at = start + size;
    }
    r.functions = bodies.size();
    std::map<std::string, WasmSizeEntry> owners;
    std::vector<WasmSizeEntry> each;
    for (std::size_t k = 0; k < bodies.size(); ++k) {
        const auto it = names.find(imported_functions + k);
        const std::string name = it == names.end() ? std::format("(function {})", imported_functions + k) : it->second;
        if (it != names.end()) ++r.named;
        const std::string owner = it == names.end() ? std::string("(no name)") : owner_of(name);
        auto& o = owners[owner];
        o.name = owner;
        o.bytes += bodies[k];
        ++o.functions;
        each.push_back({name, bodies[k], 1});
    }
    for (auto& [_, o] : owners) r.owners.push_back(std::move(o));
    std::ranges::sort(r.owners, std::greater{}, &WasmSizeEntry::bytes);
    std::ranges::sort(each, std::greater{}, &WasmSizeEntry::bytes);
    each.resize(std::min(each.size(), top));
    r.largest = std::move(each);
    std::ranges::sort(r.sections, std::greater{}, &WasmSizeEntry::bytes);
    return r;
}

std::string to_json(const WasmSizeReport& r) {
    auto list = [](const std::vector<WasmSizeEntry>& v, bool functions) {
        std::string out = "[";
        for (std::size_t i = 0; i < v.size(); ++i) {
            out += std::format("{}{{\"name\":\"{}\",\"bytes\":{}", i ? "," : "", escape(v[i].name), v[i].bytes);
            if (functions) out += std::format(",\"functions\":{}", v[i].functions);
            out += "}";
        }
        return out + "]";
    };
    return std::format("{{\"total\":{},\"shipped\":{},\"code\":{},\"data\":{},\"functions\":{},\"named\":{},\"sections\":{},\"owners\":{},\"largest\":{}}}",
                       r.total, r.shipped, r.code, r.data, r.functions, r.named, list(r.sections, false), list(r.owners, true), list(r.largest, false));
}

std::string to_markdown(const WasmSizeReport& r, std::size_t owners) {
    auto kib = [](std::size_t b) { return std::format("{:.1f} KiB", static_cast<double>(b) / 1024.0); };
    auto share = [&](std::size_t b) { return r.code ? std::format("{:.1f}%", 100.0 * static_cast<double>(b) / static_cast<double>(r.code)) : std::string("-"); };
    std::string out = std::format("**{}** shipped ({} in the file): code {}, data {}; {} functions, {} named.\n\n", kib(r.shipped), kib(r.total), kib(r.code),
                                  kib(r.data), r.functions, r.named);
    out += "| Section | Size |\n|---|---:|\n";
    for (const auto& s : r.sections) out += std::format("| {} | {} |\n", s.name, kib(s.bytes));
    out += "\n| Owner | Code | Share | Functions |\n|---|---:|---:|---:|\n";
    for (std::size_t i = 0; i < r.owners.size() && i < owners; ++i)
        out += std::format("| {} | {} | {} | {} |\n", r.owners[i].name, kib(r.owners[i].bytes), share(r.owners[i].bytes), r.owners[i].functions);
    out += "\n| Largest functions | Code |\n|---|---:|\n";
    for (const auto& f : r.largest) out += std::format("| `{}` | {} |\n", f.name.size() > 90 ? f.name.substr(0, 87) + "..." : f.name, kib(f.bytes));
    return out;
}

}  // namespace omui::gen
