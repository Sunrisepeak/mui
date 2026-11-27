module omui.gen.part;

import std;
import mcpplibs.os;
import omui.gen.size;
import mlibs.apart;

namespace omui::gen {

namespace {

namespace fs = std::filesystem;
namespace process = mcpplibs::os::process;

std::string utf8(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::expected<std::string, std::string> read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot read", utf8(file)));
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// A reader of a WebAssembly binary's bytes: LEB128 numbers, names, bytes skipped; false once past the end.
struct Bytes {
    std::span<const std::uint8_t> data;
    std::size_t                   at = 0;
    bool                          good = true;

    std::uint8_t byte() {
        if (at >= data.size()) return good = false, 0;
        return data[at++];
    }
    std::uint64_t number() {
        std::uint64_t value = 0;
        for (int shift = 0; good && shift < 64; shift += 7) {
            const std::uint8_t next = byte();
            value |= static_cast<std::uint64_t>(next & 0x7f) << shift;
            if (!(next & 0x80)) return value;
        }
        return good = false, 0;
    }
    void skip(std::uint64_t count) {
        if (count > data.size() - std::min(at, data.size())) good = false;
        else at += static_cast<std::size_t>(count);
    }
    void limits() {
        const std::uint8_t flags = byte();
        (void)number();
        if (flags & 1) (void)number();
    }
    // A value type: a number type's one byte, or a reference to a heap type (0x63, 0x64) and its index.
    void value_type() {
        if (const std::uint8_t type = byte(); type == 0x63 || type == 0x64) (void)number();
    }
};

}  // namespace

std::expected<std::size_t, std::string> function_imports(std::span<const std::uint8_t> wasm) {
    static constexpr std::array<std::uint8_t, 4> magic{0x00, 0x61, 0x73, 0x6d};
    if (wasm.size() < 8 || !std::ranges::equal(wasm.first(4), magic)) return std::unexpected("not a WebAssembly binary");
    Bytes bytes{wasm, 8};
    while (bytes.good && bytes.at < wasm.size()) {
        const std::uint8_t id = bytes.byte();
        const std::uint64_t size = bytes.number();
        if (!bytes.good || size > wasm.size() - bytes.at) break;
        if (id != 2) {
            bytes.skip(size);
            continue;
        }
        Bytes section{wasm.subspan(bytes.at, static_cast<std::size_t>(size))};
        std::size_t functions = 0;
        for (std::uint64_t count = section.number(), index = 0; section.good && index < count; ++index) {
            section.skip(section.number());  // the module's name
            section.skip(section.number());  // the field's
            switch (section.byte()) {
            case 0x00: ++functions, (void)section.number(); break;                // a function: its type
            case 0x01: section.value_type(), section.limits(); break;             // a table
            case 0x02: section.limits(); break;                                   // a memory
            case 0x03: section.value_type(), (void)section.byte(); break;         // a global: its type, mutable
            case 0x04: (void)section.byte(), (void)section.number(); break;       // a tag: its attribute, type
            default: return std::unexpected("an import of a kind this reader does not know");
            }
        }
        if (!section.good) return std::unexpected("its import section is cut short");
        return functions;
    }
    return bytes.good ? std::expected<std::size_t, std::string>(0) : std::unexpected("its sections are cut short");
}

namespace {

// Whether `text` is UTF-8 (what a JSON string holds).
bool utf8_text(std::string_view text) {
    for (std::size_t at = 0; at < text.size();) {
        const auto lead = static_cast<std::uint8_t>(text[at]);
        const std::size_t more = lead < 0x80 ? 0 : (lead >> 5) == 0x6 ? 1 : (lead >> 4) == 0xe ? 2 : (lead >> 3) == 0x1e ? 3 : 4;
        if (more == 4 || at + more >= text.size()) return false;
        for (std::size_t next = 1; next <= more; ++next)
            if ((static_cast<std::uint8_t>(text[at + next]) >> 6) != 0x2) return false;
        at += more + 1;
    }
    return true;
}

}  // namespace

std::expected<Apart, std::string> carry_apart(std::span<std::uint8_t> wasm) {
    static constexpr std::array<std::uint8_t, 4> magic{0x00, 0x61, 0x73, 0x6d};
    if (wasm.size() < 8 || !std::ranges::equal(wasm.first(4), magic)) return std::unexpected("not a WebAssembly binary");
    Apart apart;
    Bytes bytes{wasm, 8};
    while (bytes.good && bytes.at < wasm.size()) {
        const std::uint8_t id = bytes.byte();
        const std::uint64_t size = bytes.number();
        if (!bytes.good || size > wasm.size() - bytes.at) return std::unexpected("its sections are cut short");
        if (id != 11) {  // not the data section
            bytes.skip(size);
            continue;
        }
        Bytes section{wasm.subspan(bytes.at, static_cast<std::size_t>(size))};
        for (std::uint64_t count = section.number(), index = 0; section.good && index < count; ++index) {
            const std::uint64_t flags = section.number();
            std::optional<std::uint32_t> address;
            if (flags == 0 || flags == 2) {
                if (flags == 2) (void)section.number();  // its memory
                // The offset: `i32.const N; end` at a constant address; anything else is not one.
                if (section.byte() == 0x41) {
                    std::int64_t value = 0;
                    int shift = 0;
                    std::uint8_t next = 0;
                    do {
                        next = section.byte();
                        value |= static_cast<std::int64_t>(next & 0x7f) << shift;
                        shift += 7;
                    } while (section.good && (next & 0x80) && shift < 64);
                    if (shift < 64 && (next & 0x40)) value |= -(std::int64_t{1} << shift);
                    if (section.byte() == 0x0b && value >= 0 && value <= std::numeric_limits<std::uint32_t>::max())
                        address = static_cast<std::uint32_t>(value);
                } else {
                    return std::unexpected(std::format("data segment {}: an offset that is not a constant (a relocatable program)", index));
                }
            } else if (flags != 1) {
                return std::unexpected(std::format("data segment {}: flags {} this reader does not know", index, flags));
            }
            const std::uint64_t length = section.number();
            if (!section.good || length > section.data.size() - section.at) return std::unexpected("its data section is cut short");
            const std::span<std::uint8_t> data = wasm.subspan(bytes.at + section.at, static_cast<std::size_t>(length));
            section.skip(length);
            if (!address) continue;  // passive: copied where the program says, not here
            for (const mlibs::apart::Region& region : mlibs::apart::regions(data)) {
                const std::string_view text(reinterpret_cast<const char*>(data.data()) + region.at, region.length);
                if (!utf8_text(text)) return std::unexpected(std::format("a region of `{}` at {} is not UTF-8 text", region.kind, *address + region.at));
                apart.kinds[region.kind].emplace(static_cast<std::uint32_t>(*address + region.at), std::string(text));
                apart.bytes += region.length;
                std::ranges::fill(data.subspan(region.mark, region.at + region.length - region.mark), std::uint8_t{0});
            }
        }
        if (!section.good) return std::unexpected("its data section is cut short");
        bytes.skip(size);
    }
    if (!bytes.good) return std::unexpected("its sections are cut short");
    return apart;
}

std::vector<std::pair<std::size_t, std::size_t>> call_edges(std::string_view dot) {
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    // A defined function's node: its index among those the program defines, quoted ("12"); an import's is not a number.
    const auto defined = [](std::string_view quoted) -> std::optional<std::size_t> {
        if (quoted.size() < 3 || quoted.front() != '"' || quoted.back() != '"') return std::nullopt;
        std::size_t index = 0;
        const std::string_view digits = quoted.substr(1, quoted.size() - 2);
        const auto [end, failure] = std::from_chars(digits.data(), digits.data() + digits.size(), index);
        if (failure != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;
        return index;
    };
    for (const auto line : std::views::split(dot, '\n')) {
        const std::string_view text(line.begin(), line.end());
        const std::size_t arrow = text.find(" -> ");
        if (arrow == std::string_view::npos) continue;
        const std::size_t from = text.find_first_not_of(' ');
        const std::size_t end = text.find(';', arrow);
        if (from == std::string_view::npos || end == std::string_view::npos || from >= arrow) continue;
        const auto caller = defined(text.substr(from, arrow - from));
        const auto callee = defined(text.substr(arrow + 4, end - arrow - 4));
        if (caller && callee) edges.emplace_back(*caller, *callee);
    }
    return edges;
}

std::string_view linked_name(std::string_view name) noexcept {
    const std::size_t cut = name.find_last_not_of("0123456789");
    if (cut == std::string_view::npos || cut + 1 == name.size() || name[cut] != '_' || cut == 0) return name;
    return name.substr(0, cut);
}

std::map<std::size_t, std::uint64_t> body_hashes(std::string_view printed, const std::map<std::size_t, std::string>& names, std::size_t imports) {
    // A function's own index after `$` (`$12`), or an import's (`$fimport$3`): the name it is linked by.
    const auto callee = [&](std::string_view reference) -> std::string_view {
        std::size_t index = 0;
        std::size_t base = 0;
        if (reference.starts_with("fimport$")) reference.remove_prefix(8);
        else base = imports;
        const auto [end, failure] = std::from_chars(reference.data(), reference.data() + reference.size(), index);
        if (failure != std::errc{} || end != reference.data() + reference.size()) return reference;
        const auto found = names.find(index + base);
        return found == names.end() ? std::string_view("?") : linked_name(found->second);
    };
    std::map<std::size_t, std::uint64_t> out;
    std::optional<std::size_t> current;
    std::string body;
    const auto finish = [&] {
        if (current) out[*current] = std::hash<std::string>{}(body);
        current.reset();
        body.clear();
    };
    for (const auto piece : std::views::split(printed, '\n')) {
        std::string_view line(piece.begin(), piece.end());
        if (line.starts_with(" (func $")) {
            finish();
            std::size_t index = 0;
            const std::string_view rest = line.substr(8);
            if (const auto [end, failure] = std::from_chars(rest.data(), rest.data() + rest.size(), index); failure == std::errc{} && end != rest.data())
                current = index;
            continue;
        }
        if (!current || line.empty()) continue;
        if (line.starts_with(" (")) {  // the next of the module's fields: the function ended
            finish();
            continue;
        }
        // The line with what two links make differently taken out: a callee by name, a constant, a type's index.
        for (std::size_t at = 0; at < line.size();) {
            const auto mark = line.find_first_of("($", at);
            if (mark == std::string_view::npos) {
                body.append(line.substr(at));
                break;
            }
            body.append(line.substr(at, mark - at));
            const std::string_view rest = line.substr(mark);
            std::size_t taken = 1;
            for (const std::string_view call : {"(call $", "(return_call $", "(ref.func $"}) {
                if (!rest.starts_with(call)) continue;
                const std::size_t end = std::min(rest.find_first_of(" )", call.size()), rest.size());
                body.append(call.substr(0, call.size() - 1)).append(callee(rest.substr(call.size(), end - call.size())));
                taken = end;
                break;
            }
            if (taken == 1 && (rest.starts_with("(i32.const ") || rest.starts_with("(i64.const ") || rest.starts_with("(f32.const ") ||
                               rest.starts_with("(f64.const ") || rest.starts_with("(type $"))) {
                const std::size_t space = rest.find(' ');
                const std::size_t end = std::min(rest.find(')', space), rest.size());
                body.append(rest.substr(0, space)).append(" #");
                taken = end;
            } else if (taken == 1) {
                body.push_back(rest.front());
            }
            at = mark + taken;
        }
        body.push_back('\n');
    }
    finish();
    return out;
}

std::set<std::size_t> first_part(const std::map<std::size_t, std::string>& whole, const std::set<std::string>& first, std::size_t imports,
                                 std::span<const std::pair<std::size_t, std::size_t>> edges, const std::map<std::size_t, std::uint64_t>& whole_bodies,
                                 const std::set<std::uint64_t>& first_bodies) {
    std::set<std::size_t> kept;
    for (const auto& [index, name] : whole)
        if (index >= imports && first.contains(std::string(linked_name(name)))) kept.insert(index - imports);
    for (const auto& [index, body] : whole_bodies)
        if (first_bodies.contains(body)) kept.insert(index);
    std::map<std::size_t, std::vector<std::size_t>> callees;
    for (const auto& [caller, callee] : edges) callees[caller].push_back(callee);
    std::vector<std::size_t> waiting(kept.begin(), kept.end());
    while (!waiting.empty()) {
        const std::size_t caller = waiting.back();
        waiting.pop_back();
        if (const auto found = callees.find(caller); found != callees.end())
            for (const std::size_t callee : found->second)
                if (kept.insert(callee).second) waiting.push_back(callee);
    }
    return kept;
}

std::span<const std::string_view> emscripten_features() noexcept {
    static constexpr std::array<std::string_view, 8> features{"--enable-bulk-memory",
                                                              "--enable-bulk-memory-opt",
                                                              "--enable-nontrapping-float-to-int",
                                                              "--enable-sign-ext",
                                                              "--enable-mutable-globals",
                                                              "--enable-multivalue",
                                                              "--enable-reference-types",
                                                              "--enable-call-indirect-overlong"};
    return features;
}

std::optional<fs::path> binaryen_directory() {
    const auto home = mcpplibs::os::dirs::home();
    if (!home) return std::nullopt;
    // Version order: runs of digits compared as numbers, the rest as text.
    const auto parts = [](const std::string& text) {
        std::vector<std::pair<bool, std::string>> out;
        for (std::size_t at = 0; at < text.size();) {
            const bool digits = std::isdigit(static_cast<unsigned char>(text[at])) != 0;
            std::size_t end = at;
            while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) != 0) == digits) ++end;
            out.emplace_back(digits, text.substr(at, end - at));
            at = end;
        }
        return out;
    };
    const auto older = [&](const fs::path& left, const fs::path& right) {
        const auto a = parts(utf8(left.parent_path().filename())), b = parts(utf8(right.parent_path().filename()));
        return std::ranges::lexicographical_compare(a, b, [](const auto& x, const auto& y) {
            if (x.first && y.first && x.second.size() != y.second.size()) return x.second.size() < y.second.size();
            return x.second < y.second;
        });
    };
    std::vector<fs::path> found;
    std::error_code error;
    for (const fs::path packages : {*home / ".mcpp/registry/data/xpkgs/xim-x-emsdk", *home / ".xlings/data/xpkgs/xim-x-emsdk"})
        for (const auto& entry : fs::directory_iterator(packages, error)) {
            const fs::path bin = entry.path() / "bin";
            if (fs::exists(bin / "wasm-split", error) && fs::exists(bin / "wasm-opt", error)) found.push_back(bin);
            else if (fs::exists(bin / "wasm-split.exe", error) && fs::exists(bin / "wasm-opt.exe", error)) found.push_back(bin);
        }
    if (found.empty()) return std::nullopt;
    return *std::ranges::max_element(found, older);
}

