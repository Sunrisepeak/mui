module omui.hanim;
import std;

namespace omui::animation {

bool Drawing::record(PaintContext& ctx, hanim::Engine& engine, hanim::PlaybackRef playback, std::string_view role, std::optional<std::size_t> track) {
    const auto frame = track ? engine.frame(playback, *track) : engine.frame(playback);
    if (!frame || !frame->animation) return false;
    // Bound to what the frame comes from: a playback, and within it the segment's animation (a
    // program of several segments draws each from its own) — bound again only when that changes.
    if (!drawn_ || !bound_ || *bound_ != playback || animation_ != frame->animation->identity()) {
        auto bound = hanim::vector_render::PreparedVector::bind(*frame->animation, frame->effects ? *frame->effects : hanim::PreparedEffects{});
        if (!bound) return false;
        drawn_ = std::move(*bound);
        bound_ = playback;
        animation_ = frame->animation->identity();
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
    animation_ = nullptr;
}

}  // namespace omui::animation
