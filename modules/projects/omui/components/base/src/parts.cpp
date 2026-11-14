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

}  // namespace omui::parts
