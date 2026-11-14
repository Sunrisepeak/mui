// openmui.shader — Slang shaders to what an omui.rhi device takes
// (architecture §8.3, D-34): every entry point of a `.slang` file compiled by
// slangc to each shader format asked for (SPIR-V, MSL, DXBC, DXIL, WGSL, ESSL), its
// resources read from Slang's reflection and checked against the binding
// convention rhi backends share (SDL_GPU's), and the result written as a C++
// module partition of rhi::ShaderDesc constants — or kept in memory, which is
// how a shader edited while the program runs is compiled again.
//
// The convention, per stage (`[[vk::binding(index, set)]]` with the matching
// `register(…, space<set>)`):
//
//   vertex    set 0: sampled textures (Sampler2D), then read-only storage
//                    textures, then read-only storage buffers; set 1: uniforms
//   fragment  set 2: the same three kinds; set 3: uniforms
//   compute   set 0: read-only resources as above; set 1: read-write storage
//                    textures, then read-write storage buffers; set 2: uniforms
//
// Each kind takes the indices right after the kind before it, from 0, with no
// gap: index i of the stage's samplers is what rhi binds at slot i.
export module mlibs.shader.api;

import std;
import mlibs.rhi;
export import :provisioned;

export namespace mlibs::shader {

struct Entry {
    rhi::Stage                   stage = rhi::Stage::vertex;
    std::string                  name;
    rhi::Counts                  counts;
    std::array<std::uint32_t, 3> threads{1, 1, 1};  // compute: the work group
    struct Code {
        rhi::ShaderFormat      format = rhi::ShaderFormat::spirv;
        std::vector<std::byte> bytes;
    };
    std::vector<Code> code;  // in the order the formats were asked for

    [[nodiscard]] const Code* code_for(rhi::ShaderFormat f) const noexcept {
        for (const Code& c : code)
            if (c.format == f) return &c;
        return nullptr;
    }
};

struct Program {
    std::filesystem::path              source;
    std::vector<Entry>                 entries;       // in the file's order
    std::vector<std::filesystem::path> dependencies;  // the source and every file it imports or includes

    [[nodiscard]] const Entry* entry(std::string_view name) const noexcept {
        for (const Entry& e : entries)
            if (e.name == name) return &e;
        return nullptr;
    }
};

// An entry point the caller already knows.
struct EntryName {
    std::string name;
    rhi::Stage  stage = rhi::Stage::fragment;
};

struct Options {
    std::filesystem::path              slangc;  // empty: find_slangc()
    rhi::ShaderFormats                 formats = rhi::formats({rhi::ShaderFormat::spirv});
    std::vector<std::filesystem::path> includes;
    std::vector<std::string>           defines;  // NAME or NAME=VALUE
    std::filesystem::path              scratch;  // where slangc writes (empty: a temporary directory)
    // The entry points to compile, when the caller knows them (a shader
    // reloaded while the program runs): no run to discover them, so the
    // compile takes one slangc start. Empty: every entry point in the file.
    std::vector<EntryName>             entries;
    // Compile in this process when Slang's library is beside slangc: no
    // program started and no core module loaded per compile, which is most
    // of slangc's time (a hot reload's budget). The same files either way;
    // false (or OMUI_SLANGC_PROCESS=1): run slangc.
    bool                               in_process = true;
    // The text formats (WGSL, ESSL) written compact (`short_names`, then
    // `compact`): a program carries them as they are, a byte a character.
    // What a build embeds (omui-shader) is compact; false here, for text a
    // person reads.
    bool                               compact = false;
};

// Shader text (WGSL or GLSL ES) with what a compiler does not read taken out:
// indentation, line ends, comments, and the spaces between tokens that are
// still two tokens without them. A space stays between two words or numbers
// and between two operator characters (`a - -b`, `x / *y`), and a line that
// begins with `#` stays a line of its own. The same tokens in the same order:
// the shader a device compiles is the same shader. About 30% smaller for
// what Slang writes.
[[nodiscard]] std::string compact(std::string_view text);

// Shader text (WGSL or GLSL ES, as Slang writes it) with the program's own
// names made short. Slang ends every name it writes for something of the
// program's — a function, a variable, a structure, a member — with `_` and a
// number, and its temporaries are `_S` and a number; nothing a device or a
// backend knows a shader by is written so (an entry point keeps its name,
// what is bound is `omui_…` or a number). Each such name becomes one of one
// or two characters, the same one wherever it stands, the most used the
// shortest; none is a word the text already has or one a language keeps.
// Numbers, directives and every other word are as they were: the same
// program under other names. About a quarter smaller again after `compact`.
[[nodiscard]] std::string short_names(std::string_view text);

// slangc: OMUI_SLANGC, else the one mcpp provisioned for this build
// (provisioned_slangc), else the PATH's; an error says how to get it.
[[nodiscard]] std::expected<std::filesystem::path, std::string> find_slangc();

// Every entry point of `source` in every format of `options.formats`; an
// error carries slangc's diagnostics, or the binding that breaks the
// convention and where it should be.
[[nodiscard]] std::expected<Program, std::string> compile(const std::filesystem::path& source, const Options& options = {});

// The rhi::ShaderDesc of an entry, borrowing its code and name (`codes` holds
// the ShaderCode array it points into, one per format).
[[nodiscard]] rhi::ShaderDesc desc(const Entry& e, std::vector<rhi::ShaderCode>& codes, std::string_view label = {});

struct EmitOptions {
    std::string module;     // the package's module: acme.render
    std::string partition;  // the partition this file is: quad_shaders → acme.render:quad_shaders
    std::string ns;         // where the constants go: acme::render::shaders::quad
};
// A module partition: for each entry point `e`, `inline constexpr
// rhi::ShaderDesc e`, its code in every compiled format, and `entries`, all
// of them. Byte for byte the same for the same program.
[[nodiscard]] std::string emit_cpp(const Program& program, const EmitOptions& options);

// What the slang step of a build compiles to for a target OS (mcpp's
// MCPP_TARGET_OS spelling: linux, windows, macos, ios, android, emscripten …)
// on a host: SPIR-V for Vulkan everywhere but Apple and the Web, MSL on
// Apple, DXBC for D3D12 when the host is Windows (its d3dcompiler compiles
// it), and on the Web WGSL for WebGPU and ESSL (GLSL ES 3.00) for WebGL2.
[[nodiscard]] rhi::ShaderFormats formats_for(std::string_view target_os, std::string_view host_os) noexcept;

// The prerequisites of a Make-syntax depfile (slangc's): after the first
// `target:`, names separated by blanks, `\` + newline continuing a line.
// `\ `, `\#` and `\:` are the character escaped and `$$` is `$`; any other
// backslash is part of the name (a Windows separator), so `D\:\a\x.slang`
// is `D:\a\x.slang`.
[[nodiscard]] std::vector<std::string> depfile_prerequisites(std::string_view text);

}  // namespace mlibs::shader
