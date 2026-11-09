// omui-gen — the registry and contracts, to every generated artifact.
//
//   omui-gen registry <spec/registry> --out <spec> [--check] [--json]
//       openmui.h, the openmui.spec partitions, the reference tables, the Core
//       TypeScript builders and the view-document schema
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
//       and its TypeScript stubs (component-binding §8)
//   omui-gen embed <file> --module <m> --name <partition> --out <dir> [--namespace ns] [--check]
//   omui-gen embed-index <file>=<partition>… --module <m> --name <partition> --out <dir> [--namespace ns] [--check]
//       a data file (a theme) as a partition holding its text
//   omui-gen check <file.contract.omui.toml or .native.omui.toml>... --registry <dir> [--json]
//       diagnostics only
//   omui-gen abi <registry-dir> --out <modules/projects/omui/abi/src> [--check]
//       openmui.h's types and functions for the C++ that implements them, and
//       the check that their layouts agree with the header
//   omui-gen unicode <ucd-dir> --version <x.y.z> --out <dir> [--tests <dir>] [--check]
//   omui-gen cldr <cldr-json-dir> --locales en,zh,zh-Hant --version <x.y.z> --out <dir> [--check]
//   omui-gen dist-toy <staged tree> --target <name> --out <dir> [--title T] [--fonts a,b|all] …
//       a wasm32-emscripten program packaged for Bilibili Toy (openmui.gen.dist_toy;
//       what `mcpp pack --pkg-format dist-toy` runs)
//   omui-gen dist-toy-split <package dir> --primary <wasm> --deferred <wasm> [--lazy <route>,…]
//       a packed page's program cut in two by wasm-split (tools/ci/site-split.sh):
//       the first screen's half first, the rest compiled after it
//   omui-gen wasm-size <file.wasm> [--json <out.json>] [--markdown <out.md>] [--top N]
//       where a WebAssembly program's bytes go: its sections, its code by owner
//       (module, namespace, C library) and its largest functions (W8;
//       openmui.gen.wasm_size — names from an analysis build's name section)
//       openmui.i18n's locale packs (*.locale.omui.json) and likely subtags
//       openmui.unicode's tables (and a subset of the UCD's conformance tests)
//
// --check writes nothing and fails (exit 1) when an output differs from what
// is on disk: CI's proof that committed outputs are current. --json prints
// diagnostics as JSON for tools and agents.
import std;
import mcpplibs.cmdline;
import omui.gen.model;
import omui.gen.read;
import omui.gen.emit;
import omui.gen.unicode;
import omui.gen.cldr;
import omui.gen.dist_toy;
import omui.gen.wasm_size;
import omui.gen.ops;

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
                            "       omui-gen embed-index <file>=<partition>... --module <name> --name <partition> --out <dir> [--namespace ns] [--check]\n"
                            "       omui-gen check <contract or native>... --registry <dir> [--json]\n"
                            "       omui-gen abi <registry-dir> --out <modules/projects/omui/abi/src> [--check]\n"
                            "       omui-gen unicode <ucd-dir> --version <x.y.z> --out <dir> [--tests <dir>] [--check]\n"
                            "       omui-gen cldr <cldr-json-dir> --locales <id,…> --version <x.y.z> --out <dir> [--check]");
    return 2;
}

