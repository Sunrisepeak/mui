// omui-gen's parts (a world in parts): a binary's function imports counted, Binaryen's call graph read, and the
// first part — what a sibling program has by name, and what that calls directly — on data made here.
import std;
import omui.gen.part;
import mlibs.apart;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

void leb(std::vector<std::uint8_t>& out, std::uint64_t value) {
    do {
        std::uint8_t low = value & 0x7f;
        value >>= 7;
        out.push_back(value ? low | 0x80 : low);
    } while (value);
}

void name(std::vector<std::uint8_t>& out, std::string_view text) {
    leb(out, text.size());
    out.insert(out.end(), text.begin(), text.end());
}

// Imports of every kind: two functions, a table, a memory, a global and a tag; then a function section of its own.
std::vector<std::uint8_t> program() {
    std::vector<std::uint8_t> wasm{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    const auto section = [&](std::uint8_t id, const std::vector<std::uint8_t>& body) {
        wasm.push_back(id);
        leb(wasm, body.size());
        wasm.insert(wasm.end(), body.begin(), body.end());
    };
    section(1, {0x01, 0x60, 0x00, 0x00});  // one type: () -> ()
    std::vector<std::uint8_t> imports{0x06};
    name(imports, "env"), name(imports, "now"), imports.insert(imports.end(), {0x00, 0x00});         // a function
    name(imports, "env"), name(imports, "table"), imports.insert(imports.end(), {0x01, 0x70, 0x00, 0x10});  // a table
    name(imports, "env"), name(imports, "memory"), imports.insert(imports.end(), {0x02, 0x01, 0x01, 0x80, 0x01});  // a memory
    name(imports, "env"), name(imports, "stack"), imports.insert(imports.end(), {0x03, 0x7f, 0x01});  // a global
    name(imports, "env"), name(imports, "thrown"), imports.insert(imports.end(), {0x04, 0x00, 0x00});  // a tag
    name(imports, "env"), name(imports, "later"), imports.insert(imports.end(), {0x00, 0x00});       // a function
    section(2, imports);
    section(3, {0x02, 0x00, 0x00});  // two functions of its own
    return wasm;
}

}  // namespace

