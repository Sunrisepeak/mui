// openmui.render:display — contract ⑤, what a frame draws (spec §7).
//
// Components paint into a display list; a renderer (the CPU reference raster
// or the GPU renderer) turns it into pixels. The list is plain data: commands
// whose variable parts (path points, glyphs, gradient stops) live in the
// list's arena, so building a frame's list of the same shape as the last
// allocates nothing, and two lists can be compared or hashed for caching.
//
// `begin_node`/`end_node` bracket what a node drew: the renderer can write an
// object-id buffer from them, which is how devtools trace a pixel to the node
// and the source line that produced it.
export module openmui.render.vector:display;

import std;
import openmui.geom;
import openmui.memory;

export namespace omui::render {

using geom::Affine;
using geom::Color;
using geom::CornerRadii;
using geom::Point;
using geom::Rect;
using geom::Size;

// A rotation with the deterministic sine and cosine of hanim: the same
// matrix on every platform.
[[nodiscard]] Affine rotate(float radians) noexcept;

enum class FillRule : std::uint8_t { nonzero, even_odd };
enum class LineCap : std::uint8_t { butt, round, square };
enum class LineJoin : std::uint8_t { miter, round, bevel };

enum class PathVerb : std::uint8_t { move, line, quad, cubic, close };

// A path's data inside a display list (arena-owned).
struct PathView {
    std::span<const PathVerb> verbs;
    std::span<const Point>    points;
};

// A path being built.
class Path {
public:
    Path& move_to(Point p);
    Path& line_to(Point p);
    Path& quad_to(Point control, Point p);
    Path& cubic_to(Point c1, Point c2, Point p);
    // An elliptical arc in SVG's parameterisation, approximated by cubics.
    Path& arc_to(Point radii, float x_rotation, bool large_arc, bool sweep, Point p);
    Path& close();

    Path& add_rect(const Rect& r);
    Path& add_rounded_rect(const Rect& r, const CornerRadii& radii);
    Path& add_ellipse(const Rect& r);
    Path& add_circle(Point center, float radius) { return add_ellipse({center.x - radius, center.y - radius, 2 * radius, 2 * radius}); }

