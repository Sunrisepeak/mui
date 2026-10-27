// openmui.effect — a program's own pixel effects, as data and Slang (D-60,
// D-68; architecture §8).
//
// An effect is two files: `<name>.effect.omui.toml` — its name, what it does,
// its parameters (typed, limited, animatable like any property) and how far
// it reaches outside its layer — and a Slang fragment shader written against
// the effect template openmui.render carries (`import openmui_effect;`, see
// render's effects/lib/openmui_effect.slang). The library reads and checks
// the file, compiles the shader for the device's shader format, checks that
// it binds only what the template offers, and registers the effect with the
// renderer's effects (render::effects()): from then on a layer — any node's
// `effect` property — names it like a built-in, and the GPU renderer draws it.
//
//     omui::effect::Library fx({.formats = device.caps().formats});
//     auto ref = fx.load("fx/scanlines.effect.omui.toml");   // or every problem, where it is
//     …
//     for (const auto& r : fx.poll()) { … }                   // between frames: what changed, loaded again
//
// poll() loads again what changed — the file, the shader, or anything the
// shader imports — and replaces the registry's entry in place, so a running
// program shows the new shader on its next frame. A file that no longer
// loads keeps the effect as it was and says why. Call load() and poll() on
// the thread that renders, between frames.
//
// A program's effect has a GPU path only: the CPU renderer draws its layer
// without it and counts it as skipped (spec R-3).
export module openmui.effect;

import std;
import openmui.rhi;
import openmui.render;

export namespace omui::effect {

struct Diagnostic {
    std::string file;
    int         line = 0;
    int         column = 0;
    std::string message;
    std::string hint;

    // file:line:column: message (hint)
    [[nodiscard]] std::string text() const;
};

using Diagnostics = std::vector<Diagnostic>;

// What an effect file declares.
struct Param {
    std::string name;
    float       value = 0;  // its default
    float       min = 0, max = 1;
    std::string doc;
};

struct Declaration {
    std::string           name;
    std::string           doc;
    std::filesystem::path shader;  // relative to the file's directory
    std::string           entry;   // the fragment entry point
    std::vector<Param>    params;  // at most 16, the template's
    float                 reach = 0;          // logical pixels, when no parameter is named
    std::string           reach_param;        // or the parameter whose value is the reach
};

// Reads an effect file's text: what it declares, or every mistake in it.
[[nodiscard]] std::expected<Declaration, Diagnostics> read(std::string_view text, std::string_view file);

struct Options {
    // The device's shader formats (rhi::Caps::formats): an effect is compiled
    // to one of them, the first of SPIR-V, MSL, DXIL, DXBC it takes.
    rhi::ShaderFormats    formats = rhi::formats({rhi::ShaderFormat::spirv});
    std::filesystem::path slangc;  // empty: openmui.slang finds it
};

// One load: the effect it registered, how long reading and compiling took.
struct Loaded {
    std::filesystem::path     file;
    std::string               name;
    render::EffectRef         ref;
    std::chrono::microseconds took{};
};

// What poll() found: an effect loaded again, or why it could not be.
struct Reload {
    std::filesystem::path     file;
    std::string               name;
    render::EffectRef         ref;   // the effect (as it was, when problems)
    std::chrono::microseconds took{};
    Diagnostics               problems;
    [[nodiscard]] bool ok() const noexcept { return problems.empty(); }
};

class Library {
public:
    explicit Library(Options options = {});
    Library(Library&&) noexcept;
    Library& operator=(Library&&) noexcept;
    ~Library();

    // Reads `<name>.effect.omui.toml` and its shader, compiles and registers
    // the effect (a name this library loaded before is replaced in place).
    [[nodiscard]] std::expected<Loaded, Diagnostics> load(const std::filesystem::path& file);
    // Every `*.effect.omui.toml` in a directory, in name order.
    [[nodiscard]] std::expected<std::vector<Loaded>, Diagnostics> load_directory(const std::filesystem::path& dir);

    // The effects whose files changed since they were loaded, loaded again.
    [[nodiscard]] std::vector<Reload> poll();

    // The effects this library loaded, by name.
    [[nodiscard]] std::vector<std::string> names() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui::effect
