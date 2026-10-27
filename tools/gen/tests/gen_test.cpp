// The generator: the committed outputs under spec/ are what the registry
// produces now, and a broken contract is reported line by line.
import std;
import openmui.gen.model;
import openmui.gen.read;
import openmui.gen.emit;
import openmui.testing;

using namespace omui::testing;
namespace gen = omui::gen;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path(); }
std::filesystem::path spec_dir() { return here() / ".." / ".." / ".." / "spec"; }

std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"gen"};

    s.test("committed outputs are current", [] {
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        check(!diags.has_errors(), diags.text());
        auto same = [](const std::filesystem::path& p, const std::string& now) {
            check(read(p) == now, std::format("{} is out of date: run omui-gen registry spec/registry --out spec", p.generic_string()));
        };
        same(spec_dir() / "include" / "openmui.h", gen::emit_c_header(reg));
        same(spec_dir() / "src" / "generated" / "registry.cppm", gen::emit_spec_registry(reg));
        same(spec_dir() / "src" / "generated" / "catalog.cppm", gen::emit_spec_catalog(reg));
        same(spec_dir() / "docs" / "registry.md", gen::emit_docs(reg));
        same(spec_dir() / "stubs" / "core.d.ts", gen::emit_dts(reg.contracts, "the Core profile"));
        same(spec_dir() / "stubs" / "core.pyi", gen::emit_pyi(reg.contracts, "the Core profile"));
        same(spec_dir() / "stubs" / "core.lua", gen::emit_lua(reg.contracts, "the Core profile"));
        same(spec_dir() / "schema" / "core.components.json", gen::emit_view_schema(reg.contracts, "the Core profile"));
        // runtime/abi's view of the same ABI.
        const auto abi = spec_dir().parent_path() / "runtime" / "abi" / "src";
        auto current = [](const std::filesystem::path& p, const std::string& now) {
            check(read(p) == now, std::format("{} is out of date: run omui-gen abi spec/registry --out runtime/abi/src", p.generic_string()));
        };
        current(abi / "generated" / "c.cppm", gen::emit_abi_types(reg));
        current(abi / "generated" / "api.cppm", gen::emit_abi_functions(reg));
        current(abi / "native" / "layout.cpp", gen::emit_abi_layout(reg));
    });

    s.test("the base components' C++ is current", [] {
        // components/base/src/generated holds `omui-gen cpp` of the Core contracts it implements.
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        const auto dir = spec_dir().parent_path() / "components" / "base" / "src" / "generated";
        const gen::CppOptions o{"openmui.ui.base", "omui", true};
        int seen = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string file = entry.path().filename().string();
            if (!file.ends_with("_contract.cppm")) continue;
            const std::string name = file.substr(0, file.size() - std::string_view("_contract.cppm").size());
            // A contract file may hold several contracts (tabs and tab): look it up by name.
            const auto it = std::ranges::find(reg.contracts, name, &gen::Contract::name);
            check(it != reg.contracts.end(), name);
            if (it == reg.contracts.end()) continue;
            std::vector<gen::Contract> contracts{*it};
            const std::string hint = std::format(
                "out of date: run omui-gen cpp spec/registry/components/*.contract.omui.toml for {} --registry spec/registry "
                "--module openmui.ui.base --namespace omui --out components/base/src/generated --catalog",
                name);
            check(read(entry.path()) == gen::emit_cpp_contract(contracts[0], reg, o), hint);
            check(read(dir / (name + "_builder.cpp")) == gen::emit_cpp_builder(contracts[0], reg, o), hint);
            ++seen;
        }
        check(seen >= 5, std::format("{} component partitions", seen));
    });

    s.test("a contract's C++ base refuses a missing action", [] {
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        auto slider = gen::read_contracts(spec_dir() / "registry" / "components" / "slider.contract.omui.toml", reg, diags);
        const std::string cpp = gen::emit_cpp_contract(slider.at(0), reg, {"x.ui", "x::ui", false});
        for (auto expect : {"export module x.ui:slider_contract;", "struct SliderProps", "double step = 1.0;",
                            "struct SliderOptions", "std::function<void(double)> on_committed;", "class SliderContract",
                            "needs `ActResult set_value(ActContext&, const Value& value)`", "void write_value(double value)",
                            "namespace x::ui::slider_contract_tables", "inline constexpr ContractInfo info"})
            check(cpp.find(expect) != std::string::npos, expect);
    });

    s.test("a broken contract is reported line by line", [] {
        gen::Diagnostics diags;
        auto reg = gen::read_registry(spec_dir() / "registry", diags);
        auto contracts = gen::read_contracts(here() / "data" / "bad.contract.omui.toml", reg, diags);
        auto text = diags.text();
        for (auto expect : {"unknown role `spaceship`", "unknown type `huge`", "default of `mode`", "default of `count`",
                            "weaker than its default", "unknown effect `orbital`", "names `fly`",
                            "unknown token `motion.warp`"})
            check(text.find(expect) != std::string::npos, expect);
        check(text.find("bad.contract.omui.toml:3:") != std::string::npos, "diagnostics carry line numbers");
        auto json = diags.json();
        check(json.starts_with("[{\"severity\":\"error\""), json.substr(0, 60));
    });

    s.test("names follow each language", [] {
        check_eq(gen::pascal("text_box"), std::string("TextBox"));
        check_eq(gen::camel("text_box"), std::string("textBox"));
        check_eq(gen::upper("motion.fast"), std::string("MOTION_FAST"));
        check_eq(gen::ident("switch"), std::string("switch_"));
        check_eq(gen::format_double(100), std::string("100.0"));
    });

    return s.run(argc, argv);
}
