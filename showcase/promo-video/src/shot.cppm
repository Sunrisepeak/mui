// openmui.showcase.promo_video:shot — a scene of the film: a live openmui
// view drawn on the headless host, from its own time.
//
// A shot is mounted at its time 0 and drawn at any time after: before each
// frame it sets what its time says (`at`: a camera, a reveal, a value), and
// what changes in a step (a style switched, an agent's action, a game's
// step) happens at its moments, each on a frame. The player keeps to one
// rule for both — before a moment's frame, the frame before it is drawn —
// so a shot's frame drawn directly (mounted afresh, its moments replayed)
// is the frame playing up to it gives. The tests hold every shot to it.
export module openmui.showcase.promo_video:shot;

import std;
import openmui;
import :film;

export namespace omui::promo {

struct ShotContext {
    const Files*    files = nullptr;
    const Cut*      cut = nullptr;
    const tl::Clip* clip = nullptr;  // its keyed properties: a camera, a choreography
    Size            size{1920, 1080};  // logical pixels: the cut's (or a portrait 1080 × 1920)
    float           scale = 1;         // physical pixels a logical one
    // A keyed property of the clip at the shot's time `t` (seconds), or `otherwise`.
    [[nodiscard]] double value(std::string_view property, double t, double otherwise = 0) const;
    // The times (shot seconds) at which a held property changes, and to what.
    [[nodiscard]] std::vector<std::pair<double, double>> steps(std::string_view property) const;
    [[nodiscard]] bool portrait() const noexcept { return size.height > size.width; }
    [[nodiscard]] double rate() const noexcept;  // frames a second
};

// A sound the shot makes, at its own time.
struct Cue {
    double      at = 0;
    std::string sound;
    float       gain = 1, pan = 0, pitch = 1;
};

class Shot {
public:
    virtual ~Shot() = default;
    // The view, mounted at the shot's time 0.
    [[nodiscard]] virtual View view() = 0;
    // Once mounted, before its first frame (a style applied, a scene filled).
    virtual void mounted(Runtime& rt) { (void)rt; }
    // Before the frame at `t` seconds: what that time says.
    virtual void at(Runtime& rt, double t) = 0;
    // The frames (≥ 1) where something changes in a step; `moment` makes the change.
    [[nodiscard]] virtual std::vector<std::int64_t> moments() const { return {}; }
    // A simulation: every frame is a moment.
    [[nodiscard]] virtual bool simulates() const { return false; }
    virtual void moment(Runtime& rt, std::int64_t frame) { (void)rt, (void)frame; }
    // Its sounds, at its own times (from its first to its `seconds`).
    [[nodiscard]] virtual std::vector<Cue> cues(double seconds) { (void)seconds; return {}; }
};

// What the shots name, registered once: the style packs, the components,
// the wall's canvas and its behaviours (before any view is mounted).
void prepare();

// A shot by name (shot_names), for a clip of a cut.
[[nodiscard]] std::expected<std::unique_ptr<Shot>, std::string> make_shot(std::string_view name, const ShotContext& context);

// Each shot's maker (src/shots).
[[nodiscard]] std::unique_ptr<Shot> origin_shot(const ShotContext& c);
[[nodiscard]] std::unique_ptr<Shot> code_shot(const ShotContext& c);
[[nodiscard]] std::unique_ptr<Shot> infinite_shot(const ShotContext& c);
[[nodiscard]] std::unique_ptr<Shot> styles_shot(const ShotContext& c);
[[nodiscard]] std::unique_ptr<Shot> agent_shot(const ShotContext& c);
[[nodiscard]] std::unique_ptr<Shot> time_shot(const ShotContext& c);
[[nodiscard]] std::unique_ptr<Shot> end_shot(const ShotContext& c);

// The tally counter in C++ (code/tally.cpp): what the code shot shows of C++.
[[nodiscard]] View tally(Signal<int> count, Signal<int> goal);

// ── the panel the film's pixel belongs to ───────────────────────────────

// The style workshop's settings panel and dashboard in the blueprint style,
// drawn once on a headless host of its own (1440 × 900) at 1× — its pixels
// are the pixels the hook magnifies — and at 2× (what is shown smaller
// than 1:1); the pixel (inside its Save button) and what drew it, read from
// the host's object ids and semantic tree.
struct PanelPicture {
    std::shared_ptr<const raster::Surface> x1, x2;
    Size                                   size;       // logical (1×) pixels
    Point                                  pixel;      // its top left, 1× pixels
    Rect                                   button;     // the Save button's bounds
    std::string                            path;       // the node that drew the pixel
    std::string                            provenance; // "button#save · role=button · effect=reversible"
    std::vector<spec::SemanticNode>        tree;       // the panel's semantic tree, pre-order
};
[[nodiscard]] const PanelPicture& panel_picture();

// The panel magnified `zoom` times (screen pixels a 1× pixel), the point
// `look` of it (1× pixels) at `at` on the screen: its real pixels, square,
// when they are larger than the screen's, and a hairline between them.
void paint_panel(render::DisplayList& list, render::ImageStore& images, std::pair<render::ImageRef, render::ImageRef> refs, const PanelPicture& panel,
                 Size screen, Point at, Point look, double zoom, float opacity = 1);
// Where a point of the panel (1× pixels) is on the screen.
[[nodiscard]] Point panel_to_screen(Point p, Point at, Point look, double zoom) noexcept;

}  // namespace omui::promo
