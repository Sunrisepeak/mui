module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui::parts {

Motion::~Motion() {
    if (services_ && services_->animations().valid(channel_)) services_->animations().remove(channel_);
}

void Motion::attach(Services& services, double initial) {
    if (services_) return;
    services_ = &services;
    channel_ = services.animations().hold(initial);
    target_ = initial;
}

void Motion::to(double target, const Transition& t) {
    if (!services_) {
        target_ = target;
        return;
    }
    if (target == target_) return;
    target_ = target;
    hanim::Set& set = services_->animations();
    const seconds start = services_->now() + t.delay;
    if (t.kind == Transition::Kind::spring) set.spring_to(channel_, start, target, t.spring);
    else set.tween_to(channel_, start, target, t.duration, t.ease);
    services_->request_frame();
}

void Motion::jump(double v) {
    target_ = v;
    if (services_) services_->animations().jump(channel_, v);
}

double Motion::value() const {
    if (!services_) return target_;
    return services_->animations().value(channel_, services_->now());
}

bool Motion::moving() const { return services_ && !services_->animations().settled(channel_, services_->now()); }

void keep_frames(PaintContext& ctx, std::initializer_list<const Motion*> motions) {
    for (const Motion* m : motions)
        if (m && m->moving()) {
            ctx.services.invalidate_paint(ctx.node);
            ctx.services.request_frame();
            return;
        }
}

std::string segment(const Services& services, NodeId node) {
    const std::string p = services.path(node);
    const auto slash = p.rfind('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

void chevron(PaintContext& ctx, Point c, float size, Color color) {
    render::Path p;
    p.move_to({c.x - size / 2, c.y - size / 4}).line_to({c.x, c.y + size / 4}).line_to({c.x + size / 2, c.y - size / 4});
    render::StrokeStyle st;
    st.width = 1.5f;
    st.cap = render::LineCap::round;
    st.join = render::LineJoin::round;
    ctx.list.stroke_path(p, st, render::Paint::solid(color));
}

void focus_ring(PaintContext& ctx, const Rect& r, const CornerRadii& radii) {
    constexpr float gap = 2, width = 2;
    const float out = gap + width / 2;
    CornerRadii ring = radii;
    for (float* x : {&ring.top_left, &ring.top_right, &ring.bottom_right, &ring.bottom_left}) *x += out;
    ctx.list.stroke_round_rect({r.x - out, r.y - out, r.width + 2 * out, r.height + 2 * out}, ring, width,
                               render::Paint::solid(ctx.theme.color("focus", Color::hex(0x6366f1, 0.55f))));
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

typeset::Paragraph& Label::shaped(Services& services, Color color, float width) {
    if (dirty_ || !paragraph_ || !(color == color_)) {
        const Theme& theme = services.theme();
        const TextToken& tok = theme.text_style(style_);
        typeset::ParagraphStyle ps;
        ps.style.families = tok.families;
        ps.style.size = tok.size;
        ps.style.weight = tok.weight;
        ps.style.line_height = tok.line_height;
        ps.style.letter_spacing = tok.letter_spacing;
        ps.style.color = color;
        ps.wrap = std::isfinite(width);
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

float Label::baseline(Services& services, Color color) {
    if (text_.empty()) return 0;
    return shaped(services, color, width_ < 0 ? std::numeric_limits<float>::infinity() : width_).first_baseline();
}

void Label::paint(PaintContext& ctx, Point origin, Color color) {
    if (text_.empty()) return;
    shaped(ctx.services, color, width_ < 0 ? std::numeric_limits<float>::infinity() : width_).paint(ctx.list, origin);
}

}  // namespace omui::parts
