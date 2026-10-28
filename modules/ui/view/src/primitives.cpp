module openmui.view;

import std;
import openmui.core;
import openmui.layout;
import openmui.render;
import openmui.text;
import openmui.image;
import openmui.raster;

namespace omui {

// ── values ───────────────────────────────────────────────────────────────

Value ValueOf<Dim>::to(const Dim& d) {
    switch (d.length.unit) {
        case layout::Length::Unit::points: return Value(static_cast<double>(d.length.value));
        case layout::Length::Unit::percent: return Value(std::format("{}%", d.length.value));
        case layout::Length::Unit::auto_: return Value("auto");
    }
    return Value("auto");
}

Dim ValueOf<Dim>::from(const Value& v) {
    if (v.is_number()) return Dim(static_cast<float>(v.as_double()));
    std::string_view s = v.as_string();
    float x = 0;
    if (s.ends_with('%')) {
        std::from_chars(s.data(), s.data() + s.size() - 1, x);
        return Dim(layout::pct(x));
    }
    if (std::from_chars(s.data(), s.data() + s.size(), x).ec == std::errc{}) return Dim(x);
    return Dim(layout::Length::automatic());
}

Value ValueOf<Edges>::to(const Edges& e) {
    const Insets& i = e.insets;
    return Value(Value::List{Value(i.top), Value(i.right), Value(i.bottom), Value(i.left)});
}

Edges ValueOf<Edges>::from(const Value& v) {
    if (const Value::List* l = v.list(); l && l->size() == 4)
        return Edges(Insets{static_cast<float>((*l)[0].as_double()), static_cast<float>((*l)[1].as_double()),
                            static_cast<float>((*l)[2].as_double()), static_cast<float>((*l)[3].as_double())});
    return Edges(static_cast<float>(v.as_double()));
}

Fill::Fill(Color c) : text(ValueOf<Color>::to(c).as_string()) {}

// ── track lists ──────────────────────────────────────────────────────────

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == ',')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == ',')) s.remove_suffix(1);
    return s;
}

// Splits at top-level separators (spaces or commas outside parentheses).
std::vector<std::string_view> split_tracks(std::string_view s, bool commas) {
    std::vector<std::string_view> out;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        const bool end = i == s.size();
        const char c = end ? ' ' : s[i];
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        const bool sep = depth == 0 && (commas ? c == ',' : (c == ' ' || c == ','));
        if (end || sep) {
            if (auto t = trim(s.substr(start, i - start)); !t.empty()) out.push_back(t);
            start = i + 1;
        }
    }
    return out;
}

std::optional<layout::Track> parse_track(std::string_view t) {
    using layout::Track;
    t = trim(t);
    if (t == "auto") return Track::automatic();
    if (t == "min-content" || t == "min_content") return Track::min_content();
    if (t == "max-content" || t == "max_content") return Track::max_content();
    if (t.starts_with("minmax(") && t.ends_with(')')) {
        auto parts = split_tracks(t.substr(7, t.size() - 8), true);
        if (parts.size() != 2) return std::nullopt;
        auto lo = parse_track(parts[0]), hi = parse_track(parts[1]);
        if (!lo || !hi) return std::nullopt;
        return Track::minmax(*lo, *hi);
    }
    float x = 0;
    auto r = std::from_chars(t.data(), t.data() + t.size(), x);
    if (r.ec != std::errc{}) return std::nullopt;
    std::string_view unit(r.ptr, static_cast<std::size_t>(t.data() + t.size() - r.ptr));
    if (unit.empty() || unit == "px") return Track::points(x);
    if (unit == "fr") return Track::fr(x);
    if (unit == "%") return Track::percent(x);
    return std::nullopt;
}

}  // namespace

