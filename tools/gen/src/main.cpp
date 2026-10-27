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
//   omui-gen check <file.contract.omui.toml>... --registry <dir> [--json]
//       diagnostics only
//   omui-gen abi <registry-dir> --out <runtime/abi/src> [--check]
//       openmui.h's types and functions for the C++ that implements them, and
//       the check that their layouts agree with the header
//   omui-gen unicode <ucd-dir> --version <x.y.z> --out <dir> [--tests <dir>] [--check]
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
    std::string                        part;      // cpp: only `contract` or `builder`
    std::string                        version;
    std::filesystem::path              tests;
    bool                               check = false;
    bool                               json = false;
};

int usage() {
    std::println(std::cerr, "usage: omui-gen registry <registry-dir> --out <spec-dir> [--check] [--json]\n"
                            "       omui-gen stubs <contract>... --registry <dir> --module <name> --out <dir> [--check] [--json]\n"
                            "       omui-gen cpp <contract>... --registry <dir> --module <name> --out <dir> [--namespace ns] [--catalog]\n"
                            "                    [--contract name] [--part contract|builder] [--check]\n"
                            "       omui-gen check <contract>... --registry <dir> [--json]\n"
                            "       omui-gen abi <registry-dir> --out <runtime/abi/src> [--check]\n"
                            "       omui-gen unicode <ucd-dir> --version <x.y.z> --out <dir> [--tests <dir>] [--check]");
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
        else if (a == "--part") { auto v = value(); if (!v || (*v != "contract" && *v != "builder")) return std::nullopt; o.part = *v; }
        else if (a == "--version") { auto v = value(); if (!v) return std::nullopt; o.version = *v; }
        else if (a == "--tests") { auto v = value(); if (!v) return std::nullopt; o.tests = *v; }
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

int run_contracts(const Options& o, bool emit) {
    if (o.inputs.empty() || o.registry.empty()) return usage();
    if (emit && (o.module.empty() || o.out.empty())) return usage();
    Diagnostics diags;
    Registry reg = read_registry(o.registry, diags);
    std::vector<Contract> contracts;
    for (const auto& file : o.inputs)
        for (Contract& c : read_contracts(file, reg, diags)) contracts.push_back(std::move(c));
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
    if (options->command == "check") return run_contracts(*options, false);
    if (options->command == "unicode") return run_unicode(*options);
    if (options->command == "abi") return run_abi(*options);
    return usage();
}
