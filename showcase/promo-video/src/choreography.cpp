module omui.showcase.promo_video;

import std;
import hanim.core;

namespace omui::promo {

Choreography& Choreography::span(std::string target, double from, double to, const hanim::Ease& ease) {
    // Kept the whole time: 0 (its tween's start) before the span, 1 after.
    timeline_.at(hanim::seconds(from), target, hanim::Tween<double>{0.0, 1.0, hanim::seconds(to - from), ease},
                 {.fill = hanim::Fill::both});
    TimedSpan span;
    span.target = std::move(target);
    span.from = from;
    span.to = to;
    span.ease = ease;
    spans_.push_back(std::move(span));
    return *this;
}

void Choreography::sample(double t) {
    values_.clear();
    timeline_.sample(hanim::seconds(t), [&](std::string_view target, const hanim::Value& value) {
        const double v = value.as<double>().value_or(0.0);
        if (auto found = std::ranges::find(values_, target, &std::pair<std::string, double>::first); found != values_.end()) found->second = v;
        else values_.emplace_back(std::string(target), v);
    });
}

double Choreography::operator[](std::string_view target) const {
    const auto found = std::ranges::find(values_, target, &std::pair<std::string, double>::first);
    return found == values_.end() ? 0.0 : found->second;
}

}  // namespace omui::promo