std::vector<layout::Track> parse_tracks(std::string_view text) {
    std::vector<layout::Track> out;
    for (std::string_view tok : split_tracks(text, false)) {
        if (tok.starts_with("repeat(") && tok.ends_with(')')) {
            std::string_view inner = tok.substr(7, tok.size() - 8);
            auto comma = inner.find(',');
            int count = 0;
            std::string_view n = trim(inner.substr(0, comma));
            std::from_chars(n.data(), n.data() + n.size(), count);
            if (comma == std::string_view::npos || count <= 0) {
                warn("bad track list '{}': repeat(count, tracks)", text);
                continue;
            }
            std::vector<layout::Track> body = parse_tracks(inner.substr(comma + 1));
            for (int i = 0; i < count; ++i) out.insert(out.end(), body.begin(), body.end());
            continue;
        }
        if (auto t = parse_track(tok)) out.push_back(*t);
        else warn("bad track '{}' in '{}'", tok, text);
    }
    return out;
}

// ── components ───────────────────────────────────────────────────────────

namespace {

struct RootNode {
    static constexpr std::string_view name = "root";
    static constexpr Role role = Role::application;
    void style(const Theme&, layout::Style& s) const {
        s.display = layout::Display::stack;
        s.width = layout::pct(100);
        s.height = layout::pct(100);
    }
};

struct BoxNode {
    static constexpr std::string_view name = "box";
};
struct ColumnNode {
    static constexpr std::string_view name = "column";
    void style(const Theme&, layout::Style& s) const { s.direction = layout::Direction::column; }
};
struct RowNode {
    static constexpr std::string_view name = "row";
    void style(const Theme&, layout::Style& s) const {
        s.direction = layout::Direction::row;
        s.align_items = layout::Align::center;
    }
};
struct StackNode {
    static constexpr std::string_view name = "stack";
    void style(const Theme&, layout::Style& s) const { s.display = layout::Display::stack; }
};
struct GridNode {
    static constexpr std::string_view name = "grid";
    void style(const Theme&, layout::Style& s) const { s.display = layout::Display::grid; }
};
struct SpacerNode {
    static constexpr std::string_view name = "spacer";
    static constexpr Role role = Role::none;
    void style(const Theme&, layout::Style& s) const { s.grow = 1; }
};

typeset::TextAlign parse_align(std::string_view s) {
    if (s == "center") return typeset::TextAlign::center;
    if (s == "end") return typeset::TextAlign::end;
    if (s == "left") return typeset::TextAlign::left;
    if (s == "right") return typeset::TextAlign::right;
    if (s == "justify") return typeset::TextAlign::justify;
    return typeset::TextAlign::start;
}

// A paragraph laid out at the width it is drawn: a hair over the box, so the
// width measured as the widest line never wraps that line.
constexpr float width_slack = 1.0f / 1024.0f;

struct TextNode {
    static constexpr std::string_view name = "text";
    static constexpr Role role = Role::text;
    static const spec::ContractInfo& contract() { return spec::catalog::text; }

    std::string          text;
    std::string          style_token = "body";
    std::string          color;
    std::string          align = "start";
    bool                 wrap = true;
    int                  max_lines = 0;
    bool                 selectable = false;
    std::string          origin;
    std::optional<float> size, weight;

    Services* services = nullptr;
    mutable std::optional<typeset::Paragraph> paragraph;
    mutable float laid = -1;

    void mount(Services& s, NodeId) { services = &s; }
    void restyle() { paragraph.reset(); }

    bool set_prop(std::string_view k, const Value& v) {
        if (k == "text") text = ValueOf<std::string>::from(v);
        else if (k == "style") style_token = v.is_null() ? "body" : std::string(v.as_string("body"));
        else if (k == "color") color = std::string(v.as_string());
        else if (k == "align") align = std::string(v.as_string("start"));
        else if (k == "wrap") wrap = v.as_bool(true);
        else if (k == "max_lines") max_lines = static_cast<int>(v.as_int());
        else if (k == "selectable") {
            selectable = v.as_bool();
            return true;
        } else if (k == "origin") {
            origin = std::string(v.as_string());
            return true;
        } else if (k == "size") size = v.is_null() ? std::nullopt : std::optional<float>(static_cast<float>(v.as_double()));
        else if (k == "weight") weight = v.is_null() ? std::nullopt : std::optional<float>(static_cast<float>(v.as_double()));
        else return false;
        paragraph.reset();
        return true;
    }
    Value get_prop(std::string_view k) const {
        if (k == "text") return Value(text);
        return {};
    }

