module hanim.library;

import std;
import hanim.core;
import openmui.log;
import openmui.trace;

namespace hanim::detail {
struct MorphEndpoints { Transform2D from, to; std::optional<Rgba> from_fill, to_fill; };
struct ObjectMorphData {
    std::vector<std::string> ids;
    std::vector<MorphPartInfo> parts;
    std::vector<MorphContourInfo> contours;
    std::vector<MorphEndpoints> states;
    std::vector<PreparedShapeMorph> shapes;
    std::size_t cubic_count = 0;
};
}
namespace hanim {
namespace {
constexpr omui::Log diagnostic{"hanim.library"};
constexpr auto none = std::numeric_limits<std::size_t>::max();
MorphFailure failure(MorphError code, std::size_t part = none, std::size_t contour = none,
                     std::optional<ShapeMorphError> shape = {}) { return {code,part,contour,shape}; }
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
bool valid(MorphOptions options) noexcept {
    // These are aggregate preparation limits, not per-frame preallocations.
    return options.max_parts && options.max_parts <= (1u<<16) &&
        options.max_contours && options.max_contours <= (1u<<16) &&
        options.max_cubics && options.max_cubics <= (1u<<20);
}
struct CurveInput { ShapeContour view; std::vector<ShapeSegment> owned; };
struct PartInput {
    std::string id;
    PartRef ref;
    std::uint32_t parent = invalid_object_slot;
    std::vector<std::uint32_t> children;
    Transform2D local;
    std::optional<Rgba> fill;
    std::vector<CurveInput> contours;
};
struct Input { std::vector<PartInput> parts; };

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
        PartInput item; item.id=part.id; item.ref=part.ref; item.parent=visit.parent; item.local=part.local; item.fill=part.fill;
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
        if (!info.from_ref || info.from_ref.object!=frame.source_object || info.id.empty() || !finite(part.local) || !valid(part.fill) ||
            info.first_contour!=first_contour || first_contour>frame.contours.size() || info.contour_count>frame.contours.size()-first_contour)
            return std::unexpected(failure(MorphError::invalid_frame,i));
        if (info.parent==invalid_object_slot) { ++roots; root=static_cast<std::uint32_t>(i); }
        else if (info.parent>=frame.parts.size() || info.parent==i) return std::unexpected(failure(MorphError::invalid_frame,i));
        PartInput item; item.id=info.id; item.ref=info.from_ref; item.parent=info.parent; item.local=part.local; item.fill=part.fill;
        item.contours.reserve(info.contour_count);
        for (std::size_t j=0;j<info.contour_count;++j) {
            const auto& range=frame.contours[first_contour+j];
            if (range.part!=i || range.ordinal!=j || range.first_cubic!=first_cubic || !range.cubic_count ||
                first_cubic>frame.cubics.size() || range.cubic_count>frame.cubics.size()-first_cubic)
                return std::unexpected(failure(MorphError::invalid_frame,i,j));
            const auto cubics=frame.cubics.subspan(first_cubic,range.cubic_count);
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
    return result;
}

std::expected<std::shared_ptr<const detail::ObjectMorphData>,MorphFailure>
compile(Input from, Input to, MorphOptions options) {
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
    auto result=std::make_shared<detail::ObjectMorphData>();
    result->ids.reserve(count); result->parts.reserve(count); result->states.reserve(count);
    for (std::size_t i=0;i<count;++i) {
        const auto& x=from.parts[i]; const auto& y=to.parts[matched[i]];
        if ((x.parent==invalid_object_slot)!=(y.parent==invalid_object_slot) ||
            (x.parent!=invalid_object_slot && from.parts[x.parent].id!=to.parts[y.parent].id))
            return std::unexpected(failure(MorphError::hierarchy_mismatch,i));
        if (x.children.size()!=y.children.size()) return std::unexpected(failure(MorphError::hierarchy_mismatch,i));
        for (std::size_t child=0;child<x.children.size();++child)
            if (from.parts[x.children[child]].id!=to.parts[y.children[child]].id)
                return std::unexpected(failure(MorphError::ordering_mismatch,i));
        if (x.contours.size()!=y.contours.size()) return std::unexpected(failure(MorphError::contour_count_mismatch,i));
        if (x.fill.has_value()!=y.fill.has_value() || (!x.contours.empty() && !x.fill))
            return std::unexpected(failure(MorphError::missing_fill,i));
        result->ids.push_back(x.id);
        result->parts.push_back({{},x.ref,y.ref,x.parent,result->contours.size(),x.contours.size()});
        result->states.push_back({x.local,y.local,x.fill,y.fill});
        std::optional<bool> reversed;
        for (std::size_t j=0;j<x.contours.size();++j) {
            if (!x.contours[j].view.closed || !y.contours[j].view.closed) return std::unexpected(failure(MorphError::open_contour,i,j));
            const auto remaining=options.max_cubics-result->cubic_count;
            if (!remaining) return std::unexpected(failure(MorphError::cubic_budget,i,j));
            auto shape=PreparedShapeMorph::prepare(x.contours[j].view,y.contours[j].view,options.shape);
            if (!shape) return std::unexpected(failure(MorphError::shape_failure,i,j,shape.error()));
            if (shape->segment_count()>remaining) return std::unexpected(failure(MorphError::cubic_budget,i,j));
            // Independent winding flips can change a compound nonzero fill.
            // Uniform reversal of all target contours preserves that fill;
            // otherwise this first recipe refuses rather than inventing holes.
            if (reversed && *reversed!=shape->target_reversed())
                return std::unexpected(failure(MorphError::unsupported_topology,i,j));
            reversed=shape->target_reversed();
            result->contours.push_back({static_cast<std::uint32_t>(i),j,result->cubic_count,shape->segment_count()});
            result->cubic_count+=shape->segment_count(); result->shapes.push_back(std::move(*shape));
        }
    }
    for (std::size_t i=0;i<count;++i) result->parts[i].id=result->ids[i];
    omui::trace::count("morph.parts",static_cast<double>(count),"hanim.library");
    omui::trace::count("morph.cubics",static_cast<double>(result->cubic_count),"hanim.library");
    return std::shared_ptr<const detail::ObjectMorphData>(std::move(result));
}
}

std::string_view morph_error_name(MorphError error) noexcept {
    switch(error) {
        case MorphError::unprepared:return "unprepared";case MorphError::invalid_snapshot:return "invalid-snapshot";
        case MorphError::invalid_frame:return "invalid-frame";case MorphError::invalid_options:return "invalid-options";
        case MorphError::part_budget:return "part-budget";case MorphError::contour_budget:return "contour-budget";
        case MorphError::cubic_budget:return "cubic-budget";case MorphError::part_set_mismatch:return "part-set-mismatch";
        case MorphError::hierarchy_mismatch:return "hierarchy-mismatch";case MorphError::ordering_mismatch:return "ordering-mismatch";
        case MorphError::contour_count_mismatch:return "contour-count-mismatch";case MorphError::missing_fill:return "missing-fill";
        case MorphError::open_contour:return "open-contour";case MorphError::unsupported_topology:return "unsupported-topology";
        case MorphError::shape_failure:return "shape-failure";
        case MorphError::invalid_progress:return "invalid-progress";case MorphError::output_too_small:return "output-too-small";
        case MorphError::nonfinite_transform:return "nonfinite-transform";
    }
    return "unprepared";
}
std::expected<PreparedObjectMorph,MorphFailure> PreparedObjectMorph::prepare(Snapshot from, Snapshot to, MorphOptions options) {
    omui::trace::Span span("morph.prepare","hanim.library");
    if (!valid(options)) return std::unexpected(failure(MorphError::invalid_options));
    auto a=snapshot_input(from,options); if (!a) return std::unexpected(a.error());
    auto b=snapshot_input(to,options); if (!b) return std::unexpected(b.error());
    auto compiled=compile(std::move(*a),std::move(*b),options);
    if (!compiled) { diagnostic.debug("morph refused: {}",morph_error_name(compiled.error().code)); return std::unexpected(compiled.error()); }
    PreparedObjectMorph result; result.source_=from.object(); result.revision_=from.revision();
    result.from_=std::move(from); result.to_=std::move(to); result.data_=std::move(*compiled); return result;
}
std::expected<PreparedObjectMorph,MorphFailure> PreparedObjectMorph::retarget(FrameView current, Snapshot to, MorphOptions options) {
    omui::trace::Span span("morph.retarget","hanim.library");
    if (!valid(options)) return std::unexpected(failure(MorphError::invalid_options));
    auto a=frame_input(current,options); if (!a) return std::unexpected(a.error());
    auto b=snapshot_input(to,options); if (!b) return std::unexpected(b.error());
    auto compiled=compile(std::move(*a),std::move(*b),options);
    if (!compiled) { diagnostic.debug("retarget refused: {}",morph_error_name(compiled.error().code)); return std::unexpected(compiled.error()); }
    PreparedObjectMorph result; result.source_=current.source_object; result.revision_=current.source_revision;
    result.to_=std::move(to); result.data_=std::move(*compiled); return result;
}
FrameRequirements PreparedObjectMorph::requirements() const noexcept {
    return data_ ? FrameRequirements{data_->parts.size(),data_->contours.size(),data_->cubic_count} : FrameRequirements{};
}
std::span<const MorphPartInfo> PreparedObjectMorph::parts() const & noexcept { return data_ ? std::span<const MorphPartInfo>(data_->parts) : std::span<const MorphPartInfo>{}; }
std::span<const MorphContourInfo> PreparedObjectMorph::contours() const & noexcept { return data_ ? std::span<const MorphContourInfo>(data_->contours) : std::span<const MorphContourInfo>{}; }
std::expected<FrameView,MorphFailure> PreparedObjectMorph::sample(double progress, FrameBuffer output) const noexcept {
    omui::trace::Span span("morph.sample","hanim.library");
    if (!data_) return std::unexpected(failure(MorphError::unprepared));
    if (!std::isfinite(progress)) return std::unexpected(failure(MorphError::invalid_progress));
    const auto needed=requirements();
    if (output.parts.size()<needed.parts || output.cubics.size()<needed.cubics) return std::unexpected(failure(MorphError::output_too_small));
    const double u=std::clamp(progress,0.0,1.0);
    // Validate every potentially overflowing transform before publishing any
    // part or cubic. Color lanes are bounded by the object/frame contract;
    // core shape plans have already proved the numeric geometry domain.
    for (std::size_t i=0;i<data_->states.size();++i)
        if (!finite(lerp(data_->states[i].from,data_->states[i].to,u))) return std::unexpected(failure(MorphError::nonfinite_transform,i));
    for (std::size_t i=0;i<needed.parts;++i) {
        const auto& state=data_->states[i];
        output.parts[i]={data_->parts[i],lerp(state.from,state.to,u),state.from_fill ? std::optional<Rgba>(lerp(*state.from_fill,*state.to_fill,u)) : std::nullopt};
    }
    for (std::size_t i=0;i<data_->shapes.size();++i) {
        const auto& range=data_->contours[i];
        const auto sampled=data_->shapes[i].sample(u,output.cubics.subspan(range.first_cubic,range.cubic_count));
        // With a private prepared shape, finite clamped u and its exact
        // prechecked span this cannot fail; a violation is an internal bug,
        // never a partially returned successful/failed frame.
        omui::invariant(sampled.has_value(),"prepared object morph shape invariant");
    }
    return FrameView{output.parts.first(needed.parts),data_->contours,output.cubics.first(needed.cubics),u,source_,revision_,identity()};
}
} // namespace hanim
