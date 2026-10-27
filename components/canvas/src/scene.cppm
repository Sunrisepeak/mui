// openmui.ui.canvas:scene — what a canvas shows: nodes in world space,
// stored by column (canvas-markdown §1.1, "SoA scene store").
//
// A node is a world rectangle (x, y in double precision: a wall of
// thousands of windows spans far more than a float resolves; w, h in
// float), a kind, a key (its stable name: the semantic id, what scripts and
// agents call it), its fill and corner radius, and the columns a kind reads
// (numbers and text by name). Columns are vectors, so a frame touches only
// what it reads, and a script hands over ten thousand nodes in one call
// (push()). A uniform grid indexes the rectangles: a viewport query costs
// what it returns, not the scene's size.
//
// Kinds say how a node looks at each size on screen — its tiers (§3.2):
// a box (the instanced rounded rectangle), the kind's own drawing, or a
// live component it is promoted to when it is large enough to use — how it
// is hit, and what it is to the semantic tree.
export module openmui.ui.canvas:scene;

import std;
import openmui.core;
import openmui.view;
import openmui.render;
import openmui.text;

export namespace omui::world {

struct NodeRef {
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;
    [[nodiscard]] constexpr bool valid() const noexcept { return index != std::numeric_limits<std::uint32_t>::max(); }
    friend constexpr bool operator==(NodeRef a, NodeRef b) noexcept { return a.index == b.index && a.generation == b.generation; }
};

// A rectangle in world space.
struct WorldRect {
    double x = 0, y = 0, width = 0, height = 0;
    [[nodiscard]] constexpr double right() const noexcept { return x + width; }
    [[nodiscard]] constexpr double bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr bool empty() const noexcept { return !(width > 0) || !(height > 0); }
    [[nodiscard]] constexpr bool overlaps(const WorldRect& o) const noexcept {
        return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
    }
    [[nodiscard]] constexpr bool contains(double px, double py) const noexcept { return px >= x && px < right() && py >= y && py < bottom(); }
    [[nodiscard]] WorldRect united(const WorldRect& o) const noexcept;
};

class Scene;

// What a kind's drawing gets: the node, where it is on screen, how big.
struct DrawContext {
    const Scene&         scene;
    NodeRef              node;
    render::DisplayList& list;
    Rect                 screen;  // the node's rectangle in the canvas's pixels (logical)
    double               zoom;
    std::size_t          tier;
    const Theme&         theme;
    typeset::FontCollection* fonts = nullptr;  // for labels a kind shapes (and keeps: shaping is not per frame)
};

// How a node looks from a size on screen up (its larger side, logical px).
struct Tier {
    enum class Draw : std::uint8_t {
        box,      // its fill, its radius: one instanced rounded rectangle
        custom,   // the kind's draw()
        texture,  // the kind's content(), rasterised in the background at the
                  // screen's resolution (a mip level), cached; the nearest
                  // ready level stretched meanwhile, a box before any is
        promote,  // a live component (the kind's promote()), interactive
    };
    float from = 0;
    Draw  draw = Draw::box;
};

struct Kind {
    std::vector<Tier> tiers{{0, Tier::Draw::box}};
    std::function<void(DrawContext&)> draw;                       // Tier::Draw::custom
    std::function<Element(const Scene&, NodeRef)> promote;        // Tier::Draw::promote
    // Tier::Draw::texture: the node drawn in its own world units (0, 0, w,
    // h), on the UI thread; the canvas rasterises it elsewhere. Its textures
    // are drawn again when the node's content revision moves (Scene::touch).
    std::function<void(const Scene&, NodeRef, render::DisplayList&)> content;
    // Tier::Draw::texture from a view, when `content` is empty: the node as
    // an element — what promote() shows, so a texture and the live node have
    // one layout and one theme — laid out at the node's world size and
    // painted by an offscreen runtime on the task pool. Built on the UI
    // thread, rendered away from it: it holds values, not signals.
    std::function<Element(const Scene&, NodeRef)> view;
    // The semantic tree: its role, and the text column its name comes from.
    Role        role = Role::generic;
    std::string name_column = "title";
    // A strip at the top that hits as the node's title (a window's bar), logical px.
    float       title_bar = 0;
};

// A node as a script or a loader hands it over.
struct NodeDesc {
    std::string_view kind;             // empty: "box"; one not registered yet: a box of that name
    double           x = 0, y = 0;
    float            width = 0, height = 0;
    std::string_view key;              // empty: "n<index>"
    Color            fill = Color::transparent();
    float            radius = 0;
};

// A connection between two nodes (canvas-kit's connectors draw them): by
// the nodes' keys, so it outlives their slots; drawn between their sides.
struct Edge {
    std::string key;
    std::string from, to;
    Color       color = Color::hex(0x64748b);
    float       width = 2;  // logical pixels at zoom 1
    std::string label;
};

// What a point on the canvas hits.
struct Hit {
    NodeRef node;
    bool    title = false;  // inside the kind's title bar
};

class Scene {
public:
    Scene();
    explicit Scene(double cell);  // the index's grid, world units
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // ── kinds ──
    std::uint16_t kind(std::string_view name, Kind kind);  // registers (or replaces) by name
    [[nodiscard]] const Kind& kind_of(NodeRef n) const;
    [[nodiscard]] std::string_view kind_name(NodeRef n) const;
    [[nodiscard]] std::optional<std::uint16_t> find_kind(std::string_view name) const;

