module hanim.library;

import std;
import hanim.core;
import mlibs.log;
import mlibs.trace;

namespace hanim::detail {
struct MorphEndpoints { Transform2D from, to; std::optional<Rgba> from_fill, to_fill; std::optional<PartStroke> from_stroke, to_stroke; };
struct PropertyValue { Vec2 vector; double scalar = 0; Rgba color; ch::Lanes lanes{}; };
struct PropertyEndpoints { PropertyValue from, to; };
struct ChannelRun { std::size_t first=0, count=0; };
struct ObjectMorphData {
    std::vector<std::string> ids;
    std::vector<MorphPartInfo> parts;
    std::vector<MorphContourInfo> contours;
    std::vector<MorphEndpoints> states;
    std::vector<PreparedShapeMorph> shapes;
    std::vector<ShapeCubic> static_cubics;
    std::size_t cubic_count = 0;
    PointPath translation_path;
    std::vector<PropertyChannelInfo> channels;
    std::vector<PropertyEndpoints> channel_values;
    std::vector<ChannelRun> channel_runs;
    std::vector<ValueSlot> value_slots;
    std::size_t value_count = 0;
    seconds timeline_duration{0};
    std::size_t route_segments=0;
    // Each part's deformers (MorphPartInfo::deformers borrows them) and, for
    // each, its points' frozen weights: four a cubic, over the part's cubics.
    std::vector<std::vector<PartDeformer>> deformers;
    struct Deformation { std::uint32_t part = 0; std::size_t deformer = 0, first_cubic = 0, cubic_count = 0; std::vector<double> weights; };
    std::vector<Deformation> deformations;
};
}
namespace hanim {
namespace {
constexpr mlibs::log::Log diagnostic{"hanim.library"};
constexpr auto none = std::numeric_limits<std::size_t>::max();
MorphFailure failure(MorphError code, std::size_t part = none, std::size_t contour = none,
                     std::optional<ShapeMorphError> shape = {}) {
    MorphFailure error{code, {}, {part, contour}};
    if (shape) error.because(*shape);
    return error;
}
bool finite(Transform2D value) noexcept {
    for (double component : lanes<Transform2D>::to(value)) if (!std::isfinite(component)) return false;
    return true;
}
bool valid(std::optional<Rgba> fill) noexcept {
    if (!fill) return true;
    for (float component : {fill->r,fill->g,fill->b,fill->a})
        if (!std::isfinite(component) || component < 0 || component > 1) return false;
    return true;
}
bool valid(const std::optional<PartStroke>& stroke) noexcept {
    return !stroke || (valid(std::optional<Rgba>(stroke->color)) && std::isfinite(stroke->width) && stroke->width >= 0);
}
bool valid(MorphOptions options) noexcept {
    // These are aggregate preparation limits, not per-frame preallocations.
    return options.max_parts && options.max_parts <= (1u<<16) &&
        options.max_contours && options.max_contours <= (1u<<16) &&
        options.max_cubics && options.max_cubics <= (1u<<20) &&
        options.shape.point_path.valid() && options.translation_path.valid() && options.max_route_segments<=(1u<<20);
}
struct CurveInput { ShapeContour view; std::vector<ShapeSegment> owned; };
struct PartInput {
    std::string id;
    PartRef ref;
    std::uint32_t parent = invalid_object_slot;
    std::vector<std::uint32_t> children;
    Transform2D local;
    std::optional<Rgba> fill;
    std::optional<PartStroke> stroke;
    std::vector<PartDeformer> deformers;
    std::vector<CurveInput> contours;
};
// A value channel's value carried from the frame an animation continues from.
struct CarriedValue { std::string part; ch::Id id; ch::Lanes lanes{}; };
struct Input { std::vector<PartInput> parts; std::vector<CarriedValue> values; };
struct PartPose { Transform2D local; std::optional<Rgba> fill; };
struct PropertyIndex {
    const Input& input;
    std::vector<std::size_t> names;
    std::size_t root=none;
    explicit PropertyIndex(const Input& value):input(value),names(value.parts.size()) {
        std::iota(names.begin(),names.end(),std::size_t{0});
        std::ranges::sort(names,[&](auto a,auto b){return input.parts[a].id<input.parts[b].id;});
        for(std::size_t i=0;i<input.parts.size();++i)if(input.parts[i].parent==invalid_object_slot)root=i;
    }
    std::size_t find(std::string_view id) const {
        if(id.empty())return root;
        const auto found=std::ranges::lower_bound(names,id,{},[&](auto i)->std::string_view{return input.parts[i].id;});
        return found!=names.end() && input.parts[*found].id==id?*found:none;
    }
};
struct PropertyWrite { std::size_t part; std::uint8_t mask; const PropertyTargets* target; };
std::expected<std::vector<PropertyWrite>,MorphFailure> resolve_properties(
    const PropertyIndex& index,std::span<const PropertyTargets> targets,std::size_t route_budget) {
    if(targets.empty() || targets.size()>index.input.parts.size()*4)
        return std::unexpected(failure(MorphError::invalid_property));
    std::vector<std::uint8_t> masks(index.input.parts.size(),0);
    std::vector<PropertyWrite> writes;writes.reserve(targets.size());
    for(const auto& values:targets) {
        if((values.position && values.offset) || (values.route && (values.position || values.offset)) || (values.rotation && values.rotation_offset) || (values.scale && values.scale_factor) ||
            (!values.position && !values.offset && !values.route && !values.rotation && !values.rotation_offset && !values.scale && !values.scale_factor && !values.fill &&
             values.values.empty()))
            return std::unexpected(failure(MorphError::invalid_property));
        if(values.route) {
            const auto* path=values.route->path();
            if(!path || path->empty())return std::unexpected(failure(MorphError::invalid_property));
            if(path->segment_count()>route_budget)return std::unexpected(failure(MorphError::route_budget));
            route_budget-=path->segment_count();
            if(!path->valid() || !path->continuous())return std::unexpected(failure(MorphError::invalid_property));
        }
        const auto part=index.find(values.part);
        if(part==none)return std::unexpected(failure(MorphError::unknown_part));
        for(const auto& v:values.values) {
            // The built-in channels are the part's fields, written as such; a
            // value has its lanes, all finite; one write a channel a part.
            if(ch::builtin(v.id.name) || v.id.name.empty() || !v.id.lanes || v.id.lanes>ch::max_lanes ||
               !std::ranges::all_of(v.lanes,[](double x){return std::isfinite(x);}) || !std::ranges::all_of(v.rest,[](double x){return std::isfinite(x);}))
                return std::unexpected(failure(MorphError::invalid_property,part));
            for(const auto& w:writes)if(w.part==part)for(const auto& other:w.target->values)
                if(other.id.name==v.id.name)return std::unexpected(failure(MorphError::property_conflict,part));
            for(const auto& other:values.values)if(&other!=&v && other.id.name==v.id.name)
                return std::unexpected(failure(MorphError::property_conflict,part));
        }
        const auto mask=static_cast<std::uint8_t>((values.position || values.offset || values.route?1:0) |
            (values.rotation || values.rotation_offset?2:0) | (values.scale || values.scale_factor?4:0) | (values.fill?8:0));
        if(masks[part]&mask)return std::unexpected(failure(MorphError::property_conflict,part));
        masks[part]|=mask;writes.push_back({part,mask,&values});
    }
    return writes;
}
std::expected<PartPose,MorphFailure> apply_property(PartPose pose,const PropertyTargets& values,std::size_t part) {
    if(values.position){pose.local.tx=values.position->x;pose.local.ty=values.position->y;}
    if(values.offset){pose.local.tx+=values.offset->x;pose.local.ty+=values.offset->y;}
    if(values.route) {
        const auto* path=values.route->path();const auto first=path->point(0),last=path->point(1);
        pose.local.tx+=last.x-first.x;pose.local.ty+=last.y-first.y;
    }
    if(values.rotation)pose.local.rotate=*values.rotation;
    if(values.rotation_offset)pose.local.rotate+=*values.rotation_offset;
    if(values.scale){pose.local.sx=values.scale->x;pose.local.sy=values.scale->y;}
    if(values.scale_factor){pose.local.sx*=values.scale_factor->x;pose.local.sy*=values.scale_factor->y;}
    if(values.fill) {
        if(!pose.fill)return std::unexpected(failure(MorphError::missing_fill,part));
        pose.fill=*values.fill;
    }
    if(!finite(pose.local) || !valid(pose.fill))return std::unexpected(failure(MorphError::invalid_property,part));
    return pose;
}
detail::PropertyValue property_value(PartPose pose,PropertyChannel channel) {
    switch(channel) {
        case PropertyChannel::translation:return {.vector={pose.local.tx,pose.local.ty}};
        case PropertyChannel::rotation:return {.scalar=pose.local.rotate};
        case PropertyChannel::scale:return {.vector={pose.local.sx,pose.local.sy}};
        case PropertyChannel::fill:return {.color=*pose.fill};
        case PropertyChannel::value:break;
    }return {};
}
void assign_property(Transform2D& local,std::optional<Rgba>& fill,PropertyChannel channel,const detail::PropertyValue& value) {
    switch(channel) {
        case PropertyChannel::translation:local.tx=value.vector.x;local.ty=value.vector.y;break;
        case PropertyChannel::rotation:local.rotate=value.scalar;break;
        case PropertyChannel::scale:local.sx=value.vector.x;local.sy=value.vector.y;break;
        case PropertyChannel::fill:fill=value.color;break;
        case PropertyChannel::value:break;  // a value channel lands in FrameView::values
    }
}
MorphFailure channel_failure(MorphError code,const PropertyChannelInfo& info) {
    auto error=failure(code,info.part);error.where.clip=info.clip;error.where.channel=info.channel;return error;
}
// `beyond`: a plan's progress past its ends (an overshooting curve the
// program applied, PreparedObjectAnimation::sample), taken by a clip spanning the
// whole plan as its own linear progress.
std::expected<detail::PropertyValue,MorphFailure> sample_channel(
    const PropertyChannelInfo& info,const detail::PropertyEndpoints& values,seconds time,std::optional<double> beyond = {}) {
    if(time<info.start || (info.anchor_source && info.duration.count()>0 && time==info.start))
        if(!beyond || *beyond<=0)return values.from;
    const auto linear=beyond?*beyond:info.duration.count()==0?1:Tween<double>{.from=0,.to=1,.duration=info.duration,
        .ease=ease::linear,.delay=info.start}.value(time);
    const auto eased=beyond && info.easing==ease::linear?linear:info.easing(linear);
    if(!std::isfinite(eased))return std::unexpected(channel_failure(MorphError::invalid_ease,info));
    // Overshooting curves (back, elastic, a spring) overshoot a position, a
    // turn, a scale and a value; a route's arc and a colour stay within their
    // ends.
    const double u=eased,within=std::clamp(eased,0.,1.);detail::PropertyValue result;
    switch(info.channel) {
        case PropertyChannel::translation: {
            const auto straight=lerp(values.from.vector,values.to.vector,u);
            const auto modified=info.translation_path.linear()?straight:info.translation_path.interpolate(values.from.vector,values.to.vector,within);
            result.vector=modified;
            if(info.route && within>0 && within<1) {
                const auto point=info.route->path()->point(within);
                result.vector={values.from.vector.x+(point.x-info.route_origin.x)+(modified.x-straight.x),
                    values.from.vector.y+(point.y-info.route_origin.y)+(modified.y-straight.y)};
            }
            break;
        }
        case PropertyChannel::rotation:result.scalar=lerp(values.from.scalar,values.to.scalar,u);break;
        case PropertyChannel::scale:result.vector=lerp(values.from.vector,values.to.vector,u);break;
        case PropertyChannel::fill:result.color=lerp(values.from.color,values.to.color,within);break;
        case PropertyChannel::value:
            for(std::size_t i=0;i<ch::max_lanes;++i)result.lanes[i]=values.from.lanes[i]+(values.to.lanes[i]-values.from.lanes[i])*u;
            if(!std::ranges::all_of(result.lanes,[](double x){return std::isfinite(x);}))
                return std::unexpected(channel_failure(MorphError::nonfinite_transform,info));
            break;
    }
    if(!std::isfinite(result.vector.x) || !std::isfinite(result.vector.y) || !std::isfinite(result.scalar) || !valid(result.color))
        return std::unexpected(channel_failure(MorphError::nonfinite_transform,info));
    return result;
}

std::expected<Input,MorphFailure> snapshot_input(const Snapshot& snapshot, MorphOptions options) {
    if (!snapshot) return std::unexpected(failure(MorphError::invalid_snapshot));
    const auto captured = snapshot.parts();
    if (captured.size() > options.max_parts) return std::unexpected(failure(MorphError::part_budget));
    Input result; result.parts.reserve(captured.size());
    struct Visit { PartRef ref; std::uint32_t parent; };
    std::vector<Visit> todo{{snapshot.root(),invalid_object_slot}};
    std::size_t contours = 0;
    while (!todo.empty()) {
        const auto visit = todo.back(); todo.pop_back();
        const auto found = snapshot.get(visit.ref);
        if (!found || result.parts.size() >= captured.size()) return std::unexpected(failure(MorphError::invalid_snapshot));
        const auto& part = **found;
        const auto index = static_cast<std::uint32_t>(result.parts.size());
        if (part.contours.size() > options.max_contours - contours) return std::unexpected(failure(MorphError::contour_budget,index));
        contours += part.contours.size();
        PartInput item; item.id=part.id; item.ref=part.ref; item.parent=visit.parent; item.local=part.local; item.fill=part.fill; item.stroke=part.stroke;
        item.deformers=part.deformers;
        item.contours.reserve(part.contours.size()); item.children.reserve(part.children.size());
        for (std::size_t ordinal=0;ordinal<part.contours.size();++ordinal) {
            const auto view=part.contours[ordinal].view();
            if (!view) return std::unexpected(failure(MorphError::invalid_snapshot,index,ordinal));
            item.contours.push_back({*view,{}});
        }
        result.parts.push_back(std::move(item));
        if (visit.parent != invalid_object_slot) result.parts[visit.parent].children.push_back(index);
        for (auto it=part.children.rbegin();it!=part.children.rend();++it) todo.push_back({*it,index});
    }
    if (result.parts.size()!=captured.size()) return std::unexpected(failure(MorphError::invalid_snapshot));
    return result;
}

std::expected<Input,MorphFailure> frame_input(FrameView frame, MorphOptions options) {
    if (!frame.preparation || !frame.source_object || !frame.source_revision || frame.parts.empty() ||
        !std::isfinite(frame.progress) || frame.progress<0 || frame.progress>1)
        return std::unexpected(failure(MorphError::invalid_frame));
    if (frame.parts.size()>options.max_parts) return std::unexpected(failure(MorphError::part_budget));
    if (frame.contours.size()>options.max_contours) return std::unexpected(failure(MorphError::contour_budget));
    if (frame.cubics.size()>options.max_cubics) return std::unexpected(failure(MorphError::cubic_budget));
    std::vector<PartInput> raw; raw.reserve(frame.parts.size());
    std::size_t first_contour=0, first_cubic=0, roots=0; std::uint32_t root=invalid_object_slot;
    for (std::size_t i=0;i<frame.parts.size();++i) {
        const auto& part=frame.parts[i]; const auto& info=part.info;
        if (!info.from_ref || info.from_ref.object!=frame.source_object || info.id.empty() || !finite(part.local) || !valid(part.fill) || !valid(part.stroke) ||
            info.first_contour!=first_contour || first_contour>frame.contours.size() || info.contour_count>frame.contours.size()-first_contour)
            return std::unexpected(failure(MorphError::invalid_frame,i));
        if (info.parent==invalid_object_slot) { ++roots; root=static_cast<std::uint32_t>(i); }
        else if (info.parent>=frame.parts.size() || info.parent==i) return std::unexpected(failure(MorphError::invalid_frame,i));
        PartInput item; item.id=info.id; item.ref=info.from_ref; item.parent=info.parent; item.local=part.local; item.fill=part.fill; item.stroke=part.stroke;
        item.deformers.assign(info.deformers.begin(),info.deformers.end());
        item.contours.reserve(info.contour_count);
        const auto part_first_cubic=first_cubic;
        std::size_t part_cubics=0;
        for (std::size_t j=0;j<info.contour_count && first_contour+j<frame.contours.size();++j) part_cubics+=frame.contours[first_contour+j].cubic_count;
        // A deformed part goes on from its geometry before the deformers, not
        // deformed twice.
        const bool undeformed=!info.deformers.empty() && info.base.size()==part_cubics;
        for (std::size_t j=0;j<info.contour_count;++j) {
            const auto& range=frame.contours[first_contour+j];
            if (range.part!=i || range.ordinal!=j || range.first_cubic!=first_cubic || !range.cubic_count ||
                first_cubic>frame.cubics.size() || range.cubic_count>frame.cubics.size()-first_cubic)
                return std::unexpected(failure(MorphError::invalid_frame,i,j));
            const auto cubics=undeformed ? info.base.subspan(first_cubic-part_first_cubic,range.cubic_count)
                                         : frame.cubics.subspan(first_cubic,range.cubic_count);
            CurveInput curve; curve.owned.reserve(cubics.size());
            for (std::size_t k=0;k<cubics.size();++k) {
                const auto& cubic=cubics[k];
                if (cubic.end!=cubics[(k+1)%cubics.size()].start) return std::unexpected(failure(MorphError::invalid_frame,i,j));
                curve.owned.push_back(ShapeSegment::cubic(cubic.control1,cubic.control2,cubic.end));
            }
            curve.view={cubics.front().start,curve.owned,true}; item.contours.push_back(std::move(curve));
            first_cubic+=range.cubic_count;
        }
        first_contour+=info.contour_count; raw.push_back(std::move(item));
    }
    if (roots!=1 || first_contour!=frame.contours.size() || first_cubic!=frame.cubics.size())
        return std::unexpected(failure(MorphError::invalid_frame));
    for (std::size_t i=0;i<raw.size();++i)
        if (raw[i].parent!=invalid_object_slot) raw[raw[i].parent].children.push_back(static_cast<std::uint32_t>(i));
    // Canonicalize a structurally valid frame, preserving sibling array order.
    Input result; result.parts.reserve(raw.size());
    std::vector<std::pair<std::uint32_t,std::uint32_t>> todo{{root,invalid_object_slot}};
    std::vector<bool> visited(raw.size(),false);
    while (!todo.empty()) {
        const auto [index,parent]=todo.back(); todo.pop_back();
        if (visited[index]) return std::unexpected(failure(MorphError::invalid_frame,index));
        visited[index]=true;
        auto& item=raw[index]; const auto next=static_cast<std::uint32_t>(result.parts.size());
        for (auto it=item.children.rbegin();it!=item.children.rend();++it) todo.emplace_back(*it,next);
        item.children.clear(); item.parent=parent;
        result.parts.push_back(std::move(item));
        if (parent!=invalid_object_slot) result.parts[parent].children.push_back(next);
    }
    if (result.parts.size()!=raw.size()) return std::unexpected(failure(MorphError::invalid_frame));
    // Its value channels go on from where the frame has them.
    for (const auto& slot : frame.value_slots) {
        if (slot.part>=frame.parts.size() || !slot.id.lanes || slot.id.lanes>ch::max_lanes || slot.offset>frame.values.size() ||
            slot.id.lanes>frame.values.size()-slot.offset)
            return std::unexpected(failure(MorphError::invalid_frame,slot.part));
        CarriedValue carried{std::string(frame.parts[slot.part].info.id),slot.id,{}};
        std::copy_n(frame.values.begin()+static_cast<std::ptrdiff_t>(slot.offset),slot.id.lanes,carried.lanes.begin());
        result.values.push_back(std::move(carried));
    }
    return result;
}

// One contour of a morph: a pair, or one end alone and the point it grows
// from or closes into.
struct ContourPlan { std::size_t from = none, to = none; Vec2 point{}; };

std::expected<std::vector<ContourPlan>,MorphFailure> pair_contours(const PartInput& source, const PartInput& target,
    std::size_t part, ContourPairing pairing) {
    std::vector<ContourPlan> plans;
    if (pairing==ContourPairing::strict) {
        if (source.contours.size()!=target.contours.size()) return std::unexpected(failure(MorphError::contour_count_mismatch,part));
        for (std::size_t index=0;index<source.contours.size();++index) plans.push_back({index,index});
        return plans;
    }
    struct Measured { std::size_t index; ShapeMeasure measure; bool outline; };
    const auto measure_all=[&](const PartInput& input) -> std::expected<std::vector<Measured>,MorphFailure> {
        std::vector<Measured> measured;
        for (std::size_t index=0;index<input.contours.size();++index) {
            const auto& view=input.contours[index].view;
            if (!view.closed) return std::unexpected(failure(MorphError::open_contour,part,index));
            // A contour that is one point (a grown contour's first frame) is no contour.
            if (std::ranges::all_of(view.segments,[&](const ShapeSegment& segment){
                    return segment.end==view.start && (segment.verb==ShapeVerb::line || segment.control1==view.start) &&
                           (segment.verb!=ShapeVerb::cubic || segment.control2==view.start);}))
                continue;
            // The input budget is the shape plans' to enforce, with their options.
            const auto value=measure(view);
            if (!value) return std::unexpected(failure(MorphError::shape_failure,part,index,value.error()));
            measured.push_back({index,*value,true});
        }
        double largest=0, sign=1;
        for (const auto& item:measured) if (std::abs(item.measure.area)>largest) { largest=std::abs(item.measure.area); sign=item.measure.area<0?-1:1; }
        for (auto& item:measured) item.outline=item.measure.area*sign>=0;
        return measured;
    };
    const auto sources=measure_all(source); if (!sources) return std::unexpected(sources.error());
    const auto targets=measure_all(target); if (!targets) return std::unexpected(targets.error());
    double extent=0;
    for (const auto* side:{&*sources,&*targets}) for (const auto& item:*side) extent=std::max(extent,std::abs(item.measure.area));
    extent=extent>0?std::sqrt(extent):1;
    const auto distance=[](Vec2 left, Vec2 right){return std::sqrt((left.x-right.x)*(left.x-right.x)+(left.y-right.y)*(left.y-right.y));};
    struct Candidate { double cost; std::size_t from, to; };
    std::vector<Candidate> candidates;
    for (std::size_t from_index=0;from_index<sources->size();++from_index)
        for (std::size_t to_index=0;to_index<targets->size();++to_index) {
            const auto& left=(*sources)[from_index]; const auto& right=(*targets)[to_index];
            if (left.outline!=right.outline) continue;
            const double sizes=std::abs(mlibs::math::det::log(std::abs(left.measure.area)/std::abs(right.measure.area)));
            candidates.push_back({distance(left.measure.centroid,right.measure.centroid)/extent+.5*sizes,from_index,to_index});
        }
    std::ranges::sort(candidates,[](const Candidate& left, const Candidate& right){
        return std::tie(left.cost,left.from,left.to)<std::tie(right.cost,right.from,right.to);});
    std::vector<std::size_t> paired_to(sources->size(),none), paired_from(targets->size(),none);
    for (const auto& candidate:candidates)
        if (paired_to[candidate.from]==none && paired_from[candidate.to]==none) {
            paired_to[candidate.from]=candidate.to; paired_from[candidate.to]=candidate.from;
        }
    // Where a contour alone begins or ends: an outline at the nearest outline
    // of the other end (it splits off or merges in), a hole at its own centre.
    const auto meeting=[&](const Measured& alone, const std::vector<Measured>& other) {
        if (!alone.outline) return alone.measure.centroid;
        std::optional<Vec2> nearest;
        for (const auto& item:other)
            if (item.outline && (!nearest || distance(item.measure.centroid,alone.measure.centroid)<distance(*nearest,alone.measure.centroid)))
                nearest=item.measure.centroid;
        return nearest.value_or(alone.measure.centroid);
    };
    for (std::size_t from_index=0;from_index<sources->size();++from_index)
        if (paired_to[from_index]!=none) plans.push_back({(*sources)[from_index].index,(*targets)[paired_to[from_index]].index});
    for (std::size_t from_index=0;from_index<sources->size();++from_index)
        if (paired_to[from_index]==none) plans.push_back({(*sources)[from_index].index,none,meeting((*sources)[from_index],*targets)});
    for (std::size_t to_index=0;to_index<targets->size();++to_index)
        if (paired_from[to_index]==none) plans.push_back({none,(*targets)[to_index].index,meeting((*targets)[to_index],*sources)});
    return plans;
}

std::expected<std::shared_ptr<detail::ObjectMorphData>,MorphFailure>
compile(Input from, Input to, MorphOptions options, bool property_only = false) {
    if (from.parts.size()!=to.parts.size()) return std::unexpected(failure(MorphError::part_set_mismatch));
    const auto count=from.parts.size();
    std::vector<std::size_t> a(count),b(count),matched(count);
    std::iota(a.begin(),a.end(),std::size_t{0}); std::iota(b.begin(),b.end(),std::size_t{0});
    std::ranges::sort(a,[&](auto x,auto y){return from.parts[x].id<from.parts[y].id;});
    std::ranges::sort(b,[&](auto x,auto y){return to.parts[x].id<to.parts[y].id;});
    for (std::size_t i=0;i<count;++i) {
        if ((i && from.parts[a[i-1]].id==from.parts[a[i]].id) || (i && to.parts[b[i-1]].id==to.parts[b[i]].id))
            return std::unexpected(failure(MorphError::invalid_frame,a[i]));
        if (from.parts[a[i]].id!=to.parts[b[i]].id) return std::unexpected(failure(MorphError::part_set_mismatch,a[i]));
        matched[a[i]]=b[i];
    }
    // A parent change is a hierarchy error even when it also changes the
    // children sequence of an earlier group in the canonical traversal.
    for (std::size_t i=0;i<count;++i) {
        const auto& x=from.parts[i]; const auto& y=to.parts[matched[i]];
        if ((x.parent==invalid_object_slot)!=(y.parent==invalid_object_slot) ||
            (x.parent!=invalid_object_slot && from.parts[x.parent].id!=to.parts[y.parent].id))
            return std::unexpected(failure(MorphError::hierarchy_mismatch,i));
    }
    auto result=std::make_shared<detail::ObjectMorphData>();
    result->translation_path=options.translation_path;
    result->ids.reserve(count); result->parts.reserve(count); result->states.reserve(count);
    for (std::size_t i=0;i<count;++i) {
        const auto& x=from.parts[i]; const auto& y=to.parts[matched[i]];
        if (x.children.size()!=y.children.size()) return std::unexpected(failure(MorphError::hierarchy_mismatch,i));
        for (std::size_t child=0;child<x.children.size();++child)
            if (from.parts[x.children[child]].id!=to.parts[y.children[child]].id)
                return std::unexpected(failure(MorphError::ordering_mismatch,i));
        if (x.fill.has_value()!=y.fill.has_value() || ((!x.contours.empty() || !y.contours.empty()) && !x.fill && !x.stroke))
            return std::unexpected(failure(MorphError::missing_fill,i));
        if (x.stroke.has_value()!=y.stroke.has_value()) return std::unexpected(failure(MorphError::missing_stroke,i));
        // A property plan keeps its one geometry contour for contour.
        const auto plans=pair_contours(x,y,i,property_only?ContourPairing::strict:options.contours);
        if (!plans) return std::unexpected(plans.error());
        if (plans->size()>options.max_contours-std::min(options.max_contours,result->contours.size()))
            return std::unexpected(failure(MorphError::contour_budget,i));
        result->ids.push_back(x.id);
        result->parts.push_back({{},x.ref,y.ref,x.parent,result->contours.size(),plans->size()});
        result->states.push_back({x.local,y.local,x.fill,y.fill,x.stroke,y.stroke});
        std::optional<bool> reversed;
        // Pairs first: a grown contour runs the way their targets were turned.
        for (std::size_t j=0;j<plans->size();++j) {
            const auto& plan=(*plans)[j];
            if ((plan.from!=none && !x.contours[plan.from].view.closed) || (plan.to!=none && !y.contours[plan.to].view.closed))
                return std::unexpected(failure(MorphError::open_contour,i,j));
            const auto remaining=options.max_cubics-result->cubic_count;
            if (!remaining) return std::unexpected(failure(MorphError::cubic_budget,i,j));
            auto shape=plan.from==none ? PreparedShapeMorph::grow(plan.point,y.contours[plan.to].view,options.shape,reversed.value_or(false))
                     : plan.to==none   ? PreparedShapeMorph::shrink(x.contours[plan.from].view,plan.point,options.shape)
                                       : PreparedShapeMorph::prepare(x.contours[plan.from].view,y.contours[plan.to].view,options.shape);
            if (!shape) return std::unexpected(failure(MorphError::shape_failure,i,j,shape.error()));
            if (shape->segment_count()>remaining) return std::unexpected(failure(MorphError::cubic_budget,i,j));
            // Independent winding flips can change a compound nonzero fill.
            // Uniform reversal of all target contours preserves that fill;
            // otherwise this refuses rather than inventing holes.
            if (plan.from!=none && plan.to!=none) {
                if (reversed && *reversed!=shape->target_reversed())
                    return std::unexpected(failure(MorphError::unsupported_topology,i,j));
                reversed=shape->target_reversed();
            }
            result->contours.push_back({static_cast<std::uint32_t>(i),j,result->cubic_count,shape->segment_count()});
            result->cubic_count+=shape->segment_count(); result->shapes.push_back(std::move(*shape));
        }
    }
    for (std::size_t i=0;i<count;++i) result->parts[i].id=result->ids[i];
    // Deformers: the source end's, weighed on the geometry the plan starts from.
    result->deformers.resize(count);
    std::vector<ShapeCubic> start;
    for (std::size_t i=0;i<count;++i) {
        const auto& source=from.parts[i];
        if (source.deformers.empty()) continue;
        for (const auto& deformer:source.deformers)
            if (ch::builtin(deformer.channel)) return std::unexpected(failure(MorphError::invalid_property,i));
        result->deformers[i]=source.deformers;
        const auto& info=result->parts[i];
        if (!info.contour_count) continue;
        const auto& first=result->contours[info.first_contour];
        const auto& last=result->contours[info.first_contour+info.contour_count-1];
        const auto first_cubic=first.first_cubic, cubic_count=last.first_cubic+last.cubic_count-first.first_cubic;
        start.resize(cubic_count);
        for (std::size_t j=0;j<info.contour_count;++j) {
            const auto& range=result->contours[info.first_contour+j];
            const auto sampled=result->shapes[info.first_contour+j].sample(0,std::span(start).subspan(range.first_cubic-first_cubic,range.cubic_count));
            mlibs::log::invariant(sampled.has_value(),"deformer start geometry invariant");
        }
        for (std::size_t index=0;index<source.deformers.size();++index) {
            detail::ObjectMorphData::Deformation deformation{static_cast<std::uint32_t>(i),index,first_cubic,cubic_count,{}};
            deformation.weights.reserve(cubic_count*4);
            for (const auto& cubic:start)
                for (const auto point:{cubic.start,cubic.control1,cubic.control2,cubic.end})
                    deformation.weights.push_back(source.deformers[index].select.weight(point));
            result->deformations.push_back(std::move(deformation));
        }
    }
    for (std::size_t i=0;i<count;++i) result->parts[i].deformers=result->deformers[i];
    const auto set_bases=[&] {
        for (const auto& deformation:result->deformations)
            if (deformation.deformer==0 && !result->static_cubics.empty())
                result->parts[deformation.part].base=std::span<const ShapeCubic>(result->static_cubics).subspan(deformation.first_cubic,deformation.cubic_count);
    };
    // Property-only plans retain geometry once. An explicit vertex-path effect
    // still runs the normal shape sampler; no second interpolation algorithm.
    if (property_only && options.shape.point_path.linear()) {
        result->static_cubics.resize(result->cubic_count);
        for (std::size_t i=0;i<result->shapes.size();++i) {
            const auto& range=result->contours[i];
            const auto sample=result->shapes[i].sample(0,std::span(result->static_cubics).subspan(range.first_cubic,range.cubic_count));
            mlibs::log::invariant(sample.has_value(),"property geometry preparation invariant");
        }
        result->shapes.clear();
    }
    set_bases();
    mlibs::trace::count("morph.parts",static_cast<double>(count),"hanim.library");
    mlibs::trace::count("morph.cubics",static_cast<double>(result->cubic_count),"hanim.library");
    return result;
}
std::expected<std::shared_ptr<const detail::ObjectMorphData>,MorphFailure> compile_timeline(
    Input,std::span<const PropertyClip>,MorphOptions,PropertyTimelineLimits);
std::expected<std::shared_ptr<const detail::ObjectMorphData>,MorphFailure>
compile_properties(Input from, std::span<const PropertyTargets> targets, MorphOptions options) {
    const PropertyIndex index(from);const auto writes=resolve_properties(index,targets,options.max_route_segments);
    if(!writes)return std::unexpected(writes.error());
    if(std::ranges::any_of(targets,[](const auto& value){return value.route.has_value() || !value.values.empty();})) {
        if(!options.shape.point_path.linear())return std::unexpected(failure(MorphError::unsupported_property_effect));
        PropertyClip clip;clip.targets.assign(targets.begin(),targets.end());clip.options=options;
        return compile_timeline(std::move(from),std::span(&clip,1),options,{});
    }
    Input to=from;
    for(const auto& write:*writes) {
        auto& part=to.parts[write.part];const auto applied=apply_property({part.local,part.fill},*write.target,write.part);
        if(!applied)return std::unexpected(applied.error());part.local=applied->local;part.fill=applied->fill;
    }
    return compile(std::move(from),std::move(to),options,true);
}
std::expected<MorphOptions,MorphFailure> timeline_options(std::span<const PropertyClip> clips,PropertyTimelineLimits limits) {
    if(!limits.max_clips || limits.max_clips>65536 || !limits.max_channels || limits.max_channels>(1u<<20))
        return std::unexpected(failure(MorphError::invalid_options));
    if(clips.empty() || clips.size()>limits.max_clips)return std::unexpected(failure(MorphError::channel_budget));
    MorphOptions result=clips.front().options;
    for(std::size_t i=0;i<clips.size();++i) {
        const auto& clip=clips[i];const auto& o=clip.options;
        auto refuse=[&](MorphError code)->std::expected<MorphOptions,MorphFailure>{auto e=failure(code);e.where.clip=i;return std::unexpected(e);};
        if(!std::isfinite(clip.start.count()) || clip.start.count()<0 || !std::isfinite(clip.duration.count()) || clip.duration.count()<0 ||
           clip.start.count()>std::numeric_limits<double>::max()-clip.duration.count())return refuse(MorphError::invalid_time);
        if(!clip.easing.valid())return refuse(MorphError::invalid_ease);
        if(clip.source && !*clip.source)return refuse(MorphError::invalid_snapshot);
        if(clip.duration.count()>0 && clip.start.count()+clip.duration.count()==clip.start.count())return refuse(MorphError::invalid_time);
        if(!valid(o) || !o.shape.max_input_segments || o.shape.max_input_segments>4096 ||
           !o.shape.max_output_segments || o.shape.max_output_segments>16384 ||
           o.shape.arc_samples<4 || o.shape.arc_samples>256 || o.shape.alignment_samples<4 || o.shape.alignment_samples>128)
            return refuse(MorphError::invalid_options);
        if(!o.shape.point_path.linear())return refuse(MorphError::unsupported_property_effect);
        result.max_parts=std::min(result.max_parts,o.max_parts);result.max_contours=std::min(result.max_contours,o.max_contours);
        result.max_cubics=std::min(result.max_cubics,o.max_cubics);
        result.max_route_segments=std::min(result.max_route_segments,o.max_route_segments);
        result.shape.max_input_segments=std::min(result.shape.max_input_segments,o.shape.max_input_segments);
        result.shape.max_output_segments=std::min(result.shape.max_output_segments,o.shape.max_output_segments);
        result.shape.arc_samples=std::max(result.shape.arc_samples,o.shape.arc_samples);
        result.shape.alignment_samples=std::max(result.shape.alignment_samples,o.shape.alignment_samples);
    }
    // A property timeline retains source geometry: correspondence direction /
    // phase is irrelevant, while all declared representation limits still hold.
    result.shape.align_direction=false;result.shape.align_start=false;
    result.translation_path={};result.shape.point_path={};return result;
}
std::expected<std::shared_ptr<const detail::ObjectMorphData>,MorphFailure> compile_timeline(
    Input source,std::span<const PropertyClip> clips,MorphOptions options,PropertyTimelineLimits limits) {
    const PropertyIndex index(source);
    auto prepared=compile(source,source,options,true);if(!prepared)return std::unexpected(prepared.error());
    auto data=std::move(*prepared);
    std::vector<std::size_t> order(clips.size()),last(source.parts.size()*4,none),value_last;
    std::iota(order.begin(),order.end(),std::size_t{0});
    std::ranges::stable_sort(order,[&](auto a,auto b){return clips[a].start<clips[b].start;});
    for(const auto clip_index:order) {
        const auto& clip=clips[clip_index];
        data->timeline_duration=std::max(data->timeline_duration,clip.start+clip.duration);
        const auto writes=resolve_properties(index,clip.targets,options.max_route_segments-data->route_segments);
        if(!writes){auto e=writes.error();e.where.clip=clip_index;return std::unexpected(e);}
        for(const auto& write:*writes) {
            const auto& part=source.parts[write.part];PartPose from{part.local,part.fill};
            if(clip.source) {
                const auto ref=clip.source->find(part.id);
                if(!ref){auto e=failure(MorphError::unknown_part,write.part);e.where.clip=clip_index;return std::unexpected(e);}
                const auto captured=clip.source->get(*ref);
                if(!captured){auto e=failure(MorphError::invalid_snapshot,write.part);e.where.clip=clip_index;return std::unexpected(e);}
                from={(*captured)->local,(*captured)->fill};
            }
            for(std::size_t c=0;c<4;++c)if(write.mask&(1u<<c)) {
                const auto previous=last[write.part*4+c];
                PropertyChannelInfo info{write.part,clip_index,static_cast<PropertyChannel>(c),clip.start,clip.duration};
                if(c==3 && (!from.fill || !part.fill))return std::unexpected(channel_failure(MorphError::missing_fill,info));
                if(previous==none)continue;
                const auto& old=data->channels[previous];
                if(clip.start<old.start+old.duration)return std::unexpected(channel_failure(MorphError::property_conflict,info));
                if(!clip.source) {
                    const auto value=sample_channel(old,data->channel_values[previous],clip.start);
                    if(!value)return std::unexpected(value.error());
                    assign_property(from.local,from.fill,info.channel,*value);
                }
            }
            const auto to=apply_property(from,*write.target,write.part);
            if(!to){auto e=to.error();e.where.clip=clip_index;return std::unexpected(e);}
            if(write.target->route)data->route_segments+=write.target->route->path()->segment_count();
            for(std::size_t c=0;c<4;++c)if(write.mask&(1u<<c)) {
                const auto key=write.part*4+c;const auto channel=static_cast<PropertyChannel>(c);
                PropertyChannelInfo info{write.part,clip_index,channel,clip.start,clip.duration,
                    clip.anchor_source || (!clip.source && last[key]!=none)};
                info.easing=clip.easing;
                if(channel==PropertyChannel::translation) {
                    info.translation_path=clip.options.translation_path;
                    info.route=write.target->route;
                    if(info.route)info.route_origin=info.route->path()->point(0);
                }
                info.source_snapshot=clip.source.has_value();
                if(data->channels.size()>=limits.max_channels)return std::unexpected(channel_failure(MorphError::channel_budget,info));
                last[key]=data->channels.size();data->channels.push_back(info);
                data->channel_values.push_back({property_value(from,channel),property_value(*to,channel)});
            }
            // The part's value channels: one slot a part and channel; each write
            // goes on from where the channel is (the last write's end, the
            // frame continued from, else its rest).
            for(const auto& v:write.target->values) {
                std::size_t slot=none;
                for(std::size_t k=0;k<data->value_slots.size();++k)
                    if(data->value_slots[k].part==write.part && data->value_slots[k].id.name==v.id.name)slot=k;
                if(slot==none) {
                    slot=data->value_slots.size();
                    ValueSlot added{write.part,v.id,data->value_count,v.rest};
                    for(const auto& carried:source.values)if(carried.part==part.id && carried.id.name==v.id.name) {
                        // A channel is its name and its lanes, through every segment.
                        if(!(carried.id==v.id)){auto e=failure(MorphError::invalid_property,write.part);e.where.clip=clip_index;return std::unexpected(e);}
                        added.rest=carried.lanes;
                    }
                    data->value_slots.push_back(added);data->value_count+=v.id.lanes;value_last.push_back(none);
                }else if(!(data->value_slots[slot].id==v.id)){auto e=failure(MorphError::invalid_property,write.part);e.where.clip=clip_index;return std::unexpected(e);}
                PropertyChannelInfo info{write.part,clip_index,PropertyChannel::value,clip.start,clip.duration,
                    clip.anchor_source || (!clip.source && value_last[slot]!=none)};
                info.easing=clip.easing;info.value_slot=slot;info.source_snapshot=clip.source.has_value();
                detail::PropertyValue from_value;from_value.lanes=data->value_slots[slot].rest;
                if(value_last[slot]!=none) {
                    const auto& old=data->channels[value_last[slot]];
                    if(clip.start<old.start+old.duration)return std::unexpected(channel_failure(MorphError::property_conflict,info));
                    if(!clip.source) {
                        const auto value=sample_channel(old,data->channel_values[value_last[slot]],clip.start);
                        if(!value)return std::unexpected(value.error());
                        from_value.lanes=value->lanes;
                    }
                }
                detail::PropertyValue to_value=from_value;
                for(std::size_t i=0;i<v.id.lanes;++i)to_value.lanes[i]=v.relative?from_value.lanes[i]+v.lanes[i]:v.lanes[i];
                if(!std::ranges::all_of(to_value.lanes,[](double x){return std::isfinite(x);}))
                    return std::unexpected(channel_failure(MorphError::nonfinite_transform,info));
                if(data->channels.size()>=limits.max_channels)return std::unexpected(channel_failure(MorphError::channel_budget,info));
                value_last[slot]=data->channels.size();data->channels.push_back(info);
                data->channel_values.push_back({from_value,to_value});
            }
        }
    }
    // A value written before this timeline holds through it: the frame it
    // continues from carries it, unwritten here, at where it was left.
    for(const auto& carried:source.values) {
        const auto owner=std::ranges::find(source.parts,carried.part,&PartInput::id);
        if(owner==source.parts.end())continue;
        const auto part=static_cast<std::size_t>(owner-source.parts.begin());
        if(std::ranges::any_of(data->value_slots,[&](const ValueSlot& slot){return slot.part==part && slot.id.name==carried.id.name;}))continue;
        data->value_slots.push_back({part,carried.id,data->value_count,carried.lanes});data->value_count+=carried.id.lanes;
    }
    // One run per part/channel. Sampling finds just its current segment rather
    // than replaying every earlier writer or allocating a mutable pose cache.
    std::vector<std::size_t> sorted(data->channels.size());std::iota(sorted.begin(),sorted.end(),std::size_t{0});
    std::ranges::stable_sort(sorted,[&](auto a,auto b){
        const auto& x=data->channels[a];const auto& y=data->channels[b];
        return x.part!=y.part?x.part<y.part:x.channel!=y.channel?x.channel<y.channel:x.value_slot!=y.value_slot?x.value_slot<y.value_slot:x.start<y.start;
    });
    std::vector<PropertyChannelInfo> infos;std::vector<detail::PropertyEndpoints> values;
    infos.reserve(sorted.size());values.reserve(sorted.size());
    for(auto i:sorted) {
        const auto& info=data->channels[i];
        if(infos.empty() || infos.back().part!=info.part || infos.back().channel!=info.channel || infos.back().value_slot!=info.value_slot)
            data->channel_runs.push_back({infos.size(),0});
        ++data->channel_runs.back().count;infos.push_back(info);values.push_back(data->channel_values[i]);
    }
    data->channels=std::move(infos);data->channel_values=std::move(values);
    mlibs::trace::count("property.channels",static_cast<double>(data->channels.size()),"hanim.library");
    diagnostic.info("prepared {} property channel segments",data->channels.size());
    return std::shared_ptr<const detail::ObjectMorphData>(std::move(data));
}
// Moves each deformer's points by its channel's amount (its rest without
// one), each in proportion to its frozen weight.
void apply_deformers(const detail::ObjectMorphData& data, std::span<ShapeCubic> cubics, std::span<const double> values) noexcept {
    constexpr double largest = 1e100;
    for (const auto& deformation:data.deformations) {
        const auto& deformer=data.deformers[deformation.part][deformation.deformer];
        const std::uint8_t lanes=deformer.kind==DeformKind::rotate?1:2;
        std::array<double,2> amount=deformer.kind==DeformKind::scale?std::array<double,2>{1,1}:std::array<double,2>{0,0};
        for (const auto& slot:data.value_slots)
            if (slot.part==deformation.part && slot.id.name==deformer.channel && slot.id.lanes==lanes)
                for (std::size_t lane=0;lane<lanes;++lane) amount[lane]=values[slot.offset+lane];
        for (auto& lane:amount) lane=std::clamp(lane,-largest,largest);
        const auto anchor=deformer.anchor;
        const auto* weight=deformation.weights.data();
        for (auto& cubic:cubics.subspan(deformation.first_cubic,deformation.cubic_count))
            for (auto* point:{&cubic.start,&cubic.control1,&cubic.control2,&cubic.end}) {
                const double share=*weight++;
                if (share==0) continue;
                switch (deformer.kind) {
                    case DeformKind::displace: point->x+=share*amount[0]; point->y+=share*amount[1]; break;
                    case DeformKind::scale:
                        point->x=anchor.x+(point->x-anchor.x)*(1+share*(amount[0]-1));
                        point->y=anchor.y+(point->y-anchor.y)*(1+share*(amount[1]-1));
                        break;
                    case DeformKind::rotate: {
                        const double angle=share*amount[0], cosine=mlibs::math::det::cos(angle), sine=mlibs::math::det::sin(angle);
                        const double across=point->x-anchor.x, down=point->y-anchor.y;
                        point->x=anchor.x+across*cosine-down*sine; point->y=anchor.y+across*sine+down*cosine;
                        break;
                    }
                }
            }
        // A contour's seams stay shared: each start is the previous end, moved alike.
    }
}
std::size_t current_channel(const detail::ObjectMorphData& data,detail::ChannelRun run,seconds time) {
    const auto begin=data.channels.begin()+static_cast<std::ptrdiff_t>(run.first);
    const auto after=std::ranges::upper_bound(begin,begin+static_cast<std::ptrdiff_t>(run.count),time,{},&PropertyChannelInfo::start);
    return after==begin?none:static_cast<std::size_t>((after-1)-data.channels.begin());
}
}

std::string_view property_channel_name(PropertyChannel channel) noexcept {
    switch(channel) {
        case PropertyChannel::translation:return "translation";
        case PropertyChannel::rotation:return "rotation";
        case PropertyChannel::scale:return "scale";
        case PropertyChannel::fill:return "fill";
        case PropertyChannel::value:return "value";
    }return "unknown";
}
std::string describe_where(const MorphWhere& where) {
    std::string out;
    const auto add=[&](std::string_view what,std::size_t at){if(at!=none)out+=std::format("{}{} {}",out.empty()?"":", ",what,at);};
    add("part",where.part);add("contour",where.contour);add("clip",where.clip);
    if(where.channel)out+=std::format("{}channel {}",out.empty()?"":", ",property_channel_name(*where.channel));
    return out;
}
std::expected<PreparedObjectAnimation,MorphFailure> PreparedObjectAnimation::prepare(Snapshot from, Snapshot to, MorphOptions options) {
    mlibs::trace::Span span("morph.prepare","hanim.library");
    if (!valid(options)) return std::unexpected(failure(MorphError::invalid_options));
    auto a=snapshot_input(from,options); if (!a) return std::unexpected(a.error());
    auto b=snapshot_input(to,options); if (!b) return std::unexpected(b.error());
    auto compiled=compile(std::move(*a),std::move(*b),options);
    if (!compiled) { diagnostic.debug("morph refused: {}",describe(compiled.error().code).name); return std::unexpected(compiled.error()); }
    PreparedObjectAnimation result; result.source_=from.object(); result.revision_=from.revision();
    result.from_=std::move(from); result.to_=std::move(to); result.data_=std::move(*compiled); return result;
}
std::expected<PreparedObjectAnimation,MorphFailure> PreparedObjectAnimation::retarget(FrameView current, Snapshot to, MorphOptions options) {
    mlibs::trace::Span span("morph.retarget","hanim.library");
    if (!valid(options)) return std::unexpected(failure(MorphError::invalid_options));
    auto a=frame_input(current,options); if (!a) return std::unexpected(a.error());
    auto b=snapshot_input(to,options); if (!b) return std::unexpected(b.error());
    auto compiled=compile(std::move(*a),std::move(*b),options);
    if (!compiled) { diagnostic.debug("retarget refused: {}",describe(compiled.error().code).name); return std::unexpected(compiled.error()); }
    PreparedObjectAnimation result; result.source_=current.source_object; result.revision_=current.source_revision;
    result.to_=std::move(to); result.data_=std::move(*compiled); return result;
}
std::expected<PreparedObjectAnimation,MorphFailure> PreparedObjectAnimation::properties(
    Snapshot from, const PropertyTargets& values, MorphOptions options) {
    return properties(std::move(from),std::span(&values,1),options);
}
std::expected<PreparedObjectAnimation,MorphFailure> PreparedObjectAnimation::properties(
    FrameView current, const PropertyTargets& values, MorphOptions options) {
    return properties(current,std::span(&values,1),options);
}
std::expected<PreparedObjectAnimation,MorphFailure> PreparedObjectAnimation::properties(
    Snapshot from, std::span<const PropertyTargets> values, MorphOptions options) {
    mlibs::trace::Span span("properties.prepare","hanim.library");
    if (!valid(options)) return std::unexpected(failure(MorphError::invalid_options));
    auto input=snapshot_input(from,options);if(!input)return std::unexpected(input.error());
    auto compiled=compile_properties(std::move(*input),values,options);if(!compiled)return std::unexpected(compiled.error());
    PreparedObjectAnimation result;result.source_=from.object();result.revision_=from.revision();
    result.from_=std::move(from);result.data_=std::move(*compiled);return result;
}
std::expected<PreparedObjectAnimation,MorphFailure> PreparedObjectAnimation::properties(
    FrameView current, std::span<const PropertyTargets> values, MorphOptions options) {
    mlibs::trace::Span span("properties.retarget","hanim.library");
    if (!valid(options)) return std::unexpected(failure(MorphError::invalid_options));
    auto input=frame_input(current,options);if(!input)return std::unexpected(input.error());
    auto compiled=compile_properties(std::move(*input),values,options);if(!compiled)return std::unexpected(compiled.error());
    PreparedObjectAnimation result;result.source_=current.source_object;result.revision_=current.source_revision;
    result.data_=std::move(*compiled);return result;
}
std::size_t PreparedObjectAnimation::route_segment_count() const noexcept {return data_?data_->route_segments:0;}
FrameRequirements PreparedObjectAnimation::requirements() const noexcept {
    return data_ ? FrameRequirements{data_->parts.size(),data_->contours.size(),data_->cubic_count,data_->value_count} : FrameRequirements{};
}
std::expected<PreparedObjectAnimation,MorphFailure> PreparedObjectAnimation::property_timeline(
    Snapshot from,std::span<const PropertyClip> clips,PropertyTimelineLimits limits) {
    mlibs::trace::Span span("property-timeline.prepare","hanim.library");
    auto options=timeline_options(clips,limits);if(!options)return std::unexpected(options.error());
    auto input=snapshot_input(from,*options);if(!input)return std::unexpected(input.error());
    auto data=compile_timeline(std::move(*input),clips,*options,limits);if(!data)return std::unexpected(data.error());
    PreparedObjectAnimation result;result.source_=from.object();result.revision_=from.revision();
    result.from_=std::move(from);result.data_=std::move(*data);return result;
}
std::expected<PreparedObjectAnimation,MorphFailure> PreparedObjectAnimation::property_timeline(
    FrameView current,std::span<const PropertyClip> clips,PropertyTimelineLimits limits) {
    mlibs::trace::Span span("property-timeline.retarget","hanim.library");
    auto options=timeline_options(clips,limits);if(!options)return std::unexpected(options.error());
    auto input=frame_input(current,*options);if(!input)return std::unexpected(input.error());
    auto data=compile_timeline(std::move(*input),clips,*options,limits);if(!data)return std::unexpected(data.error());
    PreparedObjectAnimation result;result.source_=current.source_object;result.revision_=current.source_revision;
    result.data_=std::move(*data);return result;
}
std::span<const ValueSlot> PreparedObjectAnimation::value_slots() const & noexcept {
    return data_?std::span<const ValueSlot>(data_->value_slots):std::span<const ValueSlot>{};
}
std::span<const PropertyChannelInfo> PreparedObjectAnimation::channels() const & noexcept {
    return data_?std::span<const PropertyChannelInfo>(data_->channels):std::span<const PropertyChannelInfo>{};
}
seconds PreparedObjectAnimation::timeline_duration() const noexcept {return data_?data_->timeline_duration:seconds(0);}
std::span<const MorphPartInfo> PreparedObjectAnimation::parts() const & noexcept { return data_ ? std::span<const MorphPartInfo>(data_->parts) : std::span<const MorphPartInfo>{}; }
std::span<const MorphContourInfo> PreparedObjectAnimation::contours() const & noexcept { return data_ ? std::span<const MorphContourInfo>(data_->contours) : std::span<const MorphContourInfo>{}; }
std::expected<FrameView,MorphFailure> PreparedObjectAnimation::sample(double progress,FrameBuffer output) const noexcept {
    return sample_at(progress,output,false);
}
std::expected<FrameView,MorphFailure> PreparedObjectAnimation::sample_source(FrameBuffer output) const noexcept {
    return sample_at(0,output,true);
}
std::expected<FrameView,MorphFailure> PreparedObjectAnimation::sample_at(double progress, FrameBuffer output,bool exact_source) const noexcept {
    mlibs::trace::Span span("morph.sample","hanim.library");
    if (!data_) return std::unexpected(failure(MorphError::unprepared));
    if (!std::isfinite(progress)) return std::unexpected(failure(MorphError::invalid_progress));
    const auto needed=requirements();
    if (output.parts.size()<needed.parts || output.cubics.size()<needed.cubics || output.values.size()<needed.values)
        return std::unexpected(failure(MorphError::output_too_small));
    const double u=std::clamp(progress,0.0,1.0);
    if(!data_->channels.empty()) {
        const auto time=seconds(u==1?data_->timeline_duration.count():u*data_->timeline_duration.count());
        // Progress past an end (an overshooting curve) reaches the clips that
        // span the whole plan; the others hold at their ends.
        const auto beyond=[&](const PropertyChannelInfo& info)->std::optional<double> {
            if(progress==u || info.start.count()!=0 || info.duration!=data_->timeline_duration)return std::nullopt;
            return progress;
        };
        if(!exact_source)for(const auto run:data_->channel_runs) {
            const auto i=current_channel(*data_,run,time);if(i==none)continue;
            const auto value=sample_channel(data_->channels[i],data_->channel_values[i],time,beyond(data_->channels[i]));
            if(!value)return std::unexpected(value.error());
        }
        for(std::size_t i=0;i<needed.parts;++i)
            output.parts[i]={data_->parts[i],data_->states[i].from,data_->states[i].from_fill,data_->states[i].from_stroke};
        for(const auto& slot:data_->value_slots)
            std::copy_n(slot.rest.begin(),slot.id.lanes,output.values.begin()+static_cast<std::ptrdiff_t>(slot.offset));
        if(!exact_source)for(const auto run:data_->channel_runs) {
            const auto i=current_channel(*data_,run,time);if(i==none)continue;
            const auto& info=data_->channels[i];const auto value=sample_channel(info,data_->channel_values[i],time,beyond(info));
            if(info.channel==PropertyChannel::value) {
                const auto& slot=data_->value_slots[info.value_slot];
                std::copy_n(value->lanes.begin(),slot.id.lanes,output.values.begin()+static_cast<std::ptrdiff_t>(slot.offset));
                continue;
            }
            auto& part=output.parts[info.part];assign_property(part.local,part.fill,info.channel,*value);
        }
        std::ranges::copy(data_->static_cubics,output.cubics.begin());
        apply_deformers(*data_,output.cubics,output.values);
        return FrameView{output.parts.first(needed.parts),data_->contours,output.cubics.first(needed.cubics),u,source_,revision_,identity(),
                         output.values.first(needed.values),data_->value_slots};
    }
    // A property plan's transforms overshoot with the curve; a morph's geometry
    // and every fill stay within the ends.
    const double moved=data_->static_cubics.empty()?u:progress;
    auto transform = [&](const detail::MorphEndpoints& state) {
        auto value=lerp(state.from,state.to,moved);
        if (!data_->translation_path.linear()) {
            const auto position=data_->translation_path.interpolate({state.from.tx,state.from.ty},{state.to.tx,state.to.ty},u);
            value.tx=position.x; value.ty=position.y;
        }
        return value;
    };
    // Validate every potentially overflowing transform before publishing any
    // part or cubic. Color lanes are bounded by the object/frame contract;
    // core shape plans have already proved the numeric geometry domain.
    for (std::size_t i=0;i<data_->states.size();++i)
        if (!finite(transform(data_->states[i]))) return std::unexpected(failure(MorphError::nonfinite_transform,i));
    for (std::size_t i=0;i<needed.parts;++i) {
        const auto& state=data_->states[i];
        output.parts[i]={data_->parts[i],transform(state),state.from_fill ? std::optional<Rgba>(lerp(*state.from_fill,*state.to_fill,u)) : std::nullopt,
                         state.from_stroke ? std::optional<PartStroke>(PartStroke{lerp(state.from_stroke->color,state.to_stroke->color,u),
                                                                                  lerp(state.from_stroke->width,state.to_stroke->width,u)})
                                           : std::nullopt};
    }
    if (!data_->static_cubics.empty()) std::ranges::copy(data_->static_cubics,output.cubics.begin());
    else for (std::size_t i=0;i<data_->shapes.size();++i) {
        const auto& range=data_->contours[i];
        const auto sampled=data_->shapes[i].sample(u,output.cubics.subspan(range.first_cubic,range.cubic_count));
        // With a private prepared shape, finite clamped u and its exact
        // prechecked span this cannot fail; a violation is an internal bug,
        // never a partially returned successful/failed frame.
        mlibs::log::invariant(sampled.has_value(),"prepared object morph shape invariant");
    }
    return FrameView{output.parts.first(needed.parts),data_->contours,output.cubics.first(needed.cubics),u,source_,revision_,identity()};
}
} // namespace hanim
