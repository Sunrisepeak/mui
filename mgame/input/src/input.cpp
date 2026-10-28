module mgame.input;

import std;

namespace mgame {

namespace {

std::int16_t steps(double v) noexcept { return static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0, 1.0) * 32767)); }

}  // namespace

void Sampler::button(std::int32_t index, bool down) {
    if (index < 0 || index >= 32) return;
    const std::uint32_t bit = 1u << index;
    if (down && !(held_ & bit)) held_ |= bit, pressed_ |= bit;
    if (!down && (held_ & bit)) held_ &= ~bit, released_ |= bit;
}

void Sampler::key(std::uint32_t code, bool down) {
    const bool was = std::ranges::find(keys_down_, code) != keys_down_.end();
    if (down == was) return;  // a key's repeat changes nothing
    if (down) keys_down_.push_back(code);
    else std::erase(keys_down_, code);
    for (const auto& k : b_.keys_)
        if (k.source == code) {
            if (k.button >= 0) button(k.button, down);
            if (k.axis >= 0 && k.axis < 4) key_axes_[static_cast<std::size_t>(k.axis)] += down ? k.sign : -k.sign;
        }
}

void Sampler::pad_button(std::int32_t index, bool down) {
    for (const auto& p : b_.pads_)
        if (p.source == static_cast<std::uint32_t>(index)) button(p.button, down);
}

void Sampler::mouse(std::int32_t button, bool down) {
    for (const auto& m : b_.mice_)
        if (m.source == static_cast<std::uint32_t>(button)) this->button(m.button, down);
}

void Sampler::pad_axis(std::int32_t index, double value) {
    if (index >= 0 && index < static_cast<std::int32_t>(pad_axes_.size())) pad_axes_[static_cast<std::size_t>(index)] = value;
}

void Sampler::pointer(double x, double y) {
    if (x < 0 || x > 1 || y < 0 || y > 1) {
        px_ = py_ = -1;
        return;
    }
    px_ = static_cast<std::int16_t>(std::lround(x * 32767));
    py_ = static_cast<std::int16_t>(std::lround(y * 32767));
}

void Sampler::wheel(std::int32_t notches) { wheel_ += notches; }

void Sampler::clear() {
    for (std::int32_t i = 0; i < 32; ++i) button(i, false);
    keys_down_.clear();
    key_axes_ = {};
    pad_axes_ = {};
}

Actions Sampler::sample() {
    Actions a{held_, pressed_, released_, {}, px_, py_, static_cast<std::int16_t>(std::clamp(wheel_, -32767, 32767)), 0};
    wheel_ = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        double v = std::clamp(key_axes_[i], -1, 1);
        for (const auto& p : b_.axes_)
            if (p.axis == static_cast<std::int32_t>(i) && p.source < pad_axes_.size() && std::abs(pad_axes_[p.source]) > 0.15)  // a stick's dead zone
                v = pad_axes_[p.source] * p.sign;
        if (b_.pointer_x_ && b_.pointer_x_->index == static_cast<std::int32_t>(i) && px_ >= 0) v = (px_ / 32767.0) * 2 - 1;
        a.axes[i] = steps(v);
    }
    pressed_ = released_ = 0;
    return a;
}

std::string to_text(std::span<const Actions> frames) {
    std::string s;
    for (const Actions& a : frames)
        s += std::format("{} {} {} {} {} {} {} {} {} {}\n", a.held, a.pressed, a.released, a.axes[0], a.axes[1], a.axes[2], a.axes[3], a.pointer_x, a.pointer_y,
                         a.wheel);
    return s;
}

std::expected<std::vector<Actions>, std::string> from_text(std::string_view text) {
    std::vector<Actions> out;
    std::size_t line = 0;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view l = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++line;
        if (l.empty()) continue;
        std::array<std::int64_t, 10> v{};
        std::size_t at = 0;
        for (std::size_t k = 0; k < v.size(); ++k) {
            while (at < l.size() && l[at] == ' ') ++at;
            const auto r = std::from_chars(l.data() + at, l.data() + l.size(), v[k]);
            if (r.ec != std::errc{}) return std::unexpected(std::format("line {}: ten numbers a frame", line));
            at = static_cast<std::size_t>(r.ptr - l.data());
        }
        Actions a;
        a.held = static_cast<std::uint32_t>(v[0]), a.pressed = static_cast<std::uint32_t>(v[1]), a.released = static_cast<std::uint32_t>(v[2]);
        for (std::size_t i = 0; i < 4; ++i) a.axes[i] = static_cast<std::int16_t>(v[3 + i]);
        a.pointer_x = static_cast<std::int16_t>(v[7]), a.pointer_y = static_cast<std::int16_t>(v[8]), a.wheel = static_cast<std::int16_t>(v[9]);
        out.push_back(a);
    }
    return out;
}

}  // namespace mgame
