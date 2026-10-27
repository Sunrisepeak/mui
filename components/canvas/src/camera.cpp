module openmui.ui.canvas;

import std;
import openmui.core;
import openmui.anim;

namespace omui::world {

Point Camera::to_screen(double wx, double wy, Size viewport) const noexcept {
    return {static_cast<float>((wx - x) * zoom + viewport.width * 0.5), static_cast<float>((wy - y) * zoom + viewport.height * 0.5)};
}

Rect Camera::to_screen(const WorldRect& r, Size viewport) const noexcept {
    // Camera-relative in double, then rounded to float once.
    const double l = (r.x - x) * zoom + viewport.width * 0.5, t = (r.y - y) * zoom + viewport.height * 0.5;
    return {static_cast<float>(l), static_cast<float>(t), static_cast<float>(r.width * zoom), static_cast<float>(r.height * zoom)};
}

std::array<double, 2> Camera::to_world(Point s, Size viewport) const noexcept {
    return {x + (s.x - viewport.width * 0.5) / zoom, y + (s.y - viewport.height * 0.5) / zoom};
}

WorldRect Camera::visible(Size viewport) const noexcept {
    const double w = viewport.width / zoom, h = viewport.height / zoom;
    return {x - w / 2, y - h / 2, w, h};
}

void Camera::zoom_at(Point screen, double factor, Size viewport, double min, double max) noexcept {
    const auto before = to_world(screen, viewport);
    zoom = std::clamp(zoom * factor, min, max);
    const auto after = to_world(screen, viewport);
    x += before[0] - after[0];
    y += before[1] - after[1];
}

void Camera::pan(Point delta) noexcept {
    x -= delta.x / zoom;
    y -= delta.y / zoom;
}

Camera Camera::fit(const WorldRect& r, Size viewport, float margin, double min, double max) noexcept {
    Camera c;
    if (r.empty()) return c;
    const double w = std::max(1.0, viewport.width - 2.0 * margin), h = std::max(1.0, viewport.height - 2.0 * margin);
    c.zoom = std::clamp(std::min(w / r.width, h / r.height), min, max);
    c.x = r.x + r.width / 2;
    c.y = r.y + r.height / 2;
    return c;
}

// ── a spring between views ───────────────────────────────────────────────

CameraMotion::CameraMotion(Camera from, Camera to, anim::seconds start, anim::SpringParams params, std::array<double, 3> velocity)
    : to_(to), start_(start) {
    const std::array<double, 3> a{from.x, from.y, std::log(std::max(from.zoom, 1e-12))};
    const std::array<double, 3> b{to.x, to.y, std::log(std::max(to.zoom, 1e-12))};
    // At rest when within a hundredth of a screen pixel: world units scale with the zoom.
    const double world = 0.01 / std::max(std::min(from.zoom, to.zoom), 1e-12);
    double settle = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const anim::Rest rest = i < 2 ? anim::Rest{world, world} : anim::Rest{1e-4, 1e-4};
        lanes_[i] = anim::Oscillator(a[i], velocity[i], b[i], params, rest);
        settle = std::max(settle, lanes_[i].settle_time());
    }
    end_ = start + anim::seconds(settle);
}

Camera CameraMotion::at(anim::seconds t) const {
    if (t >= end_) return to_;
    const double local = std::max(0.0, (t - start_).count());
    return {lanes_[0].position(local), lanes_[1].position(local), std::exp(lanes_[2].position(local))};
}

std::array<double, 3> CameraMotion::velocity(anim::seconds t) const {
    if (t >= end_) return {};
    const double local = std::max(0.0, (t - start_).count());
    return {lanes_[0].velocity(local), lanes_[1].velocity(local), lanes_[2].velocity(local)};
}

// ── inertia ──────────────────────────────────────────────────────────────

Inertia::Inertia(Camera from, Point velocity, anim::seconds start, double decay, double rest)
    : from_(from), velocity_(velocity), start_(start), decay_(decay) {
    const double speed = std::sqrt(static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y);
    // speed · e^(−decay · t) = rest
    end_ = start + anim::seconds(speed > rest ? std::log(speed / rest) / decay : 0.0);
}

Camera Inertia::at(anim::seconds t) const {
    const double local = std::clamp((t - start_).count(), 0.0, (end_ - start_).count());
    // ∫ v·e^(−kt) dt = v/k · (1 − e^(−kt)): screen pixels travelled.
    const double travelled = (1 - std::exp(-decay_ * local)) / decay_;
    Camera c = from_;
    c.pan({static_cast<float>(velocity_.x * travelled), static_cast<float>(velocity_.y * travelled)});
    return c;
}

}  // namespace omui::world
