module openmui.render;

import std;
import openmui.anim;

namespace omui::render {

Affine rotate(float radians) noexcept {
    auto co = static_cast<float>(anim::dm::cos(radians));
    auto si = static_cast<float>(anim::dm::sin(radians));
    return Affine::rotation(co, si);
}

// ── Path ──────────────────────────────────────────────────────────────────

Path& Path::move_to(Point p) {
    verbs_.push_back(PathVerb::move);
    points_.push_back(p);
    start_ = current_ = p;
    return *this;
}

Path& Path::line_to(Point p) {
    if (verbs_.empty()) move_to(current_);
    verbs_.push_back(PathVerb::line);
    points_.push_back(p);
    current_ = p;
    return *this;
}

Path& Path::quad_to(Point c, Point p) {
    if (verbs_.empty()) move_to(current_);
    verbs_.push_back(PathVerb::quad);
    points_.push_back(c);
    points_.push_back(p);
    current_ = p;
    return *this;
}

Path& Path::cubic_to(Point c1, Point c2, Point p) {
    if (verbs_.empty()) move_to(current_);
    verbs_.push_back(PathVerb::cubic);
    points_.push_back(c1);
    points_.push_back(c2);
    points_.push_back(p);
    current_ = p;
    return *this;
}

Path& Path::close() {
    if (!verbs_.empty() && verbs_.back() != PathVerb::close) verbs_.push_back(PathVerb::close);
    current_ = start_;
    return *this;
}

namespace {

namespace dm = anim::dm;

// Appends cubics approximating the elliptical arc of centre c, radii (rx, ry),
// rotation phi, from angle t0 sweeping dt (radians), at most 90° per cubic.
// Each cubic runs from P(t) to P(t1) with controls P(t) + k·P'(t) and
// P(t1) − k·P'(t1), k = 4/3·tan(step/4).
void arc_cubics(Path& path, Point c, double rx, double ry, double phi, double t0, double dt) {
    int segments = static_cast<int>(std::ceil(dm::abs(dt) / (dm::half_pi + 1e-9)));
    if (segments < 1) segments = 1;
    double step = dt / segments;
    double k = 4.0 / 3.0 * (dm::sin(step / 4.0) / dm::cos(step / 4.0));
    double cp = dm::cos(phi), sp = dm::sin(phi);
    auto map = [&](double x, double y) {
        return Point{static_cast<float>(c.x + cp * x - sp * y), static_cast<float>(c.y + sp * x + cp * y)};
    };
    double t = t0;
    for (int i = 0; i < segments; ++i) {
        double t1 = t + step;
        double x0 = rx * dm::cos(t), y0 = ry * dm::sin(t);
        double x1 = rx * dm::cos(t1), y1 = ry * dm::sin(t1);
        double dx0 = -rx * dm::sin(t), dy0 = ry * dm::cos(t);
        double dx1 = -rx * dm::sin(t1), dy1 = ry * dm::cos(t1);
        path.cubic_to(map(x0 + k * dx0, y0 + k * dy0), map(x1 - k * dx1, y1 - k * dy1), map(x1, y1));
        t = t1;
    }
}

}  // namespace

Path& Path::arc_to(Point radii, float x_rotation, bool large_arc, bool sweep, Point p) {
    // SVG 1.1 implementation notes F.6: endpoint to centre parameterisation.
    Point p0 = current_;
    double rx = dm::abs(radii.x), ry = dm::abs(radii.y);
    if (rx == 0 || ry == 0 || (p0.x == p.x && p0.y == p.y)) return line_to(p);
    double phi = x_rotation;
    double cp = dm::cos(phi), sp = dm::sin(phi);
    double dx = (p0.x - p.x) / 2.0, dy = (p0.y - p.y) / 2.0;
    double x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
    double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1) {
        double s = dm::sqrt(lambda);
        rx *= s;
        ry *= s;
    }
    double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double coef = den == 0 ? 0 : dm::sqrt(std::max(0.0, num / den));
    if (large_arc == sweep) coef = -coef;
    double cxp = coef * rx * y1 / ry, cyp = -coef * ry * x1 / rx;
    Point center{static_cast<float>(cp * cxp - sp * cyp + (p0.x + p.x) / 2.0),
                 static_cast<float>(sp * cxp + cp * cyp + (p0.y + p.y) / 2.0)};
    auto angle = [](double ux, double uy, double vx, double vy) {
        return dm::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    double t0 = angle(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry);
    double dt = angle((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
    if (!sweep && dt > 0) dt -= dm::two_pi;
    else if (sweep && dt < 0) dt += dm::two_pi;
    arc_cubics(*this, center, rx, ry, phi, t0, dt);
    current_ = p;
    return *this;
}

Path& Path::add_rect(const Rect& r) {
    move_to({r.x, r.y});
    line_to({r.right(), r.y});
    line_to({r.right(), r.bottom()});
    line_to({r.x, r.bottom()});
    return close();
}

Path& Path::add_rounded_rect(const Rect& r, const CornerRadii& radii) {
    if (radii.zero()) return add_rect(r);
    // Radii larger than the sides are scaled down together (CSS §5.5).
    CornerRadii f = radii.fitted(r.size());
    float tl = f.top_left, tr = f.top_right, br = f.bottom_right, bl = f.bottom_left;
    constexpr float k = 0.5522847498307936f;  // 4/3·(√2 − 1): a quarter circle as one cubic
    float l = r.x, t = r.y, rr = r.right(), b = r.bottom();
    move_to({l + tl, t});
    line_to({rr - tr, t});
    if (tr > 0) cubic_to({rr - tr + k * tr, t}, {rr, t + tr - k * tr}, {rr, t + tr});
    line_to({rr, b - br});
    if (br > 0) cubic_to({rr, b - br + k * br}, {rr - br + k * br, b}, {rr - br, b});
    line_to({l + bl, b});
    if (bl > 0) cubic_to({l + bl - k * bl, b}, {l, b - bl + k * bl}, {l, b - bl});
    line_to({l, t + tl});
    if (tl > 0) cubic_to({l, t + tl - k * tl}, {l + tl - k * tl, t}, {l + tl, t});
    return close();
}

Path& Path::add_ellipse(const Rect& r) {
    constexpr float k = 0.5522847498307936f;
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2, rx = r.width / 2, ry = r.height / 2;
    move_to({cx + rx, cy});
    cubic_to({cx + rx, cy + k * ry}, {cx + k * rx, cy + ry}, {cx, cy + ry});
    cubic_to({cx - k * rx, cy + ry}, {cx - rx, cy + k * ry}, {cx - rx, cy});
    cubic_to({cx - rx, cy - k * ry}, {cx - k * rx, cy - ry}, {cx, cy - ry});
    cubic_to({cx + k * rx, cy - ry}, {cx + rx, cy - k * ry}, {cx + rx, cy});
    return close();
}

Path Path::transformed(const Affine& m) const {
    Path out = *this;
    for (Point& p : out.points_) p = m.apply(p);
    out.start_ = m.apply(start_);
    out.current_ = m.apply(current_);
    return out;
}

Rect Path::bounds() const noexcept {
    if (points_.empty()) return {};
    float l = points_[0].x, t = points_[0].y, r = l, b = t;
    for (const Point& p : points_) {
        l = std::min(l, p.x);
        t = std::min(t, p.y);
        r = std::max(r, p.x);
        b = std::max(b, p.y);
    }
    return Rect::ltrb(l, t, r, b);
}

// ── DisplayList ───────────────────────────────────────────────────────────

Paint DisplayList::own(const Paint& p) {
    Paint out = p;
    if (p.kind == Paint::Kind::gradient) out.gradient.stops = arena_.copy(p.gradient.stops);
    return out;
}

PathView DisplayList::own(const Path& p) { return {arena_.copy(p.verbs()), arena_.copy(p.points())}; }

StrokeStyle DisplayList::own(const StrokeStyle& s) {
    StrokeStyle out = s;
    out.dashes = arena_.copy(s.dashes);
    return out;
}

void DisplayList::fill_rect(const Rect& r, const Paint& p) { commands_.emplace_back(FillRect{r, own(p)}); }
void DisplayList::fill_round_rect(const Rect& r, const CornerRadii& radii, const Paint& p) {
    if (radii.zero()) return fill_rect(r, p);
    commands_.emplace_back(FillRoundRect{r, radii, own(p)});
}
void DisplayList::stroke_round_rect(const Rect& r, const CornerRadii& radii, float width, const Paint& p) {
    commands_.emplace_back(StrokeRoundRect{r, radii, width, own(p)});
}
void DisplayList::box_shadow(const Rect& r, const CornerRadii& radii, Point offset, float blur, float spread, Color c, bool inset) {
    commands_.emplace_back(BoxShadow{r, radii, offset, blur, spread, c, inset});
}
void DisplayList::fill_path(const Path& path, FillRule rule, const Paint& p) {
    commands_.emplace_back(FillPath{own(path), rule, own(p)});
}
void DisplayList::stroke_path(const Path& path, const StrokeStyle& style, const Paint& p) {
    commands_.emplace_back(StrokePath{own(path), own(style), own(p)});
}
void DisplayList::draw_image(ImageRef image, const Rect& source, const Rect& dest, Sampling s, float opacity) {
    commands_.emplace_back(DrawImage{image, source, dest, s, opacity});
}
void DisplayList::draw_glyphs(FontRef font, float size, Point origin, std::span<const Glyph> glyphs, const Paint& p) {
    commands_.emplace_back(DrawGlyphs{font, size, origin, arena_.copy(glyphs), own(p)});
}
void DisplayList::push_clip(const Rect& r, const CornerRadii& radii) { commands_.emplace_back(PushClipRect{r, radii}); }
void DisplayList::push_clip(const Path& path, FillRule rule) { commands_.emplace_back(PushClipPath{own(path), rule}); }
void DisplayList::pop_clip() { commands_.emplace_back(PopClip{}); }
void DisplayList::push_transform(const Affine& m) { commands_.emplace_back(PushTransform{m}); }
void DisplayList::pop_transform() { commands_.emplace_back(PopTransform{}); }
void DisplayList::push_layer(float opacity, BlendMode blend, EffectRef effect, std::span<const float> params, Rect bounds) {
    commands_.emplace_back(PushLayer{opacity, blend, effect, arena_.copy(params), bounds});
}
void DisplayList::pop_layer() { commands_.emplace_back(PopLayer{}); }
void DisplayList::external_texture(std::uint64_t texture, const Rect& dest, float opacity) {
    commands_.emplace_back(ExternalTexture{texture, dest, opacity});
}
void DisplayList::begin_node(std::uint32_t node) { commands_.emplace_back(BeginNode{node}); }
void DisplayList::end_node() { commands_.emplace_back(EndNode{}); }

Command DisplayList::own(const Command& c) {
    return std::visit(
        [&](const auto& cmd) -> Command {
            using T = std::decay_t<decltype(cmd)>;
            T copy = cmd;
            if constexpr (requires { copy.paint; }) copy.paint = own(cmd.paint);
            if constexpr (requires { copy.path; }) copy.path = {arena_.copy(cmd.path.verbs), arena_.copy(cmd.path.points)};
            if constexpr (requires { copy.style; }) copy.style = own(cmd.style);
            if constexpr (std::same_as<T, DrawGlyphs>) copy.glyphs = arena_.copy(cmd.glyphs);
            if constexpr (std::same_as<T, PushLayer>) copy.params = arena_.copy(cmd.params);
            return copy;
        },
        c);
}

void DisplayList::append(const DisplayList& other) {
    commands_.reserve(commands_.size() + other.commands_.size());
    for (const Command& c : other.commands_) commands_.push_back(own(c));
}

bool DisplayList::balanced() const noexcept {
    int clips = 0, transforms = 0, layers = 0, nodes = 0;
    for (const Command& c : commands_) {
        std::visit(
            [&](const auto& cmd) {
                using T = std::decay_t<decltype(cmd)>;
                if constexpr (std::same_as<T, PushClipRect> || std::same_as<T, PushClipPath>) ++clips;
                else if constexpr (std::same_as<T, PopClip>) --clips;
                else if constexpr (std::same_as<T, PushTransform>) ++transforms;
                else if constexpr (std::same_as<T, PopTransform>) --transforms;
                else if constexpr (std::same_as<T, PushLayer>) ++layers;
                else if constexpr (std::same_as<T, PopLayer>) --layers;
                else if constexpr (std::same_as<T, BeginNode>) ++nodes;
                else if constexpr (std::same_as<T, EndNode>) --nodes;
            },
            c);
        if (clips < 0 || transforms < 0 || layers < 0 || nodes < 0) return false;
    }
    return clips == 0 && transforms == 0 && layers == 0 && nodes == 0;
}

namespace {

struct Hasher {
    std::uint64_t h = 0xcbf29ce484222325ull;
    void bytes(const void* p, std::size_t n) {
        auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 0x100000001b3ull;
        }
    }
    template <class T>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
    void put(T v) {
        bytes(&v, sizeof v);
    }
    void put(const Point& p) { put(p.x); put(p.y); }
    void put(const Rect& r) { put(r.x); put(r.y); put(r.width); put(r.height); }
    void put(const Color& c) { put(c.r); put(c.g); put(c.b); put(c.a); }
    void put(const CornerRadii& r) { put(r.top_left); put(r.top_right); put(r.bottom_right); put(r.bottom_left); }
    void put(const Affine& m) { put(m.a); put(m.b); put(m.c); put(m.d); put(m.e); put(m.f); }
    void put(const PathView& p) {
        put(p.verbs.size());
        for (auto v : p.verbs) put(v);
        for (const auto& pt : p.points) put(pt);
    }
    void put(const Paint& p) {
        put(p.kind);
        put(p.color);
        if (p.kind == Paint::Kind::gradient) {
            put(p.gradient.kind); put(p.gradient.start); put(p.gradient.end); put(p.gradient.radius); put(p.gradient.spread);
            for (const auto& s : p.gradient.stops) { put(s.offset); put(s.color); }
        }
        if (p.kind == Paint::Kind::image) { put(p.image.id); put(p.image_transform); put(p.sampling); }
    }
    void put(const StrokeStyle& s) {
        put(s.width); put(s.cap); put(s.join); put(s.miter_limit); put(s.dash_offset);
        for (float d : s.dashes) put(d);
    }
};

}  // namespace

std::uint64_t DisplayList::hash() const noexcept {
    Hasher h;
    for (const Command& c : commands_) {
        h.put(c.index());
        std::visit(
            [&](const auto& cmd) {
                using T = std::decay_t<decltype(cmd)>;
                if constexpr (std::same_as<T, FillRect>) { h.put(cmd.rect); h.put(cmd.paint); }
                else if constexpr (std::same_as<T, FillRoundRect>) { h.put(cmd.rect); h.put(cmd.radii); h.put(cmd.paint); }
                else if constexpr (std::same_as<T, StrokeRoundRect>) { h.put(cmd.rect); h.put(cmd.radii); h.put(cmd.width); h.put(cmd.paint); }
                else if constexpr (std::same_as<T, BoxShadow>) { h.put(cmd.rect); h.put(cmd.radii); h.put(cmd.offset); h.put(cmd.blur); h.put(cmd.spread); h.put(cmd.color); h.put(cmd.inset); }
                else if constexpr (std::same_as<T, FillPath>) { h.put(cmd.path); h.put(cmd.rule); h.put(cmd.paint); }
                else if constexpr (std::same_as<T, StrokePath>) { h.put(cmd.path); h.put(cmd.style); h.put(cmd.paint); }
                else if constexpr (std::same_as<T, DrawImage>) { h.put(cmd.image.id); h.put(cmd.source); h.put(cmd.dest); h.put(cmd.sampling); h.put(cmd.opacity); }
                else if constexpr (std::same_as<T, DrawGlyphs>) {
                    h.put(cmd.font.id); h.put(cmd.size); h.put(cmd.origin); h.put(cmd.paint);
                    for (const auto& g : cmd.glyphs) { h.put(g.id); h.put(g.position); }
                }
                else if constexpr (std::same_as<T, PushClipRect>) { h.put(cmd.rect); h.put(cmd.radii); }
                else if constexpr (std::same_as<T, PushClipPath>) { h.put(cmd.path); h.put(cmd.rule); }
                else if constexpr (std::same_as<T, PushTransform>) { h.put(cmd.transform); }
                else if constexpr (std::same_as<T, PushLayer>) {
                    h.put(cmd.opacity); h.put(cmd.blend); h.put(cmd.effect.id); h.put(cmd.bounds);
                    for (float p : cmd.params) h.put(p);
                }
                else if constexpr (std::same_as<T, ExternalTexture>) { h.put(cmd.texture); h.put(cmd.dest); h.put(cmd.opacity); }
                else if constexpr (std::same_as<T, BeginNode>) { h.put(cmd.node); }
            },
            c);
    }
    return h.h;
}

void DisplayList::clear() noexcept {
    commands_.clear();
    arena_.reset();
}

}  // namespace omui::render