// What a subcommand's options say, into the Options every run_* reads.
std::optional<Options> options_of(std::string_view command, const mcpplibs::cmdline::ParsedArgs& args) {
    Options o;
    o.command = std::string(command);
    for (const std::string& input : args.positionals) o.inputs.emplace_back(input);
    o.check = args.is_flag_set("check");
    o.json = args.is_flag_set("json");
    o.catalog = args.is_flag_set("catalog");
    if (auto value = args.value("out")) o.out = *value;
    if (auto value = args.value("registry")) o.registry = *value;
    if (auto value = args.value("module")) o.module = *value;
    if (auto value = args.value("namespace")) o.ns = *value;
    if (auto value = args.value("contract")) o.contract = *value;
    if (auto value = args.value("native")) o.native = *value;
    if (auto value = args.value("name")) o.name = *value;
    if (auto value = args.value("version")) o.version = *value;
    if (auto value = args.value("tests")) o.tests = *value;
    if (auto value = args.value("part")) {
        if (*value != "contract" && *value != "builder" && *value != "cpp" && *value != "stubs") return std::nullopt;
        o.part = *value;
    }
    if (auto value = args.value("locales"))
        for (auto part : *value | std::views::split(','))
            if (!std::ranges::empty(part)) o.locales.emplace_back(part.begin(), part.end());
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
    w.put(o.out / "stubs" / "core.d.ts", emit_dts(reg.contracts, "the Core profile", true));
    // The binding API's declarations and the define functions' (written by hand, normative), carried in
    // code: what a tool puts beside an application's builders when it type-checks it (omui check).
    for (const auto& [file, name] : {std::pair{"openmui.d.ts", "binding_api"}, std::pair{"define.d.ts", "define_api"}}) {
        std::ifstream in(o.out / "stubs" / file, std::ios::binary);
        if (!in) {
            std::println(std::cerr, "omui-gen: cannot read {}", generic_utf8(o.out / "stubs" / file));
            return 1;
        }
        const std::string text(std::istreambuf_iterator<char>(in), {});
        w.put(o.out / "src" / "generated" / std::format("{}.cppm", name), emit_embed(text, std::format("spec/stubs/{}", file), "openmui.spec", "omui::spec::stubs", name));
    }
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
// modules: <module>.natives.d.ts.
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
        selected.push_back(m);
    }
    if (!o.native.empty() && selected.empty()) {
        std::println(std::cerr, "omui-gen: no native module `{}` in the files given", o.native);
        return 1;
    }
    if (o.part != "cpp") w.put(o.out / (o.module + ".natives.d.ts"), emit_native_dts(selected, o.module));
    if (o.check) {
        if (w.stale) return 1;
        std::println("omui-gen: generated files are current");
    } else {
        std::println("omui-gen: {} file(s) written", w.written);
    }
    return 0;
}

// The files embedded under a tag as one table, partition <module>:<name>;
// each input `<file>=<partition>`.
int run_embed_index(const Options& o) {
    if (o.module.empty() || o.name.empty() || o.out.empty()) return usage();  // no files: an empty table (a feature left out)
    std::vector<std::pair<std::string, std::string>> files;
    for (const auto& in : o.inputs) {
        const std::string pair = generic_utf8(in);
        const auto eq = pair.find('=');
        if (eq == std::string::npos) return usage();
        files.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
    }
    std::string ns = o.ns;
    if (ns == "omui::ui") {
        ns.clear();
        for (char c : o.module) ns += c == '.' ? std::string("::") : std::string(1, c);
    }
    Writer w{o.check};
    w.put(o.out / (o.name + ".cppm"), emit_embed_index(files, o.module, ns, o.name));
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
    w.put(o.out / (o.module + ".components.json"), emit_view_schema(contracts, o.module));
    if (o.check && w.stale) return 1;
    return 0;
}

}  // namespace

