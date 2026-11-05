// openmui.render:damage — which rows of a surface a new display list changes.
//
// A renderer that keeps its surface between frames only needs to draw where
// the new list can differ from the last one. The tracker splits a list into
// segments by its node markers (`begin_node` … `end_node`; what lies outside
// every node is the root's), and records for each segment a hash of its own
// commands — draws, clips, transforms, layers, and which children it has, in
// order — and the device bounds of its own draws and of its whole subtree.
// Compared with the last list:
//
//   - a segment whose own hash changed damages its old and new subtree (its
//     clip, transform or layer may reach everything it contains);
//   - a segment that appeared or disappeared damages its subtree;
//   - a segment whose hash is the same but whose own bounds moved (its
//     context moved it) damages its old and new own bounds.
//
// The damage is returned as bands of whole rows: the CPU raster renders a
// band byte-for-byte as it renders the same rows of the whole surface
// (mlibs::raster::Canvas::restrict_rows), so an incremental frame is exactly the
// frame a full render would give. Tests hold every backend to that.
export module xengine.render.vector.api:damage;

import std;
import mlibs.geom;
import :display;
import :resources;

export namespace xengine::render {

// Rows [top, bottom) of a surface, in device pixels.
struct RowSpan {
    std::int32_t top = 0;
    std::int32_t bottom = 0;
    friend constexpr bool operator==(RowSpan a, RowSpan b) noexcept { return a.top == b.top && a.bottom == b.bottom; }
};

// The device-space bounds of a draw command under the transform `m`, generous
// enough to hold every pixel it can touch (anti-aliasing, stroke joins,
// shadow blur); nullopt for commands that draw nothing themselves (clips,
// transforms, layers, node markers). Glyph runs use the glyph source's masks
// when one is given, and a generous em box otherwise.
[[nodiscard]] std::optional<Rect> device_bounds(const Command& command, const Affine& m, GlyphSource* glyphs = nullptr);

struct LayerExtent {
    std::size_t begin=0, end=0;
    Rect content, area, output; // device coordinates under the initial transform
};
// Shared CPU/GPU/damage measurement for opt-in content-sized effect scopes.
// Transforms, actual glyph resources and nested output reach are measured
// once. A clip outside a scope constrains composition, not its source capture.
// The supplied effect scale is the output DPI, independent of object scale.
class LayerBoundsMap {
public:
    [[nodiscard]] bool measure(const DisplayList&,const Affine& initial,float effect_scale,const Resources& = {});
    [[nodiscard]] const LayerExtent* at(std::size_t push_command) const noexcept;
    [[nodiscard]] std::span<const LayerExtent> entries() const & noexcept {return entries_;}
    std::span<const LayerExtent> entries() const && = delete;
private:
    std::vector<LayerExtent> entries_;
    std::vector<std::size_t> indices_,stack_,clip_starts_;
    std::vector<Affine> transforms_;
    std::vector<Rect> clips_;
    std::vector<float> parameters_;
    bool valid_=false;
};

class DamageTracker {
public:
    // The bands of rows of a `width` × `height` surface where `list`, drawn at
    // `scale`, can differ from the list given last time: every row the first
    // time, after invalidate(), and when the size or the scale changes; none
    // when nothing changed. The span is valid until the next call.
    std::span<const RowSpan> update(const DisplayList& list, std::int32_t width, std::int32_t height, float scale,
                                    const Resources& resources = {});

    // The next update damages every row (the pixels were lost, or a resource
    // a list refers to by id changed underneath it).
    void invalidate() noexcept { valid_ = false; }

    // What the last update found: whether it was every row, and the damaged
    // rectangles before they became rows (for the repaint overlay and devtools).
    [[nodiscard]] bool full() const noexcept { return full_; }
    [[nodiscard]] std::span<const Rect> rects() const noexcept { return rects_; }

    // Bands closer than this many rows are merged (one pass over the list
    // costs more than drawing a few extra rows).
    std::int32_t merge_gap = 16;

private:
    struct Segment {
        std::uint64_t key = 0;     // node id, and which occurrence of it (a node drawn twice)
        std::uint32_t node = 0;
        std::uint32_t order = 0;   // in the list, for telling occurrences apart
        std::uint64_t hash = 0;
        Rect          own;         // device bounds of its own draws (empty: none)
        Rect          subtree;     // and of everything it contains
        std::uint32_t parent = 0;
    };
    void build(const DisplayList& list, float scale, std::int32_t width, std::int32_t height, const Resources& resources,
               std::vector<Segment>& out);
    void add(const Rect& r);

    std::vector<Segment>       previous_, current_;
    std::vector<std::uint32_t> stack_;
    std::vector<std::uint64_t> hashers_;
    std::vector<Affine>        transforms_;
    std::vector<Rect>          clips_;
    std::vector<Rect>          rects_;
    std::vector<RowSpan>       rows_;
    std::vector<Rect>          backdrops_;  // the current list's backdrop effects' regions
    LayerBoundsMap             layer_bounds_;
    std::int32_t               width_ = 0, height_ = 0;
    float                      scale_ = 0;
    bool                       valid_ = false;
    bool                       full_ = false;
};

}  // namespace xengine::render
