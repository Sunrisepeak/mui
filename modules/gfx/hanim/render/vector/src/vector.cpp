module hanim.render.vector;

import std;
import openmui.log;
import openmui.trace;

namespace hanim::detail {
struct VectorParameter { std::size_t source; float value, min, max; };
struct VectorEffect { omui::render::EffectRef ref; std::uint64_t generation; std::size_t offset, count; };
struct VectorData {
    PreparedObjectMorph geometry;
    PreparedEffects effects;
    std::vector<std::optional<omui::render::PreparedPaintMix>> materials;
    std::vector<VectorEffect> bindings;
    std::vector<VectorParameter> parameters;
};
}

namespace hanim::vector_render {
namespace {
namespace render = omui::render;
namespace geom = omui::geom;
constexpr auto no_source = std::numeric_limits<std::size_t>::max();
constexpr omui::Log log{"hanim.render.vector"};
bool scalar(double value) noexcept {
    return std::isfinite(value) && value >= -std::numeric_limits<float>::max() && value <= std::numeric_limits<float>::max();
}
bool finite(geom::Affine m) noexcept { return scalar(m.a) && scalar(m.b) && scalar(m.c) && scalar(m.d) && scalar(m.e) && scalar(m.f); }
bool finite(geom::Rect r) noexcept { return scalar(r.x) && scalar(r.y) && scalar(r.width) && scalar(r.height) && scalar(r.right()) && scalar(r.bottom()); }
bool color(geom::Color c) noexcept {
    for (float value : {c.r, c.g, c.b, c.a}) if (!std::isfinite(value) || value < 0 || value > 1) return false;
    return true;
}
bool paint(const render::Paint& p) noexcept {
    if (p.material) return false;
    if (p.kind == render::Paint::Kind::solid) return color(p.color);
    if (p.kind != render::Paint::Kind::gradient) return false;
    const auto& g = p.gradient;
    if (g.kind != render::GradientKind::linear && g.kind != render::GradientKind::radial && g.kind != render::GradientKind::conic) return false;
    if (g.spread != render::Spread::pad && g.spread != render::Spread::repeat && g.spread != render::Spread::reflect) return false;
    if (!scalar(g.start.x) || !scalar(g.start.y) || !scalar(g.end.x) || !scalar(g.end.y) || !scalar(g.radius) ||
        (g.kind == render::GradientKind::radial && g.radius < 0) || g.stops.empty() || g.stops.size() > 4096) return false;
    float previous = 0;
    for (const auto& stop : g.stops) {
        if (!std::isfinite(stop.offset) || stop.offset < previous || stop.offset > 1 || !color(stop.color)) return false;
        previous = stop.offset;
    }
    return true;
}
geom::Point point(Vec2 p) noexcept { return {static_cast<float>(p.x), static_cast<float>(p.y)}; }
bool same(const MorphPartInfo& a, const MorphPartInfo& b) noexcept {
    return a.id == b.id && a.from_ref == b.from_ref && a.to_ref == b.to_ref && a.parent == b.parent &&
        a.first_contour == b.first_contour && a.contour_count == b.contour_count;
}
bool same(const MorphContourInfo& a, const MorphContourInfo& b) noexcept {
    return a.part == b.part && a.ordinal == b.ordinal && a.first_cubic == b.first_cubic && a.cubic_count == b.cubic_count;
}
}
std::string_view vector_error_name(VectorError error) noexcept {
    switch (error) {
        case VectorError::unprepared: return "unprepared";
        case VectorError::foreign_frame: return "foreign-frame";
        case VectorError::invalid_geometry: return "invalid-geometry";
        case VectorError::invalid_progress: return "invalid-progress";
        case VectorError::buffer_too_small: return "buffer-too-small";
        case VectorError::recording_alias: return "recording-alias";
        case VectorError::capacity: return "capacity";
        case VectorError::unknown_effect: return "unknown-effect";
        case VectorError::unsupported_effect: return "unsupported-effect";
        case VectorError::unknown_parameter: return "unknown-parameter";
        case VectorError::invalid_parameter: return "invalid-parameter";
        case VectorError::stale_effect: return "stale-effect";
        case VectorError::unknown_part: return "unknown-part";
        case VectorError::duplicate_material: return "duplicate-material";
        case VectorError::invalid_material: return "invalid-material";
        case VectorError::recording_failed: return "recording-failed";
    }
    return "unknown";
}
std::expected<PreparedVector, VectorFailure> PreparedVector::bind(
    const PreparedObjectMorph& geometry, const PreparedEffects& effects, std::span<const MaterialBinding> materials) {
    omui::trace::Span span("bind", "hanim.render.vector");
    auto refuse = [](VectorError code, std::size_t entry = 0, std::size_t detail = 0) -> std::expected<PreparedVector, VectorFailure> {
        log.warn("bind refused: {} (entry={}, detail={})", vector_error_name(code), entry, detail);
        return std::unexpected(VectorFailure{code, entry, detail});
    };
    const auto parts = geometry.parts();
    if (parts.empty()) return refuse(VectorError::unprepared);
    auto data = std::make_shared<detail::VectorData>();
    data->geometry = geometry; data->effects = effects; data->materials.resize(parts.size());
    for (std::size_t i = 0; i < materials.size(); ++i) {
        const auto& material = materials[i];
        const auto it = std::ranges::find(parts, std::string_view(material.part), &MorphPartInfo::id);
        if (it == parts.end()) return refuse(VectorError::unknown_part, i);
        const auto index = static_cast<std::size_t>(it - parts.begin());
        if (data->materials[index]) return refuse(VectorError::duplicate_material, i);
        if (!it->contour_count || !paint(material.from) || !paint(material.to)) return refuse(VectorError::invalid_material, i);
        auto plan = render::PreparedPaintMix::prepare(material.from, material.to);
        if (!plan) return refuse(VectorError::invalid_material, i);
        data->materials[index] = std::move(*plan);
    }
    auto& registry = render::effects();
    const auto descriptions = effects.effects(); const auto endpoints = effects.parameters();
    for (std::size_t i = 0; i < descriptions.size(); ++i) {
        const auto& effect = descriptions[i];
        const auto ref = registry.find(effect.id);
        if (!ref) return refuse(VectorError::unknown_effect, i);
        const auto* info = registry.info(*ref);
        if (!info || !info->cpu || info->backdrop) return refuse(VectorError::unsupported_effect, i);
        if (info->params.size() > 65536 - data->parameters.size()) return refuse(VectorError::capacity, i);
        const auto offset = data->parameters.size();
        for (std::size_t p = 0; p < info->params.size(); ++p) {
            const auto& target = info->params[p];
            if (!std::isfinite(target.value) || !std::isfinite(target.min) || !std::isfinite(target.max) ||
                target.min > target.max || target.value < target.min || target.value > target.max)
                return refuse(VectorError::unsupported_effect, i, p);
            for (std::size_t prior = 0; prior < p; ++prior)
                if (info->params[prior].name == target.name) return refuse(VectorError::unsupported_effect, i, p);
            std::size_t source = no_source;
            for (std::size_t q = 0; q < effect.count; ++q) {
                const auto index = effect.offset + q;
                if (endpoints[index].name != target.name) continue;
                source = index;
                if (endpoints[index].from < target.min || endpoints[index].from > target.max ||
                    endpoints[index].to < target.min || endpoints[index].to > target.max)
                    return refuse(VectorError::invalid_parameter, i, q);
            }
            data->parameters.push_back({source, target.value, target.min, target.max});
        }
        for (std::size_t q = 0; q < effect.count; ++q) {
            if (std::ranges::none_of(info->params, [&](const auto& param) { return param.name == endpoints[effect.offset + q].name; }))
                return refuse(VectorError::unknown_parameter, i, q);
        }
        data->bindings.push_back({*ref, registry.generation(*ref), offset, info->params.size()});
    }
    PreparedVector result; result.data_ = std::move(data);
    log.info("bound {} parts and {} CPU effects", parts.size(), descriptions.size());
    return result;
}
VectorRequirements PreparedVector::requirements() const noexcept {
    return data_ ? VectorRequirements{data_->geometry.parts().size(), data_->parameters.size(), data_->bindings.size()} : VectorRequirements{};
}
std::expected<void, VectorFailure> PreparedVector::record(
    FrameView frame, std::span<const float> effect_values, render::DisplayList& destination, VectorScratch scratch) const {
    omui::trace::Span span("record", "hanim.render.vector");
    auto refuse = [](VectorError code, std::size_t entry = 0, std::size_t detail = 0) -> std::expected<void, VectorFailure> {
        omui::trace::mark("refused", "hanim.render.vector");
        return std::unexpected(VectorFailure{code, entry, detail});
    };
    if (!data_) return refuse(VectorError::unprepared);
    if (scratch.recording == &destination) return refuse(VectorError::recording_alias);
    const auto required = requirements();
    if (!scratch.recording || scratch.world.size() < required.world_transforms ||
        scratch.backend_parameters.size() < required.backend_parameters || scratch.effect_layers.size() < required.effect_layers ||
        effect_values.size() < data_->effects.parameter_count()) return refuse(VectorError::buffer_too_small);
    const auto parts = data_->geometry.parts(); const auto contours = data_->geometry.contours();
    if (frame.preparation != data_->geometry.identity() || frame.parts.size() != parts.size() || frame.contours.size() != contours.size() ||
        frame.cubics.size() != data_->geometry.requirements().cubics ||
        frame.source_object != data_->geometry.source_object() || frame.source_revision != data_->geometry.source_revision())
        return refuse(VectorError::foreign_frame);
    if (!std::isfinite(frame.progress) || frame.progress < 0 || frame.progress > 1) return refuse(VectorError::invalid_progress);
    for (std::size_t i = 0; i < contours.size(); ++i)
        if (!same(frame.contours[i], contours[i])) return refuse(VectorError::foreign_frame, i);
    bool has_bounds = false;
    float left = 0, top = 0, right = 0, bottom = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (!same(frame.parts[i].info, parts[i])) return refuse(VectorError::foreign_frame, i);
        const auto matrix = frame.parts[i].local.matrix();
        if (std::ranges::any_of(matrix, [](double value) { return !scalar(value); })) return refuse(VectorError::invalid_geometry, i);
        const geom::Affine local{static_cast<float>(matrix[0]), static_cast<float>(matrix[1]), static_cast<float>(matrix[2]),
            static_cast<float>(matrix[3]), static_cast<float>(matrix[4]), static_cast<float>(matrix[5])};
        const auto parent = parts[i].parent;
        if (parent != invalid_object_slot && parent >= i) return refuse(VectorError::invalid_geometry, i);
        auto& world = scratch.world[i]; world = parent == invalid_object_slot ? local : scratch.world[parent] * local;
        if (!finite(world)) return refuse(VectorError::invalid_geometry, i);
        const auto fill = frame.parts[i].fill;
        if ((parts[i].contour_count && !fill) || (fill && !color({fill->r, fill->g, fill->b, fill->a})))
            return refuse(VectorError::invalid_geometry, i);
    }
    for (std::size_t i = 0; i < contours.size(); ++i) {
        const auto& contour = contours[i];
        if (contour.part >= parts.size() || !contour.cubic_count || contour.first_cubic > frame.cubics.size() ||
            contour.cubic_count > frame.cubics.size() - contour.first_cubic) return refuse(VectorError::invalid_geometry, i);
        const auto cubics = frame.cubics.subspan(contour.first_cubic, contour.cubic_count);
        for (std::size_t j = 0; j < cubics.size(); ++j) {
            const auto& cubic = cubics[j];
            if (cubic.end != cubics[(j + 1) % cubics.size()].start) return refuse(VectorError::invalid_geometry, i, j);
            for (Vec2 p : {cubic.start, cubic.control1, cubic.control2, cubic.end}) {
                if (!scalar(p.x) || !scalar(p.y)) return refuse(VectorError::invalid_geometry, i, j);
                const auto at = scratch.world[contour.part].apply(point(p));
                if (!scalar(at.x) || !scalar(at.y)) return refuse(VectorError::invalid_geometry, i, j);
                if (!has_bounds) { left = right = at.x; top = bottom = at.y; has_bounds = true; }
                else { left = std::min(left, at.x); right = std::max(right, at.x); top = std::min(top, at.y); bottom = std::max(bottom, at.y); }
            }
        }
    }
    auto bounds = geom::Rect::ltrb(left, top, right, bottom);
    if (!finite(bounds)) return refuse(VectorError::invalid_geometry);
    for (std::size_t i = 0; i < data_->parameters.size(); ++i) {
        const auto& binding = data_->parameters[i];
        const auto value = binding.source == no_source ? binding.value : effect_values[binding.source];
        if (!std::isfinite(value) || value < binding.min || value > binding.max) return refuse(VectorError::invalid_parameter, i);
        scratch.backend_parameters[i] = value;
    }
    auto& registry = render::effects();
    for (std::size_t i = 0; i < data_->bindings.size(); ++i) {
        const auto& binding = data_->bindings[i];
        if (registry.generation(binding.ref) != binding.generation) return refuse(VectorError::stale_effect, i);
        scratch.effect_layers[i] = bounds;
        const auto reach = registry.output_reach(binding.ref, scratch.backend_parameters.subspan(binding.offset, binding.count));
        if (!reach || !std::isfinite(*reach) || *reach < 0) return refuse(VectorError::unsupported_effect, i);
        bounds = bounds.outset({*reach, *reach, *reach, *reach});
        if (!finite(bounds)) return refuse(VectorError::invalid_geometry, i);
    }
    auto& output = *scratch.recording; output.clear();
    for (std::size_t i = data_->bindings.size(); i > 0; --i) {
        const auto& binding = data_->bindings[i - 1];
        output.push_layer(1, render::BlendMode::normal, binding.ref,
            scratch.backend_parameters.subspan(binding.offset, binding.count), scratch.effect_layers[i - 1]);
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i]; if (!part.contour_count) continue;
        output.push_transform(scratch.world[i]);
        render::Paint material;
        if (data_->materials[i]) {
            const auto current = data_->materials[i]->record(frame.progress, output);
            if (!current) return refuse(VectorError::recording_failed, i);
            material = *current;
        } else {
            const auto fill = *frame.parts[i].fill;
            material = render::Paint::solid({fill.r, fill.g, fill.b, fill.a});
        }
        std::size_t verbs = 0, points = 0;
        for (std::size_t j = 0; j < part.contour_count; ++j) {
            const auto& contour = contours[part.first_contour + j];
            verbs += contour.cubic_count + 2; points += contour.cubic_count * 3 + 1;
        }
        const auto path = output.record_path(verbs, points, [&](render::MutablePathView target) {
            std::size_t v = 0, p = 0;
            for (std::size_t j = 0; j < part.contour_count; ++j) {
                const auto& contour = contours[part.first_contour + j];
                const auto cubics = frame.cubics.subspan(contour.first_cubic, contour.cubic_count);
                target.verbs[v++] = render::PathVerb::move; target.points[p++] = point(cubics.front().start);
                for (const auto& cubic : cubics) {
                    target.verbs[v++] = render::PathVerb::cubic;
                    target.points[p++] = point(cubic.control1); target.points[p++] = point(cubic.control2); target.points[p++] = point(cubic.end);
                }
                target.verbs[v++] = render::PathVerb::close;
            }
        });
        if (!path || !output.fill_path(*path, render::FillRule::nonzero, material)) return refuse(VectorError::recording_failed, i);
        output.pop_transform();
    }
    for (std::size_t i = 0; i < data_->bindings.size(); ++i) output.pop_layer();
    destination.append(output);
    return {};
}
} // namespace hanim::vector_render
