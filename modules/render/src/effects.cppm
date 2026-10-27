// openmui.render:effects — pixel effects (D-60 pixel tier, D-68).
//
// An effect is a pure function of a layer's pixels and its parameters: the
// renderer draws a layer's content offscreen, hands it to the effect, and
// composites what the effect wrote. A parameter such as `progress` is an
// ordinary property of the node, so a dissolve, a wipe or a burst into
// particles animates, seeks and renders frame by frame like anything else.
//
// The registry is a plugin point (D-65): the built-ins are registered first,
// in a fixed order (their refs are the same in every process), and a program
// adds its own. An effect has a CPU reference implementation, a GPU path, or
// both: the built-ins have both, the GPU path held to the CPU's pixels
// (effects/<name>.slang, written against the effect template
// effects/lib/openmui_effect.slang); a program's Slang effect (openmui.effect)
// may have only the GPU path, and the CPU renderer counts it as skipped.
//
//     auto ref = render::effects().find("dissolve");
//     list.push_layer(1, BlendMode::normal, *ref, params, bounds);
export module openmui.render:effects;

import std;
import openmui.geom;
import openmui.raster;
import openmui.rhi;
import :display;
// The built-ins' GPU paths (effects/*.slang), compiled by build.mcpp.
export import :quad_shaders;
export import :dissolve_shaders;
export import :wipe_shaders;
export import :reveal_shaders;
export import :displace_shaders;
export import :noise_shaders;
export import :chromatic_shaders;
export import :blur_shaders;
export import :pixelate_shaders;
export import :particles_shaders;
export import :liquid_glass_shaders;
export import :neon_shaders;
export import :crt_shaders;
export import :paper_shaders;
export import :wobble_shaders;
export import :metaball_shaders;
// The template's text (effects/lib/openmui_effect.slang), for a program's
// effects compiled while it runs (openmui.effect).
export import :effect_template;

export namespace omui::render {

struct EffectParam {
    std::string_view name;
    float            value = 0;          // its default
    float            min = 0, max = 1;
    std::string_view doc;
};

// What an effect computes from: the layer drawn offscreen (premultiplied
// RGBA, device pixels), and where the layer's own rectangle lies in it (the
// surface is larger by the effect's reach on every side).
struct EffectInput {
    const raster::Surface& source;
    raster::Surface&       target;   // the same size, transparent: the result
    std::span<const float> params;   // every parameter, in the effect's order
    float                  scale = 1;  // device pixels per logical pixel (sizes in params are logical)
    geom::Rect             layer;    // the layer's rectangle in source and target
    // What lies under the layer, the same size and place as source
    // (EffectInfo::backdrop; transparent beyond the target); else null.
    const raster::Surface* backdrop = nullptr;
    // The layer's shapes in the surface's pixels (at least one: its bounds).
    std::span<const EffectShape> shapes;
};

// The signed distance from p to a shape's edge, negative inside (pixels,
// the shape's corners rounded): the effect template's shape_distance, in the
// same operations.
[[nodiscard]] float shape_distance(const EffectShape& shape, geom::Point p) noexcept;
// The smooth minimum of two distances, `k` wide (k ≤ 0: the minimum): how a
// glass group's shapes merge like drops (the template's smooth_min).
[[nodiscard]] float smooth_min(float a, float b, float k) noexcept;

// Three box passes each way of `radius` pixels, in integers: the blur
// effect's, and how a backdrop is frosted. The radius for `logical` pixels
// at a scale is box_radius's.
void box_blur(raster::Surface& surface, int radius);
[[nodiscard]] int box_radius(float logical, float scale) noexcept;

// How the GPU renderer runs an effect.
enum class EffectPass : std::uint8_t {
    none,       // no GPU path: drawn without the effect, counted as skipped
    pixel,      // `fragment`, the template's, once per pixel of the surface
    blur,       // three box passes each way (derived 0: the radius)
    pixelate,   // a compute pass, one invocation a block (derived 0…2: size, origin)
    particles,  // one instance a cell (derived 0…7, see effects/particles.slang)
};

// What the GPU path reads besides the parameters, computed on the CPU the
// way the CPU reference computes it (trigonometry, rounding of sizes): the
// template's `derived` values and, for some, one value per row of the surface.
struct EffectFrame {
    std::array<float, 8> derived{};
};

struct EffectGpu {
    EffectPass             pass = EffectPass::none;
    const rhi::ShaderDesc* fragment = nullptr;  // pass pixel
    void (*derive)(std::span<const float> params, float scale, geom::Rect layer, EffectFrame& out) = nullptr;
    // pass pixel: the aux texture's values, one a row (r32_float), when set.
    void (*rows)(std::span<const float> params, float scale, std::span<float> out) = nullptr;
};

struct EffectInfo {
    std::string_view         name;
    std::string_view         doc;
    std::vector<EffectParam> params;
    // How far, in logical pixels, the effect may move pixels outside the layer.
    std::function<float(std::span<const float> params)> reach;
    void (*cpu)(const EffectInput& in) = nullptr;
    EffectGpu                gpu;
    // The effect reads what lies under its layer (OMUI_EXT_backdrop_effect,
    // D-64): the renderer captures it where the layer begins, frosted by
    // `backdrop_blur` logical pixels (box_blur, the same on both renderers),
    // and gives it as EffectInput::backdrop (the template's aux texture).
    bool                                                 backdrop = false;
    std::function<float(std::span<const float> params)> backdrop_blur;
    // A program's effect loaded from files (openmui.effect): the file it came
    // from, and what the entry owns — name, doc, parameter names and the
    // shader point into it. Empty for the built-ins and effects registered
    // in code.
    std::string_view            source;
    std::shared_ptr<const void> storage;
};

class EffectRegistry {
public:
    EffectRegistry();
    EffectRegistry(const EffectRegistry&) = delete;
    EffectRegistry& operator=(const EffectRegistry&) = delete;

    // Registers an effect (a name registered twice keeps its first entry).
    EffectRef add(EffectInfo info);
    // Replaces a registered effect's entry (a reloaded shader, new parameters):
    // its ref stays, its generation grows. False for an unknown ref.
    bool replace(EffectRef ref, EffectInfo info);
    // How many times `ref` was replaced: a renderer's cached pipeline for it
    // is stale when this differs.
    [[nodiscard]] std::uint64_t generation(EffectRef ref) const;
    [[nodiscard]] std::optional<EffectRef> find(std::string_view name) const;
    [[nodiscard]] const EffectInfo* info(EffectRef ref) const;
    [[nodiscard]] std::vector<std::string_view> names() const;

    // The parameters in order, from values by name (defaults for the rest).
    [[nodiscard]] std::vector<float> params(EffectRef ref, std::span<const std::pair<std::string_view, float>> values) const;
    // The reach in logical pixels for these parameters.
    [[nodiscard]] float reach(EffectRef ref, std::span<const float> params) const;

private:
    mutable std::shared_mutex  lock_;
    std::deque<EffectInfo>     entries_;      // stable addresses
    std::deque<std::uint64_t>  generations_;
};

// The process's registry: dissolve, wipe, reveal, displace, noise,
// chromatic, blur, pixelate, particles; then what the program adds.
[[nodiscard]] EffectRegistry& effects();

}  // namespace omui::render