std::expected<PartReport, std::string> cut_part(const PartCut& cut) {
    const auto whole = read(cut.whole);
    if (!whole) return std::unexpected(whole.error());
    const auto imports = function_imports(std::span(reinterpret_cast<const std::uint8_t*>(whole->data()), whole->size()));
    if (!imports) return std::unexpected(std::format("{}: {}", utf8(cut.whole), imports.error()));
    const auto whole_text = read(cut.whole_names);
    if (!whole_text) return std::unexpected(std::format("{} (the whole program's symbol map: --emit-symbol-map)", whole_text.error()));
    const auto first_text = read(cut.first_names);
    if (!first_text) return std::unexpected(std::format("{} (the first part's program's symbol map)", first_text.error()));
    const std::map<std::size_t, std::string> names = symbol_map(*whole_text);
    const std::map<std::size_t, std::string> first_names = symbol_map(*first_text);
    std::set<std::string> first;
    for (const auto& [index, name] : first_names) first.emplace(linked_name(name));
    if (names.empty() || first.empty()) return std::unexpected("a symbol map names no function");
    fs::path first_program = cut.first_names;
    if (first_program.extension() == ".symbols") first_program.replace_extension();  // <program>.js.symbols: <program>.js
    first_program.replace_extension(".wasm");
    const auto first_wasm = read(first_program);
    if (!first_wasm) return std::unexpected(std::format("{} (the first part's program, beside its symbol map)", first_wasm.error()));
    const auto first_imports = function_imports(std::span(reinterpret_cast<const std::uint8_t*>(first_wasm->data()), first_wasm->size()));
    if (!first_imports) return std::unexpected(std::format("{}: {}", utf8(first_program), first_imports.error()));

    fs::path tools = cut.binaryen;
    if (tools.empty()) {
        const auto found = binaryen_directory();
        if (!found) return std::unexpected("no wasm-split and wasm-opt (Binaryen, in the emsdk mcpp installs)");
        tools = *found;
    }
    const bool windows = fs::exists(tools / "wasm-split.exe");
    const auto tool = [&](std::string_view name) { return tools / (std::string(name) + (windows ? ".exe" : "")); };
    const auto with_features = [](std::vector<std::string> args) {
        std::vector<std::string> all(emscripten_features().begin(), emscripten_features().end());
        all.insert(all.end(), std::make_move_iterator(args.begin()), std::make_move_iterator(args.end()));
        return all;
    };
    // The whole's direct calls, by Binaryen's own reading of it.
    const auto graph = process::run({.program = tool("wasm-opt"), .args = with_features({utf8(cut.whole), "--print-call-graph"})});
    if (!graph || !graph->status.success())
        return std::unexpected(std::format("wasm-opt --print-call-graph failed: {}", graph ? graph->err : graph.error().message()));
    const auto edges = call_edges(graph->out);
    // Each program's functions by body, as Binaryen prints them.
    const auto printed = [&](const fs::path& program) { return process::run({.program = tool("wasm-opt"), .args = with_features({utf8(program), "--print"})}); };
    const auto whole_printed = printed(cut.whole);
    const auto first_printed = printed(first_program);
    for (const auto* run : {&whole_printed, &first_printed})
        if (!*run || !(*run)->status.success()) return std::unexpected(std::format("wasm-opt --print failed: {}", *run ? (*run)->err : run->error().message()));
    const auto whole_bodies = body_hashes(whole_printed->out, names, *imports);
    std::set<std::uint64_t> first_bodies;
    for (const auto& [index, body] : body_hashes(first_printed->out, first_names, *first_imports)) first_bodies.insert(body);
    const std::set<std::size_t> kept = first_part(names, first, *imports, edges, whole_bodies, first_bodies);

    PartReport report;
    report.first = kept.size();
    std::set<std::size_t> by_name;
    for (const auto& [index, name] : names)
        if (index >= *imports && (++report.functions, first.contains(std::string(linked_name(name))))) by_name.insert(index - *imports);
    report.named = by_name.size();
    for (const auto& [index, body] : whole_bodies)
        if (!by_name.contains(index) && first_bodies.contains(body)) ++report.bodies;
    // wasm-split names a function the binary does not by its index among those it defines.
    std::string listed;
    for (const std::size_t index : kept) listed += std::format("{}\n", index);
    std::error_code error;
    fs::create_directories(cut.primary.parent_path(), error);
    fs::path keep = cut.primary;
    keep += ".keep";
    if (std::ofstream out(keep, std::ios::binary | std::ios::trunc); !(out << listed)) return std::unexpected(std::format("{}: cannot write", utf8(keep)));
    const auto split = process::run({.program = tool("wasm-split"),
                                     .args = with_features({"--split", "--keep-funcs=@" + utf8(keep), "--export-prefix=%", "--placeholder-namespace=placeholder",
                                                            utf8(cut.whole), "-o1", utf8(cut.primary), "-o2", utf8(cut.deferred)})});
    fs::remove(keep, error);
    if (!split || !split->status.success())
        return std::unexpected(std::format("wasm-split failed: {}", split ? split->err + split->out : split.error().message()));
    return report;
}

}  // namespace omui::gen
