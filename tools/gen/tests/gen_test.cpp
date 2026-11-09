// The generator: the committed outputs under spec/ are what the registry
// produces now, and a broken contract is reported line by line.
import std;
import omui.gen.model;
import omui.gen.read;
import omui.gen.emit;
import omui.gen.ops;
import omui.gen.data;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace gen = omui::gen;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path(); }
std::filesystem::path spec_dir() { return here() / ".." / ".." / ".." / "spec"; }

std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

UTest cases{"gen", [] {

    "committed outputs are current"_test = [] {
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        expect(!diags.has_errors()) << diags.text();
        auto same = [](const std::filesystem::path& p, const std::string& now) {
            expect(read(p) == now) << std::format("{} is out of date: run omui-gen registry spec/registry --out spec", p.generic_string());
        };
        same(spec_dir() / "include" / "openmui.h", gen::emit_c_header(reg));
        same(spec_dir() / "src" / "generated" / "registry.cppm", gen::emit_spec_registry(reg));
        same(spec_dir() / "src" / "generated" / "catalog.cppm", gen::emit_spec_catalog(reg));
        same(spec_dir() / "docs" / "registry.md", gen::emit_docs(reg));
        same(spec_dir() / "stubs" / "core.d.ts", gen::emit_dts(reg.contracts, "the Core profile", true));
        // The scripts' declarations, written by hand, carried in code.
        for (const auto& [file, name] : {std::pair{"openmui.d.ts", "binding_api"}, std::pair{"define.d.ts", "define_api"}})
            same(spec_dir() / "src" / "generated" / std::format("{}.cppm", name),
                 gen::emit_embed(read(spec_dir() / "stubs" / file), std::format("spec/stubs/{}", file), "openmui.spec", "omui::spec::stubs", name));
        same(spec_dir() / "schema" / "core.components.json", gen::emit_view_schema(reg.contracts, "the Core profile"));
        // modules/projects/omui/abi's view of the same ABI.
        const auto abi = spec_dir().parent_path() / "modules" / "projects" / "omui" / "abi" / "src";
        auto current = [](const std::filesystem::path& p, const std::string& now) {
            expect(read(p) == now) << std::format("{} is out of date: run omui-gen abi spec/registry --out modules/projects/omui/abi/src", p.generic_string());
        };
        current(abi / "generated" / "c.cppm", gen::emit_abi_types(reg));
        current(abi / "generated" / "api.cppm", gen::emit_abi_functions(reg));
        current(abi / "native" / "layout.cpp", gen::emit_abi_layout(reg));
    };

    "the base components' C++ is current"_test = [] {
        // modules/projects/omui/components/base/src/generated holds `omui-gen cpp` of the Core contracts it implements.
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        const auto dir = spec_dir().parent_path() / "modules" / "projects" / "omui" / "components" / "base" / "src" / "generated";
        const gen::CppOptions o{"omui.ui.base", "omui", true};
        int seen = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string file = entry.path().filename().string();
            if (!file.ends_with("_contract.cppm")) continue;
            const std::string name = file.substr(0, file.size() - std::string_view("_contract.cppm").size());
            // A contract file may hold several contracts (tabs and tab): look it up by name.
            const auto it = std::ranges::find(reg.contracts, name, &gen::Contract::name);
            expect(it != reg.contracts.end()) << name;
            if (it == reg.contracts.end()) continue;
            std::vector<gen::Contract> contracts{*it};
            const std::string hint = std::format(
                "out of date: run omui-gen cpp spec/registry/components/*.contract.omui.json for {} --registry spec/registry "
                "--module omui.ui.base --namespace omui --out modules/projects/omui/components/base/src/generated --catalog",
                name);
            expect(read(entry.path()) == gen::emit_cpp_contract(contracts[0], reg, o)) << hint;
            expect(read(dir / (name + "_builder.cpp")) == gen::emit_cpp_builder(contracts[0], reg, o)) << hint;
            ++seen;
        }
        expect(seen >= 5) << std::format("{} component partitions", seen);
    };

    "an embedded tag's table: every file by name with its text, its partitions exported through it"_test = [] {
        const std::vector<std::pair<std::string, std::string>> files{{"b-two.webp", "b_two_art"}, {"a-one.webp", "a_one_art"}};
        const std::string out = gen::emit_embed_index(files, "m", "omui::files", "art_files");
        expect(out.find("export module m:art_files;") != std::string::npos) << out;
        expect(out.find("export import :a_one_art;\nexport import :b_two_art;") != std::string::npos) << "its partitions, sorted, exported";
        expect(out.find("std::array<std::pair<std::string_view, std::string_view>, 2> art_files{{\n    {\"a-one.webp\", a_one_art},\n    {\"b-two.webp\", b_two_art},\n}};") !=
                  std::string::npos) << out;
    };

    "embedding bytes (a picture, a sound): escaped, NULs kept, the length given"_test = [] {
        const std::string bytes("\x89PNG\r\n\x1a\n\0\xff", 10);
        const std::string out = gen::emit_embed(bytes, "art.png", "m", "omui::files", "art_png");
        expect(out.find("\\x89\\x50\\x4e\\x47") != std::string::npos && out.find(", 10}") != std::string::npos) << out;
        expect(out.find("R\"omui(") == std::string::npos) << "not a raw string";
        const std::string text = gen::emit_embed("plain text", "a.toml", "m", "omui::files", "a_toml");
        expect(text.find("R\"omui(plain text)omui\"") != std::string::npos) << text;
    };

    "a contract's C++ base refuses a missing action"_test = [] {
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        auto slider = gen::read_contracts(spec_dir() / "registry" / "components" / "slider.contract.omui.json", reg, diags);
        const std::string cpp = gen::emit_cpp_contract(slider.at(0), reg, {"x.ui", "x::ui", false});
        for (auto wanted : {"export module x.ui:slider_contract;", "struct SliderProps", "double step = 1.0;",
                            "struct SliderOptions", "std::function<void(double)> on_committed;", "class SliderContract",
                            "needs `ActResult set_value(ActContext&, const Value& value)`", "void write_value(double value)",
                            "namespace x::ui::slider_contract_tables", "inline constexpr ContractInfo info"})
            expect(cpp.find(wanted) != std::string::npos) << wanted;
    };

    "alternatives typed in TypeScript (`f64 | min | max`)"_test = [] {
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        auto slider = gen::read_contracts(spec_dir() / "registry" / "components" / "slider.contract.omui.json", reg, diags);
        expect(!slider.empty() && !slider.at(0).props.empty());
        if (slider.empty() || slider.at(0).props.empty()) return;
        slider.at(0).props.at(0).type = "f64 | min | max";
        const std::string ts = gen::emit_dts(slider, "x");
        expect(ts.find("number | \"min\" | \"max\"") != std::string::npos) << "TypeScript";
    };

    "a property of any type is data as it is: a Value, read and handed back whole"_test = [] {
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        auto lens = gen::read_contracts(spec_dir().parent_path() / "site" / "contract" / "lens.contract.omui.ts", reg, diags);
        expect(!diags.has_errors()) << diags.text();
        const std::string cpp = gen::emit_cpp_contract(lens.at(0), reg, {"x.site", "x::site", false});
        for (auto wanted : {"::omui::Value nodes;", "::omui::Prop<::omui::Value> nodes;", "props.nodes = v.is_null() ? LensProps{}.nodes : v;"})
            expect(cpp.find(wanted) != std::string::npos) << wanted;
    };

    "a broken contract is reported line by line"_test = [] {
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        auto contracts = gen::read_contracts(here() / "data" / "bad.contract.omui.json", reg, diags);
        auto text = diags.text();
        for (auto wanted : {"unknown role `spaceship`", "unknown type `huge`", "default of `mode`", "default of `count`",
                            "weaker than its default", "unknown effect `orbital`", "names `fly`",
                            "unknown token `motion.warp`", "action `aim` argument `at` has unknown type `f64 | Up | | down`"})
            expect(text.find(wanted) != std::string::npos) << wanted;
        expect(text.find("bad.contract.omui.json:3:") != std::string::npos) << "diagnostics carry line numbers";
        auto json = diags.json();
        expect(json.starts_with("[{\"severity\":\"error\"")) << json.substr(0, 60);
    };

    "names follow each language"_test = [] {
        expect(eq(gen::pascal("text_box"), std::string("TextBox")));
        expect(eq(gen::camel("text_box"), std::string("textBox")));
        expect(eq(gen::upper("motion.fast"), std::string("MOTION_FAST")));
        expect(eq(gen::ident("switch"), std::string("switch_")));
        expect(eq(gen::format_double(100), std::string("100.0")));
    };

    "a broken native contract is reported line by line"_test = [] {
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        auto modules = gen::read_natives(here() / "data" / "bad.native.omui.json", reg, diags);
        auto text = diags.text();
        for (auto wanted : {"native module name `Bad` is not lower_snake_case", "type name `point` is not PascalCase",
                            "Node.next holds a Node in itself", "Node.weight: unknown type `decimal`",
                            "`irreversible`, which needs an answer (S-1)", "find(query) follows the optional `limit`",
                            "find returns: unknown type `Hit`"})
            expect(text.find(wanted) != std::string::npos) << wanted;
        expect(text.find("bad.native.omui.json:10:") != std::string::npos) << "diagnostics carry line numbers";
    };

    "the official native modules' generated code and stubs are current (modules/projects/omui/native/*)"_test = [] {
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        const auto root = spec_dir().parent_path() / "modules" / "projects" / "omui" / "native";
        std::error_code ec;
        int seen = 0;
        for (const auto& pkg : std::filesystem::directory_iterator(root, ec)) {
            for (const auto& f : std::filesystem::directory_iterator(pkg.path() / "native", ec)) {
                const std::string file = gen::generic_utf8(f.path().filename());
                if (!file.ends_with(".native.omui.toml") && !file.ends_with(".native.omui.ts")) continue;
                const std::string module = "omui.native." + gen::generic_utf8(pkg.path().filename());
                auto modules = gen::read_natives(f.path(), reg, diags);
                expect(!diags.has_errors()) << diags.text();
                auto read = [](const std::filesystem::path& p) {
                    std::ifstream in(p, std::ios::binary);
                    return std::string(std::istreambuf_iterator<char>(in), {});
                };
                for (const auto& m : modules) {
                    ++seen;
                    const gen::CppOptions o{module, "omui::natives", false};
                    expect(read(pkg.path() / "src" / "generated" / (m.name + "_native.cppm")) == gen::emit_cpp_native(m, o, file)) << std::format("{}: run omui-gen native {} --registry spec/registry --module {} --namespace omui::natives --out {}/src/generated --part cpp",
                                      m.name, gen::generic_utf8(f.path()), module, gen::generic_utf8(pkg.path()));
                }
                expect(read(pkg.path() / "stubs" / (module + ".natives.d.ts")) == gen::emit_native_dts(modules, module)) << module + ".natives.d.ts";
            }
        }
        expect(seen >= 1) << "modules/projects/omui/native/fuzzy at least";
    };

    "the theme packages' embedded files are current (modules/projects/omui/theme/*)"_test = [] {
        const auto root = spec_dir().parent_path() / "modules" / "projects" / "omui" / "theme";
        std::error_code ec;
        int seen = 0;
        for (const auto& pkg : std::filesystem::directory_iterator(root, ec)) {
            const std::string name = gen::generic_utf8(pkg.path().filename());
            const auto file = pkg.path() / (name + ".theme.omui.ts");
            if (!std::filesystem::exists(file)) continue;
            ++seen;
            // What the module evaluates to, as the package carries it.
            const auto snapshot = gen::snapshot(file);
            expect(snapshot.has_value()) << (snapshot ? "" : snapshot.error());
            if (!snapshot) continue;
            // A module name is identifiers: `default` (a keyword) is `default_theme`.
            const std::string module = name == "default" ? "omui.theme.default_theme" : "omui.theme." + name;
            const std::string want = gen::emit_embed(snapshot->json, snapshot->name, module, "omui::themes", name + "_theme");
            expect(read(pkg.path() / "src" / "generated" / (name + "_theme.cppm")) == want) << std::format("modules/projects/omui/theme/{0}: run omui-gen embed modules/projects/omui/theme/{0}/{0}.theme.omui.ts --module {1} --namespace omui::themes "
                              "--name {0}_theme --out modules/projects/omui/theme/{0}/src/generated",
                              name, module);
        }
        expect(seen >= 2) << "default and compact at least";
    };

    "the effect template the renderer carries is current (modules/projects/xrender/vector)"_test = [] {
        const auto render = spec_dir().parent_path() / "modules" / "projects" / "xrender" / "vector";
        std::ifstream in(render / "effects" / "lib" / "openmui_effect.slang", std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        expect(!text.empty()) << "effects/lib/openmui_effect.slang";
        expect(read(render / "src" / "generated" / "effect_template.cppm") ==
                  gen::emit_embed(text, "openmui_effect.slang", "xrender.vector.api", "xrender", "effect_template")) << "modules/projects/xrender/vector: run omui-gen embed modules/projects/xrender/vector/effects/lib/openmui_effect.slang --module xrender.vector.api "
              "--name effect_template --namespace xrender --out modules/projects/xrender/vector/src/generated";
    };

    "every operation catalog reads clean and its committed outputs are current (§10.3)"_test = [] {
        const auto repo = spec_dir().parent_path();
        std::size_t catalogs = 0;
        std::vector<std::filesystem::path> files;
        for (const auto* place : {"modules", "showcase"})
            for (const auto& entry : std::filesystem::recursive_directory_iterator(repo / place)) files.push_back(entry.path());
        for (const auto& path : files) {
            const std::filesystem::directory_entry entry(path);
            const auto name = entry.path().filename().generic_string();
            if (!entry.is_regular_file() || !(name.ends_with(".ops.omui.toml") || name.ends_with(".ops.omui.ts")) ||
                entry.path().generic_string().find("/target/") != std::string::npos)
                continue;
            ++catalogs;
            auto catalog = gen::read_catalog_file(entry.path());
            expect(catalog.has_value()) << (catalog ? std::string() : catalog.error().front());
            if (!catalog) continue;
            // Its outputs where the catalog says, beside it.
            const auto out = entry.path().parent_path() / catalog->generated;
            const auto stale = [&](const std::filesystem::path& file, const std::string& now) {
                expect(read(file) == now) << std::format("{} is out of date: run omui-gen ops {} --out {}", file.generic_string(), entry.path().generic_string(), out.generic_string());
            };
            if (!catalog->module.empty()) stale(out / (catalog->partition + ".cppm"), gen::emit_catalog_cpp(*catalog, name));
            stale(out / (catalog->product + ".ops.json"), gen::emit_catalog_json(*catalog));
            stale(out / (catalog->product + ".ops.md"), gen::emit_catalog_markdown(*catalog));
        }
        expect(catalogs >= 1) << "hanim's catalog at least";
    };

    "a malformed catalog is reported line by line"_test = [] {
        const auto broken = gen::read_catalog(R"({"ops": {
  "product": "x",
  "module": "x.control",
  "namespace": "x::control",
  "operations": [
    {"name": "Bad Name", "effect": "maybe", "summary": "something"},
    {"name": "x.change", "effect": "reversible", "summary": "changes"},
    {"name": "x.run", "effect": "read", "delegated": true, "summary": "runs what it names"},
    {"name": "x.peek", "effect": "read", "undoable": true, "summary": "reads"}
  ]
}})", "x.ops.omui.json");
        expect(!broken) << "refused";
        if (broken) return;
        const auto& problems = broken.error();
        const auto says = [&](std::string_view text) { return std::ranges::any_of(problems, [&](const std::string& p) { return p.find(text) != std::string::npos; }); };
        expect(says("x.ops.omui.json:6") && says("dotted lowercase")) << "the bad name at its line";
        expect(says("not read, reversible or irreversible")) << "the bad effect";
        expect(says("say how it is undone")) << "a change without its undo";
        expect(says("the most that can be")) << "a delegated operation's bound is above read";
        expect(says("only a reversible operation is in the history")) << "a read is not undoable";
    };
}};
