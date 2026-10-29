// openmui.showcase.promo_video:overlay — what lies over every shot, on the
// cut's own time: the lines (Chinese above English, inside the title-safe
// area), the chapter, the timeline running along the bottom (the cut's
// clips, its markers, the playhead — the film as data), and, over the
// film's own frames played back, the tape's look and its speed. A view of
// its own on a transparent host: a pure function of the frame, so any
// frame of it is drawn directly.
export module openmui.showcase.promo_video:overlay;

import std;
import openmui;
import :film;

export namespace omui::promo {

struct OverlayOptions {
    bool subtitles = true;
    bool timebar = true;
};

class Overlay {
public:
    Overlay(const Cut& cut, const Subtitles& subtitles, Size size, OverlayOptions options = {});
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    [[nodiscard]] View view();
    // Before the frame: the cut's frame, and what the picture shows there.
    // `played_back`: the film's own frame shown under it (a `film` clip), as an image of `rt`'s store.
    void at(Runtime& rt, std::int64_t frame, const std::optional<Shown>& shown, std::optional<render::ImageRef> played_back);

    // The title-safe area (5% in from each edge), where every line lies.
    [[nodiscard]] Rect safe() const noexcept;

    struct State;  // what its painters read

private:
    const Cut*             cut_;
    const Subtitles*       subtitles_;
    Size                   size_;
    OverlayOptions         options_;
    std::vector<Showing>   lines_;     // the subtitle track's, in order
    std::vector<Chapter>   chapters_;
    std::shared_ptr<State> state_;
    std::vector<NodeId>    line_nodes_, chapter_nodes_;
    NodeId                 tape_{}, look_{}, colour_{}, timecode_{}, timebar_{}, timeline_{}, playback_{};
    bool                   found_ = false;
};

}  // namespace omui::promo