static UTest cases{"omui.gen.part", [] {
    "a program's function imports are counted, past imports of every other kind"_test = [] {
        const auto wasm = program();
        const auto counted = gen::function_imports(wasm);
        expect(counted.has_value() && *counted == 2u) << (counted ? std::to_string(*counted) : counted.error());
        expect(!gen::function_imports(std::vector<std::uint8_t>{0x01, 0x02}).has_value()) << "not a WebAssembly binary";
        const std::vector<std::uint8_t> bare{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        expect(gen::function_imports(bare) == std::expected<std::size_t, std::string>(0)) << "no import section: none";
    };

    "what a program carries apart: each marked region by its kind and address, it and its mark zeroed, the rest as it was"_test = [] {
        const std::string wgsl = mlibs::apart::mark("wgsl", 6) + "fn a()";
        const std::string essl = mlibs::apart::mark("essl", 4) + "x=1;";
        const std::string first = "lead" + wgsl + "tail", second = essl;
        std::vector<std::uint8_t> data{0x03};
        const auto segment = [&](std::vector<std::uint8_t> head, const std::string& bytes) {
            data.insert(data.end(), head.begin(), head.end());
            leb(data, bytes.size());
            data.insert(data.end(), bytes.begin(), bytes.end());
        };
        segment({0x00, 0x41, 0x80, 0x08, 0x0b}, first);   // active, at 1024
        segment({0x01}, mlibs::apart::mark("wgsl", 1) + "p");  // passive: left as it is
        segment({0x02, 0x00, 0x41, 0xd0, 0x0f, 0x0b}, second);  // active in memory 0, at 2000
        std::vector<std::uint8_t> wasm = program();
        wasm.push_back(11);
        leb(wasm, data.size());
        const std::size_t data_at = wasm.size();
        wasm.insert(wasm.end(), data.begin(), data.end());
        const std::vector<std::uint8_t> before = wasm;
        const auto apart = gen::carry_apart(wasm);
        expect(apart.has_value()) << (apart ? "" : apart.error());
        if (!apart) return;
        const std::uint32_t wgsl_at = 1024 + 4 + static_cast<std::uint32_t>(wgsl.size() - 6);
        const std::uint32_t essl_at = 2000 + static_cast<std::uint32_t>(essl.size() - 4);
        expect(apart->kinds.size() == 2u && apart->kinds.at("wgsl").size() == 1u && apart->kinds.at("wgsl").at(wgsl_at) == "fn a()");
        expect(apart->kinds.at("essl").at(essl_at) == "x=1;");
        expect(eq(apart->bytes, std::size_t{10}));
        // The first segment: "lead", zeros where the mark and the text were, "tail".
        const std::size_t lead = data_at + 1 + 5 + 1;
        expect(std::string(wasm.begin() + static_cast<std::ptrdiff_t>(lead), wasm.begin() + static_cast<std::ptrdiff_t>(lead + 4)) == "lead");
        expect(std::all_of(wasm.begin() + static_cast<std::ptrdiff_t>(lead + 4), wasm.begin() + static_cast<std::ptrdiff_t>(lead + 4 + wgsl.size()),
                           [](std::uint8_t byte) { return byte == 0; }));
        expect(std::string(wasm.begin() + static_cast<std::ptrdiff_t>(lead + 4 + wgsl.size()), wasm.begin() + static_cast<std::ptrdiff_t>(lead + 8 + wgsl.size())) == "tail");
        std::size_t changed = 0;
        for (std::size_t at = 0; at < wasm.size(); ++at) changed += wasm[at] != before[at];
        expect(eq(wasm.size(), before.size()) && eq(changed, wgsl.size() + essl.size() - std::size_t{8})) << "only the regions and their marks (a mark's NULs were zero)";
        const auto again = gen::carry_apart(wasm);
        expect(again.has_value() && again->kinds.empty()) << "carried apart once: nothing left to carry";
        std::vector<std::uint8_t> not_wasm{0x01, 0x02};
        expect(!gen::carry_apart(not_wasm).has_value()) << "not a WebAssembly binary";
    };

    "Binaryen's call graph: the calls between functions the program defines, an import's left out"_test = [] {
        const std::string dot = "digraph call {\n  \"Import\" [style=\"filled\"];\n  \"A\" -> \"B\" [label = \"Direct Call\"];\n  \"0\" [style=\"filled\"];\n"
                                "  \"0\" -> \"7\"; // call\n  \"1\" -> \"fimport$4\"; // call\n  \"7\" -> \"2\"; // call\n}\n";
        const auto edges = gen::call_edges(dot);
        expect(edges == std::vector<std::pair<std::size_t, std::size_t>>{{0, 7}, {7, 2}}) << "the key's A -> B is no function's";
    };

    "the first part: what the sibling has by name, and what that calls directly in the whole"_test = [] {
        // Two imports (0, 1); the whole defines 2..7 — by its own index 0..5.
        const std::map<std::size_t, std::string> whole{{0, "now"}, {1, "later"}, {2, "main"}, {3, "read(page)"}, {4, "inlined there(x)"},
                                                       {5, "examples()"}, {6, "view document(x)"}, {7, "shared(y)"}};
        const std::set<std::string> first{"now", "main", "read(page)", "shared(y)"};
        // main -> read -> inlined there (the sibling inlined it); examples -> view document -> shared.
        const std::vector<std::pair<std::size_t, std::size_t>> edges{{0, 1}, {1, 2}, {3, 4}, {4, 5}};
        const auto kept = gen::first_part(whole, first, 2, edges);
        expect(kept == std::set<std::size_t>{0, 1, 2, 5}) << "an import is no defined function's index; what only the examples reach is the second part";
    };

    "a name as two links have it: the uniquing suffix dropped, a name that only ends in digits kept"_test = [] {
        expect(gen::linked_name("std::__function::__func<…>::~__func()_17692") == "std::__function::__func<…>::~__func()");
        expect(gen::linked_name("__cxx_global_array_dtor_23809") == "__cxx_global_array_dtor");
        expect(gen::linked_name("vec3") == "vec3" && gen::linked_name("f_") == "f_" && gen::linked_name("_12") == "_12");
    };

    "a body as two links have it: callees by name, constants and type indices left out"_test = [] {
        // The same code in two links: its callee at another index, a string's address elsewhere, another type index.
        const std::string one = " (type $0 (func))\n (func $0 (param $0 i32)\n  (call $2\n   (i32.const 1024)\n  )\n  (call_indirect $0 (type $3)\n   (local.get $0)\n  )\n )\n"
                                " (func $1\n  (call $fimport$0)\n )\n (func $2 (param $0 i32)\n  (drop\n   (local.get $0)\n  )\n )\n (export \"main\" (func $0))\n";
        const std::string two = " (func $0\n  (call $fimport$0)\n )\n (func $1 (param $0 i32)\n  (drop\n   (local.get $0)\n  )\n )\n (func $2 (param $0 i32)\n  (call $1\n   (i32.const 2048)\n  )\n"
                                "  (call_indirect $0 (type $7)\n   (local.get $0)\n  )\n )\n";
        const std::map<std::size_t, std::string> names_one{{0, "now"}, {1, "main"}, {2, "tick()"}, {3, "keep(int)_12"}};
        const std::map<std::size_t, std::string> names_two{{0, "now"}, {1, "tick()"}, {2, "keep(int)_40"}, {3, "main"}};
        const auto a = gen::body_hashes(one, names_one, 1), b = gen::body_hashes(two, names_two, 1);
        expect(a.size() == 3u && b.size() == 3u) << "the module's next field ends a function; an export is none";
        expect(a.at(0) == b.at(2) && a.at(1) == b.at(0) && a.at(2) == b.at(1)) << "the same code, linked twice";
        expect(a.at(0) != a.at(1) && a.at(1) != a.at(2)) << "other code is other";
        // Merged under another's name in the whole: kept by its body.
        const auto kept = gen::first_part({{1, "main"}, {2, "examples()"}}, {"main"}, 1, {}, {{1, 77}}, {77});
        expect(kept == std::set<std::size_t>{0, 1}) << "a function the sibling has by body, named for another";
    };

    "the features Binaryen reads and writes are Emscripten's, none newer"_test = [] {
        const auto features = gen::emscripten_features();
        expect(std::ranges::find(features, std::string_view("--enable-bulk-memory")) != features.end());
        expect(std::ranges::find(features, std::string_view("--enable-gc")) == features.end()) << "WasmGC: not what Emscripten's wasm32 output uses";
    };
}};