    // ── nodes ──
    NodeRef add(const NodeDesc& d);
    // Many nodes at once (a script's columns): `keys` and `kinds` may be one
    // value for all or one each.
    std::vector<NodeRef> push(std::span<const double> x, std::span<const double> y, std::span<const float> width, std::span<const float> height,
                              std::span<const std::string> kinds, std::span<const std::string> keys = {});
    bool remove(NodeRef n);
    void clear();
    void move(NodeRef n, double x, double y);
    void resize(NodeRef n, float width, float height);
    void set_fill(NodeRef n, Color fill, float radius);
    void raise(NodeRef n);  // above every other node
    // What the node's kind draws from it changed: its textures are stale.
    void touch(NodeRef n);
    [[nodiscard]] std::uint64_t content_revision(NodeRef n) const;

    [[nodiscard]] bool valid(NodeRef n) const noexcept;
    [[nodiscard]] WorldRect rect(NodeRef n) const;
    [[nodiscard]] Color fill(NodeRef n) const;
    [[nodiscard]] float radius(NodeRef n) const;
    [[nodiscard]] std::string_view key(NodeRef n) const;
    [[nodiscard]] std::optional<NodeRef> find(std::string_view key) const;
    [[nodiscard]] std::size_t size() const noexcept { return live_; }
    [[nodiscard]] WorldRect bounds() const;  // of every node (empty: none); kept until the scene changes
    // Bumped by every change: a canvas draws again when it moves.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // Told of every change (on the scene's thread, which is the UI's): a
    // canvas showing it draws again. Observers should only take note.
    std::uint64_t observe(std::function<void()> fn);
    void unobserve(std::uint64_t token);

    // ── columns a kind reads ──
    void set(NodeRef n, std::string_view column, double value);
    void set(NodeRef n, std::string_view column, std::string value);
    [[nodiscard]] double number(NodeRef n, std::string_view column, double fallback = 0) const;
    [[nodiscard]] std::string_view text(NodeRef n, std::string_view column) const;

    // ── queries ──
    // The nodes overlapping `r`, bottom to top: O(what overlaps) through the grid.
    void query(const WorldRect& r, std::vector<NodeRef>& out) const;
    // The topmost node at a point.
    [[nodiscard]] std::optional<Hit> hit(double x, double y) const;
    // Every node, bottom to top.
    void each(const std::function<void(NodeRef)>& f) const;
    // Every node in no particular order (by slot): a pass over the whole
    // scene that needs no stacking order skips sorting it.
    void each_unordered(const std::function<void(NodeRef)>& f) const;
    // Every node's rectangle and fill, by slot, straight from the columns:
    // the cheapest pass over the whole scene (a minimap of a million nodes);
    // over the slots [first, last), a pass can be spread over frames.
    template <class F>
    void each_rect(F&& f, std::size_t first = 0, std::size_t last = std::numeric_limits<std::size_t>::max()) const {
        for (std::size_t i = first, end = std::min(last, alive_.size()); i < end; ++i)
            if (alive_[i]) f(WorldRect{x_[i], y_[i], w_[i], h_[i]}, fill_[i]);
    }
    // The columns' length: slots, live or not.
    [[nodiscard]] std::size_t slots() const noexcept { return alive_.size(); }

    // ── edges ──
    // Connects two nodes (a key already used replaces that edge); empty key:
    // "e<n>". Removing a node removes its edges.
    std::string connect(Edge e);
    bool disconnect(std::string_view key);
    [[nodiscard]] std::span<const Edge> edges() const noexcept { return edges_; }
    [[nodiscard]] std::vector<const Edge*> edges_of(std::string_view node_key) const;

    // ── camera presets (named views of the scene: "overview", "focus") ──
    struct Preset {
        double x = 0, y = 0, zoom = 1;
    };
    void preset(std::string name, Preset p);
    [[nodiscard]] std::optional<Preset> preset(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> presets() const;

private:
    void index(std::uint32_t i);
    void unindex(std::uint32_t i);
    [[nodiscard]] std::uint32_t at(NodeRef n) const;
    [[nodiscard]] bool interior(const WorldRect& r) const noexcept;  // strictly inside the kept bounds
    void keep_bounds(const WorldRect& b);

    double cell_;
    // The columns.
    std::vector<double>        x_, y_;
    std::vector<float>         w_, h_, radius_;
    std::vector<Color>         fill_;
    std::vector<std::uint16_t> kind_;
    std::vector<std::uint32_t> generation_;
    std::vector<std::uint64_t> z_;
    std::vector<std::uint64_t> content_;
    std::vector<bool>          alive_;
    std::vector<std::string>   key_;
    std::unordered_map<std::string, std::vector<double>> numbers_;
    std::unordered_map<std::string, std::vector<std::string>> texts_;
    std::vector<std::uint32_t> free_;
    std::unordered_map<std::string, std::uint32_t> keys_;
    std::size_t                live_ = 0;
    std::uint64_t              revision_ = 0;
    std::uint64_t              next_z_ = 0;
    // Kinds.
    std::vector<std::string> kind_names_;
    std::vector<Kind>        kinds_;
    // The grid: cell → nodes; a node too large for cells is in `large_`.
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> cells_;
    std::vector<std::uint32_t> large_;
    std::vector<bool>          in_large_;
    mutable std::vector<std::uint32_t> stamp_;  // dedup per query
    mutable std::uint32_t      query_ = 0;
    std::map<std::string, Preset, std::less<>> presets_;
    std::vector<Edge>          edges_;
    std::uint64_t              next_edge_ = 0;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> observers_;
    mutable WorldRect          bounds_;
    mutable std::uint64_t      bounds_revision_ = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t next_observer_ = 1;
    void changed();
};

// Scenes by name, as models are: a view names its canvas's scene, the
// program (or a script) fills it.
[[nodiscard]] Scene& scene(std::string_view name);
[[nodiscard]] Scene* find_scene(std::string_view name);
void drop_scene(std::string_view name);

}  // namespace omui::world
