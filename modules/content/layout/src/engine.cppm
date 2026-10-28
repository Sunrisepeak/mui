// openmui.layout:engine — the layout tree and its computation.
//
// The engine keeps its own nodes (style, children, an optional measure
// function for content such as text) and computes border-box frames relative
// to each parent. It is incremental: every node caches the sizes it was asked
// for under given constraints, and a change dirties only the node and its
// ancestors, so relayout after typing one character into a text field
// measures that field and re-positions its ancestors' children, nothing else.
//
// Results are floats in logical pixels, unrounded; the renderer snaps edges
// (geom::snap). The package is compiled without floating-point contraction,
// so the same tree gives the same frames on every platform (A-level, V-2).
export module openmui.layout:engine;

import std;
import openmui.geom;
import :style;

export namespace omui::layout {

using NodeId = std::uint32_t;
inline constexpr NodeId no_node = std::numeric_limits<NodeId>::max();

// How much room a measurement may use along one axis.
struct Available {
    enum class Kind : std::uint8_t { definite, min_content, max_content };
    Kind  kind  = Kind::max_content;
    float value = 0;

    [[nodiscard]] static constexpr Available definite(float v) noexcept { return {Kind::definite, v}; }
    [[nodiscard]] static constexpr Available min_content() noexcept { return {Kind::min_content, 0}; }
    [[nodiscard]] static constexpr Available max_content() noexcept { return {Kind::max_content, 0}; }
    [[nodiscard]] constexpr bool is_definite() const noexcept { return kind == Kind::definite; }
    friend constexpr bool operator==(Available a, Available b) noexcept {
        return a.kind == b.kind && (a.kind != Kind::definite || a.value == b.value);
    }
};

// The content size of a leaf: given the size already decided on an axis (if
// any) and the room available, how large is the content (excluding padding
// and border)? Text wraps to `available` width; an image keeps its ratio.
using MeasureFn  = Size (*)(void* context, NodeId node, std::optional<float> width, std::optional<float> height,
                            Available available_width, Available available_height);
// The distance from the top of the content box to the first baseline.
using BaselineFn = float (*)(void* context, NodeId node, Size content);

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] NodeId create(Style style = {});
    // Removes a node; its children become roots (not destroyed).
    void destroy(NodeId node);
    [[nodiscard]] bool valid(NodeId node) const noexcept;

    void set_style(NodeId node, Style style);
    [[nodiscard]] const Style& style(NodeId node) const;

    void append(NodeId parent, NodeId child);
    void insert(NodeId parent, std::size_t index, NodeId child);
    void remove_child(NodeId parent, NodeId child);
    void clear_children(NodeId parent);
    [[nodiscard]] std::span<const NodeId> children(NodeId node) const;
    [[nodiscard]] NodeId parent(NodeId node) const;

    void set_measure(NodeId node, MeasureFn measure, void* context, BaselineFn baseline = nullptr);
    // The node's content changed (new text, a new image): it and its
    // ancestors are measured again at the next compute.
    void mark_dirty(NodeId node);

    // Lays out the tree under `root` in `available` room (the window).
    void compute(NodeId root, Size available);

    // Results of the last compute.
    [[nodiscard]] Rect frame(NodeId node) const;          // border box, relative to the parent's border box
    [[nodiscard]] Size content_size(NodeId node) const;   // extent of the children (scroll range)
    [[nodiscard]] float baseline(NodeId node) const;      // from the top of the border box

    struct Stats {
        std::size_t layouts = 0;      // nodes laid out (not served from cache)
        std::size_t cache_hits = 0;
        std::size_t measures = 0;     // measure-function calls
    };
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_ = {}; }

private:
    struct Node;
    struct Request;
    friend struct FlexAlgorithm;
    friend struct GridAlgorithm;

    Size layout(NodeId node, const Request& request);
    Size layout_leaf(NodeId node, const Request& request);
    Size layout_flex(NodeId node, const Request& request);
    Size layout_grid(NodeId node, const Request& request);
    Size layout_stack(NodeId node, const Request& request);
    void layout_absolute(NodeId node, Size border_box, bool perform);
    void hide_subtree(NodeId node);

    Node&       at(NodeId id);
    const Node& at(NodeId id) const;

    std::vector<Node>   nodes_;
    std::vector<NodeId> free_;
    Stats               stats_;
};

}  // namespace omui::layout
