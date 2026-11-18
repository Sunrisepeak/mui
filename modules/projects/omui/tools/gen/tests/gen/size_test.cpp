// omui-gen's size: a WebAssembly binary's sections, its code by owner (a
// module, a namespace, a C library) from its name section, its largest
// functions — on a binary made here, byte by byte; symbols' owners; and an
// ELF program's, this test's own (B3).
import std;
import omui.gen.size;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace mcpplibs::mtest;
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

static UTest cases{"gen.size", [] {

    "a symbol's owner: the module it is attached to, else its namespaces, else a C library by its prefix"_test = [] {
        expect(eq(gen::owner_of("_ZNW7openmuiW4view4omui7Runtime5frameEv"), std::string("openmui.view")));
        expect(eq(gen::owner_of("_ZNW7openmuiW4viewWP6motion4omui4Impl3runEv"), std::string("openmui.view")));
        expect(eq(gen::owner_of("_ZN4omui6render10DisplayList4fillEv"), std::string("omui::render")));
        expect(eq(gen::owner_of("_ZNKSt6vectorIiSaIiEE4sizeEv"), std::string("std")));
        expect(eq(gen::owner_of("_ZN8nlohmann16json_abi_v3_12_010basic_jsonEv", 1), std::string("nlohmann")));
        expect(eq(gen::owner_of("_Z4mainv"), std::string("(global)")));
        expect(eq(gen::owner_of("_ZTVN4omui6render10DisplayListE"), std::string("omui::render"))) << "a vtable is its type's";
        expect(eq(gen::owner_of("_ZGVZN3okl11generationsEvE1g"), std::string("okl::generations"))) << "a local static's guard";
        expect(eq(gen::owner_of("_ZZN3okl11generationsEvE1g"), std::string("okl::generations"))) << "a function's local static";
        expect(eq(gen::owner_of("_ZThn16_N4omui6render10DisplayList4fillEv"), std::string("omui::render"))) << "a thunk";
        expect(eq(gen::owner_of("_RNvMNtCs6i54tJFfzR_9accesskit4tree4Node3new"), std::string("rust accesskit")));
        expect(eq(gen::owner_of("hb_buffer_create"), std::string("harfbuzz")));
        expect(eq(gen::owner_of("FT_Load_Glyph"), std::string("freetype")));
        expect(eq(gen::owner_of("md_parse"), std::string("md4c")));
        expect(eq(gen::owner_of("__cxa_throw"), std::string("c++ runtime")));
        expect(eq(gen::owner_of("memcpy"), std::string("(C) memcpy")));
        // Demangled, as an analysis build's name section holds them.
        expect(eq(gen::owner_of("omui::Runtime@openmui.view::Impl::perform(omui::Handle@openmui.memory<omui::NodeTag>)"), std::string("openmui.view")));
        expect(eq(gen::owner_of("omui::site::(anonymous namespace)::moves_window(std::__2::shared_ptr<int>)"), std::string("omui::site")));
        expect(eq(gen::owner_of("void std::__2::vector<int, std::__2::allocator<int>>::push_back(int&&)"), std::string("std")));
        expect(eq(gen::owner_of("bool omui::render::DisplayList::empty() const"), std::string("omui::render")));
        expect(eq(gen::owner_of("OT::Layout::GSUB::apply(hb_ot_apply_context_t*)"), std::string("harfbuzz")));
        expect(eq(gen::owner_of("find_syllables_use(hb_buffer_t*)"), std::string("harfbuzz")));
        expect(eq(gen::owner_of("toml::v3::impl::impl_ex::parser::parse()"), std::string("tomlplusplus")));
        expect(eq(gen::owner_of("gongfu::present2d::read_look@gongfu.present2d(std::__2::basic_string_view<char>)"), std::string("gongfu.present2d")));
    };

    "a binary's sections, its code by owner (imports counted out of the functions' indices), its largest functions"_test = [] {
        const auto w = program();
        auto r = gen::wasm_size(w, 2);
        expect(r.has_value()) << (r ? "" : r.error());
        if (!r) return;
        expect(eq(r->functions, std::size_t{3}));
        expect(eq(r->named, std::size_t{3}));
        expect(eq(r->data, std::size_t{50}));
        expect(r->shipped < r->total) << "the name section is not shipped";
        expect(r->owners.size() == 3 && r->owners[0].name == "openmui.view" && r->owners[0].bytes == 100) << gen::to_json(*r);
        expect(r->owners[1].name == "harfbuzz" && r->owners[1].bytes == 30 && r->owners[2].name == "std") << gen::to_json(*r);
        expect(r->largest.size() == 2 && r->largest[0].bytes == 100) << "the two largest";
        const std::string md = gen::to_markdown(*r);
        expect(md.contains("| openmui.view |") && md.contains("custom:name")) << md;
        expect(gen::to_json(*r).starts_with("{\"total\":")) << gen::to_json(*r);
        expect(!gen::wasm_size(std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}).has_value()) << "not WebAssembly";
        auto cut = w;
        cut.resize(cut.size() - 10);
        expect(!gen::wasm_size(cut).has_value()) << "cut short";
    };

    "an ELF program: what it loads, by owner from its symbol table (this test itself, where it is ELF)"_test = [] {
        const auto image = mcpplibs::os::env::image();
        if (!image) return;
        std::ifstream in(*image, std::ios::binary);
        const std::vector<std::uint8_t> self((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (self.size() < 4 || self[0] != 0x7f || self[1] != 'E') return;  // not Linux: Mach-O and PE are not read yet
        auto r = gen::program_size(self, 10);
        expect(fatal(r.has_value())) << (r ? "" : r.error());
        expect(r->shipped > 0 && r->code > 0 && r->shipped <= r->total && r->code + r->data == r->shipped);
        expect(std::ranges::any_of(r->sections, [](const gen::SizeEntry& s) { return s.name == ".text"; })) << "its sections by name";
        // A stripped program (CI's profile) has its sections only; one that keeps its symbols, its owners.
        if (std::ranges::any_of(r->sections, [](const gen::SizeEntry& s) { return s.name == ".symtab"; })) {
            expect(std::ranges::any_of(r->owners, [](const gen::SizeEntry& o) { return o.name == "omui.gen.size"; })) << gen::to_markdown(*r, 15);
            expect(r->largest.size() == 10 && r->largest[0].bytes >= r->largest[9].bytes);
        } else {
            expect(r->functions == 0 && r->owners.empty());
        }
        expect(!gen::elf_size(std::vector<std::uint8_t>{0x7f, 'E', 'L', 'F'}).has_value()) << "cut short";
    };
}};
