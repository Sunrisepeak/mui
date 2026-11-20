module xrender.vector.api;
import std;
import mlibs.trace;
import :effects;

namespace xrender {
namespace {
constexpr auto absent_layer=std::numeric_limits<std::size_t>::max();
bool finite_extent(Rect r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
        std::isfinite(r.right()) && std::isfinite(r.bottom());
}
Rect aa_extent(Rect r) {return {r.x-1,r.y-1,r.width+2,r.height+2};}
}
bool LayerBoundsMap::measure(const DisplayList& list,const Affine& initial,float effect_scale,const Resources& resources) {
    mlibs::trace::Span trace("content bounds","render.effects");
    valid_=false;entries_.clear();indices_.assign(list.size(),absent_layer);stack_.clear();clip_starts_.clear();
    transforms_.assign(1,initial);clips_.clear();
    if(!std::isfinite(effect_scale) || effect_scale<=0)return false;
    const auto all=list.commands();
    auto include=[&](Rect bounds) {
        if(stack_.empty() || bounds.empty())return;
        const auto owner=stack_.back();
        for(std::size_t c=clip_starts_[owner];c<clips_.size();++c)bounds=bounds.intersection(clips_[c]);
        entries_[owner].content=entries_[owner].content.united(bounds);
    };
    for(std::size_t i=0;i<all.size();++i) {
        const auto& command=all[i];
        if(const auto* push=std::get_if<PushTransform>(&command))transforms_.push_back(transforms_.back()*push->transform);
        else if(std::holds_alternative<PopTransform>(command)) {
            if(transforms_.size()==1)return false;transforms_.pop_back();
        } else if(const auto* clip=std::get_if<PushClipRect>(&command))clips_.push_back(aa_extent(transforms_.back().map(clip->rect)));
        else if(const auto* clip=std::get_if<PushClipPath>(&command)) {
            const auto bound=clip->path.points.empty()?std::optional<Rect>{Rect{}}:
                device_bounds(FillPath{clip->path,clip->rule,{}},transforms_.back());
            clips_.push_back(bound.value_or(Rect{}));
        }
        else if(std::holds_alternative<PopClip>(command)) {
            if(clips_.empty())return false;clips_.pop_back();
        } else if(const auto* push=std::get_if<PushLayer>(&command)) {
            const auto entry=entries_.size();indices_[i]=entry;
            Rect declared=transforms_.back().map(push->bounds);
            for(const auto& shape:push->shapes)declared=declared.united(transforms_.back().map(shape.rect));
            if(!finite_extent(declared))return false;
            entries_.push_back({.begin=i,.area=declared});clip_starts_.push_back(clips_.size());stack_.push_back(entry);
        } else if(std::holds_alternative<PopLayer>(command)) {
            if(stack_.empty())return false;
            const auto index=stack_.back();stack_.pop_back();auto& entry=entries_[index];entry.end=i;
            if(clips_.size()!=clip_starts_[index])return false;
            const auto& push=std::get<PushLayer>(all[entry.begin]);
            if(push.bounds_mode==LayerBoundsMode::content)entry.area=entry.area.united(entry.content);
            else if(push.bounds_mode!=LayerBoundsMode::declared)return false;
            entry.output=entry.content;
            if(const auto* info=effects().info(push.effect)) {
                parameters_.assign(push.params.begin(),push.params.end());
                for(std::size_t p=parameters_.size();p<info->params.size();++p)parameters_.push_back(info->params[p].value);
                if(!info->identity || !info->identity(parameters_)) {
                    const auto reach=effects().output_reach(push.effect,parameters_);
                    if(!reach || !std::isfinite(*reach) || *reach<0)return false;
                    const float margin=*reach*effect_scale+1;
                    // Preserve source pixels as required by output_reach's
                    // contract; declared geometry describes generated output.
                    if(!entry.area.empty())entry.output=entry.output.united(entry.area.outset({margin,margin,margin,margin}));
                }
            }
            if(!finite_extent(entry.content) || !finite_extent(entry.area) || !finite_extent(entry.output))return false;
            include(entry.output);
        } else if(!stack_.empty())if(const auto bounds=device_bounds(command,transforms_.back(),resources.glyphs)) {
            if(!finite_extent(*bounds))return false;include(*bounds);
        }
    }
    valid_=stack_.empty() && clips_.empty() && transforms_.size()==1;
    mlibs::trace::count("measured layers",static_cast<double>(entries_.size()),"render.effects");
    return valid_;
}
const LayerExtent* LayerBoundsMap::at(std::size_t command) const noexcept {
    if(!valid_ || command>=indices_.size() || indices_[command]==absent_layer)return nullptr;
    return &entries_[indices_[command]];
}
}