    typeset::Paragraph& shaped(Services& s) const {
        if (!paragraph) {
            const Theme& theme = s.theme();
            const TextToken& tok = theme.text_style(style_token);
            typeset::ParagraphStyle ps;
            ps.style.families = tok.families;
            ps.style.size = size.value_or(tok.size);
            ps.style.weight = weight.value_or(tok.weight);
            ps.style.line_height = tok.line_height;
            ps.style.letter_spacing = tok.letter_spacing;
            ps.style.color = theme.color(color.empty() ? std::string_view("text") : std::string_view(color), Color::black());
            ps.align = parse_align(align);
            ps.wrap = wrap;
            ps.max_lines = max_lines;
            paragraph.emplace(s.fonts(), text, std::move(ps));
            laid = -1;
        }
        return *paragraph;
    }
    void lay(typeset::Paragraph& p, float width) const {
        if (width != laid) {
            p.layout(width);
            laid = width;
        }
    }

    Size measure(MeasureContext& m, std::optional<float> w, std::optional<float>, layout::Available aw, layout::Available) {
        typeset::Paragraph& p = shaped(m.services);
        float width = std::numeric_limits<float>::infinity();
        if (w) width = *w + width_slack;
        else if (!wrap || aw.kind == layout::Available::Kind::max_content) width = std::numeric_limits<float>::infinity();
        else if (aw.kind == layout::Available::Kind::min_content) width = p.min_intrinsic_width() + width_slack;
        else width = aw.value + width_slack;
        lay(p, width);
        Size s = p.size();
        return {w ? *w : s.width, s.height};
    }
    float baseline(MeasureContext& m, Size content) {
        typeset::Paragraph& p = shaped(m.services);
        lay(p, content.width + width_slack);
        return p.first_baseline();
    }
    void paint(PaintContext& ctx) const {
        typeset::Paragraph& p = shaped(ctx.services);
        lay(p, (wrap ? ctx.content.width : std::numeric_limits<float>::infinity()) + width_slack);
        p.paint(ctx.list, ctx.content.origin());
    }
    void semantics(SemanticsContext&, spec::SemanticNode& n) const {
        if (n.name.empty()) n.name = text;  // S-6: text is its own name
        if (!origin.empty())
            if (auto o = spec::parse_origin(origin)) n.origin = *o;
        if (selectable) n.actions.push_back({"set_selection", Effect::read, {}, {}, {{"anchor", "i64"}, {"focus", "i64"}}});
    }
};

struct ImageNode {
    static constexpr std::string_view name = "image";
    static constexpr Role role = Role::image;
    static constexpr bool clips = true;
    static const spec::ContractInfo& contract() { return spec::catalog::image; }

    std::string source;
    std::string alt;
    std::string fit = "contain";
    Services*   services = nullptr;
    NodeId      node;
    render::ImageRef ref;
    bool        owned = false;
    int         width = 0, height = 0;

    ImageNode() = default;
    ImageNode(const ImageNode&) = delete;
    ImageNode& operator=(const ImageNode&) = delete;
    ~ImageNode() { release(); }

    void mount(Services& s, NodeId n) {
        services = &s;
        node = n;
        if (!source.empty()) load();
    }

    void release() {
        if (owned && services) services->images().remove(ref);
        ref = {};
        owned = false;
        width = height = 0;
    }