    [[nodiscard]] Path transformed(const Affine& m) const;
    [[nodiscard]] Rect bounds() const noexcept;  // of the points, including control points
    [[nodiscard]] bool empty() const noexcept { return verbs_.empty(); }
    [[nodiscard]] PathView view() const noexcept { return {verbs_, points_}; }
    [[nodiscard]] std::span<const PathVerb> verbs() const noexcept { return verbs_; }
    [[nodiscard]] std::span<const Point> points() const noexcept { return points_; }
    void clear() noexcept {
        verbs_.clear();
        points_.clear();
    }

private:
    std::vector<PathVerb> verbs_;
    std::vector<Point>    points_;
    Point                 start_{};
    Point                 current_{};
};

struct StrokeStyle {
    float                   width = 1;
    LineCap                 cap = LineCap::butt;
    LineJoin                join = LineJoin::miter;
    float                   miter_limit = 4;
    std::span<const float>  dashes;  // on, off, on, off … (arena-owned in a list)
    float                   dash_offset = 0;
};

struct GradientStop {
    float offset = 0;
    Color color;
};

enum class GradientKind : std::uint8_t { linear, radial, conic };
enum class Spread : std::uint8_t { pad, repeat, reflect };

struct Gradient {
    GradientKind                    kind = GradientKind::linear;
    Point                           start;          // linear: from; radial and conic: centre
    Point                           end;            // linear: to; radial: focal point
    float                           radius = 0;     // radial: end radius; conic: start angle (radians)
    Spread                          spread = Spread::pad;
    std::span<const GradientStop>   stops;          // arena-owned in a list
};

struct ImageRef {
    std::uint32_t id = 0;
    friend constexpr bool operator==(ImageRef a, ImageRef b) noexcept { return a.id == b.id; }
};

struct FontRef {
    std::uint32_t id = 0;
    friend constexpr bool operator==(FontRef a, FontRef b) noexcept { return a.id == b.id; }
};

struct EffectRef {
    std::uint32_t id = 0;  // 0: none
};

enum class Sampling : std::uint8_t { nearest, linear };

// Stylised (cel) lighting of a picture: how a skinned fighter's cloth, skin
// and hands answer the stage's light as they move, or a UI picture a light
// passing over it. `normal` is a tangent-space normal map aligned with the
// picture (openmui.image's normal_map: R = x right, G = y down, B = out of
// the picture; 128 is zero), sampled where the colour is. Its xy turns with
// the rotation — and the mirror — of whatever maps the picture to the
// screen (a triangle's affine, the list's transforms), so a part keeps
// facing the light, not its picture, as it turns or flips.
//
// The light is a direction toward it in screen space (device pixels, y
// down: light {-1, -1} with light_z 0.7 is the upper left, a little in
// front), the same for every part of a figure however the parts are
// transformed.
// Lambert n·l, cut into `bands` levels (0 or 1: smooth) whose edges are
// `softness` wide, multiplies the colour toward `shadow` by
// `shadow_strength` where it is unlit; where the normal grazes (n·z below
// `rim_width`) and faces the light in the screen's plane, `rim` is added
// (by its alpha and `rim_strength`). Premultiplied alpha is kept: the alpha
// never changes, no channel exceeds it.
struct Material {
    ImageRef normal;
    Point    light{-0.6f, -0.6f};  // toward the light, screen space (y down)
    float    light_z = 0.5f;       // its part out of the screen, toward the viewer
    Color    shadow{0.35f, 0.3f, 0.5f, 1};
    float    shadow_strength = 0.6f;
    int      bands = 2;            // ≤ 16
    float    softness = 0.08f;     // a band's edge, in lambert (0: hard; the GPU may then put a pixel on the edge in the other band)
    Color    rim{1, 1, 1, 1};
    float    rim_strength = 0.6f;
    float    rim_width = 0.35f;
    friend constexpr bool operator==(const Material&, const Material&) noexcept = default;
};

struct Paint {
    // image_mask: the image's alpha as coverage of `color` (a black ink
    // stroke's silhouette as a rim of light, a picture as a stencil).
    enum class Kind : std::uint8_t { solid, gradient, image, image_mask };
    Kind     kind = Kind::solid;
    Color    color;              // solid, image_mask; the tint (alpha) of an image paint
    Gradient gradient;
    ImageRef image;
    Affine   image_transform;    // image space to paint space
    Sampling sampling = Sampling::linear;
    // Lit by a material (image and image_mask; arena-owned in a list).
    const Material* material = nullptr;

