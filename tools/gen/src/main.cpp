// omui-gen — the registry and contracts, to every generated artifact.
//
//   omui-gen registry <spec/registry> --out <spec> [--check] [--json]
//       openmui.h, the openmui.spec partitions, the reference tables, the Core
//       builders for TS / Python / Lua and the view-document schema
//   omui-gen stubs <file.contract.omui.toml>... --registry <dir> --module <m>
//                  --out <dir> [--check] [--json]
//       a component package's script builders and view-document schema
//   omui-gen cpp <file.contract.omui.toml>... --registry <dir> --module <m>
//                --out <dir> [--namespace ns] [--catalog] [--contract name]
//                [--part contract|builder] [--check]
//       a component package's C++: the contract partitions (typed props,
//       options, the CRTP base) and the builders
//   omui-gen native <file.native.omui.toml>... --registry <dir> --module <m>
//                   --out <dir> [--namespace ns] [--native name] [--part cpp|stubs] [--check]
//       a native module's C++ (records, the functions to implement, define())
//       and its TS / Python / Lua stubs (component-binding §8)
//   omui-gen embed <file> --module <m> --name <partition> --out <dir> [--namespace ns] [--check]
//       a data file (a theme) as a partition holding its text
//   omui-gen check <file.contract.omui.toml or .native.omui.toml>... --registry <dir> [--json]
//       diagnostics only
//   omui-gen abi <registry-dir> --out <modules/ui/abi/src> [--check]
//       openmui.h's types and functions for the C++ that implements them, and
//       the check that their layouts agree with the header
//   omui-gen unicode <ucd-dir> --version <x.y.z> --out <dir> [--tests <dir>] [--check]
//   omui-gen cldr <cldr-json-dir> --locales en,zh,zh-Hant --version <x.y.z> --out <dir> [--check]
//       openmui.i18n's locale packs (*.locale.omui.json) and likely subtags
//       openmui.unicode's tables (and a subset of the UCD's conformance tests)
//
// --check writes nothing and fails (exit 1) when an output differs from what
// is on disk: CI's proof that committed outputs are current. --json prints
// diagnostics as JSON for tools and agents.
import std;
import openmui.gen.model;
import openmui.gen.read;
import openmui.gen.emit;
import openmui.gen.unicode;
import openmui.gen.cldr;

using namespace omui::gen;

namespace {

struct Options {
    std::string                        command;
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path              out;
    std::filesystem::path              registry;
    std::string                        module;
    std::string                        ns = "omui::ui";
    bool                               catalog = false;
    std::string                        contract;  // cpp: only this contract of the files
    std::string                        native;    // native: only this module of the files
    std::string                        name;      // embed: the partition's and the constant's name
    std::string                        part;      // cpp: `contract` or `builder`; native: `cpp` or `stubs`
    std::string                        version;
    std::filesystem::path              tests;
    std::vector<std::string>           locales;  // cldr: the CLDR locale ids to pack
    bool                               check = false;
    bool                               json = false;
};

int usage() {
    std::println(std::cerr, "usage: omui-gen registry <registry-dir> --out <spec-dir> [--check] [--json]\n"
                            "       omui-gen stubs <contract>... --registry <dir> --module <name> --out <dir> [--check] [--json]\n"
                            "       omui-gen cpp <contract>... --registry <dir> --module <name> --out <dir> [--namespace ns] [--catalog]\n"
                            "                    [--contract name] [--part contract|builder] [--check]\n"
                            "       omui-gen native <native>... --registry <dir> --module <name> --out <dir> [--namespace ns]\n"
                            "                    [--native name] [--part cpp|stubs] [--check]\n"
                            "       omui-gen embed <file> --module <name> --name <partition> --out <dir> [--namespace ns] [--check]\n"
                            "       omui-gen check <contract or native>... --registry <dir> [--json]\n"
                            "       omui-gen abi <registry-dir> --out <modules/ui/abi/src> [--check]\n"
                            "       omui-gen unicode <ucd-dir> --version <x.y.z> --out <dir> [--tests <dir>] [--check]\n"
                            "       omui-gen cldr <cldr-json-dir> --locales <id,…> --version <x.y.z> --out <dir> [--check]");
    return 2;
}

std::optional<Options> parse_args(int argc, char** argv) {
    if (argc < 2) return std::nullopt;
    Options o;
    o.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string_view a = argv[i];
        auto value = [&]() -> std::optional<std::string> {
            if (i + 1 >= argc) return std::nullopt;
            return std::string(argv[++i]);
        };
        if (a == "--check") o.check = true;
        else if (a == "--json") o.json = true;
        else if (a == "--out") { auto v = value(); if (!v) return std::nullopt; o.out = *v; }
        else if (a == "--registry") { auto v = value(); if (!v) return std::nullopt; o.registry = *v; }
        else if (a == "--module") { auto v = value(); if (!v) return std::nullopt; o.module = *v; }
        else if (a == "--namespace") { auto v = value(); if (!v) return std::nullopt; o.ns = *v; }
        else if (a == "--catalog") o.catalog = true;
        else if (a == "--contract") { auto v = value(); if (!v) return std::nullopt; o.contract = *v; }
        else if (a == "--native") { auto v = value(); if (!v) return std::nullopt; o.native = *v; }
        else if (a == "--name") { auto v = value(); if (!v) return std::nullopt; o.name = *v; }
        else if (a == "--part") {
            auto v = value();
            if (!v || (*v != "contract" && *v != "builder" && *v != "cpp" && *v != "stubs")) return std::nullopt;
            o.part = *v;
        }
        else if (a == "--version") { auto v = value(); if (!v) return std::nullopt; o.version = *v; }
        else if (a == "--tests") { auto v = value(); if (!v) return std::nullopt; o.tests = *v; }
        else if (a == "--locales") {
            auto v = value();
            if (!v) return std::nullopt;
            for (std::size_t from = 0; from <= v->size();) {
                const std::size_t comma = std::min(v->find(',', from), v->size());
                if (comma > from) o.locales.push_back(v->substr(from, comma - from));
                from = comma + 1;
            }
        }
        else if (a.starts_with("--")) return std::nullopt;
        else o.inputs.emplace_back(a);
    }
    return o;
}

