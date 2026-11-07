module hanim.program;
import std;
import mlibs.log;
import mlibs.trace;

namespace hanim::detail {
struct ProgramNode {
    ProgramKind kind = ProgramKind::empty;
    ProgramBinding binding;
    std::optional<ProgramInputs> inputs;
    CompositeSpec scope;
    std::vector<Program> children;
    seconds step{0};
    seconds wait_duration{0};
    std::size_t repeat_count=0;
    std::unique_ptr<const ProgramFailure> failure;
};
struct ProgramData {
    std::vector<ProgramTrack> tracks;
    std::vector<PreparedSegment> segments;
    std::vector<ProgramNodeInfo> nodes;
    std::vector<PreparedScope> scopes;
    std::size_t scope_parameters = 0;
    std::size_t property_channels = 0;
    std::size_t route_segments = 0;
    seconds duration{0};
};
}
namespace hanim {
namespace {
constexpr mlibs::log::Log diagnostic{"hanim.program"};
ProgramFailure failure(ProgramError code, std::size_t node = invalid_program_index,
    std::size_t track = invalid_program_index, std::size_t segment = invalid_program_index) noexcept {
    return {.code=code,.where={.node=node,.track=track,.segment=segment}};
}
ProgramFailure animation_failure(MorphFailure error, std::size_t node, std::size_t track, std::size_t segment) noexcept {
    auto result=failure(ProgramError::animation,node,track,segment);result.because(error);return result;
}
ProgramFailure effect_failure(EffectError error, std::size_t node, std::size_t track, std::size_t segment) noexcept {
    auto result=failure(ProgramError::effect,node,track,segment);result.because(error);return result;
}
bool valid_limits(ProgramLimits limits) noexcept {
    return limits.max_nodes && limits.max_nodes<=65536 && limits.max_depth && limits.max_depth<=256 &&
        limits.max_tracks && limits.max_tracks<=65536 && limits.max_segments && limits.max_segments<=65536 &&
        limits.max_total_parts && limits.max_scopes<=65536 && limits.max_total_property_channels<=(1u<<20) && limits.max_total_route_segments<=(1u<<20);
}
std::optional<ProgramError> invalid_options(SegmentOptions options) noexcept {
    if (!std::isfinite(options.duration.count()) || options.duration.count()<0) return ProgramError::invalid_time;
    if (!options.easing.valid()) return ProgramError::invalid_ease;
    return {};
}
std::expected<double,ProgramFailure> sum(double a,double b,std::size_t node) noexcept {
    if (b>std::numeric_limits<double>::max()-a) return std::unexpected(failure(ProgramError::invalid_time,node));
    return a+b;
}
struct Branch { std::size_t node, child; ProgramKind kind; };
struct Leaf {
    const ProgramBinding* binding=nullptr;
    Snapshot from, to;
    std::size_t node=0,track=0;
    std::string path;
    seconds start{0};
    std::vector<Branch> branches;
    std::size_t scope=invalid_program_index;
};
struct ScopeInput { const CompositeSpec* spec; PreparedScope prepared; };
struct Collect {
    ProgramLimits limits;
    const ObjectRegistry* registry = nullptr;
    std::vector<Leaf> leaves;
    std::size_t nodes=0;
    std::vector<ProgramNodeInfo> hierarchy;
    std::vector<ScopeInput> scopes;
    std::expected<Snapshot,ProgramFailure> capture(const ObjectInput& input,std::size_t node) const {
        if(const auto* snapshot=std::get_if<Snapshot>(&input))return *snapshot;
        if(!registry)return std::unexpected(failure(ProgramError::unresolved_object,node));
        auto snapshot=registry->capture(std::get<ObjectRef>(input));
        if(!snapshot) {
            auto error=failure(ProgramError::object,node);error.because(snapshot.error());
            return std::unexpected(error);
        }
        return *snapshot;
    }
    std::expected<double,ProgramFailure> visit(const Program& value,double start,std::size_t depth,
                                              std::string path,std::vector<Branch>& branches,
                                              std::size_t parent=invalid_program_index,std::size_t scope=invalid_program_index) {
        const auto node=nodes++;
        if(node>=limits.max_nodes)return std::unexpected(failure(ProgramError::node_budget,node));
        if(depth>limits.max_depth)return std::unexpected(failure(ProgramError::depth_budget,node));
        if(!value)return std::unexpected(failure(ProgramError::empty_program,node));
        if(const auto* authored=value.failure()) {
            auto contextual=*authored;contextual.where.node=node;
            return std::unexpected(std::move(contextual));
        }
        hierarchy.push_back({.kind=value.kind(),.parent=parent,.subtree_end=node+1,.start=seconds(start),.scope=scope});
        if(value.kind()==ProgramKind::composite) {
            const auto* spec=value.scope();
            if(scopes.size()>=limits.max_scopes) {
                auto error=failure(ProgramError::scope_budget,node);error.where.scope=scopes.size();return std::unexpected(error);
            }
            if(const auto invalid=invalid_options({seconds(0),spec->options.effect_easing}))
                return std::unexpected(failure(*invalid,node));
            const auto next=scopes.size();
            scopes.push_back({spec,{.node=node,.parent=scope,.depth=scope==invalid_program_index?1:scopes[scope].prepared.depth+1,
                .start=seconds(start),.effect_easing=spec->options.effect_easing}});
            scope=next;hierarchy[node].scope=scope;
        }
        if(value.kind()==ProgramKind::wait) {
            const auto length=value.wait_duration();
            if(const auto invalid=invalid_options({length,ease::linear}))
                return std::unexpected(failure(*invalid,node));
            const auto end=sum(start,length.count(),node);
            if(!end)return std::unexpected(end.error());
            if(length.count()>0 && *end==start)return std::unexpected(failure(ProgramError::invalid_time,node));
            hierarchy[node].duration=length;
            return *end;
        }
        if(value.kind()==ProgramKind::binding) {
            const auto* binding=value.binding();
            if(!binding)return std::unexpected(failure(ProgramError::invalid_snapshot,node));
            if(binding->source>ProgramSource::snapshot)return std::unexpected(failure(ProgramError::invalid_source,node));
            if(!binding->properties && !binding->additional_properties.empty())
                return std::unexpected(failure(ProgramError::invalid_snapshot,node));
            if(const auto invalid=invalid_options(binding->options))return std::unexpected(failure(*invalid,node));
            const auto end=sum(start,binding->options.duration.count(),node);
            if(!end)return std::unexpected(end.error());
            if(binding->options.duration.count()>0 && *end==start)
                return std::unexpected(failure(ProgramError::invalid_time,node));
            if(leaves.size()>=limits.max_segments)return std::unexpected(failure(ProgramError::segment_budget,node));
            Snapshot from=binding->from,to=binding->to;
            if(const auto* inputs=value.inputs()) {
                auto first=capture(inputs->from,node);if(!first)return std::unexpected(first.error());
                auto last=binding->properties ? first : capture(inputs->to,node);if(!last)return std::unexpected(last.error());
                from=std::move(*first);to=std::move(*last);
            }
            if(!from || !to)return std::unexpected(failure(ProgramError::invalid_snapshot,node));
            leaves.push_back({binding,std::move(from),std::move(to),node,0,std::move(path),seconds(start),branches,scope});
            hierarchy[node].duration=binding->options.duration;
            return *end;
        }
        const auto children=value.children();
        if(children.empty())return std::unexpected(failure(ProgramError::empty_program,node));
        const bool repeating=value.kind()==ProgramKind::repeat;
        const auto count=repeating?value.repeat_count():children.size();
        if(repeating && count==0)return std::unexpected(failure(ProgramError::invalid_repeat,node));
        hierarchy[node].repeat_count=repeating?count:0;
        const auto step=value.step().count();
        if(!std::isfinite(step) || step<0)return std::unexpected(failure(ProgramError::invalid_time,node));
        // Return absolute ends and pass that exact value to serial successors.
        // Re-associating start + (local offset + duration) can otherwise make
        // adjacent decimal-time clips overlap by one representable time step.
        const bool serial=value.kind()==ProgramKind::sequence || repeating;
        double offset=0,end=start,cursor=start;
        for(std::size_t i=0;i<count;++i) {
            const auto child_start=serial?std::expected<double,ProgramFailure>{cursor}:sum(start,offset,node);
            if(!child_start)return std::unexpected(child_start.error());
            branches.push_back({node,i,value.kind()});
            const auto child_node=nodes;
            auto child_end=visit(children[repeating?0:i],*child_start,depth+1,path+"/"+std::to_string(i),branches,node,scope);
            branches.pop_back();if(!child_end)return std::unexpected(child_end.error());
            if(repeating)hierarchy[child_node].iteration=i;
            end=std::max(end,*child_end);
            if(serial)cursor=*child_end;
            else if(value.kind()==ProgramKind::stagger && i+1<children.size()) {
                const auto next=sum(offset,step,node);if(!next)return std::unexpected(next.error());offset=*next;
            }
        }
        hierarchy[node].duration=seconds(end-start);hierarchy[node].subtree_end=nodes;
        if(value.kind()==ProgramKind::composite)scopes[scope].prepared.duration=seconds(end-start);
        return end;
    }
};
bool serial_before(const Leaf& a,const Leaf& b) noexcept {
    const auto count=std::min(a.branches.size(),b.branches.size());
    for(std::size_t i=0;i<count;++i) {
        const auto& x=a.branches[i];const auto& y=b.branches[i];
        if(x.node!=y.node)return false;
        if(x.child!=y.child)return (x.kind==ProgramKind::sequence || x.kind==ProgramKind::repeat) && x.child<y.child;
    }
    return false;
}
struct Progress { double linear, sampled; };
std::expected<Progress,ProgramFailure> progress_at(seconds start,SegmentOptions options,bool anchor_source,
    seconds time,bool exact_source,ProgramFailure error) noexcept {
    if(exact_source || time<start)return Progress{0,0};
    const auto duration=options.duration.count();
    const auto linear=duration==0?1:Tween<double>{.from=0,.to=1,
        .duration=options.duration,.ease=ease::linear,.delay=start}.value(time);
    if(anchor_source && duration>0 && linear==0)return Progress{linear,0};
    const auto eased=options.easing(linear);
    if(!std::isfinite(eased))return std::unexpected(error);
    // An overshooting curve's progress leaves [0, 1]: a property overshoots
    // with it; geometry and effects take it within (PreparedObjectMorph::sample).
    return Progress{linear,eased};
}
std::expected<Progress,ProgramFailure> progress_at(const PreparedSegment& segment,seconds time,bool exact_source,
                                                std::size_t index) noexcept {
    // The compiled first-stage segment has only offset/duration and both fills.
    // Use the core's simple inline time primitive instead of evaluating the
    // general repeat/alternate mapper for every object. Author easing is still
    // applied below, including jump-start at zero.
    return progress_at(segment.start,segment.options,segment.anchor_source,time,exact_source,
        failure(ProgramError::invalid_ease,segment.node,segment.track,index));
}
std::expected<PreparedObjectMorph,ProgramFailure> from_previous(const PreparedSegment& previous,const Snapshot& to,
    MorphOptions options,std::span<const PropertyTargets> properties,std::size_t node,std::size_t track,std::size_t segment) {
    const auto count=previous.animation.requirements();
    std::vector<FramePart> parts(count.parts);std::vector<ShapeCubic> cubics(count.cubics);std::vector<double> values(count.values);
    const auto progress=progress_at(previous,previous.start+previous.options.duration,false,segment-1);
    if(!progress)return std::unexpected(progress.error());
    const auto pose=previous.animation.sample(progress->sampled,{parts,cubics,values});
    if(!pose)return std::unexpected(animation_failure(pose.error(),node,track,segment));
    const auto result=!properties.empty() ? PreparedObjectMorph::properties(*pose,properties,options)
        : PreparedObjectMorph::retarget(*pose,to,options);
    if(!result)return std::unexpected(animation_failure(result.error(),node,track,segment));
    return *result;
}
PropertySet property_targets(const ProgramBinding& binding) {
    PropertySet result;
    if(binding.properties) {
        result.reserve(1+binding.additional_properties.size());result.push_back(*binding.properties);
        result.insert(result.end(),binding.additional_properties.begin(),binding.additional_properties.end());
    }
    return result;
}
bool overlaps_time(const Leaf& first,const Leaf& second) {
    return second.start<first.start+first.binding->options.duration ||
        (second.start==first.start &&
         (first.binding->options.duration.count()==0 || second.binding->options.duration.count()==0) && !serial_before(first,second));
}
struct PropertySchedule {
    std::vector<Leaf> leaves;
    std::vector<std::pair<std::size_t,std::size_t>> groups;
};
PropertySchedule schedule_groups(std::vector<Leaf> input) {
    PropertySchedule result;result.leaves.reserve(input.size());result.groups.reserve(input.size());
    auto append=[&](std::span<const std::size_t> indices) {
        const auto begin=result.leaves.size();
        for(auto index:indices)result.leaves.push_back(std::move(input[index]));
        result.groups.emplace_back(begin,result.leaves.size());
    };
    for(std::size_t begin=0;begin<input.size();) {
        std::size_t end=begin+1;auto until=input[begin].start+input[begin].binding->options.duration;
        bool zero=input[begin].binding->options.duration.count()==0,concurrent=false;
        while(end<input.size() && input[end].track==input[begin].track &&
              (input[end].start<until || input[end].start==input[begin].start)) {
            concurrent |= input[end].start<until || overlaps_time(input[end-1],input[end]);
            zero |= input[end].binding->options.duration.count()==0;
            until=std::max(until,input[end].start+input[end].binding->options.duration);++end;
        }
        std::vector<std::size_t> indices(end-begin);std::iota(indices.begin(),indices.end(),begin);
        if(!concurrent)for(auto i:indices)append(std::span(&i,1));
        else if(!zero)append(indices);
        else {
            // Instantaneous writes need the author dependency relation, not
            // just half-open intervals. Retain ordered zero-duration barriers
            // (including morphs) between actual concurrent components.
            std::vector<std::size_t> parent(indices.size());std::iota(parent.begin(),parent.end(),std::size_t{0});
            auto root=[&](std::size_t i) {
                while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;
            };
            for(std::size_t j=1;j<indices.size();++j)for(std::size_t i=0;i<j;++i)
                if(overlaps_time(input[begin+i],input[begin+j])) {
                    const auto a=root(i),b=root(j);if(a!=b)parent[std::max(a,b)]=std::min(a,b);
                }
            for(std::size_t i=0;i<parent.size();++i)parent[i]=root(i);
            std::ranges::stable_sort(indices,[&](auto a,auto b){return parent[a-begin]<parent[b-begin];});
            for(std::size_t i=0;i<indices.size();) {
                std::size_t j=i+1;while(j<indices.size() && parent[indices[j]-begin]==parent[indices[i]-begin])++j;
                append(std::span(indices).subspan(i,j-i));i=j;
            }
        }
        begin=end;
    }
    return result;
}
std::uint8_t property_mask(const PropertyTargets& target) {
    return static_cast<std::uint8_t>((target.position || target.offset || target.route?1:0) | (target.rotation || target.rotation_offset?2:0) |
        (target.scale || target.scale_factor?4:0) | (target.fill?8:0));
}
std::string_view part_name(const Leaf& leaf,const PropertyTargets& target) {
    if(!target.part.empty())return target.part;
    const auto root=leaf.from.get(leaf.from.root());return root?std::string_view((*root)->id):std::string_view{};
}
std::expected<PreparedSegment,ProgramFailure> compile_property_group(std::span<const Leaf> leaves,
    const PreparedSegment* previous,std::size_t index,std::size_t channel_budget) {
    const auto& first=leaves.front();
    auto conflict=[&](std::size_t i){return failure(ProgramError::binding_conflict,leaves[i].node,first.track,index);};
    // Full geometry writes and explicit whole-object source resets retain the
    // old exclusive domain. Only actual property writers may share an output.
    const auto full=std::ranges::find_if(leaves,[](const Leaf& leaf){
        return !leaf.binding->properties || leaf.binding->source==ProgramSource::snapshot;
    });
    if(full!=leaves.end()) {
        const auto at=static_cast<std::size_t>(full-leaves.begin());
        for(std::size_t k=0;k<leaves.size();++k)if(k!=at) {
            const auto i=std::min(at,k),j=std::max(at,k);
            if(overlaps_time(leaves[i],leaves[j]))
                return std::unexpected(failure(ProgramError::binding_conflict,leaves[j].node,first.track,index+j));
        }
        return std::unexpected(conflict(at));
    }
    std::vector<PropertyClip> clips;clips.reserve(leaves.size());
    std::vector<ProgramContribution> contributions;contributions.reserve(leaves.size());
    std::size_t draw_node=first.node;std::string_view draw_path=first.path;
    for(std::size_t i=0;i<leaves.size();++i) {
        const auto& leaf=leaves[i];const auto& binding=*leaf.binding;
        if(!binding.properties || binding.source==ProgramSource::snapshot)return std::unexpected(conflict(i));
        if(leaf.scope!=first.scope || !binding.effects.empty()) {
            auto error=failure(ProgramError::scope_conflict,leaf.node,leaf.track,index);error.where.scope=leaf.scope;
            return std::unexpected(error);
        }
        // Rebase both canonical endpoints, not start plus an independently
        // rounded duration. Otherwise an origin shift can reintroduce overlap.
        const auto local_start=leaf.start-first.start;
        const auto local_end=leaf.start+binding.options.duration-first.start;
        if(binding.options.duration.count()>0 && local_end==local_start)
            return std::unexpected(failure(ProgramError::invalid_time,leaf.node,first.track,index));
        clips.push_back({property_targets(binding),local_start,local_end-local_start,
            binding.options.easing,binding.morph,{},previous && leaf.start==first.start});
        contributions.push_back({leaf.node,leaf.path,leaf.start,binding.options});
        if(leaf.node<draw_node){draw_node=leaf.node;draw_path=leaf.path;}
    }
    // Check writes in their original absolute coordinates before rebasing.
    // This preserves even representable-step overlaps and the author's serial
    // ordering for instantaneous writes; local rounding cannot grant a write.
    std::optional<ProgramFailure> absolute_conflict;
    struct Write { std::string_view part; std::size_t channel,leaf; };
    std::vector<Write> writes;
    for(std::size_t i=0;i<leaves.size();++i)for(const auto& target:clips[i].targets)for(std::size_t c=0;c<4;++c)
        if(property_mask(target)&(1u<<c)) {
            if(writes.size()>=channel_budget)return std::unexpected(failure(ProgramError::channel_budget,leaves[i].node,first.track,index));
            writes.push_back({part_name(leaves[i],target),c,i});
        }
    std::ranges::sort(writes,[](const Write& a,const Write& b){
        return a.part!=b.part?a.part<b.part:a.channel!=b.channel?a.channel<b.channel:a.leaf<b.leaf;
    });
    for(std::size_t k=1;k<writes.size();++k) {
        const auto& a=writes[k-1];const auto& b=writes[k];
        if(a.part!=b.part || a.channel!=b.channel || a.leaf==b.leaf)continue;
        const auto& before=leaves[a.leaf];const auto& after=leaves[b.leaf];
        if(!absolute_conflict && overlaps_time(before,after)) {
            auto error=conflict(b.leaf);
            error.because(MorphFailure{.code=MorphError::property_conflict,.where={.clip=b.leaf,
                .channel=static_cast<PropertyChannel>(b.channel)}});
            absolute_conflict=error;
        }
    }
    if(!channel_budget)return std::unexpected(failure(ProgramError::channel_budget,first.node,first.track,index));
    const PropertyTimelineLimits limits{leaves.size(),channel_budget};
    std::expected<PreparedObjectAnimation,MorphFailure> result=std::unexpected(MorphFailure{});
    if(previous) {
        const auto count=previous->animation.requirements();
        std::vector<FramePart> parts(count.parts);std::vector<ShapeCubic> cubics(count.cubics);std::vector<double> values(count.values);
        const auto progress=progress_at(*previous,previous->start+previous->options.duration,false,index-1);
        if(!progress)return std::unexpected(progress.error());
        const auto pose=previous->animation.sample(progress->sampled,{parts,cubics,values});
        if(!pose)return std::unexpected(animation_failure(pose.error(),first.node,first.track,index));
        result=PreparedObjectAnimation::property_timeline(*pose,clips,limits);
    }else result=PreparedObjectAnimation::property_timeline(first.from,clips,limits);
    if(!result) {
        const auto& cause=result.error();const auto c=cause.where.clip<leaves.size()?cause.where.clip:0;
        auto error=animation_failure(cause,leaves[c].node,first.track,index);
        if(cause.code==MorphError::property_conflict)error.code=ProgramError::binding_conflict;
        if(cause.code==MorphError::channel_budget)error.code=ProgramError::channel_budget;
        return std::unexpected(error);
    }
    // Preserve the library's detailed part/channel diagnostic when available;
    // never publish a plan if rebasing alone hid an original-coordinate clash.
    if(absolute_conflict)return std::unexpected(*absolute_conflict);
    const auto empty=PreparedEffects::prepare({});
    if(!empty)return std::unexpected(effect_failure(empty.error(),first.node,first.track,index));
    return PreparedSegment{first.track,draw_node,std::string(draw_path),first.start,{result->timeline_duration(),ease::linear},
        std::move(*result),*empty,false,first.scope,std::move(contributions)};
}
std::expected<void,ProgramFailure> account(detail::ProgramData& data,PreparedSegment segment,ProgramLimits limits,
    std::size_t& parts,std::size_t& cubics,std::size_t& parameters) {
    const auto count=segment.animation.requirements();const auto params=segment.effects.parameter_count();
    const auto index=data.segments.size();
    const auto channels=segment.animation.channels().size();
    const auto routes=segment.animation.route_segment_count();
    if(routes>limits.max_total_route_segments-data.route_segments)
        return std::unexpected(failure(ProgramError::route_budget,segment.node,segment.track,index));
    if(channels>limits.max_total_property_channels-data.property_channels)
        return std::unexpected(failure(ProgramError::channel_budget,segment.node,segment.track,index));
    if(count.parts>limits.max_total_parts-parts)return std::unexpected(failure(ProgramError::part_budget,segment.node,segment.track,index));
    if(count.cubics>limits.max_total_cubics-cubics)return std::unexpected(failure(ProgramError::cubic_budget,segment.node,segment.track,index));
    if(params>limits.max_total_effect_parameters-parameters)return std::unexpected(failure(ProgramError::effect_parameter_budget,segment.node,segment.track,index));
    parts+=count.parts;cubics+=count.cubics;parameters+=params;data.property_channels+=channels;data.route_segments+=routes;
    auto& track=data.tracks[segment.track];
    track.requirements.parts=std::max(track.requirements.parts,count.parts);
    track.requirements.contours=std::max(track.requirements.contours,count.contours);
    track.requirements.cubics=std::max(track.requirements.cubics,count.cubics);
    track.requirements.values=std::max(track.requirements.values,count.values);
    track.max_effect_parameters=std::max(track.max_effect_parameters,params);
    if(!track.segment_count)track.first_segment=index;
    ++track.segment_count;data.segments.push_back(std::move(segment));
    return {};
}
}
ProgramKind Program::kind() const noexcept {return node_?node_->kind:ProgramKind::empty;}
std::span<const Program> Program::children() const & noexcept {return node_?std::span<const Program>(node_->children):std::span<const Program>{};}
const ProgramBinding* Program::binding() const & noexcept {return node_ && node_->kind==ProgramKind::binding?&node_->binding:nullptr;}
const ProgramInputs* Program::inputs() const & noexcept {return node_ && node_->inputs?&*node_->inputs:nullptr;}
const CompositeSpec* Program::scope() const & noexcept {return node_ && node_->kind==ProgramKind::composite?&node_->scope:nullptr;}
seconds Program::step() const noexcept {return node_?node_->step:seconds(0);}
const ProgramFailure* Program::failure() const & noexcept {return node_?node_->failure.get():nullptr;}
Program invalid_program(ProgramFailure error) {
    auto node=std::make_shared<detail::ProgramNode>();node->kind=ProgramKind::invalid;
    node->failure=std::make_unique<const ProgramFailure>(std::move(error));return Program(std::move(node));
}
Program bind(Snapshot from,Snapshot to,Effects effects,SegmentOptions options,MorphOptions morph,ProgramSource source) {
    auto node=std::make_shared<detail::ProgramNode>();node->kind=ProgramKind::binding;
    node->binding={std::move(from),std::move(to),std::move(effects),options,morph,source};return Program(std::move(node));
}
Program bind(ProgramInputs inputs,Effects effects,SegmentOptions options,MorphOptions morph,ProgramSource source,
    std::optional<PropertyTargets> properties,PropertySet additional_properties) {
    auto node=std::make_shared<detail::ProgramNode>();node->kind=ProgramKind::binding;
    node->binding={{},{},std::move(effects),options,morph,source,std::move(properties),std::move(additional_properties)};
    node->inputs=std::move(inputs);return Program(std::move(node));
}
seconds Program::wait_duration() const noexcept {return node_ && node_->kind==ProgramKind::wait?node_->wait_duration:seconds(0);}
std::size_t Program::repeat_count() const noexcept {return node_ && node_->kind==ProgramKind::repeat?node_->repeat_count:0;}
Program repeat(std::size_t count,Program child) {
    auto node=std::make_shared<detail::ProgramNode>();node->kind=ProgramKind::repeat;
    node->repeat_count=count;node->children.push_back(std::move(child));return Program(std::move(node));
}
Program wait(seconds duration) {
    auto node=std::make_shared<detail::ProgramNode>();node->kind=ProgramKind::wait;
    node->wait_duration=duration;return Program(std::move(node));
}
Program sequence(std::span<const Program> children) {
    auto node=std::make_shared<detail::ProgramNode>();node->kind=ProgramKind::sequence;node->children.assign(children.begin(),children.end());return Program(std::move(node));
}
Program parallel(std::span<const Program> children) {
    auto node=std::make_shared<detail::ProgramNode>();node->kind=ProgramKind::parallel;node->children.assign(children.begin(),children.end());return Program(std::move(node));
}
Program stagger(seconds step,std::span<const Program> children) {
    auto node=std::make_shared<detail::ProgramNode>();node->kind=ProgramKind::stagger;node->step=step;node->children.assign(children.begin(),children.end());return Program(std::move(node));
}
Program composite(Program child,Effects effects,CompositeOptions options) {
    auto node=std::make_shared<detail::ProgramNode>();node->kind=ProgramKind::composite;
    node->scope={std::move(effects),options};node->children.push_back(std::move(child));return Program(std::move(node));
}
Program sequence(std::initializer_list<Program> children) {return sequence(std::span<const Program>(children.begin(),children.size()));}
Program parallel(std::initializer_list<Program> children) {return parallel(std::span<const Program>(children.begin(),children.size()));}
Program stagger(seconds step,std::initializer_list<Program> children) {return stagger(step,std::span<const Program>(children.begin(),children.size()));}
std::string describe_where(const ProgramWhere& where) {
    std::string out;
    const auto add=[&](std::string_view what,std::size_t at){if(at!=invalid_program_index)out+=std::format("{}{} {}",out.empty()?"":", ",what,at);};
    add("node",where.node);add("track",where.track);add("segment",where.segment);add("scope",where.scope);
    return out;
}
std::expected<PreparedProgram,ProgramFailure> PreparedProgram::prepare(const Program& program,ProgramLimits limits) {
    return prepare_with(program,nullptr,limits);
}
std::expected<PreparedProgram,ProgramFailure> PreparedProgram::prepare_with(const Program& program,const ObjectRegistry* registry,ProgramLimits limits) {
    mlibs::trace::Span span("prepare","hanim.program");
    auto refuse=[](ProgramFailure error)->std::expected<PreparedProgram,ProgramFailure>{
        diagnostic.warn("prepare refused: {}",error.text());
        mlibs::trace::mark("refused","hanim.program");return std::unexpected(error);
    };
    if(!valid_limits(limits))return refuse(failure(ProgramError::invalid_limits));
    const auto empty_effects=PreparedEffects::prepare({},limits.effects);
    if(!empty_effects)return refuse(effect_failure(empty_effects.error(),invalid_program_index,invalid_program_index,invalid_program_index));
    Collect collect{limits,registry};std::vector<Branch> branches;
    const auto duration=collect.visit(program,0,1,"0",branches);if(!duration)return refuse(duration.error());
    auto data=std::make_shared<detail::ProgramData>();data->duration=seconds(*duration);
    data->nodes=std::move(collect.hierarchy);
    for(auto& input:collect.scopes) {
        auto& scope=input.prepared;const auto index=data->scopes.size();
        const auto effects=PreparedEffects::prepare(input.spec->effects,limits.effects);
        if(!effects) {
            auto error=effect_failure(effects.error(),scope.node,invalid_program_index,invalid_program_index);
            error.where.scope=index;return refuse(error);
        }
        if(effects->parameter_count()>limits.max_total_effect_parameters-data->scope_parameters) {
            auto error=failure(ProgramError::effect_parameter_budget,scope.node);error.where.scope=index;return refuse(error);
        }
        scope.parameter_offset=data->scope_parameters;scope.effects=*effects;
        data->scope_parameters+=effects->parameter_count();data->scopes.push_back(std::move(scope));
    }
    for(auto& leaf:collect.leaves) {
        const auto object=leaf.from.object();
        auto found=std::ranges::find(data->tracks,object,&ProgramTrack::object);
        if(found==data->tracks.end()) {
            if(data->tracks.size()>=limits.max_tracks)return refuse(failure(ProgramError::track_budget,leaf.node));
            leaf.track=data->tracks.size();data->tracks.push_back({.object=object,.source_revision=leaf.from.revision()});
        }else leaf.track=static_cast<std::size_t>(found-data->tracks.begin());
    }
    std::ranges::stable_sort(collect.leaves,[](const Leaf& a,const Leaf& b){
        if(a.track!=b.track)return a.track<b.track;
        if(a.start!=b.start)return a.start<b.start;
        return a.node<b.node;
    });
    auto schedule=schedule_groups(std::move(collect.leaves));
    std::size_t parts=0,cubics=0,parameters=data->scope_parameters;
    const Leaf* previous_leaf=nullptr;
    for(const auto [begin,end]:schedule.groups) {
        const auto& leaf=schedule.leaves[begin];
        const auto& binding=*leaf.binding;auto& track=data->tracks[leaf.track];
        const auto index=data->segments.size();const PreparedSegment* previous=nullptr;
        if(!track.segment_count)track.source_revision=leaf.from.revision();
        if(track.segment_count)previous=&data->segments.back();
        if(previous) {
            const auto end=previous->start+previous->options.duration;
            if(leaf.start<end || (leaf.start==previous->start &&
                (binding.options.duration.count()==0 || previous->options.duration.count()==0) && !serial_before(*previous_leaf,leaf)))
                return refuse(failure(ProgramError::binding_conflict,leaf.node,leaf.track,index));
            if(binding.source==ProgramSource::previous_endpoint && leaf.from.revision()!=track.source_revision)
                return refuse(failure(ProgramError::source_version_mismatch,leaf.node,leaf.track,index));
        }
        for(std::size_t i=begin+1;i<end;++i)
            if(schedule.leaves[i].binding->source==ProgramSource::previous_endpoint && schedule.leaves[i].from.revision()!=track.source_revision)
                return refuse(failure(ProgramError::source_version_mismatch,schedule.leaves[i].node,leaf.track,index));
        if(end>begin+1) {
            auto prepared=compile_property_group(std::span(schedule.leaves).subspan(begin,end-begin),previous,index,
                limits.max_total_property_channels-data->property_channels);
            if(!prepared)return refuse(prepared.error());
            const auto added=account(*data,std::move(*prepared),limits,parts,cubics,parameters);
            if(!added)return refuse(added.error());
            for(std::size_t i=begin;i<end;++i) {
                auto& node=data->nodes[schedule.leaves[i].node];node.track=leaf.track;node.segment=index;node.contribution=i-begin;
            }
            previous_leaf=&schedule.leaves[end-1];continue;
        }
        std::expected<PreparedObjectMorph,ProgramFailure> animation=std::unexpected(failure(ProgramError::animation));
        const bool chained=previous && binding.source==ProgramSource::previous_endpoint;
        const auto targets=property_targets(binding);
        if(chained)animation=from_previous(*previous,leaf.to,binding.morph,targets,leaf.node,leaf.track,index);
        else {
            const auto result=binding.properties ? PreparedObjectMorph::properties(leaf.from,targets,binding.morph)
                : PreparedObjectMorph::prepare(leaf.from,leaf.to,binding.morph);
            if(!result)return refuse(animation_failure(result.error(),leaf.node,leaf.track,index));
            animation=*result;
        }
        if(!animation)return refuse(animation.error());
        const auto effects=PreparedEffects::prepare(binding.effects,limits.effects);
        if(!effects)return refuse(effect_failure(effects.error(),leaf.node,leaf.track,index));
        const auto added=account(*data,{leaf.track,leaf.node,leaf.path,leaf.start,binding.options,*animation,*effects,chained,leaf.scope},limits,parts,cubics,parameters);
        if(!added)return refuse(added.error());previous_leaf=&leaf;
        data->nodes[leaf.node].track=leaf.track;data->nodes[leaf.node].segment=index;
    }
    mlibs::trace::count("tracks",static_cast<double>(data->tracks.size()),"hanim.program");
    mlibs::trace::count("segments",static_cast<double>(data->segments.size()),"hanim.program");
    mlibs::trace::count("scopes",static_cast<double>(data->scopes.size()),"hanim.program");
    diagnostic.info("prepared {} tracks and {} segments",data->tracks.size(),data->segments.size());
    PreparedProgram result;result.data_=std::move(data);return result;
}
std::expected<PreparedProgram,ProgramFailure> PreparedProgram::single(PreparedObjectMorph animation,PreparedEffects effects,
    SegmentOptions options,bool anchor_source,ProgramLimits limits) {
    mlibs::trace::Span span("single","hanim.program");
    if(!valid_limits(limits))return std::unexpected(failure(ProgramError::invalid_limits));
    if(!animation)return std::unexpected(failure(ProgramError::unprepared));
    if(const auto invalid=invalid_options(options))return std::unexpected(failure(*invalid,0,0,0));
    const auto empty=PreparedEffects::prepare({},limits.effects);
    if(!empty)return std::unexpected(effect_failure(empty.error(),0,0,0));
    if(effects.effects().size()>limits.effects.max_effects || effects.parameter_count()>limits.effects.max_parameters)
        return std::unexpected(effect_failure(EffectError::capacity,0,0,0));
    auto data=std::make_shared<detail::ProgramData>();data->duration=options.duration;
    data->nodes.push_back({.kind=ProgramKind::binding,.subtree_end=1,.duration=options.duration,.track=0,.segment=0});
    data->tracks.push_back({.object=animation.source_object(),.source_revision=animation.source_revision()});
    std::size_t parts=0,cubics=0,parameters=0;
    auto added=account(*data,{0,0,"0",seconds(0),options,std::move(animation),std::move(effects),anchor_source},limits,parts,cubics,parameters);
    if(!added)return std::unexpected(added.error());
    PreparedProgram result;result.data_=std::move(data);return result;
}
std::size_t PreparedProgram::route_segment_count() const noexcept {return data_?data_->route_segments:0;}
seconds PreparedProgram::duration() const noexcept {return data_?data_->duration:seconds(0);}
std::span<const ProgramTrack> PreparedProgram::tracks() const & noexcept {return data_?std::span<const ProgramTrack>(data_->tracks):std::span<const ProgramTrack>{};}
std::span<const PreparedSegment> PreparedProgram::segments() const & noexcept {return data_?std::span<const PreparedSegment>(data_->segments):std::span<const PreparedSegment>{};}
std::span<const ProgramNodeInfo> PreparedProgram::nodes() const & noexcept {return data_?std::span<const ProgramNodeInfo>(data_->nodes):std::span<const ProgramNodeInfo>{};}
std::span<const PreparedScope> PreparedProgram::scopes() const & noexcept {return data_?std::span<const PreparedScope>(data_->scopes):std::span<const PreparedScope>{};}
std::size_t PreparedProgram::scope_parameter_count() const noexcept {return data_?data_->scope_parameters:0;}
std::expected<void,ProgramFailure> PreparedProgram::sample_scopes(seconds time,std::span<float> output,bool exact_source) const noexcept {
    if(!data_)return std::unexpected(failure(ProgramError::unprepared));
    if(!std::isfinite(time.count()))return std::unexpected(failure(ProgramError::invalid_time));
    if(output.size()<data_->scope_parameters)return std::unexpected(failure(ProgramError::output_too_small));
    if(data_->scopes.empty())return {};
    mlibs::trace::Span span("sample-scopes","hanim.program");
    auto progress=[&](std::size_t i) {
        const auto& scope=data_->scopes[i];auto error=failure(ProgramError::invalid_ease,scope.node);error.where.scope=i;
        return progress_at(scope.start,{scope.duration,scope.effect_easing},false,time,exact_source,error);
    };
    // Preflight every easing before touching caller output; immutable effects
    // cannot fail once capacity and finite normalized progress are known.
    for(std::size_t i=0;i<data_->scopes.size();++i) {
        const auto value=progress(i);if(!value)return std::unexpected(value.error());
    }
    for(std::size_t i=0;i<data_->scopes.size();++i) {
        const auto& scope=data_->scopes[i];const auto value=progress(i);
        const auto result=scope.effects.sample(value->sampled,output.subspan(scope.parameter_offset,scope.effects.parameter_count()));
        if(!result) {auto error=effect_failure(result.error(),scope.node,invalid_program_index,invalid_program_index);error.where.scope=i;return std::unexpected(error);}
    }
    return {};
}
std::expected<ProgramSample,ProgramFailure> PreparedProgram::sample(std::size_t track_index,seconds time,FrameBuffer output,
    std::span<float> values,bool exact_source) const noexcept {
    mlibs::trace::Span span("sample","hanim.program");
    if(!data_)return std::unexpected(failure(ProgramError::unprepared));
    if(track_index>=data_->tracks.size())return std::unexpected(failure(ProgramError::invalid_track,invalid_program_index,track_index));
    if(!std::isfinite(time.count()))return std::unexpected(failure(ProgramError::invalid_time,invalid_program_index,track_index));
    const auto& track=data_->tracks[track_index];auto index=track.first_segment;
    if(!exact_source) {
        const auto begin=data_->segments.begin()+static_cast<std::ptrdiff_t>(track.first_segment);
        const auto end=begin+static_cast<std::ptrdiff_t>(track.segment_count);
        const auto after=std::ranges::upper_bound(begin,end,time,{},&PreparedSegment::start);
        if(after!=begin)index=static_cast<std::size_t>((after-1)-data_->segments.begin());
    }
    const auto& segment=data_->segments[index];const auto count=segment.animation.requirements();
    if(output.parts.size()<count.parts || output.cubics.size()<count.cubics || output.values.size()<count.values ||
       values.size()<segment.effects.parameter_count())
        return std::unexpected(failure(ProgramError::output_too_small,segment.node,track_index,index));
    const auto progress=progress_at(segment,time,exact_source,index);if(!progress)return std::unexpected(progress.error());
    const bool raw_source=exact_source || time<segment.start ||
        (segment.anchor_source && segment.options.duration.count()>0 && time==segment.start);
    const auto geometry=raw_source?segment.animation.sample_source(output):segment.animation.sample(progress->sampled,output);
    if(!geometry)return std::unexpected(animation_failure(geometry.error(),segment.node,track_index,index));
    // Bounds and finite progress were checked before geometry was written;
    // this immutable effect plan has no remaining sample failure condition.
    const auto effects=segment.effects.sample(std::clamp(progress->sampled,0.0,1.0),values);
    if(!effects)return std::unexpected(effect_failure(effects.error(),segment.node,track_index,index));
    return ProgramSample{*geometry,index,progress->linear,progress->sampled,segment.effects.parameter_count()};
}
} // namespace hanim
