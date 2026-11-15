// Swarm on screen (the release plan's RC2.13): the arena as a display list, and a window that shows it and is played.
//
//   xrender::DisplayList list;
//   swarm::picture::draw(game.state(), list, {0, 0, 960, 540});     // what the state looks like; changes nothing
//   return omui::run(swarm::window::app({.record = "session.swarm"}), options);
//
// The picture reads the state and nothing else: the arena, every enemy (its wave's colour) and bullet, the player
// with the gun's line toward `aim` (blinking while a touch costs nothing). The window's component `Arena` holds the
// same `swarm::Standalone` a headless program holds, steps it on the interface's clock (`xengine.ui`'s pacer, 120
// steps a second) and gives each step the person's input — W A S D (or the arrows) move, the pointer aims, the left
// button or Space shoots; or a gamepad: the left stick moves, A shoots. The picture is drawn on the CPU each frame and
// shown cell for cell. Every step's input is recorded: what the window played is the game with no window
// (`replayed`), the same bits.
export module omui.showcase.swarm.window;

import std;
export import omui;
export import xengine.ui;
export import omui.showcase.swarm.headless;
export import xrender.raster;
export import xrender.vector;
export import mlibs.geom;

export namespace swarm::picture {

// The state as a display list in `dest` (the whole arena, stretched to it): appended to `list`.
void draw(const State& state, xrender::DisplayList& list, mlibs::geom::Rect dest);

}  // namespace swarm::picture

export namespace swarm::window {

// What the interface shows beside the arena, kept by the arena as it steps.
struct Hud {
    omui::Signal<std::string> lives = omui::signal(std::string()), score = omui::signal(std::string()), wave = omui::signal(std::string()),
                              said = omui::signal(std::string("W A S D move, the pointer aims, the left button or Space shoots — or a gamepad"));
};
struct Options {
    std::filesystem::path record;  // none: not written; else each step's input, when the window closes
    std::uint64_t         seed = 1;  // which game (`swarm::start`)
};

class Arena {
public:
    static constexpr std::string_view name = "swarm-arena";
    static constexpr bool focusable = true;
    // Pixels a unit of the arena: 960 × 540.
    static constexpr int scale = 10;

    Arena();
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    ~Arena();

    void mount(omui::Services& services, omui::NodeId node);
    omui::Size measure(omui::MeasureContext& ctx, std::optional<float> width, std::optional<float> height, mlibs::layout::Available across, mlibs::layout::Available down) {
        return view_.measure(ctx, width, height, across, down);
    }
    void paint(omui::PaintContext& ctx) const { view_.paint(ctx); }
    void semantics(omui::SemanticsContext& ctx, omui::spec::SemanticNode& node) const { view_.semantics(ctx, node); }
    bool on_event(omui::EventContext& ctx, const omui::InputEvent& event);

    // The game as played so far (none before it is mounted).
    [[nodiscard]] const Standalone* game() const noexcept { return game_ ? &*game_ : nullptr; }
    [[nodiscard]] const xrender::raster::Surface& picture() const noexcept { return frame_; }

private:
    void run(std::int32_t steps);
    void draw();

    omui::Services*           services_ = nullptr;
    omui::NodeId              node_{};
    xengine::ui::Viewport     view_;
    std::optional<Standalone> game_;
    xengine::Sampler          sampler_;
    xengine::ui::Pacer        pacer_;
    xrender::DisplayList      list_;
    xrender::CpuRenderer      renderer_;
    xrender::raster::Surface  frame_{1, 1};
};

// The application: the arena, and under it the lives, the score and the wave.
[[nodiscard]] omui::AppSpec app(Options options = {});
// The HUD the arena keeps (one window a process).
[[nodiscard]] const std::shared_ptr<Hud>& hud();
// The options the arena was made with (set by `app`; one window a process).
[[nodiscard]] Options& options();
// Each step's input of the game a window played: `seed N`, then a line a step (xengine.input's `to_text`).
[[nodiscard]] std::string recorded(const Standalone& game);
// Those lines played by the game with no window: its state after them, or why not.
[[nodiscard]] std::expected<State, std::string> replayed(std::string_view lines);

}  // namespace swarm::window
