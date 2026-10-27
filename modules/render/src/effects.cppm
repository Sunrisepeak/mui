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
// adds its own. Each effect has a CPU reference implementation — the pixels
// the GPU path (M4: a Slang fragment or compute shader of the same name) is
// held to.
//
//     auto ref = render::effects().find("dissolve");
//     list.push_layer(1, BlendMode::normal, *ref, params, bounds);
export module openmui.render:effects;

import std;
import openmui.geom;
import openmui.raster;
import :display;

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
};

struct EffectInfo {
    std::string_view         name;
    std::string_view         doc;
    std::vector<EffectParam> params;
    // How far, in logical pixels, the effect may move pixels outside the layer.
    float (*reach)(std::span<const float> params) = nullptr;
    void (*cpu)(const EffectInput& in) = nullptr;
};

class EffectRegistry {
public:
    EffectRegistry();
    EffectRegistry(const EffectRegistry&) = delete;
    EffectRegistry& operator=(const EffectRegistry&) = delete;

    // Registers an effect (a name registered twice keeps its first entry).
    EffectRef add(EffectInfo info);
    [[nodiscard]] std::optional<EffectRef> find(std::string_view name) const;
    [[nodiscard]] const EffectInfo* info(EffectRef ref) const;
    [[nodiscard]] std::vector<std::string_view> names() const;

    // The parameters in order, from values by name (defaults for the rest).
    [[nodiscard]] std::vector<float> params(EffectRef ref, std::span<const std::pair<std::string_view, float>> values) const;
    // The reach in logical pixels for these parameters.
    [[nodiscard]] float reach(EffectRef ref, std::span<const float> params) const;

private:
    mutable std::shared_mutex lock_;
    std::deque<EffectInfo>    entries_;  // stable addresses
};

// The process's registry: dissolve, wipe, reveal, displace, noise,
// chromatic, blur, pixelate, particles; then what the program adds.
[[nodiscard]] EffectRegistry& effects();

}  // namespace omui::render