// Writes outputs, or under --check compares them; counts what differs.
struct Writer {
    bool check;
    int  stale = 0;
    int  written = 0;

    void put(const std::filesystem::path& path, std::string_view content) {
        std::string current;
        if (std::ifstream in{path, std::ios::binary}) current.assign(std::istreambuf_iterator<char>(in), {});
        if (current == content) return;
        if (check) {
            ++stale;
            std::println(std::cerr, "out of date: {}", generic_utf8(path));
            return;
        }
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << content;
        ++written;
    }
};

int report(const Diagnostics& diags, bool json) {
    if (json) std::println("{}", diags.json());
    else if (!diags.list.empty()) std::print(std::cerr, "{}", diags.text());
    return diags.has_errors() ? 1 : 0;
}

int run_registry(const Options& o) {
    if (o.inputs.size() != 1 || o.out.empty()) return usage();
    Diagnostics diags;
    Registry reg = read_registry(o.inputs[0], diags);
    if (report(diags, o.json) != 0) return 1;

    Writer w{o.check};
    w.put(o.out / "include" / "openmui.h", emit_c_header(reg));
    w.put(o.out / "src" / "generated" / "registry.cppm", emit_spec_registry(reg));
    w.put(o.out / "src" / "generated" / "catalog.cppm", emit_spec_catalog(reg));
    w.put(o.out / "docs" / "registry.md", emit_docs(reg));
    w.put(o.out / "stubs" / "core.d.ts", emit_dts(reg.contracts, "the Core profile"));
    w.put(o.out / "stubs" / "core.pyi", emit_pyi(reg.contracts, "the Core profile"));
    w.put(o.out / "stubs" / "core.lua", emit_lua(reg.contracts, "the Core profile"));
    w.put(o.out / "schema" / "core.components.json", emit_view_schema(reg.contracts, "the Core profile"));
    if (o.check) {
        if (w.stale) {
            std::println(std::cerr, "{} generated file(s) are out of date; run: omui-gen registry {} --out {}", w.stale,
                         generic_utf8(o.inputs[0]), generic_utf8(o.out));
            return 1;
        }
        std::println("omui-gen: generated files are current");
    } else {
        std::println("omui-gen: {} file(s) written", w.written);
    }
    return 0;
}

