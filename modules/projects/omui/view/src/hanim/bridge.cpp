module omui.hanim;
import std;
import mlibs.trace;

namespace omui::animation::detail {
struct BindingImageChain {
    hanim::vector_render::PreparedImageEffects binding;
    std::size_t layers=0,parameters=0,total_layers=0,total_parameters=0;
};
struct BindingImages {
    std::vector<BindingImageChain> segments,scopes;
    std::size_t max_layers=0,max_parameters=0;
};
}
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
    if(images_ || !frame.animation || !frame.effect_values.empty() || frame.info.scope_count ||
       (frame.effects && !frame.effects->effects().empty()))
        return std::unexpected(BindingFailure{BindingError::unsupported_frame});
    if(auto loaded=load_pose(frame.geometry);!loaded)return loaded;
    return publish();
}
std::expected<void,BindingFailure> Binding::load_pose(const hanim::FrameView& frame) {
    if(frame.source_object!=object_)return std::unexpected(BindingFailure{BindingError::foreign_frame});
    if(frame.parts.size()!=targets_.size() || !frame.contours.empty() || !frame.cubics.empty())
        return std::unexpected(BindingFailure{BindingError::unsupported_frame});
    for(std::size_t i=0;i<targets_.size();++i) {
        const auto& part=frame.parts[parts_[i]];const auto& pose=part.local;
        const auto channels=static_cast<unsigned>(targets_[i].channels);
        if(part.info.id!=targets_[i].part || part.info.parent!=parents_[i] || part.fill)
            return std::unexpected(BindingFailure{BindingError::unsupported_frame,i});
        if((!(channels&1u) && (pose.tx!=0 || pose.ty!=0)) || (!(channels&2u) && pose.rotate!=0) ||
           (!(channels&4u) && (pose.sx!=1 || pose.sy!=1)) || (!(channels&8u) && pose.skew_x!=0))
            return std::unexpected(BindingFailure{BindingError::unsupported_frame,i});
        poses_[i]=pose;
    }
    return {};
}
std::expected<void,BindingFailure> Binding::publish() {
    const auto published=lease_.publish(poses_,image_frames_);
    if(!published)return std::unexpected(BindingFailure{.code=BindingError::ui,.target=published.error().target,.ui=published.error()});
    if(!images_ && image_reserved_ && !(static_cast<unsigned>(targets_.front().channels)&16u)) {
        lease_.release_effects(0);image_reserved_=false;image_frames_.clear();
    }
    return {};
}
std::expected<void,BindingFailure> Binding::prepare(const hanim::PreparedProgram& program,hanim::vector_render::ProgramVectorLimits limits) {
    mlibs::trace::Span trace("bind-program","omui.hanim");
    auto refuse=[](BindingError code)->std::expected<void,BindingFailure>{return std::unexpected(BindingFailure{code});};
    if(!attached())return std::unexpected(BindingFailure{.code=BindingError::ui,.ui=PresentationFailure{PresentationError::stale}});
    if(!program)return refuse(BindingError::unprepared);
    if(program.tracks().size()!=1 || program.tracks().front().object!=object_)return refuse(BindingError::foreign_frame);
    bool has_images=!program.scopes().empty();
    for(const auto& segment:program.segments()) {
        const auto& animation=segment.animation;
        if(animation.source_object()!=object_ || animation.parts().size()!=targets_.size() || !animation.contours().empty() || animation.requirements().cubics)
            return refuse(BindingError::unsupported_frame);
        for(std::size_t i=0;i<targets_.size();++i) {
            const auto& part=animation.parts()[parts_[i]];
            if(part.id!=targets_[i].part || part.parent!=parents_[i])return refuse(BindingError::unsupported_frame);
        }
        has_images|=!segment.effects.effects().empty();
    }
    if(!has_images) {
        prepared_=program;images_.reset();image_layers_.clear();image_parameters_.clear();
        for(auto& frame:image_frames_)frame={};
        return {};
    }
    auto next=std::make_shared<detail::BindingImages>();
    next->segments.reserve(program.segments().size());next->scopes.reserve(program.scopes().size());
    std::size_t total_layers=0,total_parameters=0;
    auto bind=[&](const hanim::PreparedEffects& effects,std::size_t segment,std::size_t scope)->std::expected<detail::BindingImageChain,BindingFailure> {
        detail::BindingImageChain chain;
        if(effects.effects().empty())return chain;
        auto result=hanim::vector_render::PreparedImageEffects::bind(effects);
        auto failure=[&](hanim::vector_render::VectorFailure error)->std::expected<detail::BindingImageChain,BindingFailure>{
            error.segment=segment;error.scope=scope;return std::unexpected(BindingFailure{.code=BindingError::rendering,.rendering=error});};
        if(!result)return failure(result.error());
        for(std::size_t i=0;i<result->bindings().size();++i) {
            const auto* info=xrender::effects().info(result->bindings()[i].ref);
            if(!info || !info->preserves_hit_geometry || info->source_of)return failure({hanim::vector_render::VectorError::unsupported_effect,i});
        }
        chain.layers=result->bindings().size();chain.parameters=result->parameter_count();chain.binding=std::move(*result);
        if(chain.layers>limits.max_prepared_effect_layers-total_layers || chain.parameters>limits.max_prepared_backend_parameters-total_parameters)
            return std::unexpected(BindingFailure{BindingError::capacity});
        total_layers+=chain.layers;total_parameters+=chain.parameters;return chain;
    };
    for(std::size_t i=0;i<program.scopes().size();++i) {
        const auto& scope=program.scopes()[i];auto chain=bind(scope.effects,hanim::invalid_program_index,i);
        if(!chain)return std::unexpected(chain.error());
        chain->total_layers=chain->layers;chain->total_parameters=chain->parameters;
        if(scope.parent!=hanim::invalid_program_index) {
            if(scope.parent>=i)return refuse(BindingError::unsupported_frame);
            chain->total_layers+=next->scopes[scope.parent].total_layers;chain->total_parameters+=next->scopes[scope.parent].total_parameters;
        }
        if(chain->total_layers>presentation_max_image_layers || chain->total_parameters>presentation_max_image_parameters)
            return refuse(BindingError::capacity);
        next->scopes.push_back(std::move(*chain));
    }
    for(std::size_t i=0;i<program.segments().size();++i) {
        const auto& segment=program.segments()[i];auto chain=bind(segment.effects,i,hanim::invalid_program_index);
        if(!chain)return std::unexpected(chain.error());
        chain->total_layers=chain->layers;chain->total_parameters=chain->parameters;
        if(segment.scope!=hanim::invalid_program_index) {
            if(segment.scope>=next->scopes.size())return refuse(BindingError::unsupported_frame);
            chain->total_layers+=next->scopes[segment.scope].total_layers;chain->total_parameters+=next->scopes[segment.scope].total_parameters;
        }
        if(chain->total_layers>presentation_max_image_layers || chain->total_parameters>presentation_max_image_parameters)
            return refuse(BindingError::capacity);
        next->max_layers=std::max(next->max_layers,chain->total_layers);next->max_parameters=std::max(next->max_parameters,chain->total_parameters);
        next->segments.push_back(std::move(*chain));
    }
    if(next->max_layers>presentation_max_image_layers || next->max_parameters>presentation_max_image_parameters)
        return refuse(BindingError::capacity);
    std::vector<float> parameters(next->max_parameters);
    std::vector<PresentationImageLayer> layers(next->max_layers);
    std::vector<PresentationEffects> frames(targets_.size());
    if(next->max_layers) {
        const auto reserved=lease_.reserve_effects(0,next->max_layers,next->max_parameters);
        if(!reserved)return std::unexpected(BindingFailure{.code=BindingError::ui,.target=reserved.error().target,.ui=reserved.error()});
        image_reserved_=true;
    }
    prepared_=program;images_=std::move(next);image_parameters_=std::move(parameters);image_layers_=std::move(layers);image_frames_=std::move(frames);
    return {};
}
std::expected<void,BindingFailure> Binding::present(const hanim::EngineProgramFrame& frame) {
    mlibs::trace::Span trace("present-program","omui.hanim");
    if(!prepared_)return std::unexpected(BindingFailure{BindingError::unprepared});
    const auto& scene=frame.scene;
    if(scene.preparation!=prepared_.identity() || scene.tracks.size()!=1 || scene.scope_effect_values.size()!=prepared_.scope_parameter_count())
        return std::unexpected(BindingFailure{BindingError::foreign_frame});
    const auto& track=scene.tracks.front();const auto segment=track.segment_index;
    if(segment>=prepared_.segments().size() || track.geometry.preparation!=prepared_.segments()[segment].animation.identity())
        return std::unexpected(BindingFailure{BindingError::foreign_frame});
    if(track.effect_values.size()!=prepared_.segments()[segment].effects.parameter_count() || !std::isfinite(scene.position.count()))
        return std::unexpected(BindingFailure{BindingError::foreign_frame});
    if(auto pose=load_pose(track.geometry);!pose)return pose;
    if(!images_)return publish();
    std::size_t parameters=0,layers=0;
    auto append=[&](const detail::BindingImageChain& chain,std::span<const float> values,std::size_t scope)->std::expected<void,BindingFailure>{
        if(!chain.layers)return {};
        auto output=std::span(image_parameters_).subspan(parameters,chain.parameters);
        const auto resolved=chain.binding.resolve(values,output);
        if(!resolved) {auto error=resolved.error();error.segment=segment;error.scope=scope;
            return std::unexpected(BindingFailure{.code=BindingError::rendering,.rendering=error});}
        for(const auto& effect:chain.binding.bindings())
            image_layers_[layers++]={effect.ref,effect.generation,output.subspan(effect.offset,effect.count)};
        parameters+=chain.parameters;return {};
    };
    if(auto leaf=append(images_->segments[segment],track.effect_values,hanim::invalid_program_index);!leaf)return leaf;
    for(auto scope=prepared_.segments()[segment].scope;scope!=hanim::invalid_program_index;scope=prepared_.scopes()[scope].parent) {
        const auto& description=prepared_.scopes()[scope];
        if(auto group=append(images_->scopes[scope],scene.scope_effect_values.subspan(description.parameter_offset,description.effects.parameter_count()),scope);!group)return group;
    }
    image_frames_.front().layers=std::span(image_layers_).first(layers);
    return publish();
}
}
