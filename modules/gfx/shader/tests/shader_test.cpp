// openmui.shader with the slangc mcpp provisioned: every entry point of a
// file in every format asked for, with its resources counted from Slang's
// reflection; what breaks rhi's binding convention refused with where it
// should be; the C++ a build compiles, and rhi descriptions that a device's
// checks accept.
import std;
import mlibs.rhi;
import mlibs.shader;
import mlibs.testing;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;

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

std::string refusal(std::string_view file, mlibs::rhi::ShaderFormats formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv})) {
    auto p = mlibs::shader::compile(data(file), {.formats = formats});
    return p ? std::string("<compiled>") : p.error();
}

}  // namespace

UTest cases{"shader", [] {

    "slangc is found: the provisioned one, unless OMUI_SLANGC names another"_test = [] {
        auto c = mlibs::shader::find_slangc();
        expect(c.has_value()) << (c ? "" : c.error());
        expect(!mlibs::shader::provisioned_slangc.empty()) << "mcpp provisioned xim:slang for this build";
    };

    "a vertex and a fragment shader: each in SPIR-V and MSL, resources counted"_test = [] {
        auto p = mlibs::shader::compile(data("quad.slang"), {.formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::msl})});
        expect(p.has_value()) << (p ? "" : p.error());
        if (!p) return;
        expect(eq(p->entries.size(), std::size_t{2}));
        const mlibs::shader::Entry* vs = p->entry("quad_vs");
        const mlibs::shader::Entry* fs = p->entry("quad_fs");
        expect(vs && fs);
        if (!vs || !fs) return;
        expect(vs->stage == mlibs::rhi::Stage::vertex && fs->stage == mlibs::rhi::Stage::fragment);
        expect(vs->counts == mlibs::rhi::Counts{.uniform_buffers = 1}) << "the vertex stage: one constant buffer";
        expect(fs->counts == mlibs::rhi::Counts{.samplers = 1, .uniform_buffers = 1}) << "the fragment stage: a sampled texture and a constant buffer";
        const auto* spv = fs->code_for(mlibs::rhi::ShaderFormat::spirv);
        expect(spv && spv->bytes.size() > 20 && word(spv->bytes) == 0x07230203u) << "SPIR-V, by its magic number";
        const auto* msl = fs->code_for(mlibs::rhi::ShaderFormat::msl);
        expect(msl && text(msl->bytes).contains("quad_fs") && text(msl->bytes).contains("[[texture(0)]]") &&
                  text(msl->bytes).starts_with("#pragma METAL fp math_mode(safe)\n#pragma METAL fp contract(off)\n")) << "MSL, its entry point kept by name and its texture at slot 0";
        expect(std::ranges::any_of(p->dependencies, [](const auto& d) { return d.filename() == "quad.slang"; })) << "the depfile names the source";
        // The description a device checks.
        std::vector<mlibs::rhi::ShaderCode> codes;
        const mlibs::rhi::ShaderDesc d = mlibs::shader::desc(*fs, codes, "quad.slang:quad_fs");
        expect(mlibs::rhi::check(d, mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv})).empty() && d.samplers == 1 && d.entry == "quad_fs");
    };

    "entries the caller names: the same program as discovery's, in one slangc start"_test = [] {
        const auto formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::msl});
        auto found = mlibs::shader::compile(data("quad.slang"), {.formats = formats});
        const auto from = std::chrono::steady_clock::now();
        auto named = mlibs::shader::compile(data("quad.slang"), {.formats = formats,
                                                         .entries = {{"quad_vs", mlibs::rhi::Stage::vertex}, {"quad_fs", mlibs::rhi::Stage::fragment}}});
        const auto took = std::chrono::steady_clock::now() - from;
        expect(found.has_value() && named.has_value()) << (named ? "" : named.error());
        if (!found || !named) return;
        expect(eq(named->entries.size(), std::size_t{2}));
        for (const mlibs::shader::Entry& e : found->entries) {
            const mlibs::shader::Entry* n = named->entry(e.name);
            expect(n && n->stage == e.stage && n->counts == e.counts && n->code.size() == e.code.size()) << e.name;
            if (!n) continue;
            for (std::size_t i = 0; i < e.code.size() && i < n->code.size(); ++i)
                expect(n->code[i].format == e.code[i].format && n->code[i].bytes == e.code[i].bytes) << std::format("{}: the same bytes", e.name);
        }
        std::println("  named entries, SPIR-V and MSL: {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(took).count());
        // A name the file does not have is slangc's error, with the name.
        auto missing = mlibs::shader::compile(data("quad.slang"), {.entries = {{"nope_fs", mlibs::rhi::Stage::fragment}}});
        expect(!missing && missing.error().contains("nope_fs")) << (missing ? "" : missing.error());
    };

    "in this process (Slang's library, a warm session) and by slangc: the same program, the same diagnostics"_test = [] {
        const auto formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::msl});
        auto warm = mlibs::shader::compile(data("quad.slang"), {.formats = formats});  // the session made, the core module loaded
        const auto from = std::chrono::steady_clock::now();
        auto inside = mlibs::shader::compile(data("quad.slang"), {.formats = formats});
        const auto took = std::chrono::steady_clock::now() - from;
        auto outside = mlibs::shader::compile(data("quad.slang"), {.formats = formats, .in_process = false});
        expect(warm.has_value() && inside.has_value() && outside.has_value()) << (inside ? (outside ? "" : outside.error()) : inside.error());
        if (!inside || !outside) return;
        expect(eq(inside->entries.size(), outside->entries.size()));
        for (const mlibs::shader::Entry& e : outside->entries) {
            const mlibs::shader::Entry* i = inside->entry(e.name);
            expect(i && i->stage == e.stage && i->counts == e.counts && i->code.size() == e.code.size()) << e.name;
            if (!i) continue;
            for (std::size_t k = 0; k < e.code.size() && k < i->code.size(); ++k) {
                const auto& a = i->code[k].bytes;
                const auto& b = e.code[k].bytes;
                std::size_t at = 0;
                while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
                expect(i->code[k].format == e.code[k].format && a == b) << std::format("{} ({}): the same bytes — {} and {} long, first difference at {}", e.name, mlibs::rhi::name(e.code[k].format), a.size(),
                                  b.size(), at);
            }
        }
        auto names = [](const mlibs::shader::Program& p) {
            std::set<std::string> out;
            for (const auto& d : p.dependencies) out.insert(d.filename().string());
            return out;
        };
        expect(names(*inside) == names(*outside)) << "the same files read";
        std::println("  in this process, SPIR-V and MSL: {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(took).count());
        // A mistake: slangc's words either way, with where it is.
        const auto dir = scratch_dir("slang-inprocess");
        const auto broken = dir / "broken.slang";
        {
            std::ofstream f(broken, std::ios::binary);
            f << "[shader(\"fragment\")]\nfloat4 paint() : SV_Target {\n    return undefined_thing;\n}\n";
        }
        auto a = mlibs::shader::compile(broken, {.entries = {{"paint", mlibs::rhi::Stage::fragment}}});
        auto b = mlibs::shader::compile(broken, {.entries = {{"paint", mlibs::rhi::Stage::fragment}}, .in_process = false});
        expect(!a && !b);
        if (!a && !b) {
            expect(a.error().contains("undefined_thing") && a.error().contains("broken.slang:3:12")) << a.error();
            expect(b.error().contains("undefined_thing") && b.error().contains("broken.slang:3:12")) << b.error();
        }
    };

    "a compute shader: its work group, what it reads and writes"_test = [] {
        auto p = mlibs::shader::compile(data("blur.slang"));
        expect(p.has_value()) << (p ? "" : p.error());
        if (!p) return;
        const mlibs::shader::Entry& e = p->entries.at(0);
        expect(e.stage == mlibs::rhi::Stage::compute && e.name == "blur");
        expect(e.threads == std::array<std::uint32_t, 3>{8, 8, 1});
        expect(e.counts == mlibs::rhi::Counts{.samplers = 1, .uniform_buffers = 1, .rw_storage_textures = 1});
    };

    "MSL: bound as SDL_GPU binds Metal, not by register numbers (uniforms, then read-only, then read-write)"_test = [] {
        auto p = mlibs::shader::compile(data("buffers.slang"), {.formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::msl})});
        expect(p.has_value()) << (p ? "" : p.error());
        if (!p) return;
        const mlibs::shader::Entry& e = p->entries.at(0);
        expect(e.counts == mlibs::rhi::Counts{.storage_buffers = 2, .uniform_buffers = 1, .rw_storage_textures = 1, .rw_storage_buffers = 1});
        const auto* msl = e.code_for(mlibs::rhi::ShaderFormat::msl);
        expect(msl != nullptr);
        if (!msl) return;
        const std::string t = text(msl->bytes);
        // Slang alone writes t0, u1 and b0 as buffer(0), buffer(1), buffer(0).
        expect(t.contains("params_1 [[buffer(0)]]")) << "the uniform buffer first";
        expect(t.contains("before_1 [[buffer(1)]]") && t.contains("rules_1 [[buffer(2)]]")) << "then the read-only storage buffers";
        expect(t.contains("after_1 [[buffer(3)]]")) << "then the read-write storage buffer";
        expect(t.contains("marks_1 [[texture(0)]]")) << "the read-write storage texture after no read-only ones";
    };

    "what breaks the convention is refused, with where it should be"_test = [] {
        expect(eq(refusal("wrong_set.slang"),
                 std::string("wrong_set.slang (fragment, the fragment entry point `paint`): `image` is a sampled texture at set 0, binding 0; "
                             "as the stage's read-only resources it should be [[vk::binding(0, 2)]] … register(t0, space2) : register(s0, space2) "
                             "(rhi's binding convention: each kind right after the kind before it, from 0)")));
        expect(refusal("separate.slang").contains("`smp` is a separate sampler: rhi binds a texture with its sampler")) << refusal("separate.slang");
        const std::string m = refusal("mismatch.slang", mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::msl}));
        expect(m.contains("`b` is at set 2, binding 1 for Vulkan but at register(t5, space2) for D3D and Metal")) << m;
        expect(m.contains("write both: [[vk::binding(1, 2)]]")) << m;
        expect(refusal("missing.slang").contains("missing.slang")) << "a file that is not there: slangc says so";
    };

    "the C++ a build compiles: constants per entry, the same text for the same program"_test = [] {
        auto p = mlibs::shader::compile(data("quad.slang"));
        expect(p.has_value()) << (p ? "" : p.error());
        if (!p) return;
        const mlibs::shader::EmitOptions o{.module = "acme.render", .partition = "quad_shaders", .ns = "acme::render::shaders::quad"};
        const std::string cpp = mlibs::shader::emit_cpp(*p, o);
        expect(cpp.starts_with("// Generated by omui-shader from quad.slang"));
        expect(cpp.contains("export module acme.render:quad_shaders;"));
        expect(cpp.contains("inline constexpr mlibs::rhi::ShaderDesc quad_fs{"));
        expect(cpp.contains("    .samplers = 1,\n"));
        expect(cpp.contains("{mlibs::rhi::ShaderFormat::spirv, code::quad_vs_spirv}"));
        expect(cpp.contains("inline constexpr std::array<const mlibs::rhi::ShaderDesc*, 2> entries{&quad_vs, &quad_fs};"));
        auto again = mlibs::shader::compile(data("quad.slang"));
        expect(again && mlibs::shader::emit_cpp(*again, o) == cpp) << "deterministic";
    };

    "a depfile's prerequisites: escapes, Windows paths, continued lines"_test = [] {
        using mlibs::shader::depfile_prerequisites;
        using V = std::vector<std::string>;
        expect(depfile_prerequisites("out.spv: a.slang b\\ c.slang\n") == V{"a.slang", "b c.slang"});
        // slangc on Windows: the drive's colon escaped, backslash separators kept.
        expect(depfile_prerequisites("D\\:/a/x.spv: D\\:/a/mui/shaders/ui.slang \\\r\n  D\\:\\a\\inc\\common.slang\r\n") ==
              V{"D:/a/mui/shaders/ui.slang", "D:\\a\\inc\\common.slang"});
        expect(depfile_prerequisites("C:/out/x.spv: C:/src/x.slang \\\n C:/src/y.slang\n") == V{"C:/src/x.slang", "C:/src/y.slang"});
        expect(depfile_prerequisites("x: a$$b \\#c\nother: d\n") == V{"a$b", "#c"});
        expect(depfile_prerequisites("no rule here").empty());
    };

    "WGSL: each sampled texture and its sampler a pair of bindings, storage after them, write-only where only written"_test = [] {
        auto p = mlibs::shader::compile(data("layers.slang"), {.formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::wgsl})});
        expect(p.has_value()) << (p ? "" : p.error());
        if (!p) return;
        const mlibs::shader::Entry* fs = p->entry("layers_fs");
        const mlibs::shader::Entry* cs = p->entry("layers_cs");
        expect(fs && cs && fs->code_for(mlibs::rhi::ShaderFormat::wgsl) && cs->code_for(mlibs::rhi::ShaderFormat::wgsl));
        if (!fs || !cs) return;
        expect(fs->counts == mlibs::rhi::Counts{.samplers = 2, .storage_buffers = 1, .uniform_buffers = 1});
        const std::string f = text(fs->code_for(mlibs::rhi::ShaderFormat::wgsl)->bytes);
        // Slang wrote base's sampler and over's texture both at binding 1.
        expect(f.contains("@binding(0) @group(2) var base_texture_0 : texture_2d<f32>;")) << f;
        expect(f.contains("@binding(1) @group(2) var base_sampler_0 : sampler;")) << f;
        expect(f.contains("@binding(2) @group(2) var over_texture_0 : texture_2d<f32>;")) << f;
        expect(f.contains("@binding(3) @group(2) var over_sampler_0 : sampler;")) << f;
        expect(f.contains("@binding(4) @group(2) var<storage, read> weights_0")) << f;
        expect(f.contains("@binding(0) @group(3) var<uniform> mix_0")) << f;
        const std::string c = text(cs->code_for(mlibs::rhi::ShaderFormat::wgsl)->bytes);
        expect(c.contains("@binding(0) @group(1) var written_0 : texture_storage_2d<rgba8unorm, write>;")) << c;
        expect(c.contains("@binding(1) @group(1) var summed_0 : texture_storage_2d<r32float, read_write>;")) << c;
        expect(fs->code_for(mlibs::rhi::ShaderFormat::spirv) != nullptr) << "SPIR-V beside it, unchanged";
        // What Slang writes that is not WGSL: `precise var`, and @interpolate
        // on a struct the fragment stage copies (no @location there).
        auto flat = mlibs::shader::compile(data("flat.slang"), {.formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::wgsl})});
        expect(flat.has_value()) << (flat ? "" : flat.error());
        if (flat)
            for (const mlibs::shader::Entry& e : flat->entries) {
                const std::string w = text(e.code_for(mlibs::rhi::ShaderFormat::wgsl)->bytes);
                expect(!w.contains("precise")) << w;
                for (auto line : std::views::split(w, '\n')) {
                    const std::string_view l(line.begin(), line.end());
                    expect(!l.contains("@interpolate(") || l.contains("@location(")) << std::string(l);
                }
                if (e.stage == mlibs::rhi::Stage::fragment) expect(w.contains("@location(0) @interpolate(flat)") || w.contains("@interpolate(flat) @location(0)")) << w;
            }
        // A storage texture with no format: WGSL needs one.
        const std::string m = refusal("unformatted.slang", mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::wgsl}));
        expect(m.contains("`target` is a read-write storage texture with no format") && m.contains("[format(\"rgba8\")]")) << m;
        expect(eq(refusal("unformatted.slang"), std::string("<compiled>")));
    };

    "ESSL: GLSL ES 3.00 with rhi's slots and locations by name, GL's clip space, nothing ES lacks"_test = [] {
        const auto formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::essl});
        std::vector<std::pair<std::string, std::string>> written;  // name, text: what glslang is given below
        auto essl = [&](const mlibs::shader::Entry* e) {
            const auto* c = e ? e->code_for(mlibs::rhi::ShaderFormat::essl) : nullptr;
            if (!c) return std::string();
            written.emplace_back(e->name, text(c->bytes));
            return written.back().second;
        };
        auto quad = mlibs::shader::compile(data("quad.slang"), {.formats = formats});
        expect(quad.has_value()) << (quad ? "" : quad.error());
        if (!quad) return;
        const std::string vs = essl(quad->entry("quad_vs")), fs = essl(quad->entry("quad_fs"));
        expect(vs.starts_with("#version 300 es\n") && fs.starts_with("#version 300 es\nprecision highp float;")) << vs;
        expect(vs.contains("layout(std140) uniform omui_vs_uniform0")) << vs;
        expect(vs.contains("out vec2 omui_v0;") && fs.contains("in vec2 omui_v0;")) << "a varying by its location";
        expect(vs.contains("void omui_main()") && vs.contains("gl_Position.y = -gl_Position.y;") &&
                  vs.contains("gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;")) << "the vertex stage ends in GL's clip space";
        expect(fs.contains("uniform sampler2D omui_fs_sampler0;") && fs.contains("texture((omui_fs_sampler0)")) << fs;
        expect(fs.contains("layout(std140) uniform omui_fs_uniform0")) << fs;
        for (const std::string& t : {vs, fs}) expect(!t.contains("layout(binding") && !t.contains("#line") && !t.contains("#extension")) << t;
        // Flat varyings, `precise` gone.
        auto flat = mlibs::shader::compile(data("flat.slang"), {.formats = formats});
        expect(flat.has_value()) << (flat ? "" : flat.error());
        if (flat) {
            const std::string fv = essl(flat->entry("flat_vs")), ff = essl(flat->entry("flat_fs"));
            expect(fv.contains("flat out vec4 omui_v0;") && ff.contains("flat in vec4 omui_v0;")) << fv;
            expect(!ff.contains("precise")) << ff;
        }
        // Draw parameters through omui_base; a name GLSL reserves renamed.
        auto drawn = mlibs::shader::compile(data("drawn.slang"), {.formats = formats});
        expect(drawn.has_value()) << (drawn ? "" : drawn.error());
        if (drawn) {
            const std::string dv = essl(drawn->entry("drawn_vs"));
            expect(dv.contains("uniform highp ivec4 omui_base;") && dv.contains("gl_VertexID + omui_base.x") && dv.contains("gl_InstanceID + omui_base.z")) << dv;
            expect(!dv.contains("gl_VertexIndex") && !dv.contains("gl_InstanceIndex") && !dv.contains("gl_BaseVertex")) << dv;
            expect(dv.contains("omui_gl_0") && !dv.contains(" gl_0")) << dv;
            expect(essl(drawn->entry("drawn_fs")).contains("flat in uint omui_v0;"));
        }
        // No compute and no storage in ES 3.00: such entries have none.
        auto layers = mlibs::shader::compile(data("layers.slang"), {.formats = formats});
        expect(layers.has_value()) << (layers ? "" : layers.error());
        if (layers) {
            expect(!layers->entry("layers_fs")->code_for(mlibs::rhi::ShaderFormat::essl)) << "a storage buffer: no ESSL";
            expect(!layers->entry("layers_cs")->code_for(mlibs::rhi::ShaderFormat::essl)) << "compute: no ESSL";
            expect(layers->entry("layers_fs")->code_for(mlibs::rhi::ShaderFormat::spirv) != nullptr);
        }
        // What glslang (OMUI_GLSLANG, or glslangValidator on PATH) says of it all.
        std::optional<std::filesystem::path> glslang;
        if (auto g = mcpplibs::os::env::get("OMUI_GLSLANG"); g && !g->empty()) glslang = mcpplibs::os::fs::from_utf8(*g);
        else if (auto p = mcpplibs::os::env::find_executable("glslangValidator")) glslang = *p;
        if (!glslang) {
            std::println("  no glslangValidator (OMUI_GLSLANG): the ESSL is not compiled here; tools/ci/browser.sh compiles it in the browser");
            return;
        }
        const auto dir = std::filesystem::temp_directory_path() / "omui-essl-test";
        std::filesystem::create_directories(dir);
        for (const auto& [name, t] : written) {
            const auto file = dir / (name + (name.ends_with("_vs") ? ".vert" : ".frag"));
            std::ofstream(file, std::ios::binary) << t;
            mcpplibs::os::process::spawn_options o;
            o.program = *glslang;
            o.args = {mcpplibs::os::fs::utf8(file)};
            auto r = mcpplibs::os::process::run(std::move(o));
            expect(r && r->status.success()) << std::format("glslang on {}: {}", name, r ? r->out : r.error().message());
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    };

    "compact text: the same tokens with nothing between them that a compiler does not read"_test = [] {
        using mlibs::shader::compact;
        expect(eq(compact("#version 300 es\nprecision highp float;\n\nstruct Lamp_0\n{\n    vec4 colour_0;  // its light\n};\n"), std::string("#version 300 es\nprecision highp float;struct Lamp_0{vec4 colour_0;};")));
        // Two words stay two words; two operator characters stay two operators.
        expect(eq(compact("return a - -b;"), std::string("return a- -b;")));
        expect(eq(compact("x = y / *p + + z;\nconst  highp   float w = 1.0 - .5;"), std::string("x=y/ *p+ +z;const highp float w=1.0-.5;")));
        expect(eq(compact("var<uniform> mix_0 : Mix_std140_0;\nfn main( a : vec4<f32>) -> @location(0) vec4<f32>\n{\n}\n"), std::string("var<uniform>mix_0:Mix_std140_0;fn main(a:vec4<f32>)->@location(0)vec4<f32>{}")));
        // A word at a line's end and one at the next line's start are two words.
        expect(eq(compact("else\nreturn"), std::string("else return")));
        // A directive is a line of its own wherever it stands.
        expect(eq(compact("a;\n  #define B 1  \nb;"), std::string("a;\n#define B 1\nb;")));
        expect(eq(compact(""), std::string()) && eq(compact(" \n\t\n"), std::string()));
        // What a build carries: the compiled text, compact — and compacting it again changes nothing.
        const auto both = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::wgsl, mlibs::rhi::ShaderFormat::essl});
        auto plain = mlibs::shader::compile(data("quad.slang"), {.formats = both});
        auto small = mlibs::shader::compile(data("quad.slang"), {.formats = both, .compact = true});
        expect(fatal(plain.has_value() && small.has_value())) << (plain ? (small ? "" : small.error()) : plain.error());
        std::size_t before = 0, after = 0;
        for (const mlibs::shader::Entry& entry : plain->entries)
            for (const mlibs::shader::Entry::Code& code : entry.code) {
                const mlibs::shader::Entry* other = small->entry(entry.name);
                const auto* same = other ? other->code_for(code.format) : nullptr;
                if (!(expect(same != nullptr))) continue;
                const std::string was = text(code.bytes), is = text(same->bytes);
                expect(eq(is, compact(was)) && eq(compact(is), is)) << is;
                expect(!is.contains("  ") && !is.contains("\n\n") && (!is.contains('\n') || is.starts_with("#version 300 es\n"))) << is;
                before += was.size(), after += is.size();
            }
        // A quad's two small shaders: 2,795 characters become 2,394 (the mesh pass's, with bodies to indent: 30% fewer).
        expect(gt(before, std::size_t{0}) && lt(after * 100, before * 90)) << before << ' ' << after;
    };

    "the formats a build compiles to, by target and host"_test = [] {
        using enum mlibs::rhi::ShaderFormat;
        expect(mlibs::shader::formats_for("linux", "linux") == mlibs::rhi::formats({spirv}));
        expect(mlibs::shader::formats_for("macos", "macos") == mlibs::rhi::formats({msl}));
        expect(mlibs::shader::formats_for("windows", "windows") == mlibs::rhi::formats({spirv, dxbc}));
        expect(mlibs::shader::formats_for("windows", "linux") == mlibs::rhi::formats({spirv})) << "DXBC needs the Windows host's compiler";
        expect(mlibs::shader::formats_for("emscripten", "linux") == mlibs::rhi::formats({wgsl, essl})) << "the Web: WebGPU's and WebGL2's";
    };
}};