// omui-gen wasm-size: the report on stdout (Markdown), and as files when asked.
int wasm_size_main(const mcpplibs::cmdline::ParsedArgs& args) {
    const std::string file = args.positional(0), json = args.value("json").value_or(""), markdown = args.value("markdown").value_or("");
    const std::size_t top = args.value_as<std::size_t>("top").value_or(40);
    std::ifstream in(std::filesystem::path(file), std::ios::binary);
    if (!in) {
        std::println(std::cerr, "omui-gen wasm-size: cannot read {}", file);
        return 1;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto r = omui::gen::wasm_size(bytes, top);
    if (!r) {
        std::println(std::cerr, "omui-gen wasm-size: {}: {}", file, r.error());
        return 1;
    }
    auto write = [](const std::string& path, const std::string& text) {
        std::ofstream o(std::filesystem::path(path), std::ios::binary);
        o << text;
        return static_cast<bool>(o);
    };
    if (!json.empty() && !write(json, omui::gen::to_json(*r))) return 1;
    const std::string md = omui::gen::to_markdown(*r);
    if (!markdown.empty() && !write(markdown, md)) return 1;
    std::print("{}", md);
    return 0;
}

// omui-gen ops <file.ops.omui.toml or .ops.omui.ts> --out <dir> [--check]: the C++ partition
// (<dir>/<partition>.cppm, catalog.cppm by default) and the listings (<dir>/<product>.ops.json, .ops.md).
int ops_main(const mcpplibs::cmdline::ParsedArgs& args) {
    const std::string file = args.positional(0), out = args.value("out").value_or("");
    const bool check = args.is_flag_set("check");
    const auto name = generic_utf8(std::filesystem::path(file).filename());
    auto catalog = omui::gen::read_catalog_file(std::filesystem::path(file));
    if (!catalog) {
        for (const auto& problem : catalog.error()) std::println(std::cerr, "{}", problem);
        return 1;
    }
    Writer w{check};
    // Its outputs where the catalog says, beside it, unless --out says otherwise.
    const std::filesystem::path dir = out.empty() ? std::filesystem::path(file).parent_path() / catalog->generated : std::filesystem::path(out);
    if (!catalog->module.empty()) w.put(dir / (catalog->partition + ".cppm"), omui::gen::emit_catalog_cpp(*catalog, name));
    w.put(dir / (catalog->product + ".ops.json"), omui::gen::emit_catalog_json(*catalog));
    w.put(dir / (catalog->product + ".ops.md"), omui::gen::emit_catalog_markdown(*catalog));
    if (check && w.stale) {
        std::println(std::cerr, "{} generated file(s) are out of date; run: omui-gen ops {} --out {}", w.stale, file, out);
        return 1;
    }
    std::println("omui-gen ops: {} operations, {} tiers, {} file(s) {}", catalog->operations.size(), catalog->tiers.size(), check ? 0 : w.written,
                 check ? "checked" : "written");
    return 0;
}

namespace {

// The command line (mcpplibs.cmdline): a subcommand each, its options.
mcpplibs::cmdline::App command_line() {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    const auto files = Arg("file").required().help("FILE…");
    const auto check = Option("check").help("write nothing; exit 1 when an output differs from what is on disk (CI's proof)");
    const auto json = Option("json").help("diagnostics as JSON, for tools and agents");
    const auto out = Option("out").takes_value().value_name("DIR").help("where the outputs go");
    const auto registry = Option("registry").takes_value().value_name("DIR").help("spec/registry");
    const auto module = Option("module").takes_value().value_name("M").help("the package's module");
    const auto space = Option("namespace").takes_value().value_name("NS").help("its C++ namespace");
    const auto name = Option("name").takes_value().value_name("PARTITION").help("the partition's and the constant's name");
    const auto version = Option("version").takes_value().value_name("X.Y.Z").help("the data's version");
    return App("omui-gen")
        .description("The registry and contracts, to every generated artifact")
        .subcommand(App("registry")
                        .description("openmui.h, the openmui.spec partitions, the reference tables, the Core TypeScript builders, the view schema")
                        .arg(Arg("registry").required().help("spec/registry"))
                        .option(out).option(check).option(json))
        .subcommand(App("stubs")
                        .description("a component package's TypeScript builders and view-document schema")
                        .arg(files).option(registry).option(module).option(out).option(check).option(json))
        .subcommand(App("cpp")
                        .description("a component package's C++: the contract partitions and the builders")
                        .arg(files).option(registry).option(module).option(out).option(space)
                        .option(Option("catalog").help("also the catalogue partition"))
                        .option(Option("contract").takes_value().value_name("NAME").help("only this contract of the files"))
                        .option(Option("part").takes_value().value_name("contract|builder").help("only this part"))
                        .option(check))
        .subcommand(App("native")
                        .description("a native module's C++ (records, the functions to implement, define()) and its TypeScript stubs")
                        .arg(files).option(registry).option(module).option(out).option(space)
                        .option(Option("native").takes_value().value_name("NAME").help("only this module of the files"))
                        .option(Option("part").takes_value().value_name("cpp|stubs").help("only this part"))
                        .option(check))
        .subcommand(App("embed")
                        .description("a data file as a partition holding its text")
                        .arg(Arg("file").required()).option(module).option(name).option(out).option(space).option(check))
        .subcommand(App("embed-index")
                        .description("a table of embedded files, by name")
                        .arg(Arg("file=partition").help("FILE=PARTITION… (none: an empty table)")).option(module).option(name).option(out).option(space).option(check))
        .subcommand(App("check")
                        .description("a contract's or native module's diagnostics only")
                        .arg(files).option(registry).option(json))
        .subcommand(App("abi")
                        .description("openmui.h's types and functions for the C++ that implements them, and their layouts' check")
                        .arg(Arg("registry").required()).option(out).option(check))
        .subcommand(App("unicode")
                        .description("openmui.unicode's tables (and a subset of the UCD's conformance tests)")
                        .arg(Arg("ucd").required().help("the UCD's directory")).option(version).option(out)
                        .option(Option("tests").takes_value().value_name("DIR").help("where the conformance subset goes"))
                        .option(check))
        .subcommand(App("cldr")
                        .description("openmui.i18n's locale packs and likely subtags")
                        .arg(Arg("cldr").required().help("CLDR's JSON directory"))
                        .option(Option("locales").takes_value().value_name("ID,…").help("en,zh,zh-Hant …"))
                        .option(version).option(out).option(check))
        .subcommand(App("ops")
                        .description("an operation catalog's C++ partition and listings (JSON, Markdown)")
                        .arg(Arg("file").required().help("*.ops.omui.ts")).option(out).option(check))
        .subcommand(App("dist-toy")
                        .description("a wasm32-emscripten program packaged for Bilibili Toy (what `mcpp pack --pkg-format dist-toy` runs)")
                        .arg(Arg("stage").required().help("the staged tree"))
                        .option(Option("target").takes_value().value_name("NAME").help("the program"))
                        .option(out)
                        .option(Option("title").takes_value().help("the page's title"))
                        .option(Option("fonts").takes_value().value_name("A,B|all").help("the fonts it carries"))
                        .option(Option("fonts-dir").takes_value().value_name("DIR").help("where they are"))
                        .option(Option("graph").takes_value().value_name("FILE").help("the program's graph"))
                        .option(Option("background").takes_value().value_name("#RRGGBB").help("the page's colour while it loads"))
                        .option(Option("no-sdk").help("a program without the SDK"))
                        .option(Option("later").takes_value().multiple().value_name("DIR|SUFFIX|MOUNT").help("files loaded after the first frame"))
                        .option(Option("subset").takes_value().multiple().value_name("FONT=PATH,…").help("a font cut to these files' text"))
                        .option(Option("font-languages").takes_value().multiple().value_name("FONT=LANG,…").help("a font's languages"))
                        .option(Option("font-initial").takes_value().multiple().value_name("FONT=TEXT").help("a font's first screen's text")))
        .subcommand(App("dist-toy-split")
                        .description("a packed page's program cut in two by wasm-split: the first screen's half first")
                        .arg(Arg("dir").required().help("the package"))
                        .option(Option("primary").takes_value().value_name("WASM").help("wasm-split's first half"))
                        .option(Option("deferred").takes_value().value_name("WASM").help("its second"))
                        .option(Option("lazy").takes_value().value_name("ROUTE,…").help("the routes whose first screen the first half covers")))
        .subcommand(App("wasm-size")
                        .description("where a WebAssembly program's bytes go: its sections, its code by owner, its largest functions")
                        .arg(Arg("file").required().help("FILE.wasm"))
                        .option(Option("json").takes_value().value_name("FILE").help("also as JSON"))
                        .option(Option("markdown").takes_value().value_name("FILE").help("also as Markdown"))
                        .option(Option("top").value_type<std::size_t>().help("the largest N functions (default 40)")));
}

}  // namespace

int main(int argc, char** argv) {
    const auto app = command_line();
    auto parsed = app.parse(argc, argv);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;  // --help
        std::println(std::cerr, "omui-gen: {}", parsed.error().message);
        return 2;
    }
    if (!parsed->has_subcommand()) return usage();
    const std::string command(parsed->subcommand_name());
    const mcpplibs::cmdline::ParsedArgs& args = parsed->subcommand()->get();
    if (command == "ops") return ops_main(args);
    if (command == "dist-toy") return dist_toy_main(args);
    if (command == "dist-toy-split") return dist_toy_split_main(args);
    if (command == "wasm-size") return wasm_size_main(args);
    auto options = options_of(command, args);
    if (!options) return usage();
    if (options->command == "registry") return run_registry(*options);
    if (options->command == "stubs") return run_contracts(*options, true);
    if (options->command == "cpp") return run_cpp(*options);
    if (options->command == "native") return run_native(*options);
    if (options->command == "embed") return run_embed(*options);
    if (options->command == "embed-index") return run_embed_index(*options);
    if (options->command == "check") return run_contracts(*options, false);
    if (options->command == "unicode") return run_unicode(*options);
    if (options->command == "cldr") return run_cldr(*options);
    if (options->command == "abi") return run_abi(*options);
    return usage();
}