    void load() {
        release();
        if (!services || source.empty()) return;
        if (source.starts_with("image:")) {
            std::uint32_t id = 0;
            std::from_chars(source.data() + 6, source.data() + source.size(), id);
            ref = {id};
            if (const raster::Surface* s = services->images().get(ref)) {
                width = s->width();
                height = s->height();
                services->emit(node, "loaded", Value{}.set("width", width).set("height", height));
            } else {
                services->emit(node, "failed", Value{}.set("reason", std::format("no stored image {}", id)));
            }
            return;
        }
        auto img = imageio::load_png(source);
        if (!img) {
            services->emit(node, "failed", Value{}.set("reason", img.error()));
            return;
        }
        if (img->alpha == imageio::Alpha::straight) imageio::premultiply(*img);
        std::vector<raster::Rgba8> px(static_cast<std::size_t>(img->width) * static_cast<std::size_t>(img->height));
        std::memcpy(px.data(), img->rgba.data(), px.size() * 4);
        width = img->width;
        height = img->height;
        ref = services->images().add(width, height, std::move(px));
        owned = true;
        services->emit(node, "loaded", Value{}.set("width", width).set("height", height));
    }

    bool set_prop(std::string_view k, const Value& v) {
        if (k == "source") {
            source = std::string(v.as_string());
            load();
        } else if (k == "alt") alt = std::string(v.as_string());
        else if (k == "fit") fit = v.is_null() ? "contain" : std::string(v.as_string("contain"));
        else return false;
        return true;
    }

    Size measure(MeasureContext&, std::optional<float> w, std::optional<float> h, layout::Available aw, layout::Available) {
        if (width <= 0 || height <= 0) return {w.value_or(0), h.value_or(0)};
        const float ratio = static_cast<float>(width) / static_cast<float>(height);
        if (w && h) return {*w, *h};
        if (w) return {*w, *w / ratio};
        if (h) return {*h * ratio, *h};
        float iw = static_cast<float>(width);
        if (aw.is_definite() && iw > aw.value) iw = aw.value;  // shrink to fit, keeping the ratio
        return {iw, iw / ratio};
    }

    void paint(PaintContext& ctx) const {
        if (width <= 0 || height <= 0 || ctx.content.empty()) return;
        const Rect box = ctx.content;
        const float iw = static_cast<float>(width), ih = static_cast<float>(height);
        Rect dest = box;
        if (fit != "fill") {
            float sx = box.width / iw, sy = box.height / ih;
            float s = fit == "cover" ? std::max(sx, sy) : fit == "none" ? 1.0f : std::min(sx, sy);
            if (fit == "scale_down") s = std::min(1.0f, std::min(sx, sy));
            dest = {box.x + (box.width - iw * s) / 2, box.y + (box.height - ih * s) / 2, iw * s, ih * s};
        }
        ctx.list.draw_image(ref, {0, 0, iw, ih}, dest, render::Sampling::linear, 1.0f);
    }

    void semantics(SemanticsContext&, spec::SemanticNode& n) const {
        if (alt.empty() && n.name.empty()) n.role = Role::none;  // decorative
        else if (n.name.empty()) n.name = alt;
    }
};

struct ScrollNode {
    static constexpr std::string_view name = "scroll_view";
    static constexpr Role role = Role::scroll_view;
    static constexpr bool clips = true;
    static const spec::ContractInfo& contract() { return spec::catalog::scroll_view; }

    bool      horizontal = false;
    bool      vertical = true;
    Point     target;  // where the offset is going
    Services* services = nullptr;
    NodeId    node;
    hanim::Set::Channel cx, cy;
    // touch drags
    bool      dragging = false;
    Point     last;
    seconds   last_time{0};
    Point     velocity;

    void mount(Services& s, NodeId n) {
        services = &s;
        node = n;
        cx = s.animations().hold(0);
        cy = s.animations().hold(0);
    }
    ~ScrollNode() = default;

    void style(const Theme&, layout::Style& s) const {
        s.overflow_x = horizontal ? layout::Overflow::scroll : layout::Overflow::clip;
        s.overflow_y = vertical ? layout::Overflow::scroll : layout::Overflow::clip;
        s.direction = horizontal && !vertical ? layout::Direction::row : layout::Direction::column;
    }

