// openmui.site:mui — Mui (喵), the site's figure (the site plan §9, D1).
//
// A pixel cat drawn by hanim as a pixel picture (one path an ink), a little
// soft: one soft body (hanim's Engine::simulate, the host's one engine) under
// a hull round the figure carries every ink of whichever frame it shows
// (Engine::carry), standing on the page's surfaces (ledges: the page's
// bottom, the nodes it is given, the one it goes to), walking (a push) and
// hopping. It embodies the visitor's agent (omui.ui.base's
// agent_presence_state): it goes where the agent acts, its eyes lit, and
// holds up a card while the agent waits for the person. Asked, it moves,
// looks, points, says a line and sleeps — the page's own actions on a node of
// its own. Less motion asked: it changes frames in place, never walks.
export module omui.site:mui;

import std;
import omui.core;
import omui.view;
import omui.ui.base;
import hanim.engine;
import hanim.render.vector;
import :mascot_contract;

export namespace omui::site {

// Mui's art (site/brand/mascot.sprite.omui.json, embedded): each frame of the
// body of 24 as rows, each palette as inks.
struct MascotArt {
    std::map<std::string, std::vector<std::string>, std::less<>> frames;
    std::map<std::string, std::vector<hanim::PixelInk>, std::less<>> palettes;
    int size = 24;
};
[[nodiscard]] const MascotArt& mascot_art();

class Mascot : public MascotContract<Mascot> {
public:
    ~Mascot();
    void mounted();
    void prop_changed(MascotProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult sleep(ActContext& ctx);
    ActResult wake(ActContext& ctx);
    ActResult move_to(ActContext& ctx, const Value& node);
    ActResult look_at(ActContext& ctx, const Value& node);
    ActResult point_at(ActContext& ctx, const Value& node);
    ActResult say(ActContext& ctx, const Value& text);

    // What it is doing now (tests, agents): its state (asleep, asking,
    // walking, pointing, looking, quiet), the frame and palette it shows,
    // where it stands (its feet, in its own box), whom it faces, what it
    // goes to and what it said.
    struct Now {
        std::string state, frame, palette, target, said;
        Point       feet;
        bool        facing_right = false;
    };
    [[nodiscard]] Now now() const;

private:
    // The figure on the host's engine: its object (the hull and the frame's inks),
    // the playback showing it, the soft body under the hull, its vector binding.
    struct Figure {
        hanim::Engine*                                       engine = nullptr;
        hanim::ObjectRef                                     object;
        std::optional<hanim::PlaybackRef>                    showing;
        std::optional<hanim::BodyRef>                        body;
        std::optional<hanim::vector_render::PreparedVector>  drawn;
        Point                                                spawn;  // the root's place in its box
        std::string                                          frame, palette, state = "quiet", target, said_frame;
        bool                                                 right = false;          // its art mirrored
        bool                                                 facing_right = false;   // the way it faces
        std::vector<std::string>                             inks;
        double                                               last = -1, next_blink = 3.7, stride = 0, hopped = -1, settled_at = -1;
        std::size_t                                          ledges_seen = 0;
        bool                                                 started = false, asleep = false;  // its body: stepped only while awake
        Size                                                 room;
        bool                                                 was_moving = false;  // frames asked for last paint
        // Its arrival (`arrive`): its pixels gathering where it comes from, before it has a body.
        bool                                                 arrived = true;
        double                                               arrive_at = 0, gather_length = 0;
        hanim::ObjectRef                                     pixels;
        std::optional<hanim::PlaybackRef>                    gathering;
        std::optional<hanim::vector_render::PreparedVector>  gathering_drawn;
    };
    void arrive(PaintContext& ctx) const;
    void release_arrival() const;
    void ensure(const PaintContext& ctx) const;
    void show(const std::string& frame, const std::string& palette, bool facing_right) const;
    void steer(const PaintContext& ctx) const;
    [[nodiscard]] std::optional<Rect> box_of(std::string_view path) const;

    mutable std::unique_ptr<Figure> figure_;
    std::string             goal_;       // where it was asked to go (a node's path)
    std::string             goal_kind_;  // move, point, look
    std::string             said_;
    double                  said_until_ = 0;
    mutable parts::Label    bubble_;
};

}  // namespace omui::site
