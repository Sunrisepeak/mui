// openmui.shader with the slangc mcpp provisioned: every entry point of a
// file in every format asked for, with its resources counted from Slang's
// reflection; what breaks rhi's binding convention refused with where it
// should be; the C++ a build compiles, and rhi descriptions that a device's
// checks accept.
import std;
import mlibs.rhi;
import mlibs.shader;
import mlibs.testing;
import mcpplibs.os;

using namespace mlibs::testing;

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

int main(int argc, char** argv) {
    Suite s{"shader"};

    s.test("slangc is found: the provisioned one, unless OMUI_SLANGC names another", [] {
        auto c = mlibs::shader::find_slangc();
        check(c.has_value(), c ? "" : c.error());
        check(!mlibs::shader::provisioned_slangc.empty(), "mcpp provisioned xim:slang for this build");
    });

    s.test("a vertex and a fragment shader: each in SPIR-V and MSL, resources counted", [] {
        auto p = mlibs::shader::compile(data("quad.slang"), {.formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::msl})});
        check(p.has_value(), p ? "" : p.error());
        if (!p) return;
        check_eq(p->entries.size(), std::size_t{2});
        const mlibs::shader::Entry* vs = p->entry("quad_vs");
        const mlibs::shader::Entry* fs = p->entry("quad_fs");
        check(vs && fs);
        if (!vs || !fs) return;
        check(vs->stage == mlibs::rhi::Stage::vertex && fs->stage == mlibs::rhi::Stage::fragment);
        check(vs->counts == mlibs::rhi::Counts{.uniform_buffers = 1}, "the vertex stage: one constant buffer");
        check(fs->counts == mlibs::rhi::Counts{.samplers = 1, .uniform_buffers = 1}, "the fragment stage: a sampled texture and a constant buffer");
        const auto* spv = fs->code_for(mlibs::rhi::ShaderFormat::spirv);
        check(spv && spv->bytes.size() > 20 && word(spv->bytes) == 0x07230203u, "SPIR-V, by its magic number");
        const auto* msl = fs->code_for(mlibs::rhi::ShaderFormat::msl);
        check(msl && text(msl->bytes).contains("quad_fs") && text(msl->bytes).contains("[[texture(0)]]") &&
                  text(msl->bytes).starts_with("#pragma METAL fp math_mode(safe)\n#pragma METAL fp contract(off)\n"),
              "MSL, its entry point kept by name and its texture at slot 0");
        check(std::ranges::any_of(p->dependencies, [](const auto& d) { return d.filename() == "quad.slang"; }), "the depfile names the source");
        // The description a device checks.
        std::vector<mlibs::rhi::ShaderCode> codes;
        const mlibs::rhi::ShaderDesc d = mlibs::shader::desc(*fs, codes, "quad.slang:quad_fs");
        check(mlibs::rhi::check(d, mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv})).empty() && d.samplers == 1 && d.entry == "quad_fs");
    });

    s.test("entries the caller names: the same program as discovery's, in one slangc start", [] {
        const auto formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::msl});
        auto found = mlibs::shader::compile(data("quad.slang"), {.formats = formats});
        const auto from = std::chrono::steady_clock::now();
        auto named = mlibs::shader::compile(data("quad.slang"), {.formats = formats,
                                                         .entries = {{"quad_vs", mlibs::rhi::Stage::vertex}, {"quad_fs", mlibs::rhi::Stage::fragment}}});
        const auto took = std::chrono::steady_clock::now() - from;
        check(found.has_value() && named.has_value(), named ? "" : named.error());
        if (!found || !named) return;
        check_eq(named->entries.size(), std::size_t{2});
        for (const mlibs::shader::Entry& e : found->entries) {
            const mlibs::shader::Entry* n = named->entry(e.name);
            check(n && n->stage == e.stage && n->counts == e.counts && n->code.size() == e.code.size(), e.name);
            if (!n) continue;
            for (std::size_t i = 0; i < e.code.size() && i < n->code.size(); ++i)
                check(n->code[i].format == e.code[i].format && n->code[i].bytes == e.code[i].bytes, std::format("{}: the same bytes", e.name));
        }
        std::println("  named entries, SPIR-V and MSL: {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(took).count());
        // A name the file does not have is slangc's error, with the name.
        auto missing = mlibs::shader::compile(data("quad.slang"), {.entries = {{"nope_fs", mlibs::rhi::Stage::fragment}}});
        check(!missing && missing.error().contains("nope_fs"), missing ? "" : missing.error());
    });

    s.test("in this process (Slang's library, a warm session) and by slangc: the same program, the same diagnostics", [] {
        const auto formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::msl});
        auto warm = mlibs::shader::compile(data("quad.slang"), {.formats = formats});  // the session made, the core module loaded
        const auto from = std::chrono::steady_clock::now();
        auto inside = mlibs::shader::compile(data("quad.slang"), {.formats = formats});
        const auto took = std::chrono::steady_clock::now() - from;
        auto outside = mlibs::shader::compile(data("quad.slang"), {.formats = formats, .in_process = false});
        check(warm.has_value() && inside.has_value() && outside.has_value(), inside ? (outside ? "" : outside.error()) : inside.error());
        if (!inside || !outside) return;
        check_eq(inside->entries.size(), outside->entries.size());
        for (const mlibs::shader::Entry& e : outside->entries) {
            const mlibs::shader::Entry* i = inside->entry(e.name);
            check(i && i->stage == e.stage && i->counts == e.counts && i->code.size() == e.code.size(), e.name);
            if (!i) continue;
            for (std::size_t k = 0; k < e.code.size() && k < i->code.size(); ++k) {
                const auto& a = i->code[k].bytes;
                const auto& b = e.code[k].bytes;
                std::size_t at = 0;
                while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
                check(i->code[k].format == e.code[k].format && a == b,
                      std::format("{} ({}): the same bytes — {} and {} long, first difference at {}", e.name, mlibs::rhi::name(e.code[k].format), a.size(),
                                  b.size(), at));
            }
        }
        auto names = [](const mlibs::shader::Program& p) {
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
        auto a = mlibs::shader::compile(broken, {.entries = {{"paint", mlibs::rhi::Stage::fragment}}});
        auto b = mlibs::shader::compile(broken, {.entries = {{"paint", mlibs::rhi::Stage::fragment}}, .in_process = false});
        check(!a && !b);
        if (!a && !b) {
            check(a.error().contains("undefined_thing") && a.error().contains("broken.slang:3:12"), a.error());
            check(b.error().contains("undefined_thing") && b.error().contains("broken.slang:3:12"), b.error());
        }
    });

    s.test("a compute shader: its work group, what it reads and writes", [] {
        auto p = mlibs::shader::compile(data("blur.slang"));
        check(p.has_value(), p ? "" : p.error());
        if (!p) return;
        const mlibs::shader::Entry& e = p->entries.at(0);
        check(e.stage == mlibs::rhi::Stage::compute && e.name == "blur");
        check(e.threads == std::array<std::uint32_t, 3>{8, 8, 1});
        check(e.counts == mlibs::rhi::Counts{.samplers = 1, .uniform_buffers = 1, .rw_storage_textures = 1});
    });

    s.test("MSL: bound as SDL_GPU binds Metal, not by register numbers (uniforms, then read-only, then read-write)", [] {
        auto p = mlibs::shader::compile(data("buffers.slang"), {.formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::msl})});
        check(p.has_value(), p ? "" : p.error());
        if (!p) return;
        const mlibs::shader::Entry& e = p->entries.at(0);
        check(e.counts == mlibs::rhi::Counts{.storage_buffers = 2, .uniform_buffers = 1, .rw_storage_textures = 1, .rw_storage_buffers = 1});
        const auto* msl = e.code_for(mlibs::rhi::ShaderFormat::msl);
        check(msl != nullptr);
        if (!msl) return;
        const std::string t = text(msl->bytes);
        // Slang alone writes t0, u1 and b0 as buffer(0), buffer(1), buffer(0).
        check(t.contains("params_1 [[buffer(0)]]"), "the uniform buffer first");
        check(t.contains("before_1 [[buffer(1)]]") && t.contains("rules_1 [[buffer(2)]]"), "then the read-only storage buffers");
        check(t.contains("after_1 [[buffer(3)]]"), "then the read-write storage buffer");
        check(t.contains("marks_1 [[texture(0)]]"), "the read-write storage texture after no read-only ones");
    });

    s.test("what breaks the convention is refused, with where it should be", [] {
        check_eq(refusal("wrong_set.slang"),
                 std::string("wrong_set.slang (fragment, the fragment entry point `paint`): `image` is a sampled texture at set 0, binding 0; "
                             "as the stage's read-only resources it should be [[vk::binding(0, 2)]] … register(t0, space2) : register(s0, space2) "
                             "(rhi's binding convention: each kind right after the kind before it, from 0)"));
        check(refusal("separate.slang").contains("`smp` is a separate sampler: rhi binds a texture with its sampler"), refusal("separate.slang"));
        const std::string m = refusal("mismatch.slang", mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::msl}));
        check(m.contains("`b` is at set 2, binding 1 for Vulkan but at register(t5, space2) for D3D and Metal"), m);
        check(m.contains("write both: [[vk::binding(1, 2)]]"), m);
        check(refusal("missing.slang").contains("missing.slang"), "a file that is not there: slangc says so");
    });

    s.test("the C++ a build compiles: constants per entry, the same text for the same program", [] {
        auto p = mlibs::shader::compile(data("quad.slang"));
        check(p.has_value(), p ? "" : p.error());
        if (!p) return;
        const mlibs::shader::EmitOptions o{.module = "acme.render", .partition = "quad_shaders", .ns = "acme::render::shaders::quad"};
        const std::string cpp = mlibs::shader::emit_cpp(*p, o);
        check(cpp.starts_with("// Generated by omui-shader from quad.slang"));
        check(cpp.contains("export module acme.render:quad_shaders;"));
        check(cpp.contains("inline constexpr mlibs::rhi::ShaderDesc quad_fs{"));
        check(cpp.contains("    .samplers = 1,\n"));
        check(cpp.contains("{mlibs::rhi::ShaderFormat::spirv, code::quad_vs_spirv}"));
        check(cpp.contains("inline constexpr std::array<const mlibs::rhi::ShaderDesc*, 2> entries{&quad_vs, &quad_fs};"));
        auto again = mlibs::shader::compile(data("quad.slang"));
        check(again && mlibs::shader::emit_cpp(*again, o) == cpp, "deterministic");
    });

    s.test("a depfile's prerequisites: escapes, Windows paths, continued lines", [] {
        using mlibs::shader::depfile_prerequisites;
        using V = std::vector<std::string>;
        check(depfile_prerequisites("out.spv: a.slang b\\ c.slang\n") == V{"a.slang", "b c.slang"});
        // slangc on Windows: the drive's colon escaped, backslash separators kept.
        check(depfile_prerequisites("D\\:/a/x.spv: D\\:/a/mui/shaders/ui.slang \\\r\n  D\\:\\a\\inc\\common.slang\r\n") ==
              V{"D:/a/mui/shaders/ui.slang", "D:\\a\\inc\\common.slang"});
        check(depfile_prerequisites("C:/out/x.spv: C:/src/x.slang \\\n C:/src/y.slang\n") == V{"C:/src/x.slang", "C:/src/y.slang"});
        check(depfile_prerequisites("x: a$$b \\#c\nother: d\n") == V{"a$b", "#c"});
        check(depfile_prerequisites("no rule here").empty());
    });

    s.test("WGSL: each sampled texture and its sampler a pair of bindings, storage after them, write-only where only written", [] {
        auto p = mlibs::shader::compile(data("layers.slang"), {.formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::wgsl})});
        check(p.has_value(), p ? "" : p.error());
        if (!p) return;
        const mlibs::shader::Entry* fs = p->entry("layers_fs");
        const mlibs::shader::Entry* cs = p->entry("layers_cs");
        check(fs && cs && fs->code_for(mlibs::rhi::ShaderFormat::wgsl) && cs->code_for(mlibs::rhi::ShaderFormat::wgsl));
        if (!fs || !cs) return;
        check(fs->counts == mlibs::rhi::Counts{.samplers = 2, .storage_buffers = 1, .uniform_buffers = 1});
        const std::string f = text(fs->code_for(mlibs::rhi::ShaderFormat::wgsl)->bytes);
        // Slang wrote base's sampler and over's texture both at binding 1.
        check(f.contains("@binding(0) @group(2) var base_texture_0 : texture_2d<f32>;"), f);
        check(f.contains("@binding(1) @group(2) var base_sampler_0 : sampler;"), f);
        check(f.contains("@binding(2) @group(2) var over_texture_0 : texture_2d<f32>;"), f);
        check(f.contains("@binding(3) @group(2) var over_sampler_0 : sampler;"), f);
        check(f.contains("@binding(4) @group(2) var<storage, read> weights_0"), f);
        check(f.contains("@binding(0) @group(3) var<uniform> mix_0"), f);
        const std::string c = text(cs->code_for(mlibs::rhi::ShaderFormat::wgsl)->bytes);
        check(c.contains("@binding(0) @group(1) var written_0 : texture_storage_2d<rgba8unorm, write>;"), c);
        check(c.contains("@binding(1) @group(1) var summed_0 : texture_storage_2d<r32float, read_write>;"), c);
        check(fs->code_for(mlibs::rhi::ShaderFormat::spirv) != nullptr, "SPIR-V beside it, unchanged");
        // What Slang writes that is not WGSL: `precise var`, and @interpolate
        // on a struct the fragment stage copies (no @location there).
        auto flat = mlibs::shader::compile(data("flat.slang"), {.formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::wgsl})});
        check(flat.has_value(), flat ? "" : flat.error());
        if (flat)
            for (const mlibs::shader::Entry& e : flat->entries) {
                const std::string w = text(e.code_for(mlibs::rhi::ShaderFormat::wgsl)->bytes);
                check(!w.contains("precise"), w);
                for (auto line : std::views::split(w, '\n')) {
                    const std::string_view l(line.begin(), line.end());
                    check(!l.contains("@interpolate(") || l.contains("@location("), std::string(l));
                }
                if (e.stage == mlibs::rhi::Stage::fragment) check(w.contains("@location(0) @interpolate(flat)") || w.contains("@interpolate(flat) @location(0)"), w);
            }
        // A storage texture with no format: WGSL needs one.
        const std::string m = refusal("unformatted.slang", mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::wgsl}));
        check(m.contains("`target` is a read-write storage texture with no format") && m.contains("[format(\"rgba8\")]"), m);
        check_eq(refusal("unformatted.slang"), std::string("<compiled>"));
    });

    s.test("ESSL: GLSL ES 3.00 with rhi's slots and locations by name, GL's clip space, nothing ES lacks", [] {
        const auto formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv, mlibs::rhi::ShaderFormat::essl});
        std::vector<std::pair<std::string, std::string>> written;  // name, text: what glslang is given below
        auto essl = [&](const mlibs::shader::Entry* e) {
            const auto* c = e ? e->code_for(mlibs::rhi::ShaderFormat::essl) : nullptr;
            if (!c) return std::string();
            written.emplace_back(e->name, text(c->bytes));
            return written.back().second;
        };
        auto quad = mlibs::shader::compile(data("quad.slang"), {.formats = formats});
        check(quad.has_value(), quad ? "" : quad.error());
        if (!quad) return;
        const std::string vs = essl(quad->entry("quad_vs")), fs = essl(quad->entry("quad_fs"));
        check(vs.starts_with("#version 300 es\n") && fs.starts_with("#version 300 es\nprecision highp float;"), vs);
        check(vs.contains("layout(std140) uniform omui_vs_uniform0"), vs);
        check(vs.contains("out vec2 omui_v0;") && fs.contains("in vec2 omui_v0;"), "a varying by its location");
        check(vs.contains("void omui_main()") && vs.contains("gl_Position.y = -gl_Position.y;") &&
                  vs.contains("gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;"),
              "the vertex stage ends in GL's clip space");
        check(fs.contains("uniform sampler2D omui_fs_sampler0;") && fs.contains("texture((omui_fs_sampler0)"), fs);
        check(fs.contains("layout(std140) uniform omui_fs_uniform0"), fs);
        for (const std::string& t : {vs, fs}) check(!t.contains("layout(binding") && !t.contains("#line") && !t.contains("#extension"), t);
        // Flat varyings, `precise` gone.
        auto flat = mlibs::shader::compile(data("flat.slang"), {.formats = formats});
        check(flat.has_value(), flat ? "" : flat.error());
        if (flat) {
            const std::string fv = essl(flat->entry("flat_vs")), ff = essl(flat->entry("flat_fs"));
            check(fv.contains("flat out vec4 omui_v0;") && ff.contains("flat in vec4 omui_v0;"), fv);
            check(!ff.contains("precise"), ff);
        }
        // Draw parameters through omui_base; a name GLSL reserves renamed.
        auto drawn = mlibs::shader::compile(data("drawn.slang"), {.formats = formats});
        check(drawn.has_value(), drawn ? "" : drawn.error());
        if (drawn) {
            const std::string dv = essl(drawn->entry("drawn_vs"));
            check(dv.contains("uniform highp ivec4 omui_base;") && dv.contains("gl_VertexID + omui_base.x") && dv.contains("gl_InstanceID + omui_base.z"),
                  dv);
            check(!dv.contains("gl_VertexIndex") && !dv.contains("gl_InstanceIndex") && !dv.contains("gl_BaseVertex"), dv);
            check(dv.contains("omui_gl_0") && !dv.contains(" gl_0"), dv);
            check(essl(drawn->entry("drawn_fs")).contains("flat in uint omui_v0;"));
        }
        // No compute and no storage in ES 3.00: such entries have none.
        auto layers = mlibs::shader::compile(data("layers.slang"), {.formats = formats});
        check(layers.has_value(), layers ? "" : layers.error());
        if (layers) {
            check(!layers->entry("layers_fs")->code_for(mlibs::rhi::ShaderFormat::essl), "a storage buffer: no ESSL");
            check(!layers->entry("layers_cs")->code_for(mlibs::rhi::ShaderFormat::essl), "compute: no ESSL");
            check(layers->entry("layers_fs")->code_for(mlibs::rhi::ShaderFormat::spirv) != nullptr);
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
            check(r && r->status.success(), std::format("glslang on {}: {}", name, r ? r->out : r.error().message()));
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    });

    s.test("the formats a build compiles to, by target and host", [] {
        using enum mlibs::rhi::ShaderFormat;
        check(mlibs::shader::formats_for("linux", "linux") == mlibs::rhi::formats({spirv}));
        check(mlibs::shader::formats_for("macos", "macos") == mlibs::rhi::formats({msl}));
        check(mlibs::shader::formats_for("windows", "windows") == mlibs::rhi::formats({spirv, dxbc}));
        check(mlibs::shader::formats_for("windows", "linux") == mlibs::rhi::formats({spirv}), "DXBC needs the Windows host's compiler");
        check(mlibs::shader::formats_for("emscripten", "linux") == mlibs::rhi::formats({wgsl, essl}), "the Web: WebGPU's and WebGL2's");
    });

    return s.run(argc, argv);
}