    [[nodiscard]] Size viewport() const {
        const Rect b = services->bounds(node);
        const Insets in = services->content_insets(node);
        return {std::max(0.0f, b.width - in.horizontal()), std::max(0.0f, b.height - in.vertical())};
    }
    [[nodiscard]] Point max_offset() const {
        const Size c = services->content_size(node), v = viewport();
        return {horizontal ? std::max(0.0f, c.width - v.width) : 0.0f, vertical ? std::max(0.0f, c.height - v.height) : 0.0f};
    }
    [[nodiscard]] Point clamp(Point p) const {
        const Point m = max_offset();
        return {std::clamp(p.x, 0.0f, m.x), std::clamp(p.y, 0.0f, m.y)};
    }

    // Moves toward `to`; `smooth` glides there (wheel notches, keys, actions).
    bool scroll(Point to, bool smooth) {
        to = clamp(to);
        if (to.x == target.x && to.y == target.y) return false;
        target = to;
        hanim::Set& a = services->animations();
        const seconds now = services->now();
        if (smooth) {
            a.tween_to(cx, now, to.x, seconds(0.14), hanim::ease::out_cubic);
            a.tween_to(cy, now, to.y, seconds(0.14), hanim::ease::out_cubic);
        } else {
            a.jump(cx, to.x);
            a.jump(cy, to.y);
        }
        services->write_back(node, "x", Value(static_cast<double>(to.x)));
        services->write_back(node, "y", Value(static_cast<double>(to.y)));
        services->emit(node, "scrolled", Value{}.set("x", static_cast<double>(to.x)).set("y", static_cast<double>(to.y)));
        services->invalidate_paint(node);
        services->invalidate_semantics(node);
        return true;
    }

    bool set_prop(std::string_view k, const Value& v) {
        if (k == "horizontal") horizontal = v.as_bool(false);
        else if (k == "vertical") vertical = v.is_null() || v.as_bool(true);
        else if (k == "x" || k == "y") {
            // From outside (a binding, a script): go there without gliding.
            float value = static_cast<float>(v.as_double());
            Point to = k == "x" ? Point{value, target.y} : Point{target.x, value};
            if (services) {
                target = to;
                services->animations().jump(k == "x" ? cx : cy, k == "x" ? to.x : to.y);
            } else {
                target = to;
            }
            return true;
        } else return false;
        return true;
    }

    [[nodiscard]] Point scroll_offset() const {
        if (!services) return target;
        const seconds now = services->now();
        const hanim::Set& a = services->animations();
        Point p{static_cast<float>(a.value(cx, now)), static_cast<float>(a.value(cy, now))};
        return clamp(p);
    }

