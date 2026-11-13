module xengine.ui;

import std;
import omui.core;
import omui.view;
import xengine.input;
import mlibs.simulation;
import mlibs.agent;
import mlibs.layout;
import xrender.raster;
import xrender.vector;

namespace xengine::ui {

bool feed(Sampler& in, const omui::InputEvent& e, omui::Rect area) {
    if (const auto* k = std::get_if<omui::KeyEvent>(&e)) {
        in.key(code(k->key), k->kind == omui::KeyKind::down);
        return true;
    }
    if (const auto* p = std::get_if<omui::PointerEvent>(&e)) {
        if (area.width <= 0 || area.height <= 0) return false;
        in.pointer((p->position.x - area.x) / area.width, (p->position.y - area.y) / area.height);
        if (p->kind == omui::PointerKind::down || p->kind == omui::PointerKind::up) in.mouse(static_cast<std::int32_t>(p->button), p->kind == omui::PointerKind::down);
        return true;
    }
    if (const auto* g = std::get_if<omui::GamepadEvent>(&e)) {
        // Any gamepad: its buttons and axes by their place in the standard set.
        if (g->kind == omui::GamepadKind::down || g->kind == omui::GamepadKind::up) in.pad_button(pad(g->button), g->kind == omui::GamepadKind::down);
        else if (g->kind == omui::GamepadKind::axis) in.pad_axis(pad(g->axis), g->value);
        return true;
    }
    if (const auto* f = std::get_if<omui::FocusEvent>(&e); f && !f->gained) {
        in.clear();
        return true;
    }
    return false;
}

void Pacer::start(omui::Services& services, double hz, std::function<void(std::int32_t)> run, std::int32_t catch_up) {
    stop();
    services_ = &services;
    run_ = std::move(run);
    clock_ = mlibs::simulation::Clock(hz, catch_up);
    clock_.reset(services.now().count());
    frames_ = services.demand_frames({.min_hz = 60, .max_hz = hz, .preferred_hz = hz, .reason = "game"});
    wake();
}

void Pacer::stop() {
    if (services_ && timer_) services_->cancel(timer_);
    timer_ = 0;
    services_ = nullptr;
    frames_ = {};
}

void Pacer::wake() {
    timer_ = 0;
    if (!services_) return;
    if (const std::int32_t n = clock_.advance(services_->now().count()); n > 0 && run_) run_(n);
    if (!services_) return;  // the run stopped it
    // The next step due: what is left of this one, on the clock's time scale.
    const double scale = clock_.scale > 0 ? clock_.scale : 1;
    const double wait = (1 - clock_.alpha()) * clock_.step() / scale;
    timer_ = services_->after(omui::seconds(std::max(0.0, wait)), [this] { wake(); });
}

void Viewport::show(Shown what) {
    clear();
    shown_ = std::move(what);
    if (!services_) return;
    if (shown_.subject) services_->add_subject(*shown_.subject);
    if (shown_.pixels) number_ = services_->add_picture(shown_.pixels);
    services_->invalidate_layout(node_);
}
void Viewport::clear() {
    if (services_) {
        if (shown_.subject) services_->remove_subject(*shown_.subject);
        if (number_ != 0) services_->remove_picture(number_);
        services_->invalidate_paint(node_);
    }
    shown_ = {}, number_ = 0;
}
void Viewport::changed() {
    number_ = omui::Services::picture_changed(number_);
    if (services_) services_->invalidate_paint(node_);
}
std::optional<omui::Point> Viewport::within(omui::Point local) const {
    if (!(size_.width > 0) || !(size_.height > 0) || local.x < 0 || local.y < 0 || local.x > size_.width || local.y > size_.height) return std::nullopt;
    return omui::Point{local.x / size_.width, local.y / size_.height};
}
omui::Size Viewport::measure(omui::MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available across, mlibs::layout::Available down) {
    // What the layout says; else as wide as it is let be, as high as its picture's shape makes that.
    const float shape = shown_.width > 0 ? shown_.height / shown_.width : 0.5625f;
    const float wide = width.value_or(across.is_definite() ? across.value : shown_.width);
    return {wide, height.value_or(down.is_definite() ? std::min(down.value, wide * shape) : wide * shape)};
}
void Viewport::paint(omui::PaintContext& ctx) const {
    size_ = ctx.size;
    const omui::Rect whole{0, 0, ctx.size.width, ctx.size.height};
    // Under the picture: what shows until there is one (and on a host that shows none).
    ctx.list.fill_rect(whole, xrender::Paint::solid(ctx.theme.color("surface")));
    if (number_ != 0) ctx.list.external_texture(number_, whole, 1, shown_.pixels_stay ? xrender::Sampling::nearest : xrender::Sampling::linear);
}
void Viewport::semantics(omui::SemanticsContext&, omui::spec::SemanticNode& node) const {
    node.name = shown_.label;
    // Which subject it shows: an agent goes from this node to the world's own questions (`subjects`), and back.
    node.value = omui::Value(shown_.subject ? std::string(shown_.subject->name()) : std::string());
    node.description = shown_.subject ? "a world shown here; ask its own subject" : "nothing shown";
}

}  // namespace xengine::ui
