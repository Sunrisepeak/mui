// openmui.core:display — contract ⑤, what a frame draws (spec §7).
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
export module openmui.core:display;

import std;
import :base;
import :arena;

export namespace omui {

// x' = a·x + c·y + e, y' = b·x + d·y + f
struct Affine {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    [[nodiscard]] static constexpr Affine translate(float x, float y) noexcept { return {1, 0, 0, 1, x, y}; }
    [[nodiscard]] static constexpr Affine scale(float sx, float sy) noexcept { return {sx, 0, 0, sy, 0, 0}; }
    [[nodiscard]] static Affine rotate(float radians) noexcept;
    // This transform after `o`: (this ∘ o)(p) = this(o(p)).
    [[nodiscard]] constexpr Affine operator*(const Affine& o) const noexcept {
        return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.e + c * o.f + e,
                b * o.e + d * o.f + f};
    }
    [[nodiscard]] constexpr Point apply(Point p) const noexcept { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
    [[nodiscard]] std::optional<Affine> inverse() const noexcept;
    [[nodiscard]] constexpr bool identity() const noexcept { return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0; }
    [[nodiscard]] constexpr bool translation_only() const noexcept { return a == 1 && b == 0 && c == 0 && d == 1; }
    // The bounding box of a transformed rectangle.
    [[nodiscard]] Rect map(const Rect& r) const noexcept;

    friend constexpr bool operator==(const Affine& x, const Affine& y) noexcept {
        return x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d && x.e == y.e && x.f == y.f;
    }
};

struct CornerRadii {
    float top_left = 0, top_right = 0, bottom_right = 0, bottom_left = 0;

    [[nodiscard]] static constexpr CornerRadii all(float r) noexcept { return {r, r, r, r}; }
    [[nodiscard]] constexpr bool zero() const noexcept {
        return top_left == 0 && top_right == 0 && bottom_right == 0 && bottom_left == 0;
    }
    friend constexpr bool operator==(const CornerRadii& x, const CornerRadii& y) noexcept {
        return x.top_left == y.top_left && x.top_right == y.top_right && x.bottom_right == y.bottom_right &&
               x.bottom_left == y.bottom_left;
    }
};

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

struct Paint {
    enum class Kind : std::uint8_t { solid, gradient, image };
    Kind     kind = Kind::solid;
    Color    color;              // solid; also the tint of an image paint
    Gradient gradient;
    ImageRef image;
    Affine   image_transform;    // image space to paint space
    Sampling sampling = Sampling::linear;

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
};

// ── commands ──────────────────────────────────────────────────────────────

struct FillRect      { Rect rect; Paint paint; };
struct FillRoundRect { Rect rect; CornerRadii radii; Paint paint; };
struct StrokeRoundRect { Rect rect; CornerRadii radii; float width; Paint paint; };  // centred on the edge
struct BoxShadow     { Rect rect; CornerRadii radii; Point offset; float blur; float spread; Color color; bool inset; };
struct FillPath      { PathView path; FillRule rule; Paint paint; };
struct StrokePath    { PathView path; StrokeStyle style; Paint paint; };
struct DrawImage     { ImageRef image; Rect source; Rect dest; Sampling sampling; float opacity; };
struct DrawGlyphs    { FontRef font; float size; Point origin; std::span<const Glyph> glyphs; Paint paint; };
struct PushClipRect  { Rect rect; CornerRadii radii; };
struct PushClipPath  { PathView path; FillRule rule; };
struct PopClip       {};
struct PushTransform { Affine transform; };
struct PopTransform  {};
struct PushLayer     { float opacity; BlendMode blend; EffectRef effect; std::span<const float> params; Rect bounds; };
struct PopLayer      {};
struct ExternalTexture { std::uint64_t texture; Rect dest; float opacity; };
struct BeginNode     { std::uint32_t node; };
struct EndNode       {};

using Command = std::variant<FillRect, FillRoundRect, StrokeRoundRect, BoxShadow, FillPath, StrokePath, DrawImage,
                             DrawGlyphs, PushClipRect, PushClipPath, PopClip, PushTransform, PopTransform, PushLayer,
                             PopLayer, ExternalTexture, BeginNode, EndNode>;

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
    void draw_glyphs(FontRef font, float size, Point origin, std::span<const Glyph> glyphs, const Paint& p);
    void push_clip(const Rect& r, const CornerRadii& radii = {});
    void push_clip(const Path& path, FillRule rule = FillRule::nonzero);
    void pop_clip();
    void push_transform(const Affine& m);
    void pop_transform();
    void push_layer(float opacity, BlendMode blend = BlendMode::normal, EffectRef effect = {},
                    std::span<const float> params = {}, Rect bounds = {});
    void pop_layer();
    void external_texture(std::uint64_t texture, const Rect& dest, float opacity = 1);
    void begin_node(std::uint32_t node);
    void end_node();

    // Appends another list's commands (copying their payloads into this arena).
    void append(const DisplayList& other);

    [[nodiscard]] std::span<const Command> commands() const noexcept { return commands_; }
    [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }
    [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }
    // Pushes and pops are balanced.
    [[nodiscard]] bool balanced() const noexcept;
    // A hash of the content (not of addresses): equal lists hash equal.
    [[nodiscard]] std::uint64_t hash() const noexcept;

    void clear() noexcept;

private:
    Paint own(const Paint& p);
    PathView own(const Path& p);
    StrokeStyle own(const StrokeStyle& s);
    Command own(const Command& c);

    std::vector<Command> commands_;
    Arena                arena_{16 * 1024};
};

}  // namespace omui
