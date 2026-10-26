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
