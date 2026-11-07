module hanim.render.vector;

import std;
import mlibs.log;
import mlibs.trace;

namespace hanim::detail {
struct VectorParameter { std::size_t source; float value, min, max; };
struct VectorEffects {
    PreparedEffects plan;
    std::vector<vector_render::BoundImageEffect> bindings;
    std::vector<VectorParameter> parameters;
};
struct VectorData {
    PreparedObjectAnimation geometry;
    VectorEffects effects;
    std::vector<std::optional<xrender::PreparedPaintMix>> materials;
};
struct VectorProgramData {
    PreparedProgram program;
    std::vector<std::shared_ptr<const VectorData>> segments;
    std::vector<VectorEffects> scopes;
    std::vector<vector_render::VectorRequirements> track_offsets, scope_offsets;
    vector_render::ProgramVectorRequirements required;
    bool direct_order = true;
};
}

namespace hanim::vector_render {
namespace {
namespace render = xrender;
namespace geom = mlibs::geom;
constexpr auto no_source = std::numeric_limits<std::size_t>::max();
constexpr mlibs::log::Log log{"hanim.render.vector"};
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
template<class T>bool overlaps(std::span<T> a,std::span<T> b) {
    if(a.empty() || b.empty())return false;
    const auto first=reinterpret_cast<std::uintptr_t>(a.data()),second=reinterpret_cast<std::uintptr_t>(b.data());
    return first<=second?second-first<a.size_bytes():first-second<b.size_bytes();
}
std::expected<detail::VectorEffects, VectorFailure> bind_effects(const PreparedEffects& effects) {
    auto refuse=[](VectorError code,std::size_t entry=0,std::size_t detail=0)->std::expected<detail::VectorEffects,VectorFailure> {
        return std::unexpected(VectorFailure{code, {}, {entry, detail}});
    };
    detail::VectorEffects data; data.plan=effects;
    auto& registry = render::effects();
    const auto descriptions = effects.effects(); const auto endpoints = effects.parameters();
    for (std::size_t i = 0; i < descriptions.size(); ++i) {
        const auto& effect = descriptions[i];
        const auto ref = registry.find(effect.id);
        if (!ref) return refuse(VectorError::unknown_effect, i);
        const auto* info = registry.info(*ref);
        if (!info || !info->cpu || info->backdrop) return refuse(VectorError::unsupported_effect, i);
        if (info->params.size() > 65536 - data.parameters.size()) return refuse(VectorError::capacity, i);
        const auto offset = data.parameters.size();
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
            data.parameters.push_back({source, target.value, target.min, target.max});
        }
        for (std::size_t q = 0; q < effect.count; ++q) {
            if (std::ranges::none_of(info->params, [&](const auto& param) { return param.name == endpoints[effect.offset + q].name; }))
                return refuse(VectorError::unknown_parameter, i, q);
        }
        data.bindings.push_back({*ref, registry.generation(*ref), offset, info->params.size()});
    }
    return data;
}
std::expected<void,VectorFailure> resolve_effects(const detail::VectorEffects& data,
    std::span<const float> effect_values,std::span<float> parameters) noexcept {
    auto refuse=[](VectorError code,std::size_t entry=0,std::size_t detail=0)->std::expected<void,VectorFailure> {
        return std::unexpected(VectorFailure{code, {}, {entry, detail}});
    };
    if(effect_values.size()<data.plan.parameter_count() || parameters.size()<data.parameters.size())
        return refuse(VectorError::buffer_too_small);
    if(overlaps<const float>(effect_values.first(data.plan.parameter_count()),parameters.first(data.parameters.size())))
        return refuse(VectorError::recording_alias);
    auto& registry=render::effects();
    for(std::size_t i=0;i<data.bindings.size();++i)
        if(registry.generation(data.bindings[i].ref)!=data.bindings[i].generation)return refuse(VectorError::stale_effect,i);
    for (std::size_t i = 0; i < data.parameters.size(); ++i) {
        const auto& binding = data.parameters[i];
        const auto value = binding.source == no_source ? binding.value : effect_values[binding.source];
        if (!std::isfinite(value) || value < binding.min || value > binding.max) return refuse(VectorError::invalid_parameter, i);
    }
    for(std::size_t i=0;i<data.parameters.size();++i) {
        const auto& binding=data.parameters[i];
        parameters[i]=binding.source==no_source?binding.value:effect_values[binding.source];
    }
    return {};
}
std::expected<geom::Rect,VectorFailure> measure_effects(const detail::VectorEffects& data,
    std::span<const float> effect_values,geom::Rect bounds,VectorScratch scratch) {
    auto refuse=[](VectorError code,std::size_t entry=0,std::size_t detail=0)->std::expected<geom::Rect,VectorFailure> {
        return std::unexpected(VectorFailure{code, {}, {entry, detail}});
    };
    if(auto resolved=resolve_effects(data,effect_values,scratch.backend_parameters);!resolved)return std::unexpected(resolved.error());
    auto& registry = render::effects();
    for (std::size_t i = 0; i < data.bindings.size(); ++i) {
        const auto& binding = data.bindings[i];
        if (registry.generation(binding.ref) != binding.generation) return refuse(VectorError::stale_effect, i);
        scratch.effect_layers[i] = bounds;
        const auto reach = registry.output_reach(binding.ref, scratch.backend_parameters.subspan(binding.offset, binding.count));
        if (!reach || !std::isfinite(*reach) || *reach < 0) return refuse(VectorError::unsupported_effect, i);
        bounds = bounds.outset({*reach, *reach, *reach, *reach});
        if (!finite(bounds)) return refuse(VectorError::invalid_geometry, i);
    }
    return bounds;
}
std::expected<geom::Rect,VectorFailure> measure(const detail::VectorData& data,FrameView frame,
    std::span<const float> effect_values,VectorScratch scratch) {
    auto refuse=[](VectorError code,std::size_t entry=0,std::size_t detail=0)->std::expected<geom::Rect,VectorFailure> {
        return std::unexpected(VectorFailure{code, {}, {entry, detail}});
    };
    const auto parts = data.geometry.parts(); const auto contours = data.geometry.contours();
    if (frame.preparation != data.geometry.identity() || frame.parts.size() != parts.size() || frame.contours.size() != contours.size() ||
        frame.cubics.size() != data.geometry.requirements().cubics ||
        frame.source_object != data.geometry.source_object() || frame.source_revision != data.geometry.source_revision())
        return refuse(VectorError::foreign_frame);
    if (!std::isfinite(frame.progress) || frame.progress < 0 || frame.progress > 1) return refuse(VectorError::invalid_progress);
    for (std::size_t i = 0; i < contours.size(); ++i)
        if (!same(frame.contours[i], contours[i])) return refuse(VectorError::foreign_frame, i);
    bool has_bounds = false;
    float left = 0, top = 0, right = 0, bottom = 0, stroke_reach = 0;
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
        const auto& stroke = frame.parts[i].stroke;
        if ((parts[i].contour_count && !fill && !stroke) || (fill && !color({fill->r, fill->g, fill->b, fill->a})))
            return refuse(VectorError::invalid_geometry, i);
        if (stroke) {
            const auto& ink = stroke->color;
            if (!color({ink.r, ink.g, ink.b, ink.a}) || !scalar(stroke->width) || stroke->width < 0) return refuse(VectorError::invalid_geometry, i);
            // Miter joins reach miter_limit · width / 2 beyond the outline.
            stroke_reach = std::max(stroke_reach, static_cast<float>(stroke->width) * 2 * world.max_scale());
        }
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
    auto bounds = geom::Rect::ltrb(left - stroke_reach, top - stroke_reach, right + stroke_reach, bottom + stroke_reach);
    if (!finite(bounds)) return refuse(VectorError::invalid_geometry);
    return measure_effects(data.effects,effect_values,bounds,scratch);
}
void open_effects(const detail::VectorEffects& data,VectorScratch scratch,render::DisplayList& output) {
    for (std::size_t i = data.bindings.size(); i > 0; --i) {
        const auto& binding = data.bindings[i - 1];
        output.push_layer(1, render::BlendMode::normal, binding.ref,
            scratch.backend_parameters.subspan(binding.offset, binding.count), scratch.effect_layers[i - 1]);
    }
}
std::expected<void,VectorFailure> emit(const detail::VectorData& data,FrameView frame,VectorScratch scratch,
    render::DisplayList& output) {
    auto refuse=[](VectorError code,std::size_t entry=0,std::size_t detail=0)->std::expected<void,VectorFailure> {
        return std::unexpected(VectorFailure{code, {}, {entry, detail}});
    };
    const auto parts=data.geometry.parts();const auto contours=data.geometry.contours();
    open_effects(data.effects,scratch,output);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i]; if (!part.contour_count) continue;
        output.push_transform(scratch.world[i]);
        const auto& drawn = frame.parts[i];
        std::optional<render::Paint> material;
        if (data.materials[i]) {
            const auto current = data.materials[i]->record(frame.progress, output);
            if (!current) return refuse(VectorError::recording_failed, i);
            material = *current;
        } else if (drawn.fill) {
            material = render::Paint::solid({drawn.fill->r, drawn.fill->g, drawn.fill->b, drawn.fill->a});
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
        if (!path || (material && !output.fill_path(*path, render::FillRule::nonzero, *material)))
            return refuse(VectorError::recording_failed, i);
        if (drawn.stroke && drawn.stroke->width > 0) {
            const auto trim = [&](const ch::Channel<double>& channel) {
                const auto value = frame.value(i, channel.id);
                return static_cast<float>(value ? (*value)[0] : channel.rest);
            };
            const auto& ink = drawn.stroke->color;
            const render::StrokeStyle style{.width = static_cast<float>(drawn.stroke->width),
                                            .join = render::LineJoin::round,
                                            .trim_start = trim(ch::trim_start),
                                            .trim_end = trim(ch::trim_end),
                                            .trim_offset = trim(ch::trim_offset)};
            if (!output.stroke_path(*path, style, render::Paint::solid({ink.r, ink.g, ink.b, ink.a})))
                return refuse(VectorError::recording_failed, i);
        }
        output.pop_transform();
    }
    for (std::size_t i = 0; i < data.effects.bindings.size(); ++i) output.pop_layer();
    return {};
}

}
std::expected<PreparedImageEffects,VectorFailure> PreparedImageEffects::bind(const PreparedEffects& effects) {
    mlibs::trace::Span span("bind-image-effects","hanim.render.vector");
    auto bound=bind_effects(effects);if(!bound)return std::unexpected(bound.error());
    PreparedImageEffects result;result.data_=std::make_shared<detail::VectorEffects>(std::move(*bound));return result;
}
std::span<const BoundImageEffect> PreparedImageEffects::bindings() const & noexcept {
    return data_?std::span<const BoundImageEffect>(data_->bindings):std::span<const BoundImageEffect>{};
}
std::size_t PreparedImageEffects::parameter_count() const noexcept {return data_?data_->parameters.size():0;}
std::expected<void,VectorFailure> PreparedImageEffects::resolve(std::span<const float> sampled,std::span<float> output) const noexcept {
    if(!data_)return std::unexpected(VectorFailure{VectorError::unprepared});
    return resolve_effects(*data_,sampled,output);
}
std::string describe_where(const VectorWhere& where) {
    std::string out=std::format("entry {}, detail {}",where.entry,where.detail);
    if(where.track!=invalid_program_index)out+=std::format(", track {}",where.track);
    if(where.segment!=invalid_program_index)out+=std::format(", segment {}",where.segment);
    if(where.scope!=invalid_program_index)out+=std::format(", scope {}",where.scope);
    return out;
}
std::expected<PreparedVector, VectorFailure> PreparedVector::bind(
    const PreparedObjectAnimation& geometry, const PreparedEffects& effects, std::span<const MaterialBinding> materials) {
    mlibs::trace::Span span("bind", "hanim.render.vector");
    auto refuse = [](VectorError code, std::size_t entry = 0, std::size_t detail = 0) -> std::expected<PreparedVector, VectorFailure> {
        log.warn("bind refused: {} (entry={}, detail={})", describe(code).name, entry, detail);
        return std::unexpected(VectorFailure{code, {}, {entry, detail}});
    };
    const auto parts = geometry.parts();
    if (parts.empty()) return refuse(VectorError::unprepared);
    auto data = std::make_shared<detail::VectorData>();
    data->geometry = geometry; data->materials.resize(parts.size());
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
    auto bound=bind_effects(effects);if(!bound)return std::unexpected(bound.error());
    data->effects=std::move(*bound);
    PreparedVector result; result.data_ = std::move(data);
    log.info("bound {} parts and {} CPU effects", parts.size(), effects.effects().size());
    return result;
}
VectorRequirements PreparedVector::requirements() const noexcept {
    return data_ ? VectorRequirements{data_->geometry.parts().size(), data_->effects.parameters.size(), data_->effects.bindings.size()} : VectorRequirements{};
}
std::expected<void, VectorFailure> PreparedVector::record(
    FrameView frame, std::span<const float> effect_values, render::DisplayList& destination, VectorScratch scratch) const {
    mlibs::trace::Span span("record", "hanim.render.vector");
    auto refuse = [](VectorError code, std::size_t entry = 0, std::size_t detail = 0) -> std::expected<void, VectorFailure> {
        mlibs::trace::mark("refused", "hanim.render.vector");
        return std::unexpected(VectorFailure{code, {}, {entry, detail}});
    };
    if (!data_) return refuse(VectorError::unprepared);
    if (scratch.recording == &destination) return refuse(VectorError::recording_alias);
    const auto required = requirements();
    if (!scratch.recording || scratch.world.size() < required.world_transforms ||
        scratch.backend_parameters.size() < required.backend_parameters || scratch.effect_layers.size() < required.effect_layers ||
        effect_values.size() < data_->effects.plan.parameter_count()) return refuse(VectorError::buffer_too_small);
    const auto measured=measure(*data_,frame,effect_values,scratch);
    if(!measured)return std::unexpected(measured.error());
    auto& output=*scratch.recording;output.clear();
    const auto emitted=emit(*data_,frame,scratch,output);if(!emitted)return emitted;
    destination.append(output);
    return {};
}
namespace {
VectorScratch portion(VectorScratch scratch,VectorRequirements offset) {
    return {scratch.recording,scratch.world.subspan(offset.world_transforms),
        scratch.backend_parameters.subspan(offset.backend_parameters),scratch.effect_layers.subspan(offset.effect_layers)};
}
VectorFailure located(VectorFailure error,std::size_t track=invalid_program_index,
    std::size_t segment=invalid_program_index,std::size_t scope=invalid_program_index) {
    error.where.track=track;error.where.segment=segment;error.where.scope=scope;return error;
}
}
std::expected<PreparedVectorProgram,VectorFailure> PreparedVectorProgram::bind(
    const PreparedProgram& program,ProgramVectorLimits limits) {
    mlibs::trace::Span span("bind-program","hanim.render.vector");
    if(!program)return std::unexpected(VectorFailure{VectorError::unprepared});
    auto data=std::make_shared<detail::VectorProgramData>();data->program=program;
    std::size_t parameters=0,layers=0;
    auto account=[&](const detail::VectorEffects& effects) {
        if(effects.parameters.size()>limits.max_prepared_backend_parameters-parameters ||
            effects.bindings.size()>limits.max_prepared_effect_layers-layers)return false;
        parameters+=effects.parameters.size();layers+=effects.bindings.size();return true;
    };
    for(std::size_t i=0;i<program.segments().size();++i) {
        const auto& segment=program.segments()[i];
        auto bound=PreparedVector::bind(segment.animation,segment.effects);
        if(!bound)return std::unexpected(located(bound.error(),segment.track,i));
        if(!account(bound->data_->effects))return std::unexpected(located({VectorError::capacity},segment.track,i));
        data->segments.push_back(std::move(bound->data_));
    }
    auto& required=data->required;required.tracks=program.tracks().size();required.scopes=program.scopes().size();
    std::size_t last_node=0;
    for(const auto& track:program.tracks()) {
        VectorRequirements max;
        for(std::size_t i=track.first_segment;i<track.first_segment+track.segment_count;++i) {
            const auto& segment=*data->segments[i];
            max.world_transforms=std::max(max.world_transforms,segment.geometry.parts().size());
            max.backend_parameters=std::max(max.backend_parameters,segment.effects.parameters.size());
            max.effect_layers=std::max(max.effect_layers,segment.effects.bindings.size());
        }
        data->track_offsets.push_back(required.buffers);
        required.buffers.world_transforms+=max.world_transforms;
        required.buffers.backend_parameters+=max.backend_parameters;required.buffers.effect_layers+=max.effect_layers;
        const auto node=program.segments()[track.first_segment].node;
        data->direct_order &= track.segment_count==1 && node>=last_node;last_node=node;
    }
    for(std::size_t i=0;i<program.scopes().size();++i) {
        const auto& scope=program.scopes()[i];auto bound=bind_effects(scope.effects);
        if(!bound)return std::unexpected(located(bound.error(),no_source,no_source,i));
        if(!account(*bound))return std::unexpected(located({VectorError::capacity},no_source,no_source,i));
        data->scope_offsets.push_back(required.buffers);
        required.buffers.backend_parameters+=bound->parameters.size();required.buffers.effect_layers+=bound->bindings.size();
        required.scope_depth=std::max(required.scope_depth,scope.depth);data->scopes.push_back(std::move(*bound));
    }
    mlibs::trace::count("image-scopes",static_cast<double>(required.scopes),"hanim.render.vector");
    log.info("bound program with {} tracks and {} image scopes",required.tracks,required.scopes);
    PreparedVectorProgram result;result.data_=std::move(data);return result;
}
ProgramVectorRequirements PreparedVectorProgram::requirements() const noexcept {
    return data_?data_->required:ProgramVectorRequirements{};
}
std::expected<void,VectorFailure> PreparedVectorProgram::record(
    ProgramFrameView frame,render::DisplayList& destination,ProgramVectorScratch scratch) const {
    mlibs::trace::Span span("record-program","hanim.render.vector");
    auto refuse=[](VectorError code)->std::expected<void,VectorFailure> {return std::unexpected(VectorFailure{code});};
    if(!data_)return refuse(VectorError::unprepared);
    const auto& program=data_->program;const auto required=data_->required;
    if(frame.preparation!=program.identity() || frame.tracks.size()!=required.tracks ||
        frame.scope_effect_values.size()!=program.scope_parameter_count())return refuse(VectorError::foreign_frame);
    if(!std::isfinite(frame.position.count()))return refuse(VectorError::invalid_progress);
    if(scratch.buffers.recording==&destination || overlaps(scratch.scope_members,scratch.order) ||
        overlaps(scratch.scope_stack,scratch.order) || overlaps(scratch.scope_stack,scratch.scope_members) ||
        overlaps(scratch.bounds,scratch.buffers.effect_layers))return refuse(VectorError::recording_alias);
    if(!scratch.buffers.recording || scratch.buffers.world.size()<required.buffers.world_transforms ||
        scratch.buffers.backend_parameters.size()<required.buffers.backend_parameters ||
        scratch.buffers.effect_layers.size()<required.buffers.effect_layers ||
        scratch.bounds.size()<required.tracks+required.scopes || scratch.scope_members.size()<required.scopes ||
        scratch.order.size()<required.tracks || scratch.scope_stack.size()<required.scope_depth)
        return refuse(VectorError::buffer_too_small);
    std::ranges::fill(scratch.scope_members.first(required.scopes),0);
    auto scope_bounds=scratch.bounds.subspan(required.tracks,required.scopes);
    auto include=[&](std::size_t scope,geom::Rect bounds) {
        auto& count=scratch.scope_members[scope];
        scope_bounds[scope]=count?scope_bounds[scope].united(bounds):bounds;++count;
    };
    for(std::size_t i=0;i<required.tracks;++i) {
        const auto& view=frame.tracks[i];const auto& track=program.tracks()[i];const auto index=view.segment_index;
        if(index<track.first_segment || index-track.first_segment>=track.segment_count)
            return std::unexpected(located({VectorError::foreign_frame},i,index));
        const auto& segment=program.segments()[index];
        const auto measured=measure(*data_->segments[index],view.geometry,view.effect_values,portion(scratch.buffers,data_->track_offsets[i]));
        if(!measured)return std::unexpected(located(measured.error(),i,index));
        scratch.bounds[i]=*measured;scratch.order[i]=i;
        if(segment.scope!=no_source && !view.geometry.contours.empty())include(segment.scope,*measured);
    }
    // Children first: each group's extent includes its children's already
    // expanded output. Each effect chain then expands that union exactly once.
    for(std::size_t end=required.scopes;end>0;--end) {
        const auto i=end-1;if(!scratch.scope_members[i])continue;
        const auto& scope=program.scopes()[i];
        const auto measured=measure_effects(data_->scopes[i],
            frame.scope_effect_values.subspan(scope.parameter_offset,scope.effects.parameter_count()),
            scope_bounds[i],portion(scratch.buffers,data_->scope_offsets[i]));
        if(!measured)return std::unexpected(located(measured.error(),no_source,no_source,i));
        scope_bounds[i]=*measured;if(scope.parent!=no_source)include(scope.parent,*measured);
    }
    auto order=scratch.order.first(required.tracks);
    if(!data_->direct_order)std::ranges::sort(order,[&](std::size_t a,std::size_t b) {
        return program.segments()[frame.tracks[a].segment_index].node<program.segments()[frame.tracks[b].segment_index].node;
    });
    auto& output=*scratch.buffers.recording;output.clear();
    std::size_t current=no_source;
    auto depth=[&](std::size_t index) {return index==no_source?std::size_t(0):program.scopes()[index].depth;};
    auto close=[&]() {
        for(std::size_t i=0;i<data_->scopes[current].bindings.size();++i)output.pop_layer();
        current=program.scopes()[current].parent;
    };
    for(auto track:order) {
        const auto& view=frame.tracks[track];if(view.geometry.contours.empty())continue;
        auto next=program.segments()[view.segment_index].scope;std::size_t count=0;
        // Find the common ancestor while closing old scopes and collecting
        // new ones. Author preorder guarantees a scope is entered only once.
        while(current!=next) {
            if(depth(current)>depth(next))close();
            else {scratch.scope_stack[count++]=next;next=program.scopes()[next].parent;}
        }
        while(count) {
            current=scratch.scope_stack[--count];
            open_effects(data_->scopes[current],portion(scratch.buffers,data_->scope_offsets[current]),output);
        }
        const auto emitted=emit(*data_->segments[view.segment_index],view.geometry,
            portion(scratch.buffers,data_->track_offsets[track]),output);
        if(!emitted)return std::unexpected(located(emitted.error(),track,view.segment_index));
    }
    while(current!=no_source)close();
    destination.append(output);return {};
}
} // namespace hanim::vector_render
