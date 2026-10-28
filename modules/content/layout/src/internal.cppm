// openmui.layout:internal — the engine's node storage and requests, shared by
// the implementation units (engine, flex, grid) and exported to no one.
module openmui.layout:internal;

import std;
import openmui.geom;
import :style;
import :engine;

namespace omui::layout {

// ── nodes and caches ──────────────────────────────────────────────────────

struct Engine::Request {
    std::optional<float> width;   // border box decided by the parent
    std::optional<float> height;
    Available            avail_width;
    Available            avail_height;
    std::optional<float> parent_width;   // the parent's content box, for percentages
    std::optional<float> parent_height;
    bool                 perform = false;

    [[nodiscard]] bool same_constraints(const Request& o) const noexcept {
        return width == o.width && height == o.height && avail_width == o.avail_width &&
               avail_height == o.avail_height && parent_width == o.parent_width && parent_height == o.parent_height;
    }
};

struct Engine::Node {
    Style               style;
    NodeId              parent = no_node;
    std::vector<NodeId> children;
    MeasureFn           measure = nullptr;
    void*               context = nullptr;
    BaselineFn          baseline_fn = nullptr;
    bool                alive = true;

    Rect  frame;
    Size  content;
    float baseline = 0;

    struct Entry {
        Request request;
        Size    size;
        float   baseline = 0;
    };
    static constexpr std::size_t cache_slots = 8;
    std::array<Entry, cache_slots> cache{};
    std::uint8_t                   cache_count = 0;
    std::uint8_t                   cache_next = 0;
    std::optional<Entry>           performed;

    void invalidate() noexcept {
        cache_count = 0;
        cache_next = 0;
        performed.reset();
    }
};

}  // namespace omui::layout
