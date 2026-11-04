module openmui.render.vector;
import std;

namespace omui::render {
namespace {
bool same_leaf(const Paint& a, const Paint& b) {
    if (a.kind != b.kind || a.color != b.color || (a.material == nullptr) != (b.material == nullptr)) return false;
    if (a.material && !(*a.material == *b.material)) return false;
    if (a.kind == Paint::Kind::gradient) {
        const auto& x = a.gradient; const auto& y = b.gradient;
        return x.kind == y.kind && x.start == y.start && x.end == y.end && x.radius == y.radius && x.spread == y.spread &&
            std::ranges::equal(x.stops, y.stops, [](const auto& p, const auto& q) { return p.offset == q.offset && p.color == q.color; });
    }
    if (a.kind == Paint::Kind::image || a.kind == Paint::Kind::image_mask)
        return a.image == b.image && a.image_transform == b.image_transform && a.sampling == b.sampling;
    return a.kind == Paint::Kind::solid;
}
}
std::expected<void, PaintMixError> validate_paint_mix(const Paint& paint) noexcept {
    if (paint.kind != Paint::Kind::mix) return {};
    if (paint.mix.empty() || paint.mix.size() > max_paint_mix_terms) return std::unexpected(PaintMixError::term_budget);
    if (!std::isfinite(paint.mix_opacity) || paint.mix_opacity < 0 || paint.mix_opacity > 1)
        return std::unexpected(PaintMixError::invalid_weight);
    double total = 0;
    for (const auto& term : paint.mix) {
        if (term.paint.kind == Paint::Kind::mix) return std::unexpected(PaintMixError::nested_mix);
        if (term.paint.kind != Paint::Kind::solid && term.paint.kind != Paint::Kind::gradient &&
            term.paint.kind != Paint::Kind::image && term.paint.kind != Paint::Kind::image_mask)
            return std::unexpected(PaintMixError::invalid_leaf);
        if (!std::isfinite(term.weight) || term.weight < 0 || term.weight > 1)
            return std::unexpected(PaintMixError::invalid_weight);
        total += term.weight;
    }
    if (std::abs(total - 1) > 1e-5) return std::unexpected(PaintMixError::not_normalized);
    return {};
}
std::expected<PreparedPaintMix, PaintMixError>
PreparedPaintMix::prepare(const Paint& from, const Paint& to, PaintMixOptions options) {
    if (options.max_terms == 0 || options.max_terms > max_paint_mix_terms) return std::unexpected(PaintMixError::invalid_options);
    if (auto ok = validate_paint_mix(from); !ok) return std::unexpected(ok.error());
    if (auto ok = validate_paint_mix(to); !ok) return std::unexpected(ok.error());
    PreparedPaintMix plan;
    plan.leaves_.reserve(options.max_terms); plan.from_.reserve(options.max_terms); plan.to_.reserve(options.max_terms);
    auto add = [&](const Paint& paint, double weight, bool destination) -> std::expected<void, PaintMixError> {
        if (weight == 0) return {};
        if (paint.kind == Paint::Kind::mix || (paint.kind != Paint::Kind::solid && paint.kind != Paint::Kind::gradient &&
            paint.kind != Paint::Kind::image && paint.kind != Paint::Kind::image_mask)) return std::unexpected(PaintMixError::invalid_leaf);
        std::size_t index = 0;
        while (index < plan.leaves_.size() && !same_leaf(plan.leaves_[index].paint, paint)) ++index;
        if (index == plan.leaves_.size()) {
            if (index == options.max_terms) return std::unexpected(PaintMixError::term_budget);
            plan.leaves_.emplace_back();
            auto& owned = plan.leaves_.back(); owned.paint = paint;
            if (paint.kind == Paint::Kind::gradient) {
                owned.stops.assign(paint.gradient.stops.begin(), paint.gradient.stops.end()); owned.paint.gradient.stops = owned.stops;
            }
            if (paint.material) { owned.material = *paint.material; owned.paint.material = &*owned.material; }
            plan.from_.push_back(0); plan.to_.push_back(0);
        }
        (destination ? plan.to_[index] : plan.from_[index]) += weight;
        return {};
    };
    auto ingest = [&](const Paint& paint, bool destination) -> std::expected<void, PaintMixError> {
        if (paint.kind != Paint::Kind::mix) return add(paint, 1, destination);
        double total = 0; for (const auto& term : paint.mix) total += term.weight;
        for (const auto& term : paint.mix) {
            if (auto ok = add(term.paint, term.weight / total * paint.mix_opacity, destination); !ok) return ok;
        }
        if (paint.mix_opacity < 1) return add(Paint::solid(Color::transparent()), 1 - paint.mix_opacity, destination);
        return {};
    };
    if (auto ok = ingest(from, false); !ok) return std::unexpected(ok.error());
    if (auto ok = ingest(to, true); !ok) return std::unexpected(ok.error());
    return plan;
}
std::expected<std::size_t, PaintMixError>
PreparedPaintMix::sample(double progress, std::span<WeightedPaint> output) const noexcept {
    if (leaves_.empty()) return std::unexpected(PaintMixError::unprepared);
    if (!std::isfinite(progress)) return std::unexpected(PaintMixError::invalid_progress);
    if (output.size() < leaves_.size()) return std::unexpected(PaintMixError::output_too_small);
    const double t = std::clamp(progress, 0.0, 1.0);
    for (std::size_t i = 0; i < leaves_.size(); ++i) {
        const double weight = t == 0 ? from_[i] : t == 1 ? to_[i] : from_[i] + (to_[i] - from_[i]) * t;
        output[i] = {leaves_[i].paint, static_cast<float>(std::clamp(weight, 0.0, 1.0))};
    }
    return leaves_.size();
}
std::expected<Paint, PaintMixError> PreparedPaintMix::record(double progress, DisplayList& output, float opacity) const {
    if (leaves_.empty()) return std::unexpected(PaintMixError::unprepared);
    if (!std::isfinite(progress)) return std::unexpected(PaintMixError::invalid_progress);
    return output.record_paint_mix(leaves_.size(), [&](std::span<WeightedPaint> buffer) { (void)sample(progress, buffer); }, opacity);
}
} // namespace omui::render