    [[nodiscard]] static Paint solid(Color c) noexcept {
        Paint p;
        p.color = c;
        return p;
    }
};

enum class BlendMode : std::uint8_t {
    normal, multiply, screen, overlay, darken, lighten, color_dodge, color_burn, hard_light, soft_light,
    difference, exclusion, plus, erase,
};

struct Glyph {
    std::uint32_t id = 0;
    Point         position;  // the glyph's origin relative to the run's origin (baseline)
    // Where it came from (D-68): the first byte of its cluster in the source
    // text, and its grapheme, word and line in the paragraph. Nothing draws
    // with these; glyph-level animation, text ranges and selection address them.
    std::uint32_t cluster = 0, grapheme = 0, word = 0, line = 0;
};

// One glyph's own style over its run's (D-68): an affine transform about the
// glyph's origin (its pen position on the baseline), an opacity, and a tint
// mixed over the run's colour by the tint's alpha. The default is no change.
struct GlyphStyle {
    Affine transform;
    float  opacity = 1;
    Color  tint{0, 0, 0, 0};
    [[nodiscard]] bool plain() const noexcept { return transform == Affine{} && opacity == 1 && tint.a == 0; }
    [[nodiscard]] bool translation_only() const noexcept {
        return transform.a == 1 && transform.b == 0 && transform.c == 0 && transform.d == 1;
    }
};

// ── commands ──────────────────────────────────────────────────────────────

struct FillRect      { Rect rect; Paint paint; };
struct FillRoundRect { Rect rect; CornerRadii radii; Paint paint; };
struct StrokeRoundRect { Rect rect; CornerRadii radii; float width; Paint paint; };  // centred on the edge
struct BoxShadow     { Rect rect; CornerRadii radii; Point offset; float blur; float spread; Color color; bool inset; };
struct FillPath      { PathView path; FillRule rule; Paint paint; };
struct StrokePath    { PathView path; StrokeStyle style; Paint paint; };
struct DrawImage     { ImageRef image; Rect source; Rect dest; Sampling sampling; float opacity; };
// `styles` is empty, or one style per glyph (D-68).
struct DrawGlyphs    { FontRef font; float size; Point origin; std::span<const Glyph> glyphs; Paint paint; std::span<const GlyphStyle> styles; };
struct PushClipRect  { Rect rect; CornerRadii radii; };
struct PushClipPath  { PathView path; FillRule rule; };
struct PopClip       {};
struct PushTransform { Affine transform; };
struct PopTransform  {};
// A shape an effect reads (D-64): a rounded rectangle in the layer's space.
// A glass group's layer carries each member's; none: the layer's bounds.
struct EffectShape {
    Rect        rect;
    CornerRadii radii;
};
struct PushLayer     { float opacity; BlendMode blend; EffectRef effect; std::span<const float> params; Rect bounds;
                       std::span<const EffectShape> shapes = {}; };
struct PopLayer      {};
struct ExternalTexture { std::uint64_t texture; Rect dest; float opacity; };
struct BeginNode     { std::uint32_t node; };
struct EndNode       {};

using Command = std::variant<FillRect, FillRoundRect, StrokeRoundRect, BoxShadow, FillPath, StrokePath, DrawImage,
                             DrawGlyphs, PushClipRect, PushClipPath, PopClip, PushTransform, PopTransform, PushLayer,
                             PopLayer, ExternalTexture, BeginNode, EndNode>;

// FNV-1a of a command's content (its payloads included, never addresses),
// continuing from `seed`: DisplayList::hash is this over every command.
[[nodiscard]] std::uint64_t hash_command(const Command& command, std::uint64_t seed) noexcept;

class DisplayList {
public:
    DisplayList() = default;
    DisplayList(const DisplayList&) = delete;
    DisplayList& operator=(const DisplayList&) = delete;
    DisplayList(DisplayList&&) noexcept = default;
    DisplayList& operator=(DisplayList&&) noexcept = default;