int run_abi(const Options& o) {
    if (o.inputs.size() != 1 || o.out.empty()) return usage();
    Diagnostics diags;
    Registry reg = read_registry(o.inputs[0], diags);
    if (report(diags, o.json) != 0) return 1;
    Writer w{o.check};
    w.put(o.out / "generated" / "c.cppm", emit_abi_types(reg));
    w.put(o.out / "generated" / "api.cppm", emit_abi_functions(reg));
    w.put(o.out / "native" / "layout.cpp", emit_abi_layout(reg));
    if (o.check && w.stale) {
        std::println(std::cerr, "{} generated file(s) are out of date; run: omui-gen abi {} --out {}", w.stale, generic_utf8(o.inputs[0]),
                     generic_utf8(o.out));
        return 1;
    }
    std::println("omui-gen: {} file(s) written", w.written);
    return 0;
}

// CLDR's JSON to openmui.i18n's locale packs (D-74).
int run_cldr(const Options& o) {
    if (o.inputs.size() != 1 || o.out.empty() || o.version.empty() || o.locales.empty()) return usage();
    auto packs = emit_cldr_packs(o.inputs[0], o.locales, o.version);
    if (!packs) {
        std::println(std::cerr, "omui-gen: {}", packs.error());
        return 1;
    }
    Writer w{o.check};
    for (const CldrPack& p : *packs) w.put(o.out / p.file, p.text);
    if (o.check && w.stale) return 1;
    std::println("omui-gen: {} file(s) written", w.written);
    return 0;
}

int run_unicode(const Options& o) {
    if (o.inputs.size() != 1 || o.out.empty() || o.version.empty()) return usage();
    auto tables = emit_unicode_tables(o.inputs[0], o.version);
    if (!tables) {
        std::println(std::cerr, "omui-gen: {}", tables.error());
        return 1;
    }
    Writer w{o.check};
    w.put(o.out / "tables.cppm", *tables);
    if (!o.tests.empty()) {
        for (auto [file, step] : {std::pair{"LineBreakTest.txt", 4}, std::pair{"GraphemeBreakTest.txt", 1},
                                  std::pair{"WordBreakTest.txt", 1}}) {
            auto subset = unicode_test_subset(o.inputs[0] / file, step);
            if (!subset) {
                std::println(std::cerr, "omui-gen: {}", subset.error());
                return 1;
            }
            w.put(o.tests / file, *subset);
        }
    }
    if (o.check && w.stale) return 1;
    std::println("omui-gen: {} file(s) written", w.written);
    return 0;
}

// C++ for a component package: per contract the partition <name>_contract.cppm
// and the builder <name>_builder.cpp.
int run_cpp(const Options& o) {
    if (o.inputs.empty() || o.registry.empty() || o.module.empty() || o.out.empty()) return usage();
    Diagnostics diags;
    Registry reg = read_registry(o.registry, diags);
    std::vector<Contract> contracts;
    for (const auto& file : o.inputs)
        for (Contract& c : read_contracts(file, reg, diags)) contracts.push_back(std::move(c));
    if (report(diags, o.json) != 0) return 1;
    const CppOptions cpp{o.module, o.ns, o.catalog};
    Writer w{o.check};
    int matched = 0;
    for (const Contract& c : contracts) {
        if (!o.contract.empty() && c.name != o.contract) continue;
        ++matched;
        if (o.part != "builder") w.put(o.out / (c.name + "_contract.cppm"), emit_cpp_contract(c, reg, cpp));
        if (o.part != "contract") w.put(o.out / (c.name + "_builder.cpp"), emit_cpp_builder(c, reg, cpp));
    }
    if (!o.contract.empty() && matched == 0) {
        std::println(std::cerr, "omui-gen: no contract `{}` in the files given", o.contract);
        return 1;
    }
    if (o.check) {
        if (w.stale) return 1;
        std::println("omui-gen: generated files are current");
    } else {
        std::println("omui-gen: {} file(s) written", w.written);
    }
    return 0;
}

bool is_native(const std::filesystem::path& file) { return generic_utf8(file).ends_with(".native.omui.toml"); }