    bool on_event(EventContext& ctx, const InputEvent& e) {
        if (const auto* w = std::get_if<WheelEvent>(&e)) {
            Point d = w->delta;
            if (!vertical && horizontal && d.x == 0) d = {d.y, 0};  // a plain wheel scrolls a horizontal view
            return scroll({target.x + d.x, target.y + d.y}, !w->precise);
        }
        if (const auto* p = std::get_if<PointerEvent>(&e); p && p->type == PointerType::touch) {
            const seconds now = services->now();
            switch (p->kind) {
                case PointerKind::down:
                    dragging = true;
                    last = p->position;
                    last_time = now;
                    velocity = {};
                    services->capture_pointer(ctx.node);
                    return false;
                case PointerKind::move:
                    if (!dragging) return false;
                    {
                        const Point d{last.x - p->position.x, last.y - p->position.y};
                        const double dt = (now - last_time).count();
                        if (dt > 0) velocity = {static_cast<float>(d.x / dt), static_cast<float>(d.y / dt)};
                        last = p->position;
                        last_time = now;
                        scroll({target.x + d.x, target.y + d.y}, false);
                    }
                    return true;
                case PointerKind::up:
                case PointerKind::cancel:
                    if (!dragging) return false;
                    dragging = false;
                    {
                        // Momentum: the offset decays from the release velocity.
                        hanim::Set& a = services->animations();
                        auto glide = [&](hanim::Set::Channel& c, float from, float v) {
                            if (v == 0) return;
                            a.remove(c);
                            c = a.decay(from, v, now);
                        };
                        glide(cx, target.x, horizontal ? velocity.x : 0);
                        glide(cy, target.y, vertical ? velocity.y : 0);
                        target = clamp({static_cast<float>(a.target(cx)), static_cast<float>(a.target(cy))});
                        services->write_back(node, "x", Value(static_cast<double>(target.x)));
                        services->write_back(node, "y", Value(static_cast<double>(target.y)));
                        services->invalidate_paint(node);
                    }
                    return true;
                default: return false;
            }
        }
        if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down && k->mods.bits == 0) {
            constexpr float line = 40;
            switch (k->key) {
                case Key::down: return vertical && scroll({target.x, target.y + line}, true);
                case Key::up: return vertical && scroll({target.x, target.y - line}, true);
                case Key::right: return horizontal && scroll({target.x + line, target.y}, true);
                case Key::left: return horizontal && scroll({target.x - line, target.y}, true);
                default: return false;
            }
        }
        return false;
    }

    // Arguments are numbers or the symbols of the contract's keys: end, page, -page.
    float resolve(const Value* v, float current, float page, float end) const {
        if (!v) return current;
        if (v->is_number()) return static_cast<float>(v->as_double());
        std::string_view s = v->as_string();
        if (s == "end") return end;
        if (s == "page") return page;
        if (s == "-page") return -page;
        return current;
    }

    ActResult act(ActContext& ctx, std::string_view action) {
        const Size v = viewport();
        const Point m = max_offset();
        if (action == "scroll_to") {
            const float x = resolve(ctx.args.get("x"), target.x, v.width, m.x);
            const float y = resolve(ctx.args.get("y"), target.y, v.height, m.y);
            scroll({x, y}, true);
            return ActResult::done(Value{}.set("x", static_cast<double>(target.x)).set("y", static_cast<double>(target.y)));
        }
        if (action == "scroll_by") {
            const float dx = resolve(ctx.args.get("dx"), 0, v.width * 0.9f, m.x);
            const float dy = resolve(ctx.args.get("dy"), 0, v.height * 0.9f, m.y);
            scroll({target.x + dx, target.y + dy}, true);
            return ActResult::done(Value{}.set("x", static_cast<double>(target.x)).set("y", static_cast<double>(target.y)));
        }
        return ActResult::fail(std::format("scroll_view has no action '{}'", action));
    }

    void paint_over(PaintContext& ctx) const {
        const seconds now = services->now();
        const hanim::Set& a = services->animations();
        if (!a.settled(cx, now) || !a.settled(cy, now)) {
            services->invalidate_paint(node);
            services->request_frame();
        }
        const Size c = services->content_size(node), view = viewport();
        const Point off = scroll_offset();
        const Color thumb = ctx.theme.color("border_strong", Color::hex(0x94a3b8)).with_alpha(0.7f);
        constexpr float bar = 6, inset = 2;
        if (vertical && c.height > view.height && view.height > 0) {
            const float h = std::max(24.0f, view.height * view.height / c.height);
            const float y = (view.height - h) * (off.y / std::max(1.0f, c.height - view.height));
            ctx.list.fill_round_rect({ctx.size.width - bar - inset, y + ctx.content.y, bar, h}, CornerRadii::all(bar / 2),
                                     render::Paint::solid(thumb));
        }
        if (horizontal && c.width > view.width && view.width > 0) {
            const float w = std::max(24.0f, view.width * view.width / c.width);
            const float x = (view.width - w) * (off.x / std::max(1.0f, c.width - view.width));
            ctx.list.fill_round_rect({x + ctx.content.x, ctx.size.height - bar - inset, w, bar}, CornerRadii::all(bar / 2),
                                     render::Paint::solid(thumb));
        }
    }

