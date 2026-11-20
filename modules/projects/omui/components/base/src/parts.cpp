module omui.ui.base;

import std;
import omui.core;
import omui.view;
import xrender.text;

namespace omui::parts {

std::string segment(const Services& services, NodeId node) {
    const std::string p = services.path(node);
    const auto slash = p.rfind('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

void chevron(PaintContext& ctx, Point c, float size, Color color) {
    xrender::Path p;
    p.move_to({c.x - size / 2, c.y - size / 4}).line_to({c.x, c.y + size / 4}).line_to({c.x + size / 2, c.y - size / 4});
    xrender::StrokeStyle st;
    st.width = 1.5f;
    st.cap = xrender::LineCap::round;
    st.join = xrender::LineJoin::round;
    ctx.list.stroke_path(p, st, xrender::Paint::solid(color));
}

void focus_ring(PaintContext& ctx, const Rect& r, const CornerRadii& radii) {
    constexpr float gap = 2, width = 2;
    const float out = gap + width / 2;
    CornerRadii ring = radii;
    for (float* x : {&ring.top_left, &ring.top_right, &ring.bottom_right, &ring.bottom_left}) *x += out;
    ctx.list.stroke_round_rect({r.x - out, r.y - out, r.width + 2 * out, r.height + 2 * out}, ring, width,
                               xrender::Paint::solid(ctx.theme.color("focus", Color::hex(0x6366f1, 0.55f))));
}

std::optional<Rect> view_box(std::string_view text) noexcept {
    std::array<float, 4> numbers{};
    std::size_t found = 0;
    while (found < numbers.size() && !text.empty()) {
        const auto start = text.find_first_not_of(" ,\t");
        if (start == std::string_view::npos) break;
        text.remove_prefix(start);
        const auto [end, failure] = std::from_chars(text.data(), text.data() + text.size(), numbers[found]);
        if (failure != std::errc{}) break;
        text.remove_prefix(static_cast<std::size_t>(end - text.data()));
        ++found;
    }
    if (found != 4 || !(numbers[2] > 0) || !(numbers[3] > 0)) return std::nullopt;
    return Rect{numbers[0], numbers[1], numbers[2], numbers[3]};
}

mlibs::geom::Affine fitted(const Rect& view_box, const Rect& into) noexcept {
    const float scale = std::min(into.width / view_box.width, into.height / view_box.height);
    const float left = into.x + (into.width - view_box.width * scale) / 2 - view_box.x * scale;
    const float top = into.y + (into.height - view_box.height * scale) / 2 - view_box.y * scale;
    return mlibs::geom::Affine::translate(left, top) * mlibs::geom::Affine::scale(scale, scale);
}

std::optional<xrender::Path> shape_of(std::string_view data) {
    const auto parsed = mlibs::geom::parse_path_data(data);
    if (!parsed || parsed->verbs.empty()) return std::nullopt;
    xrender::Path made;
    std::size_t at = 0;
    for (const mlibs::geom::PathVerb verb : parsed->verbs) {
        switch (verb) {
            case mlibs::geom::PathVerb::move: made.move_to(parsed->points[at++]); break;
            case mlibs::geom::PathVerb::line: made.line_to(parsed->points[at++]); break;
            case mlibs::geom::PathVerb::quad:
                made.quad_to(parsed->points[at], parsed->points[at + 1]);
                at += 2;
                break;
            case mlibs::geom::PathVerb::cubic:
                made.cubic_to(parsed->points[at], parsed->points[at + 1], parsed->points[at + 2]);
                at += 3;
                break;
            case mlibs::geom::PathVerb::close: made.close(); break;
        }
    }
    return made;
}

void agent_corners(xrender::DisplayList& list, const Rect& box, Color ink, float arm, float width) {
    xrender::Path corners;
    const auto corner = [&](Point p, float dx, float dy) { corners.move_to({p.x + dx * arm, p.y}).line_to(p).line_to({p.x, p.y + dy * arm}); };
    corner({box.x, box.y}, 1, 1);
    corner({box.right(), box.y}, -1, 1);
    corner({box.x, box.bottom()}, 1, -1);
    corner({box.right(), box.bottom()}, -1, -1);
    list.stroke_path(corners, {.width = width, .cap = xrender::LineCap::round, .join = xrender::LineJoin::round}, xrender::Paint::solid(ink));
}

void cross(PaintContext& ctx, Point c, float size, Color color) {
    xrender::Path p;
    p.move_to({c.x - size / 2, c.y - size / 2}).line_to({c.x + size / 2, c.y + size / 2});
    p.move_to({c.x + size / 2, c.y - size / 2}).line_to({c.x - size / 2, c.y + size / 2});
    xrender::StrokeStyle st;
    st.width = 1.5f;
    st.cap = xrender::LineCap::round;
    ctx.list.stroke_path(p, st, xrender::Paint::solid(color));
}

void check_mark(PaintContext& ctx, Point c, float size, Color color, float width) {
    xrender::Path p;
    p.move_to({c.x - size / 2, c.y}).line_to({c.x - size / 8, c.y + size * 0.35f}).line_to({c.x + size / 2, c.y - size * 0.35f});
    xrender::StrokeStyle st;
    st.width = width;
    st.cap = xrender::LineCap::round;
    st.join = xrender::LineJoin::round;
    ctx.list.stroke_path(p, st, xrender::Paint::solid(color));
}

void shadow(PaintContext& ctx, std::string_view token, const Rect& box, const CornerRadii& r) {
    if (auto it = ctx.theme.shadows.find(token); it != ctx.theme.shadows.end())
        paint_shadow(ctx.list, box, r, it->second);
}

void depth(PaintContext& ctx, std::string_view token, const Rect& box, const CornerRadii& radii, bool hovered, bool pressed,
           std::optional<ShadowToken> fallback, Color object) {
    const auto& shadows = ctx.theme.shadows;
    const auto named = [&](std::string_view state) -> const ShadowToken* {
        const auto it = shadows.find(state.empty() ? std::string(token) : std::format("{}:{}", token, state));
        return it == shadows.end() ? nullptr : &it->second;
    };
    const ShadowToken* shown = pressed ? named("pressed") : nullptr;
    if (!shown && (hovered || pressed)) shown = named("hovered");
    if (!shown) shown = named("");
    if (!shown && fallback) shown = &*fallback;
    const auto seen = [](const ShadowToken& s) { return s.color.a > 0 || (s.ambient && s.ambient->color.a > 0); };
    if (shown && seen(*shown)) paint_shadow(ctx.list, box, radii, *shown, 1, object);
}

void Label::set_text(std::string text) {
    if (text == text_) return;
    text_ = std::move(text);
    dirty_ = true;
}

void Label::set_style(std::string token) {
    if (token == style_) return;
    style_ = std::move(token);
    dirty_ = true;
}

void Label::set_max_lines(int lines) {
    if (lines == max_lines_) return;
    max_lines_ = lines;
    dirty_ = true;
}

mlibs::text::Paragraph& Label::shaped(Services& services, Color color, float width) {
    if (dirty_ || !paragraph_ || !(color == color_) || paragraph_->stale()) {
        const Theme& theme = services.theme();
        const TextToken& tok = theme.text_style(style_);
        mlibs::text::ParagraphStyle ps;
        ps.style.families = tok.families;
        ps.style.size = tok.size;
        ps.style.weight = tok.weight;
        ps.style.line_height = tok.line_height;
        ps.style.letter_spacing = tok.letter_spacing;
        ps.style.color = color;
        ps.max_lines = max_lines_;
        ps.wrap = true;  // whatever width it is first shaped at: an infinite one does not wrap, a later finite one does
        paragraph_.emplace(services.fonts(), text_, std::move(ps));
        color_ = color;
        width_ = -1;
        dirty_ = false;
    }
    if (width != width_) {
        paragraph_->layout(width);
        width_ = width;
    }
    return *paragraph_;
}

Size Label::measure(Services& services, Color color, float max_width) {
    if (text_.empty()) {
        const TextToken& tok = services.theme().text_style(style_);
        return {0, tok.size * tok.line_height};
    }
    return shaped(services, color, max_width).size();
}

float Label::min_content(Services& services, Color color) {
    if (text_.empty()) return 0;
    return shaped(services, color, width_ < 0 ? std::numeric_limits<float>::infinity() : width_).min_intrinsic_width();
}

float Label::baseline(Services& services, Color color) {
    if (text_.empty()) return 0;
    return shaped(services, color, width_ < 0 ? std::numeric_limits<float>::infinity() : width_).first_baseline();
}

void Label::paint(PaintContext& ctx, Point origin, Color color) {
    if (text_.empty()) return;
    xrender::text::paint(shaped(ctx.services, color, width_ < 0 ? std::numeric_limits<float>::infinity() : width_), ctx.list, origin);
}

Pressed::Pressed(PaintContext& ctx, const Rect& box, float pressed) : ctx_(ctx) {
    const float scale = 1 - press_scale * pressed;
    if (scale == 1) return;
    const Point center = box.center();
    ctx.list.push_transform(Affine::translate(center.x, center.y) * Affine::scale(scale, scale) * Affine::translate(-center.x, -center.y));
    pushed_ = true;
}

Pressed::~Pressed() {
    if (pushed_) ctx_.list.pop_transform();
}

float rubber_band(float past, float limit) noexcept {
    if (limit <= 0 || past == 0) return 0;
    const float shown = limit * (1 - 1 / (1 + std::abs(past) / limit));
    return past < 0 ? -shown : shown;
}

double magnetic(double raw, double snapped, double half) noexcept {
    if (half <= 0) return raw;
    const double away = std::clamp((raw - snapped) / half, -1.0, 1.0);
    // Cubed: flat at the tick (held), steep towards the middle (let go), reaching the pointer's half-way mark.
    return snapped + away * away * away * half;
}

}  // namespace omui::parts
