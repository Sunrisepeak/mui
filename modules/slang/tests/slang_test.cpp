// openmui.slang with the slangc mcpp provisioned: every entry point of a
// file in every format asked for, with its resources counted from Slang's
// reflection; what breaks rhi's binding convention refused with where it
// should be; the C++ a build compiles, and rhi descriptions that a device's
// checks accept.
import std;
import openmui.rhi;
import openmui.slang;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

std::filesystem::path data(std::string_view name) {
    return std::filesystem::path(std::source_location::current().file_name()).parent_path() / "data" / name;
}

std::uint32_t word(const std::vector<std::byte>& b) {
    std::uint32_t w = 0;
    std::memcpy(&w, b.data(), 4);
    return w;
}

std::string text(const std::vector<std::byte>& b) { return {reinterpret_cast<const char*>(b.data()), b.size()}; }

std::string refusal(std::string_view file, rhi::ShaderFormats formats = rhi::formats({rhi::ShaderFormat::spirv})) {
    auto p = slang::compile(data(file), {.formats = formats});
    return p ? std::string("<compiled>") : p.error();
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"slang"};

    s.test("slangc is found: the provisioned one, unless OMUI_SLANGC names another", [] {
        auto c = slang::find_slangc();
        check(c.has_value(), c ? "" : c.error());
        check(!slang::provisioned_slangc.empty(), "mcpp provisioned xim:slang for this build");
    });

    s.test("a vertex and a fragment shader: each in SPIR-V and MSL, resources counted", [] {
        auto p = slang::compile(data("quad.slang"), {.formats = rhi::formats({rhi::ShaderFormat::spirv, rhi::ShaderFormat::msl})});
        check(p.has_value(), p ? "" : p.error());
        if (!p) return;
        check_eq(p->entries.size(), std::size_t{2});
        const slang::Entry* vs = p->entry("quad_vs");
        const slang::Entry* fs = p->entry("quad_fs");
        check(vs && fs);
        if (!vs || !fs) return;
        check(vs->stage == rhi::Stage::vertex && fs->stage == rhi::Stage::fragment);
        check(vs->counts == rhi::Counts{.uniform_buffers = 1}, "the vertex stage: one constant buffer");
        check(fs->counts == rhi::Counts{.samplers = 1, .uniform_buffers = 1}, "the fragment stage: a sampled texture and a constant buffer");
        const auto* spv = fs->code_for(rhi::ShaderFormat::spirv);
        check(spv && spv->bytes.size() > 20 && word(spv->bytes) == 0x07230203u, "SPIR-V, by its magic number");
        const auto* msl = fs->code_for(rhi::ShaderFormat::msl);
        check(msl && text(msl->bytes).contains("quad_fs") && text(msl->bytes).contains("[[texture(0)]]") &&
                  text(msl->bytes).starts_with("#pragma METAL fp math_mode(safe)\n#pragma METAL fp contract(off)\n"),
              "MSL, its entry point kept by name and its texture at slot 0");
        check(std::ranges::any_of(p->dependencies, [](const auto& d) { return d.filename() == "quad.slang"; }), "the depfile names the source");
        // The description a device checks.
        std::vector<rhi::ShaderCode> codes;
        const rhi::ShaderDesc d = slang::desc(*fs, codes, "quad.slang:quad_fs");
        check(rhi::check(d, rhi::formats({rhi::ShaderFormat::spirv})).empty() && d.samplers == 1 && d.entry == "quad_fs");
    });

    s.test("entries the caller names: the same program as discovery's, in one slangc start", [] {
        const auto formats = rhi::formats({rhi::ShaderFormat::spirv, rhi::ShaderFormat::msl});
        auto found = slang::compile(data("quad.slang"), {.formats = formats});
        const auto from = std::chrono::steady_clock::now();
        auto named = slang::compile(data("quad.slang"), {.formats = formats,
                                                         .entries = {{"quad_vs", rhi::Stage::vertex}, {"quad_fs", rhi::Stage::fragment}}});
        const auto took = std::chrono::steady_clock::now() - from;
        check(found.has_value() && named.has_value(), named ? "" : named.error());
        if (!found || !named) return;
        check_eq(named->entries.size(), std::size_t{2});
        for (const slang::Entry& e : found->entries) {
            const slang::Entry* n = named->entry(e.name);
            check(n && n->stage == e.stage && n->counts == e.counts && n->code.size() == e.code.size(), e.name);
            if (!n) continue;
            for (std::size_t i = 0; i < e.code.size() && i < n->code.size(); ++i)
                check(n->code[i].format == e.code[i].format && n->code[i].bytes == e.code[i].bytes, std::format("{}: the same bytes", e.name));
        }
        std::println("  named entries, SPIR-V and MSL: {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(took).count());
        // A name the file does not have is slangc's error, with the name.
        auto missing = slang::compile(data("quad.slang"), {.entries = {{"nope_fs", rhi::Stage::fragment}}});
        check(!missing && missing.error().contains("nope_fs"), missing ? "" : missing.error());
    });

    s.test("in this process (Slang's library, a warm session) and by slangc: the same program, the same diagnostics", [] {
        const auto formats = rhi::formats({rhi::ShaderFormat::spirv, rhi::ShaderFormat::msl});
        auto warm = slang::compile(data("quad.slang"), {.formats = formats});  // the session made, the core module loaded
        const auto from = std::chrono::steady_clock::now();
        auto inside = slang::compile(data("quad.slang"), {.formats = formats});
        const auto took = std::chrono::steady_clock::now() - from;
        auto outside = slang::compile(data("quad.slang"), {.formats = formats, .in_process = false});
        check(warm.has_value() && inside.has_value() && outside.has_value(), inside ? (outside ? "" : outside.error()) : inside.error());
        if (!inside || !outside) return;
        check_eq(inside->entries.size(), outside->entries.size());
        for (const slang::Entry& e : outside->entries) {
            const slang::Entry* i = inside->entry(e.name);
            check(i && i->stage == e.stage && i->counts == e.counts && i->code.size() == e.code.size(), e.name);
            if (!i) continue;
            for (std::size_t k = 0; k < e.code.size() && k < i->code.size(); ++k) {
                const auto& a = i->code[k].bytes;
                const auto& b = e.code[k].bytes;
                std::size_t at = 0;
                while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
                check(i->code[k].format == e.code[k].format && a == b,
                      std::format("{} ({}): the same bytes — {} and {} long, first difference at {}", e.name, rhi::name(e.code[k].format), a.size(),
                                  b.size(), at));
            }
        }
        auto names = [](const slang::Program& p) {
            std::set<std::string> out;
            for (const auto& d : p.dependencies) out.insert(d.filename().string());
            return out;
        };
        check(names(*inside) == names(*outside), "the same files read");
        std::println("  in this process, SPIR-V and MSL: {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(took).count());
        // A mistake: slangc's words either way, with where it is.
        const auto dir = scratch_dir("slang-inprocess");
        const auto broken = dir / "broken.slang";
        {
            std::ofstream f(broken, std::ios::binary);
            f << "[shader(\"fragment\")]\nfloat4 paint() : SV_Target {\n    return undefined_thing;\n}\n";
        }
        auto a = slang::compile(broken, {.entries = {{"paint", rhi::Stage::fragment}}});
        auto b = slang::compile(broken, {.entries = {{"paint", rhi::Stage::fragment}}, .in_process = false});
        check(!a && !b);
        if (!a && !b) {
            check(a.error().contains("undefined_thing") && a.error().contains("broken.slang:3:12"), a.error());
            check(b.error().contains("undefined_thing") && b.error().contains("broken.slang:3:12"), b.error());
        }
    });

    s.test("a compute shader: its work group, what it reads and writes", [] {
        auto p = slang::compile(data("blur.slang"));
        check(p.has_value(), p ? "" : p.error());
        if (!p) return;
        const slang::Entry& e = p->entries.at(0);
        check(e.stage == rhi::Stage::compute && e.name == "blur");
        check(e.threads == std::array<std::uint32_t, 3>{8, 8, 1});
        check(e.counts == rhi::Counts{.samplers = 1, .uniform_buffers = 1, .rw_storage_textures = 1});
    });

    s.test("what breaks the convention is refused, with where it should be", [] {
        check_eq(refusal("wrong_set.slang"),
                 std::string("wrong_set.slang (fragment, the fragment entry point `paint`): `image` is a sampled texture at set 0, binding 0; "
                             "as the stage's read-only resources it should be [[vk::binding(0, 2)]] … register(t0, space2) : register(s0, space2) "
                             "(rhi's binding convention: each kind right after the kind before it, from 0)"));
        check(refusal("separate.slang").contains("`smp` is a separate sampler: rhi binds a texture with its sampler"), refusal("separate.slang"));
        const std::string m = refusal("mismatch.slang", rhi::formats({rhi::ShaderFormat::spirv, rhi::ShaderFormat::msl}));
        check(m.contains("`b` is at set 2, binding 1 for Vulkan but at register(t5, space2) for D3D and Metal"), m);
        check(m.contains("write both: [[vk::binding(1, 2)]]"), m);
        check(refusal("missing.slang").contains("missing.slang"), "a file that is not there: slangc says so");
    });

    s.test("the C++ a build compiles: constants per entry, the same text for the same program", [] {
        auto p = slang::compile(data("quad.slang"));
        check(p.has_value(), p ? "" : p.error());
        if (!p) return;
        const slang::EmitOptions o{.module = "acme.render", .partition = "quad_shaders", .ns = "acme::render::shaders::quad"};
        const std::string cpp = slang::emit_cpp(*p, o);
        check(cpp.starts_with("// Generated by omui-slang from quad.slang"));
        check(cpp.contains("export module acme.render:quad_shaders;"));
        check(cpp.contains("inline constexpr omui::rhi::ShaderDesc quad_fs{"));
        check(cpp.contains("    .samplers = 1,\n"));
        check(cpp.contains("{omui::rhi::ShaderFormat::spirv, code::quad_vs_spirv}"));
        check(cpp.contains("inline constexpr std::array<const omui::rhi::ShaderDesc*, 2> entries{&quad_vs, &quad_fs};"));
        auto again = slang::compile(data("quad.slang"));
        check(again && slang::emit_cpp(*again, o) == cpp, "deterministic");
    });

    s.test("a depfile's prerequisites: escapes, Windows paths, continued lines", [] {
        using omui::slang::depfile_prerequisites;
        using V = std::vector<std::string>;
        check(depfile_prerequisites("out.spv: a.slang b\\ c.slang\n") == V{"a.slang", "b c.slang"});
        // slangc on Windows: the drive's colon escaped, backslash separators kept.
        check(depfile_prerequisites("D\\:/a/x.spv: D\\:/a/mui/shaders/ui.slang \\\r\n  D\\:\\a\\inc\\common.slang\r\n") ==
              V{"D:/a/mui/shaders/ui.slang", "D:\\a\\inc\\common.slang"});
        check(depfile_prerequisites("C:/out/x.spv: C:/src/x.slang \\\n C:/src/y.slang\n") == V{"C:/src/x.slang", "C:/src/y.slang"});
        check(depfile_prerequisites("x: a$$b \\#c\nother: d\n") == V{"a$b", "#c"});
        check(depfile_prerequisites("no rule here").empty());
    });

    s.test("the formats a build compiles to, by target and host", [] {
        using enum rhi::ShaderFormat;
        check(slang::formats_for("linux", "linux") == rhi::formats({spirv}));
        check(slang::formats_for("macos", "macos") == rhi::formats({msl}));
        check(slang::formats_for("windows", "windows") == rhi::formats({spirv, dxbc}));
        check(slang::formats_for("windows", "linux") == rhi::formats({spirv}), "DXBC needs the Windows host's compiler");
    });

    return s.run(argc, argv);
}