    void semantics(SemanticsContext&, spec::SemanticNode& n) const {
        const Point m = max_offset();
        n.value = Value{}
                      .set("x", static_cast<double>(target.x))
                      .set("y", static_cast<double>(target.y))
                      .set("max_x", static_cast<double>(m.x))
                      .set("max_y", static_cast<double>(m.y));
    }
};

struct PainterNode {
    static constexpr std::string_view name = "paint";
    static constexpr Role role = Role::canvas;
    std::function<void(PaintContext&)>                    draw;
    std::function<bool(EventContext&, const InputEvent&)> handle;

    void paint(PaintContext& ctx) const {
        if (draw) draw(ctx);
    }
    bool on_event(EventContext& ctx, const InputEvent& e) { return handle ? handle(ctx, e) : false; }
};

struct Registry {
    std::map<std::string, const ComponentType*, std::less<>> types;
    Registry() {
        for (const ComponentType* t :
             {&component_type<RootNode>(), &component_type<BoxNode>(), &component_type<ColumnNode>(),
              &component_type<RowNode>(), &component_type<StackNode>(), &component_type<GridNode>(),
              &component_type<SpacerNode>(), &component_type<TextNode>(), &component_type<ImageNode>(),
              &component_type<ScrollNode>(), &component_type<PainterNode>()})
            types.emplace(std::string(t->name), t);
    }
};

Registry& registry() {
    static Registry r;
    return r;
}

void put(Element& e, std::string_view name, std::string_view value) {
    if (!value.empty()) e.prop(std::string(name), Source{Value(value), {}, {}});
}

void common(Element& e, const BoxOptions& o) {
    e.id = o.id;
    e.prop("width", o.width).prop("height", o.height);
    e.prop("min_width", o.min_width).prop("min_height", o.min_height);
    e.prop("max_width", o.max_width).prop("max_height", o.max_height);
    e.prop("padding", o.padding).prop("margin", o.margin).prop("gap", o.gap);
    put(e, "justify", o.justify);
    put(e, "align", o.align);
    put(e, "align_self", o.align_self);
    put(e, "wrap", o.wrap);
    e.prop("grow", o.grow).prop("shrink", o.shrink);
    e.prop("background", o.background).prop("border_color", o.border_color);
    e.prop("border_width", o.border_width).prop("radius", o.radius);
    put(e, "shadow", o.shadow);
    e.prop("opacity", o.opacity).prop("translate_x", o.translate_x).prop("translate_y", o.translate_y);
    e.prop("scale", o.scale).prop("rotation", o.rotation);
    if (o.clip) e.prop("clip", Source{Value(true), {}, {}});
    e.prop("visible", o.visible).prop("disabled", o.disabled);
    put(e, "label", o.label);
    put(e, "description", o.description);
    for (const auto& [k, h] : o.on) e.on(k, h);
    for (const auto& [k, t] : o.animate) e.animate(k, t);
}

Element boxed(const ComponentType& type, BoxOptions o, std::vector<Element> children, std::source_location where) {
    Element e;
    e.type = &type;
    e.where = where;
    common(e, o);
    e.children = std::move(children);
    return e;
}

}  // namespace

const ComponentType& root_type() { return component_type<RootNode>(); }

const ComponentType* find_type(std::string_view name) {
    auto& r = registry();
    auto it = r.types.find(name);
    return it == r.types.end() ? nullptr : it->second;
}

void register_type(const ComponentType& type) { registry().types.insert_or_assign(std::string(type.name), &type); }

std::vector<const ComponentType*> registered_types() {
    std::vector<const ComponentType*> out;
    for (const auto& [name, type] : registry().types) out.push_back(type);
    std::ranges::sort(out, {}, &ComponentType::name);
    return out;
}

