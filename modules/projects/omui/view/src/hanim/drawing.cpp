module omui.hanim;
import std;

namespace omui::animation {

bool Drawing::record(PaintContext& ctx, hanim::Engine& engine, hanim::PlaybackRef playback, std::string_view role, std::optional<std::size_t> track) {
    const auto frame = track ? engine.frame(playback, *track) : engine.frame(playback);
    if (!frame || !frame->animation) return false;
    if (!drawn_ || !bound_ || *bound_ != playback) {
        auto bound = hanim::vector_render::PreparedVector::bind(*frame->animation, frame->effects ? *frame->effects : hanim::PreparedEffects{});
        if (!bound) return false;
        drawn_ = std::move(*bound);
        bound_ = playback;
        const auto needs = drawn_->requirements();
        world_.resize(needs.world_transforms);
        parameters_.resize(needs.backend_parameters);
        layers_.resize(needs.effect_layers);
    }
    staging_.clear();
    bool recorded = false;
    ctx.part({.role = role, .kind = xrender::PartKind::content}, [&] {
        recorded = drawn_->record(frame->geometry, frame->effect_values, ctx.list, {&staging_, world_, parameters_, layers_}).has_value();
    });
    return recorded;
}

void Drawing::reset() noexcept {
    drawn_.reset();
    bound_.reset();
}

}  // namespace omui::animation
