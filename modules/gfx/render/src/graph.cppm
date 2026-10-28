// openmui.render:graph — a frame as passes over the device's textures (D-81).
//
// A rendering feature describes its frame as passes, in the order they run:
// each names what it reads and writes, and records its commands when the
// graph is recorded. Textures come from outside the graph (the frame's
// target, an atlas, an image) or are made for the frame (`transient`, from
// the pool). Before anything is recorded the graph is checked and planned:
//
//   - a pass that reads a transient texture no earlier pass wrote is a
//     problem, named with both;
//   - a pass whose writes reach neither an output, nor a texture from outside
//     the graph, nor a later pass that is run is left out (a layer nobody
//     composites costs nothing);
//   - what is left runs in the order it was described, each pass in a trace
//     span of its name (category "render").
//
// Order is the description's: passes are not reordered, so two features in
// one graph (2D over 3D, 3D inside 2D) compose by the order they are added.
//
//     render::TexturePool pool(device);
//     render::Graph graph(pool);
//     auto layer = graph.transient({w, h, rhi::Format::rgba8_unorm, color_target | sampled}, "layer");
//     graph.pass({.name = "ui.layer", .writes = {*layer}}, [&](rhi::CommandList& l) { … });
//     graph.pass({.name = "ui.composite", .reads = {*layer}, .writes = {target}}, [&](rhi::CommandList& l) { … });
//     auto report = graph.record(list, {&target, 1});
export module openmui.render:graph;

import std;
import openmui.rhi;
import :pool;

export namespace omui::render {

struct PassDesc {
    // What a trace span calls it: a string that outlives the program (a literal).
    const char*               name = "render.pass";
    // What a problem calls it: anything that tells this pass from the others.
    std::string               label;
    std::vector<rhi::Texture> reads, writes;
};

struct GraphProblem {
    std::size_t pass = 0;  // the pass it is about, in the order passes were added
    std::string text;
};

// The passes that run, and those left out, by their index.
struct GraphPlan {
    std::vector<std::size_t> run, culled;
};

struct GraphReport {
    std::size_t recorded = 0, culled = 0, transients = 0;
};

class Graph {
public:
    explicit Graph(TexturePool& pool) : pool_(&pool) {}

    // A texture made for this frame: the pool's, free again at its next frame.
    [[nodiscard]] rhi::Result<rhi::Texture> transient(const TextureNeed& need, std::string_view label);

    // A pass, run after the passes added before it.
    void pass(PassDesc desc, std::function<void(rhi::CommandList&)> record);

    // What would run, or what is wrong.
    [[nodiscard]] std::expected<GraphPlan, std::vector<GraphProblem>> plan(std::span<const rhi::Texture> outputs) const;

    // Checks and plans the graph, then records the passes that run into
    // `list`, in order, each in its trace span.
    [[nodiscard]] std::expected<GraphReport, std::vector<GraphProblem>> record(rhi::CommandList& list, std::span<const rhi::Texture> outputs);

    // Ready for the next frame: the passes and transients forgotten (the pool keeps the textures).
    void clear();

    [[nodiscard]] std::size_t size() const noexcept { return passes_.size(); }
    [[nodiscard]] const PassDesc& desc(std::size_t i) const { return passes_.at(i).desc; }
    [[nodiscard]] bool transient(rhi::Texture t) const noexcept;

private:
    struct Pass {
        PassDesc                                 desc;
        std::function<void(rhi::CommandList&)> record;
    };
    struct Made {
        rhi::Texture texture;
        std::string  label;
    };
    [[nodiscard]] const Made* made(rhi::Texture t) const noexcept;

    TexturePool*      pool_;
    std::vector<Pass> passes_;
    std::vector<Made> transients_;
};

}  // namespace omui::render