Element box(BoxOptions o, std::vector<Element> children, std::source_location where) {
    return boxed(component_type<BoxNode>(), std::move(o), std::move(children), where);
}
Element column(BoxOptions o, std::vector<Element> children, std::source_location where) {
    return boxed(component_type<ColumnNode>(), std::move(o), std::move(children), where);
}
Element row(BoxOptions o, std::vector<Element> children, std::source_location where) {
    return boxed(component_type<RowNode>(), std::move(o), std::move(children), where);
}
Element stack(BoxOptions o, std::vector<Element> children, std::source_location where) {
    return boxed(component_type<StackNode>(), std::move(o), std::move(children), where);
}
Element grid(BoxOptions o, std::string columns, std::vector<Element> children, std::source_location where) {
    Element e = boxed(component_type<GridNode>(), std::move(o), std::move(children), where);
    put(e, "columns", columns);
    return e;
}
Element spacer(float grow, std::source_location where) {
    Element e;
    e.type = &component_type<SpacerNode>();
    e.where = where;
    if (grow != 1) e.prop("grow", Source{Value(grow), {}, {}});
    return e;
}

Element text(TextOptions o, std::source_location where) {
    Element e;
    e.type = &component_type<TextNode>();
    e.where = where;
    e.id = o.id;
    e.prop("text", o.text);
    if (o.style != "body") put(e, "style", o.style);
    e.prop("color", o.color);
    put(e, "align", o.align);
    if (!o.wrap) e.prop("wrap", Source{Value(false), {}, {}});
    if (o.max_lines) e.prop("max_lines", Source{Value(o.max_lines), {}, {}});
    if (o.selectable) e.prop("selectable", Source{Value(true), {}, {}});
    put(e, "origin", o.origin);
    e.prop("size", o.size).prop("weight", o.weight);
    e.prop("width", o.width).prop("max_width", o.max_width);
    e.prop("grow", o.grow).prop("opacity", o.opacity);
    for (const auto& [k, t] : o.animate) e.animate(k, t);
    return e;
}

Element image(ImageOptions o, std::source_location where) {
    Element e;
    e.type = &component_type<ImageNode>();
    e.where = where;
    e.id = o.id;
    put(e, "source", o.source);
    put(e, "alt", o.alt);
    if (o.fit != "contain") put(e, "fit", o.fit);
    e.prop("width", o.width).prop("height", o.height);
    e.prop("radius", o.radius).prop("opacity", o.opacity);
    return e;
}

Element scroll(ScrollOptions o, std::vector<Element> children, std::source_location where) {
    Element e;
    e.type = &component_type<ScrollNode>();
    e.where = where;
    e.id = o.id;
    if (o.horizontal) e.prop("horizontal", Source{Value(true), {}, {}});
    if (!o.vertical) e.prop("vertical", Source{Value(false), {}, {}});
    e.prop("x", o.x).prop("y", o.y);
    e.prop("width", o.width).prop("height", o.height).prop("max_height", o.max_height);
    e.prop("grow", o.grow).prop("gap", o.gap).prop("padding", o.padding).prop("background", o.background);
    put(e, "label", o.label);
    e.children = std::move(children);
    return e;
}

Element painter(PaintOptions o, std::source_location where) {
    Element e;
    e.type = &component_type<PainterNode>();
    e.where = where;
    e.id = o.id;
    e.prop("width", o.width).prop("height", o.height).prop("grow", o.grow);
    if (o.role != Role::canvas) put(e, "role", spec::name(o.role));
    put(e, "label", o.label);
    if (o.focusable) e.prop("focusable", Source{Value(true), {}, {}});
    for (const auto& [k, h] : o.on) e.on(k, h);
    e.configure = [draw = std::move(o.paint), handle = std::move(o.on_event)](void* p) {
        auto* node = static_cast<PainterNode*>(p);
        node->draw = draw;
        node->handle = handle;
    };
    return e;
}

}  // namespace omui