// A native module's C++ (<name>_native.cppm) and the stubs of the files'
// modules: <module>.natives.d.ts, <name>.pyi, <module>.natives.lua.
int run_native(const Options& o) {
    if (o.inputs.empty() || o.registry.empty() || o.module.empty() || o.out.empty()) return usage();
    Diagnostics diags;
    Registry reg = read_registry(o.registry, diags);
    std::vector<std::pair<NativeModule, std::string>> modules;  // the module, the file it is from
    for (const auto& file : o.inputs)
        for (NativeModule& m : read_natives(file, reg, diags)) modules.emplace_back(std::move(m), generic_utf8(file.filename()));
    if (report(diags, o.json) != 0) return 1;
    // The package's namespace: --namespace, else its module's (acme.finder → acme::finder).
    std::string ns = o.ns;
    if (ns == "omui::ui") {
        ns.clear();
        for (char c : o.module) ns += c == '.' ? std::string("::") : std::string(1, c);
    }
    const CppOptions cpp{o.module, ns, false};
    Writer w{o.check};
    std::vector<NativeModule> selected;
    for (auto& [m, source] : modules) {
        if (!o.native.empty() && m.name != o.native) continue;
        if (o.part != "stubs") w.put(o.out / (m.name + "_native.cppm"), emit_cpp_native(m, cpp, source));
        if (o.part != "cpp") w.put(o.out / (m.name + ".pyi"), emit_native_pyi(m, o.module));
        selected.push_back(m);
    }
    if (!o.native.empty() && selected.empty()) {
        std::println(std::cerr, "omui-gen: no native module `{}` in the files given", o.native);
        return 1;
    }
    if (o.part != "cpp") {
        w.put(o.out / (o.module + ".natives.d.ts"), emit_native_dts(selected, o.module));
        w.put(o.out / (o.module + ".natives.lua"), emit_native_lua(selected, o.module));
    }
    if (o.check) {
        if (w.stale) return 1;
        std::println("omui-gen: generated files are current");
    } else {
        std::println("omui-gen: {} file(s) written", w.written);
    }
    return 0;
}

// A data file as a partition <module>:<name> with its text (a theme a
// package carries, a document).
int run_embed(const Options& o) {
    if (o.inputs.size() != 1 || o.module.empty() || o.name.empty() || o.out.empty()) return usage();
    std::ifstream in(o.inputs[0], std::ios::binary);
    if (!in) {
        std::println(std::cerr, "omui-gen: cannot read {}", generic_utf8(o.inputs[0]));
        return 1;
    }
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    std::string ns = o.ns;
    if (ns == "omui::ui") {
        ns.clear();
        for (char c : o.module) ns += c == '.' ? std::string("::") : std::string(1, c);
    }
    Writer w{o.check};
    w.put(o.out / (o.name + ".cppm"), emit_embed(text, generic_utf8(o.inputs[0].filename()), o.module, ns, o.name));
    if (o.check) {
        if (w.stale) return 1;
        std::println("omui-gen: generated files are current");
    } else {
        std::println("omui-gen: {} file(s) written", w.written);
    }
    return 0;
}

int run_contracts(const Options& o, bool emit) {
    if (o.inputs.empty() || o.registry.empty()) return usage();
    if (emit && (o.module.empty() || o.out.empty())) return usage();
    Diagnostics diags;
    Registry reg = read_registry(o.registry, diags);
    std::vector<Contract> contracts;
    for (const auto& file : o.inputs) {
        if (is_native(file)) {
            (void)read_natives(file, reg, diags);  // checked: their code is `omui-gen native`'s
            continue;
        }
        for (Contract& c : read_contracts(file, reg, diags)) contracts.push_back(std::move(c));
    }
    if (report(diags, o.json) != 0 || !emit) return diags.has_errors() ? 1 : 0;

    Writer w{o.check};
    w.put(o.out / (o.module + ".d.ts"), emit_dts(contracts, o.module));
    w.put(o.out / (o.module + ".pyi"), emit_pyi(contracts, o.module));
    w.put(o.out / (o.module + ".lua"), emit_lua(contracts, o.module));
    w.put(o.out / (o.module + ".components.json"), emit_view_schema(contracts, o.module));
    if (o.check && w.stale) return 1;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    auto options = parse_args(argc, argv);
    if (!options) return usage();
    if (options->command == "registry") return run_registry(*options);
    if (options->command == "stubs") return run_contracts(*options, true);
    if (options->command == "cpp") return run_cpp(*options);
    if (options->command == "native") return run_native(*options);
    if (options->command == "embed") return run_embed(*options);
    if (options->command == "check") return run_contracts(*options, false);
    if (options->command == "unicode") return run_unicode(*options);
    if (options->command == "cldr") return run_cldr(*options);
    if (options->command == "abi") return run_abi(*options);
    return usage();
}
