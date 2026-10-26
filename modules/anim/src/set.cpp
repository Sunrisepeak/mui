module openmui.anim;

import std;

namespace omui::anim {

std::uint32_t Set::allocate() {
    std::uint32_t i;
    if (!free_.empty()) {
        i = free_.back();
        free_.pop_back();
    } else {
        i = static_cast<std::uint32_t>(kind_.size());
        kind_.push_back(Kind::free);
        generation_.push_back(0);
        start_.push_back(0);
        end_.push_back(0);
        from_.push_back(0);
        target_.push_back(0);
        duration_.push_back(0);
        ease_.push_back({});
        osc_.push_back({});
        glide_.push_back({});
        params_.push_back({});
        rest_.push_back({});
    }
    ++live_;
    return i;
}

Set::Channel Set::hold(double value) {
    std::uint32_t i = allocate();
    kind_[i] = Kind::hold;
    from_[i] = target_[i] = value;
    start_[i] = end_[i] = 0;
    return {i, generation_[i]};
}

Set::Channel Set::tween(double from, double to, seconds start, seconds duration, Ease e) {
    std::uint32_t i = allocate();
    kind_[i] = Kind::tween;
    from_[i] = from;
    target_[i] = to;
    start_[i] = start.count();
    duration_[i] = std::max(0.0, duration.count());
    end_[i] = start_[i] + duration_[i];
    ease_[i] = e;
    return {i, generation_[i]};
}

Set::Channel Set::spring(double from, double to, seconds start, SpringParams p, double velocity, Rest rest) {
    std::uint32_t i = allocate();
    kind_[i] = Kind::spring;
    start_[i] = start.count();
    from_[i] = from;
    target_[i] = to;
    params_[i] = p;
    rest_[i] = rest;
    osc_[i] = Oscillator(from, velocity, to, p, rest);
    end_[i] = start_[i] + osc_[i].settle_time();
    return {i, generation_[i]};
}

Set::Channel Set::decay(double from, double velocity, seconds start, DecayParams p) {
    std::uint32_t i = allocate();
    kind_[i] = Kind::decay;
    start_[i] = start.count();
    from_[i] = from;
    glide_[i] = Glide(from, velocity, p);
    target_[i] = glide_[i].destination();
    end_[i] = start_[i] + glide_[i].settle_time();
    return {i, generation_[i]};
}

bool Set::valid(Channel c) const noexcept {
    return c.index < kind_.size() && kind_[c.index] != Kind::free && generation_[c.index] == c.generation;
}

void Set::spring_to(Channel c, seconds now, double target, std::optional<SpringParams> p) {
    if (!valid(c)) return;
    std::uint32_t i = c.index;
    double t = now.count();
    double x = value_at(i, t, {});
    double v = velocity_at(i, t);
    SpringParams params = p.value_or(kind_[i] == Kind::spring ? params_[i] : springs::standard);
    Rest rest = kind_[i] == Kind::spring ? rest_[i] : Rest{};
    kind_[i] = Kind::spring;
    start_[i] = t;
    from_[i] = x;
    target_[i] = target;
    params_[i] = params;
    rest_[i] = rest;
    osc_[i] = Oscillator(x, v, target, params, rest);
    end_[i] = t + osc_[i].settle_time();
}

void Set::tween_to(Channel c, seconds now, double target, seconds duration, Ease e) {
    if (!valid(c)) return;
    std::uint32_t i = c.index;
    double t = now.count();
    double x = value_at(i, t, {});
    kind_[i] = Kind::tween;
    start_[i] = t;
    from_[i] = x;
    target_[i] = target;
    duration_[i] = std::max(0.0, duration.count());
    end_[i] = t + duration_[i];
    ease_[i] = e;
}

void Set::jump(Channel c, double value) {
    if (!valid(c)) return;
    std::uint32_t i = c.index;
    kind_[i] = Kind::hold;
    from_[i] = target_[i] = value;
    end_[i] = start_[i] = 0;
}

void Set::remove(Channel c) {
    if (!valid(c)) return;
    kind_[c.index] = Kind::free;
    ++generation_[c.index];
    free_.push_back(c.index);
    --live_;
}

void Set::clear() {
    for (std::uint32_t i = 0; i < kind_.size(); ++i) {
        if (kind_[i] == Kind::free) continue;
        kind_[i] = Kind::free;
        ++generation_[i];
        free_.push_back(i);
    }
    live_ = 0;
}

double Set::value_at(std::uint32_t i, double t, Policy policy) const noexcept {
    if (policy.reduce_motion) return target_[i];
    double local = t - start_[i];
    switch (kind_[i]) {
        case Kind::free:
        case Kind::hold: return from_[i];
        case Kind::tween: {
            if (local <= 0.0) return from_[i];
            if (local >= duration_[i]) return target_[i];
            return dm::mix(from_[i], target_[i], ease_[i](local / duration_[i]));
        }
        case Kind::spring: return osc_[i].position(local);
        case Kind::decay:  return glide_[i].position(local);
    }
    return from_[i];
}

double Set::velocity_at(std::uint32_t i, double t) const noexcept {
    double local = t - start_[i];
    switch (kind_[i]) {
        case Kind::free:
        case Kind::hold: return 0.0;
        case Kind::tween:
            if (local < 0.0 || local >= duration_[i] || duration_[i] <= 0.0) return 0.0;
            return (target_[i] - from_[i]) * ease_[i].slope(local / duration_[i]) / duration_[i];
        case Kind::spring: return osc_[i].velocity(local);
        case Kind::decay:  return glide_[i].velocity(local);
    }
    return 0.0;
}

double Set::value(Channel c, seconds t, Policy policy) const noexcept {
    return valid(c) ? value_at(c.index, t.count(), policy) : 0.0;
}

double Set::velocity(Channel c, seconds t) const noexcept {
    return valid(c) ? velocity_at(c.index, t.count()) : 0.0;
}

double Set::target(Channel c) const noexcept { return valid(c) ? target_[c.index] : 0.0; }

bool Set::settled(Channel c, seconds t) const noexcept {
    return !valid(c) || kind_[c.index] == Kind::hold || t.count() >= end_[c.index];
}

seconds Set::settle_time(Channel c) const noexcept { return seconds(valid(c) ? end_[c.index] : 0.0); }

std::size_t evaluate(const Set& set, seconds t, std::span<double> out, Policy policy) {
    double now = t.count();
    std::size_t moving = 0;
    std::size_t n = std::min(out.size(), set.kind_.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (set.kind_[i] == Set::Kind::free) continue;
        auto idx = static_cast<std::uint32_t>(i);
        out[i] = set.value_at(idx, now, policy);
        if (!policy.reduce_motion && set.kind_[i] != Set::Kind::hold && now < set.end_[i]) ++moving;
    }
    return moving;
}

}  // namespace omui::anim
