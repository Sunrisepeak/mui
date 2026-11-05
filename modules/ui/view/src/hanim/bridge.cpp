module omui.hanim;
import std;
import mlibs.trace;

namespace omui::animation {
std::expected<Binding,BindingFailure> Binding::attach(Runtime& runtime,std::span<const Target> targets,hanim::Engine& engine) {
    mlibs::trace::Span span("attach","omui.hanim");
    if(targets.empty() || targets.size()>4096)return std::unexpected(BindingFailure{BindingError::invalid_selection});
    std::vector<PresentationTarget> selected;selected.reserve(targets.size());
    hanim::ObjectDescription object;object.parts.reserve(targets.size());
    for(std::size_t i=0;i<targets.size();++i) {
        const auto& target=targets[i];
        if(!runtime.valid(target.node) || target.part.empty() ||
           std::ranges::any_of(targets.first(i),[&](const auto& prior){return prior.part==target.part || prior.node==target.node;}))
            return std::unexpected(BindingFailure{BindingError::invalid_selection,i});
        std::string parent;bool inside=i==0;
        for(NodeId node=target.node;runtime.valid(node) && !inside;node=runtime.parent(node)) {
            if(node==targets[0].node){inside=true;break;}
            const auto info=runtime.inspect(node);
            if(const auto* overlay=info.get("overlay");overlay && overlay->as_bool())break;
        }
        if(!inside)return std::unexpected(BindingFailure{BindingError::invalid_selection,i});
        if(i)for(NodeId node=runtime.parent(target.node);runtime.valid(node);node=runtime.parent(node)) {
            const auto found=std::ranges::find(targets,node,&Target::node);
            if(found!=targets.end()){parent=found->part;break;}
        }
        selected.push_back({target.node,target.channels,target.anchor});
        object.parts.push_back({.id=target.part,.parent=std::move(parent)});
    }
    auto lease=runtime.acquire_presentation(selected);
    if(!lease)return std::unexpected(BindingFailure{.code=BindingError::ui,.target=lease.error().target,.ui=lease.error()});
    auto created=engine.create_object(object);
    if(!created)return std::unexpected(BindingFailure{.code=BindingError::object,.object=created.error()});
    const auto snapshot=engine.capture_object(*created);
    if(!snapshot){(void)engine.destroy_object(*created);return std::unexpected(BindingFailure{.code=BindingError::object,.object=snapshot.error()});}
    Binding result;result.object_=*created;result.lease_=std::move(*lease);
    result.targets_.assign(targets.begin(),targets.end());result.poses_.resize(targets.size());
    const auto captured=snapshot->parts();
    for(const auto& target:targets) {
        const auto part=std::ranges::find(captured,target.part,&hanim::CapturedPart::id);
        result.parts_.push_back(static_cast<std::size_t>(part-captured.begin()));
        std::size_t parent=hanim::invalid_object_slot;
        if(part->parent)parent=static_cast<std::size_t>(std::ranges::find(captured,*part->parent,&hanim::CapturedPart::ref)-captured.begin());
        result.parents_.push_back(parent);
    }
    return result;
}
std::expected<void,BindingFailure> Binding::present(const hanim::EngineFrame& frame) {
    mlibs::trace::Span span("present","omui.hanim");
    if(frame.geometry.source_object!=object_)return std::unexpected(BindingFailure{BindingError::foreign_frame});
    if(!frame.animation || frame.geometry.parts.size()!=targets_.size() || !frame.geometry.contours.empty() ||
       !frame.geometry.cubics.empty() || !frame.effect_values.empty() || frame.info.scope_count)
        return std::unexpected(BindingFailure{BindingError::unsupported_frame});
    for(std::size_t i=0;i<targets_.size();++i) {
        const auto& part=frame.geometry.parts[parts_[i]];const auto& pose=part.local;
        const auto channels=static_cast<unsigned>(targets_[i].channels);
        if(part.info.id!=targets_[i].part || part.info.parent!=parents_[i] || part.fill)
            return std::unexpected(BindingFailure{BindingError::unsupported_frame,i});
        if((!(channels&1u) && (pose.tx!=0 || pose.ty!=0)) || (!(channels&2u) && pose.rotate!=0) ||
           (!(channels&4u) && (pose.sx!=1 || pose.sy!=1)) || (!(channels&8u) && pose.skew_x!=0))
            return std::unexpected(BindingFailure{BindingError::unsupported_frame,i});
        poses_[i]=pose;
    }
    const auto published=lease_.publish(poses_);
    if(!published)return std::unexpected(BindingFailure{.code=BindingError::ui,.target=published.error().target,.ui=published.error()});
    return {};
}
}
