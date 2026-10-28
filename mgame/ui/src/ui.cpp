module mgame.ui;

import std;
import openmui.core;
import openmui.view;
import mgame.input;
import mgame.sim;

namespace mgame::ui {

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
    clock_ = Clock(hz, catch_up);
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

}  // namespace mgame::ui
