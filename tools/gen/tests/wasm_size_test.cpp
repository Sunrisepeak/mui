// omui-gen's wasm size: a WebAssembly binary's sections, its code by owner
// (a module, a namespace, a C library) from its name section, its largest
// functions — on a binary made here, byte by byte; and symbols' owners.
import std;
import openmui.gen.wasm_size;
import mlibs.testing;

using namespace mlibs::testing;
using namespace omui;

namespace {

void leb(std::vector<std::uint8_t>& out, std::uint64_t v) {
    do {
        std::uint8_t b = v & 0x7f;
        v >>= 7;
        out.push_back(v ? b | 0x80 : b);
    } while (v);
}

void name(std::vector<std::uint8_t>& out, std::string_view s) {
    leb(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

void section(std::vector<std::uint8_t>& out, std::uint8_t id, const std::vector<std::uint8_t>& body) {
    out.push_back(id);
    leb(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
}

// An imported function, then three of its own (bodies of 100, 30 and 7 bytes), named.
std::vector<std::uint8_t> program() {
    std::vector<std::uint8_t> w{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    section(w, 1, {0x01, 0x60, 0x00, 0x00});  // one type: () -> ()
    std::vector<std::uint8_t> imports{0x01};
    name(imports, "env"), name(imports, "now"), imports.push_back(0x00), imports.push_back(0x00);
    section(w, 2, imports);
    section(w, 3, {0x03, 0x00, 0x00, 0x00});
    std::vector<std::uint8_t> code{0x03};
    for (std::size_t n : {100u, 30u, 7u}) {
        leb(code, n);
        code.insert(code.end(), n, 0x01);  // nops (a body's bytes; their meaning is not read)
    }
    section(w, 10, code);
    section(w, 11, std::vector<std::uint8_t>(50, 0));
    std::vector<std::uint8_t> names;
    name(names, "name");
    std::vector<std::uint8_t> functions;
    leb(functions, 4);
    leb(functions, 0), name(functions, "now");
    leb(functions, 1), name(functions, "_ZNW7openmuiW4view4omui7Runtime5frameEv");
    leb(functions, 2), name(functions, "hb_shape_plan_execute");
    leb(functions, 3), name(functions, "_ZNSt6vectorIiSaIiEE9push_backEOi");
    names.push_back(1);
    leb(names, functions.size());
    names.insert(names.end(), functions.begin(), functions.end());
    section(w, 0, names);
    return w;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"gen.wasm_size"};

    s.test("a symbol's owner: the module it is attached to, else its namespaces, else a C library by its prefix", [] {
        check_eq(gen::owner_of("_ZNW7openmuiW4view4omui7Runtime5frameEv"), std::string("openmui.view"));
        check_eq(gen::owner_of("_ZNW7openmuiW4viewWP6motion4omui4Impl3runEv"), std::string("openmui.view"));
        check_eq(gen::owner_of("_ZN4omui6render10DisplayList4fillEv"), std::string("omui::render"));
        check_eq(gen::owner_of("_ZNKSt6vectorIiSaIiEE4sizeEv"), std::string("std"));
        check_eq(gen::owner_of("_ZN8nlohmann16json_abi_v3_12_010basic_jsonEv", 1), std::string("nlohmann"));
        check_eq(gen::owner_of("_Z4mainv"), std::string("(global)"));
        check_eq(gen::owner_of("hb_buffer_create"), std::string("harfbuzz"));
        check_eq(gen::owner_of("FT_Load_Glyph"), std::string("freetype"));
        check_eq(gen::owner_of("md_parse"), std::string("md4c"));
        check_eq(gen::owner_of("__cxa_throw"), std::string("c++ runtime"));
        check_eq(gen::owner_of("memcpy"), std::string("(C) memcpy"));
        // Demangled, as an analysis build's name section holds them.
        check_eq(gen::owner_of("omui::Runtime@openmui.view::Impl::perform(omui::Handle@openmui.memory<omui::NodeTag>)"), std::string("openmui.view"));
        check_eq(gen::owner_of("omui::site::(anonymous namespace)::moves_window(std::__2::shared_ptr<int>)"), std::string("omui::site"));
        check_eq(gen::owner_of("void std::__2::vector<int, std::__2::allocator<int>>::push_back(int&&)"), std::string("std"));
        check_eq(gen::owner_of("bool omui::render::DisplayList::empty() const"), std::string("omui::render"));
        check_eq(gen::owner_of("OT::Layout::GSUB::apply(hb_ot_apply_context_t*)"), std::string("harfbuzz"));
        check_eq(gen::owner_of("find_syllables_use(hb_buffer_t*)"), std::string("harfbuzz"));
        check_eq(gen::owner_of("toml::v3::impl::impl_ex::parser::parse()"), std::string("tomlplusplus"));
        check_eq(gen::owner_of("gongfu::present2d::read_look@gongfu.present2d(std::__2::basic_string_view<char>)"), std::string("gongfu.present2d"));
    });

    s.test("a binary's sections, its code by owner (imports counted out of the functions' indices), its largest functions", [] {
        const auto w = program();
        auto r = gen::wasm_size(w, 2);
        check(r.has_value(), r ? "" : r.error());
        if (!r) return;
        check_eq(r->functions, std::size_t{3});
        check_eq(r->named, std::size_t{3});
        check_eq(r->data, std::size_t{50});
        check(r->shipped < r->total, "the name section is not shipped");
        check(r->owners.size() == 3 && r->owners[0].name == "openmui.view" && r->owners[0].bytes == 100, gen::to_json(*r));
        check(r->owners[1].name == "harfbuzz" && r->owners[1].bytes == 30 && r->owners[2].name == "std", gen::to_json(*r));
        check(r->largest.size() == 2 && r->largest[0].bytes == 100, "the two largest");
        const std::string md = gen::to_markdown(*r);
        check(md.contains("| openmui.view |") && md.contains("custom:name"), md);
        check(gen::to_json(*r).starts_with("{\"total\":"), gen::to_json(*r));
        check(!gen::wasm_size(std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}).has_value(), "not WebAssembly");
        auto cut = w;
        cut.resize(cut.size() - 10);
        check(!gen::wasm_size(cut).has_value(), "cut short");
    });

    return s.run(argc, argv);
}