    void fill_rect(const Rect& r, const Paint& p);
    void fill_rect(const Rect& r, Color c) { fill_rect(r, Paint::solid(c)); }
    void fill_round_rect(const Rect& r, const CornerRadii& radii, const Paint& p);
    void stroke_round_rect(const Rect& r, const CornerRadii& radii, float width, const Paint& p);
    void box_shadow(const Rect& r, const CornerRadii& radii, Point offset, float blur, float spread, Color c, bool inset = false);
    void fill_path(const Path& path, FillRule rule, const Paint& p);
    void stroke_path(const Path& path, const StrokeStyle& style, const Paint& p);
    void draw_image(ImageRef image, const Rect& source, const Rect& dest, Sampling s = Sampling::linear, float opacity = 1);
    // A textured mesh (2D skinning: a picture bent by bones): triangles of
    // `indices` over `positions` (where each vertex is drawn) and `uvs` (where
    // it is in the image, pixels). Each triangle is the image mapped onto it —
    // its own affine — grown by `seam` pixels about its middle, so neighbours
    // overlap and no crack shows between them. Recorded as fills, so every
    // renderer draws it as it draws any image-painted path.
    //
    // With a material, each triangle is lit (Material): the normal map
    // sampled with the colour's uvs, turned by the triangle's own affine.
    void draw_mesh(ImageRef image, std::span<const Point> positions, std::span<const Point> uvs, std::span<const std::uint16_t> indices,
                   Sampling s = Sampling::linear, float opacity = 1, float seam = 0.5f, const Material* material = nullptr);
    // The same mesh with the picture as a mask: its alpha in `color` (lit too,
    // with a material).
    void draw_mesh_mask(ImageRef image, std::span<const Point> positions, std::span<const Point> uvs, std::span<const std::uint16_t> indices,
                        Color color, Sampling s = Sampling::linear, float seam = 0.5f, const Material* material = nullptr);
    void draw_glyphs(FontRef font, float size, Point origin, std::span<const Glyph> glyphs, const Paint& p,
                     std::span<const GlyphStyle> styles = {});
    // Gives the DrawGlyphs command at `index` per-glyph styles (one per glyph),
    // or removes them (empty): how the runtime animates any node's text after
    // the node painted it (D-68).
    void style_glyphs(std::size_t index, std::span<const GlyphStyle> styles);
    void push_clip(const Rect& r, const CornerRadii& radii = {});
    void push_clip(const Path& path, FillRule rule = FillRule::nonzero);
    void pop_clip();
    void push_transform(const Affine& m);
    void pop_transform();
    void push_layer(float opacity, BlendMode blend = BlendMode::normal, EffectRef effect = {},
                    std::span<const float> params = {}, Rect bounds = {}, std::span<const EffectShape> shapes = {});
    void pop_layer();
    void external_texture(std::uint64_t texture, const Rect& dest, float opacity = 1);
    void begin_node(std::uint32_t node);
    void end_node();

    // Appends another list's commands (copying their payloads into this arena).
    void append(const DisplayList& other);
    // One command of another list, with its payload copied into this one.
    void append_command(const Command& c) { commands_.push_back(own(c)); }

    [[nodiscard]] std::span<const Command> commands() const noexcept { return commands_; }
    [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }
    [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }
    // Pushes and pops are balanced.
    [[nodiscard]] bool balanced() const noexcept;
    // A hash of the content (not of addresses): equal lists hash equal.
    [[nodiscard]] std::uint64_t hash() const noexcept;

    void clear() noexcept;
    // Room for `commands` commands: a list rebuilt every frame (clear, then
    // draw) whose frames vary in size allocates nothing below it.
    void reserve(std::size_t commands) { commands_.reserve(commands); }

    // Recolours the commands [from, to): `f` maps each solid (and image
    // mask) paint's colour and each shadow's (a picture drawn, then turned
    // grey, tinted, faded — whoever drew it). Gradients and images keep theirs.
    template <class F>
    void recolor(std::size_t from, std::size_t to, F&& f) {
        for (std::size_t i = from; i < std::min(to, commands_.size()); ++i)
            std::visit(
                [&](auto& c) {
                    if constexpr (requires { c.paint; }) {
                        if (c.paint.kind == Paint::Kind::solid || c.paint.kind == Paint::Kind::image_mask) c.paint.color = f(c.paint.color);
                    } else if constexpr (requires { c.color; }) {
                        c.color = f(c.color);
                    }
                },
                commands_[i]);
    }

private:
    Path mesh_scratch_;  // draw_mesh's triangle, reused
    // The material copied last into the arena: the triangles of a mesh (and a
    // run of commands appended) share one copy.
    struct LastMaterial {
        const Material* at = nullptr;
        LastMaterial() = default;
        LastMaterial(LastMaterial&& o) noexcept : at(std::exchange(o.at, nullptr)) {}
        LastMaterial& operator=(LastMaterial&& o) noexcept {
            at = std::exchange(o.at, nullptr);
            return *this;
        }
    } material_;
    const Material* own(const Material* m);
    void mesh(const Paint& base, std::span<const Point> positions, std::span<const Point> uvs, std::span<const std::uint16_t> indices, float seam);
    Paint own(const Paint& p);
    PathView own(const Path& p);
    StrokeStyle own(const StrokeStyle& s);
    Command own(const Command& c);

    std::vector<Command> commands_;
    Arena                arena_{16 * 1024};
};

}  // namespace omui::render
