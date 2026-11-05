module openmui.view;
import std;
import mlibs.trace;
import :impl;

namespace omui {
namespace {
constexpr auto no_projection=std::numeric_limits<std::size_t>::max();
bool selected(PresentationChannel channels,std::size_t lane) noexcept {
    return (static_cast<unsigned>(channels)&(1u<<lane))!=0;
}
hanim::Transform2D merge_pose(hanim::Transform2D base,const hanim::Transform2D& value,PresentationChannel channels) {
    if(selected(channels,0)){base.tx=value.tx;base.ty=value.ty;}
    if(selected(channels,1))base.rotate=value.rotate;
    if(selected(channels,2)){base.sx=value.sx;base.sy=value.sy;}
    if(selected(channels,3))base.skew_x=value.skew_x;
    return base;
}
bool finite_pose(const hanim::Transform2D& pose) {
    for(double value:hanim::lanes<hanim::Transform2D>::to(pose))if(!std::isfinite(value))return false;
    for(double value:pose.matrix())if(!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max())return false;
    return true;
}
bool finite_matrix(const Affine& m) {
    for(float value:{m.a,m.b,m.c,m.d,m.e,m.f})if(!std::isfinite(value))return false;
    return true;
}
bool identity(const Affine& m) {return m.a==1 && m.b==0 && m.c==0 && m.d==1 && m.e==0 && m.f==0;}
Affine matrix(const hanim::Transform2D& pose) {
    const auto a=pose.matrix();
    return {static_cast<float>(a[0]),static_cast<float>(a[1]),static_cast<float>(a[2]),
        static_cast<float>(a[3]),static_cast<float>(a[4]),static_cast<float>(a[5])};
}
}
PresentationLease::~PresentationLease(){release();}
PresentationLease::PresentationLease(PresentationLease&& other) noexcept {*this=std::move(other);}
PresentationLease& PresentationLease::operator=(PresentationLease&& other) noexcept {
    if(this!=&other){release();runtime_=std::exchange(other.runtime_,nullptr);token_=std::exchange(other.token_,0);
        life_=std::move(other.life_);targets_=std::move(other.targets_);}
    return *this;
}
bool PresentationLease::attached() const noexcept {
    return runtime_ && token_ && !life_.expired() && runtime_->owns_presentation(*this);
}
std::expected<void,PresentationFailure> PresentationLease::publish(std::span<const hanim::Transform2D> poses) {
    if(!attached())return std::unexpected(PresentationFailure{PresentationError::stale});
    return runtime_->publish_presentation(*this,poses);
}
void PresentationLease::release() noexcept {
    if(runtime_ && token_ && !life_.expired())runtime_->release_presentation(*this);
    runtime_=nullptr;token_=0;targets_.clear();life_.reset();
}

std::expected<PresentationLease,PresentationFailure> Runtime::acquire_presentation(std::span<const PresentationTarget> targets) {
    mlibs::trace::Span span("acquire","view.presentation");auto& d=*impl_;
    if(targets.empty() || targets.size()>4096)return std::unexpected(PresentationFailure{PresentationError::capacity});
    for(std::size_t i=0;i<targets.size();++i) {
        const auto& target=targets[i];
        auto refuse=[&](PresentationError code)->std::expected<PresentationLease,PresentationFailure>{return std::unexpected(PresentationFailure{code,i});};
        if(!d.alive(target.node))return refuse(PresentationError::invalid_node);
        const auto bits=static_cast<unsigned>(target.channels);
        if(!bits || bits>15 || !std::isfinite(target.anchor.x) || !std::isfinite(target.anchor.y) ||
           target.anchor.x<0 || target.anchor.x>1 || target.anchor.y<0 || target.anchor.y>1)
            return refuse(PresentationError::invalid_configuration);
        if(std::ranges::any_of(targets.first(i),[&](const auto& prior){return prior.node==target.node;}))
            return refuse(PresentationError::invalid_configuration);
        const auto& node=d.at(target.node);const auto& current=node.presentation;
        if(current.attached() && current.anchor!=target.anchor)return refuse(PresentationError::invalid_configuration);
        for(std::size_t lane=0;lane<4;++lane)if(selected(target.channels,lane) && current.writers[lane])
            return refuse(PresentationError::occupied);
        // Adding a representation boundary in the middle of an already
        // flattened parent morph needs repartitioning of that old plan. This
        // first adapter explicitly refuses it; it never cancels base feedback.
        if(!current.attached())for(NodeId at=target.node;d.alive(at);at=d.at(at).parent) {
            const auto& base=d.at(at);
            if(base.morph && !base.morph->settled(base.clock.local(d.now().count())))return refuse(PresentationError::busy);
        }
    }
    if(!d.next_presentation_token || d.next_presentation_token==std::numeric_limits<std::uint64_t>::max())
        return std::unexpected(PresentationFailure{PresentationError::capacity});
    PresentationLease lease;lease.targets_.assign(targets.begin(),targets.end());
    d.presentation_owners.reserve(d.presentation_owners.size()+targets.size());
    lease.runtime_=this;lease.life_=d.life;lease.token_=d.next_presentation_token++;
    d.presentation_used=true;
    for(const auto& target:targets) {
        auto& current=d.at(target.node).presentation;
        if(!current.attached())d.presentation_owners.push_back(target.node);
        current.anchor=target.anchor;
        for(std::size_t lane=0;lane<4;++lane)if(selected(target.channels,lane))current.writers[lane]=lease.token_;
    }
    d.touch(dirty_paint);return lease;
}
bool Runtime::owns_presentation(const PresentationLease& lease) const noexcept {
    if(lease.runtime_!=this || !lease.token_)return false;
    for(const auto& target:lease.targets_) {
        if(!impl_->alive(target.node))return false;
        for(std::size_t lane=0;lane<4;++lane)
            if(selected(target.channels,lane) && impl_->at(target.node).presentation.writers[lane]!=lease.token_)return false;
    }
    return true;
}
std::expected<void,PresentationFailure> Runtime::publish_presentation(const PresentationLease& lease,std::span<const hanim::Transform2D> poses) {
    mlibs::trace::Span span("publish","view.presentation");auto& d=*impl_;
    if(!owns_presentation(lease))return std::unexpected(PresentationFailure{PresentationError::stale});
    if(poses.size()!=lease.targets_.size())return std::unexpected(PresentationFailure{PresentationError::invalid_configuration});
    for(std::size_t i=0;i<poses.size();++i) {
        const auto& target=lease.targets_[i];
        if(!finite_pose(poses[i]) || !finite_pose(merge_pose(d.at(target.node).presentation.pose,poses[i],target.channels)))
            return std::unexpected(PresentationFailure{PresentationError::invalid_pose,i});
    }
    bool changed=false;
    for(std::size_t i=0;i<poses.size();++i) {
        const auto& target=lease.targets_[i];auto& value=d.at(target.node).presentation.pose;
        const auto next=merge_pose(value,poses[i],target.channels);
        if(value!=next){d.at(target.node).presentation.applied=false;changed=true;}
        value=next;
    }
    if(changed)d.touch(dirty_paint);
    return {};
}
void Runtime::release_presentation(const PresentationLease& lease) noexcept {
    mlibs::trace::Span span("release","view.presentation");auto& d=*impl_;bool changed=false;
    for(const auto& target:lease.targets_)if(d.alive(target.node)) {
        auto& value=d.at(target.node).presentation;
        for(std::size_t lane=0;lane<4;++lane)if(selected(target.channels,lane) && value.writers[lane]==lease.token_) {
            value.writers[lane]=0;value.pose=merge_pose(value.pose,{},static_cast<PresentationChannel>(1u<<lane));value.applied=false;changed=true;
        }
        if(!value.attached())std::erase(d.presentation_owners,target.node);
    }
    if(changed)d.touch(dirty_paint);
}
std::optional<PresentationState> Runtime::presentation(NodeId id) const noexcept {
    if(!impl_->alive(id))return std::nullopt;
    const auto& value=impl_->at(id).presentation;unsigned bits=0;
    for(std::size_t lane=0;lane<4;++lane)if(value.writers[lane])bits|=1u<<lane;
    return PresentationState{id,value.pose,value.anchor,static_cast<PresentationChannel>(bits),value.has_frame,value.applied,value.error};
}

std::optional<Affine> Runtime::Impl::projected_target_world(NodeId id) const {
    if(presentation_owners.empty() || !alive(id))return std::nullopt;
    // Overlay placement uses target layout. Apply the same declared local
    // projection to that target, stopping at each portal's rendered root.
    // Current sampled geometry/IME continue to use collect_presentation.
    const auto delta=[&](auto&& self,NodeId node)->std::pair<Affine,bool> {
        if(!alive(node))return {{},false};
        const auto& n=at(node);
        auto inherited=n.type->overlay?std::pair<Affine,bool>{{},false}:self(self,n.parent);
        if(n.presentation.attached() && n.presentation.pose!=hanim::Transform2D{}) {
            const auto inverse=n.world.inverse();if(!inverse)return inherited;
            const Point anchor{n.frame.width*n.presentation.anchor.x,n.frame.height*n.presentation.anchor.y};
            const auto own=n.world*Affine::translate(anchor.x,anchor.y)*matrix(n.presentation.pose)*
                Affine::translate(-anchor.x,-anchor.y)*(*inverse);
            if(finite_matrix(own)){inherited.first=inherited.first*own;inherited.second=true;}
        }
        return inherited;
    };
    const auto [offset,changed]=delta(delta,id);if(!changed)return std::nullopt;
    return offset*at(id).world;
}
std::optional<Rect> Runtime::Impl::projected_target_bounds(NodeId id) const {
    const auto world=projected_target_world(id);if(!world)return std::nullopt;
    const auto& node=at(id);return world->map({0,0,node.frame.width,node.frame.height});
}

void Runtime::Impl::project_presentation() {
    mlibs::trace::Span span("project","view.presentation");
    std::erase_if(presentation_owners,[&](NodeId id){return !alive(id) || !at(id).presentation.attached();});
    if(!presentation_used)return;
    for(const auto id:presentation_owners) {
        auto& p=at(id).presentation;p.has_frame=false;p.applied=false;p.error.reset();
    }
    bool anchored=false;
    for(const auto id:overlays)if(alive(id) && at(id).live_anchor_placement) {
        auto& p=at(id).presentation;p.has_frame=false;p.applied=false;p.error.reset();anchored=true;
    }
    if(presentation_owners.empty() && !anchored)return;
    projection_ranges.clear();projection_nodes.clear();projection_transforms.assign(1,Affine{});
    projection_for_node.assign(by_index.size(),no_projection);
    const auto commands=list.commands();
    for(std::size_t i=0;i<commands.size();++i) {
        if(history_range.second>history_range.first && i==history_range.first){i=history_range.second-1;continue;}
        const auto& command=commands[i];
        if(const auto* begin=std::get_if<render::BeginNode>(&command)) {
            std::size_t range=no_projection;const auto marker=begin->node;
            if(marker<by_index.size() && alive(by_index[marker]) &&
               (at(by_index[marker]).presentation.attached() || at(by_index[marker]).live_anchor_placement) &&
               projection_for_node[marker]==no_projection) {
                range=projection_ranges.size();projection_for_node[marker]=range;
                projection_ranges.push_back({marker,i,0,projection_transforms.size()});
            }
            projection_nodes.push_back({marker,range});
        }else if(std::holds_alternative<render::EndNode>(command)) {
            if(!projection_nodes.empty()) {
                const auto [marker,range]=projection_nodes.back();projection_nodes.pop_back();
                if(range!=no_projection){projection_ranges[range].end=i+1;projection_for_node[marker]=no_projection;}
            }
        }else if(const auto* transform=std::get_if<render::PushTransform>(&command))projection_transforms.push_back(transform->transform);
        else if(std::holds_alternative<render::PopTransform>(command)) {if(projection_transforms.size()>1)projection_transforms.pop_back();}
        else if(const auto* frame=std::get_if<render::NodeFrame>(&command);frame && frame->node<by_index.size()) {
            const auto index=projection_for_node[frame->node];if(index==no_projection)continue;
            auto& range=projection_ranges[index];if(range.frame)continue;
            auto& p=at(by_index[frame->node]).presentation;p.has_frame=true;
            Affine base;
            for(std::size_t depth=range.depth;depth<projection_transforms.size();++depth)base=base*projection_transforms[depth];
            const auto inverse=base.inverse();
            if(p.pose==hanim::Transform2D{})range.delta={};
            else if(inverse && finite_matrix(base)) {
                const Point anchor{frame->rect.x+frame->rect.width*p.anchor.x,frame->rect.y+frame->rect.height*p.anchor.y};
                range.delta=base*Affine::translate(anchor.x,anchor.y)*matrix(p.pose)*Affine::translate(-anchor.x,-anchor.y)*(*inverse);
            }else {p.error=PresentationError::unsupported_frame;continue;}
            if(!finite_matrix(range.delta)){p.error=PresentationError::unsupported_frame;continue;}
            const auto& owner=at(by_index[frame->node]);
            if(owner.live_anchor_placement) {
                const auto origin=frame->sampled_placement_origin.value_or(frame->placement.origin());
                range.delta=Affine::translate(owner.frame.x-origin.x,owner.frame.y-origin.y)*range.delta;
            }
            if(!finite_matrix(range.delta)){p.error=PresentationError::unsupported_frame;continue;}
            range.frame=true;p.applied=true;
        }
    }
    for(auto& range:projection_ranges)if(at(by_index[range.marker]).presentation.error) {
        range.frame=false;at(by_index[range.marker]).presentation.applied=false;
    }
    if(std::ranges::none_of(projection_ranges,[](const auto& range){return range.frame && !identity(range.delta);}))return;
    presentation_source_history=history_range;presentation_source_valid=true;
    std::swap(list,presentation_source);list.clear();
    list.reserve(presentation_source.size()+2*projection_ranges.size());
    const auto source=presentation_source.commands();projection_nodes.clear();std::size_t next=0;
    std::pair<std::size_t,std::size_t> projected_history{};
    for(std::size_t i=0;i<source.size();++i) {
        if(history_range.second>history_range.first && i==history_range.first)projected_history.first=list.size();
        if(next<projection_ranges.size() && projection_ranges[next].begin==i) {
            const auto& range=projection_ranges[next];
            if(range.frame && !identity(range.delta)){list.push_transform(range.delta);projection_nodes.push_back({0,next});}
            ++next;
        }
        list.append_command(source[i]);
        if(!projection_nodes.empty() && projection_ranges[projection_nodes.back().second].end==i+1) {
            list.pop_transform();projection_nodes.pop_back();
        }
        if(history_range.second>history_range.first && i+1==history_range.second)projected_history.second=list.size();
    }
    history_range=projected_history;
}
} // namespace omui
