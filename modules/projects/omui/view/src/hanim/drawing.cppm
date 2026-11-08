// omui.hanim:drawing — a hanim object drawn by a node (the vector tier): what
// a playback shows now, its geometry and effects, recorded into the node's
// paint. The binding is made once a playback and kept; its buffers are kept
// too, so a frame drawn again allocates nothing. One owner for every
// component that draws hanim's own geometry (a figure, a written mark, a
// pixel picture) rather than moving UI nodes (Playback, above).
//
//     omui::animation::Drawing drawn;                    // a member
//     drawn.record(ctx, services().engine(), showing);   // in paint()
export module omui.hanim:drawing;
import std;
export import hanim;
export import hanim.render.vector;
export import omui.view;

export namespace omui::animation {

class Drawing {
public:
    // The frame `playback` shows now (one of its tracks, when it has several),
    // recorded into the node's list as its part `role`; false when there is
    // nothing to draw (a stale playback, a frame without geometry). A new
    // playback is bound again.
    bool record(PaintContext& ctx, hanim::Engine& engine, hanim::PlaybackRef playback, std::string_view role = "figure",
                std::optional<std::size_t> track = std::nullopt);
    // Bound again at the next record (its object changed in a way its playback did not).
    void reset() noexcept;

private:
    std::optional<hanim::vector_render::PreparedVector> drawn_;
    std::optional<hanim::PlaybackRef>                   bound_;
    xrender::DisplayList                                staging_;
    std::vector<mlibs::geom::Affine>                    world_;
    std::vector<float>                                  parameters_;
    std::vector<mlibs::geom::Rect>                      layers_;
};

}  // namespace omui::animation
