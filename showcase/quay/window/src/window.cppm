// Quay in a window (the FPS slice design, F5): the harbour shown in an interface, played with keys and the mouse.
//
//   return omui::run(quay::window::app({.record = "session.quay"}), options);
//
// The component `Harbour` is the game's window: it holds the same `quay::Standalone` a headless program holds,
// steps it on the interface's clock (`xengine.ui`'s pacer, 60 steps a second) and gives each step the person's
// input — W A S D to walk, the mouse to look (a press keeps the pointer to the world; Escape lets it go), the left
// button to fire, Space to jump, E to work the beacon (or a fuse box), Shift to run; or a gamepad: the left stick
// walks, the right stick looks, the right trigger (or R1) fires, A jumps, X works, the left stick pressed runs. The picture is drawn each frame on the host's
// device (`quay::picture::OnDevice`) and shown as it is; a host that draws on the CPU is shown the reference's
// pixels. Each step's events are heard (`quay::Sound`: sounds in a place, at the watchkeeper's ears) — the window
// renders them a block at a time on its own thread and keeps the system's output a little ahead (a `Feed`).
// Every step's input is recorded: what the window played is the game with no window — the recording
// played by the runtime alone gives the same bits (`quay-play replay`).
export module omui.showcase.quay.window;

import std;
export import omui;
export import xengine.ui;
export import omui.showcase.quay.headless;
export import omui.showcase.quay.picture;
export import omui.showcase.quay.sound;
export import xengine.audio.device;

export namespace quay::window {

// What the interface shows beside the world, kept by the harbour as it steps.
struct Hud {
    omui::Signal<std::string> health = omui::signal(std::string()), rounds = omui::signal(std::string()), tide = omui::signal(std::string()),
                              said = omui::signal(std::string("Click the harbour to look about; W A S D, Space, E, Shift, the left button — or a gamepad"));
};

// Where the harbour's sound goes.
enum class Hearing : std::uint8_t {
    speakers,  // the system's output
    silent,    // the null output: heard at real time, nothing sounds (tests, a machine with no output)
    none,      // not made at all
};

struct Options {
    std::filesystem::path record;  // none: not written; else each step's input, when the window closes
    Hearing               hearing = Hearing::speakers;
    std::string           level = "harbour";  // `quay::level::named`: "harbour", "second-night"
};

class Harbour {
public:
    static constexpr std::string_view name = "quay-harbour";
    static constexpr bool focusable = true;

    Harbour();
    Harbour(const Harbour&) = delete;
    Harbour& operator=(const Harbour&) = delete;
    ~Harbour();

    void mount(omui::Services& services, omui::NodeId node);
    omui::Size measure(omui::MeasureContext& ctx, std::optional<float> width, std::optional<float> height, mlibs::layout::Available across, mlibs::layout::Available down) {
        return view_.measure(ctx, width, height, across, down);
    }
    void paint(omui::PaintContext& ctx) const { view_.paint(ctx); }
    void semantics(omui::SemanticsContext& ctx, omui::spec::SemanticNode& node) const { view_.semantics(ctx, node); }
    bool on_event(omui::EventContext& ctx, const omui::InputEvent& event);

    // The game as played so far (none before it is mounted).
    [[nodiscard]] const Standalone* game() const noexcept { return game_ ? &*game_ : nullptr; }
    [[nodiscard]] const xengine::ui::Viewport& viewport() const noexcept { return view_; }
    // Whether the picture is drawn on the host's device and shown as it is (else: the reference's pixels).
    [[nodiscard]] bool drawn_on_device() const noexcept { return on_device_.has_value(); }
    // What the window hears (none: not made, or no output opened) and the output it plays through.
    [[nodiscard]] const Sound* sound() const noexcept { return sound_ ? &*sound_ : nullptr; }
    [[nodiscard]] const xengine::audio::Device* output() const noexcept { return output_ ? &*output_ : nullptr; }
    [[nodiscard]] const xengine::audio::Feed* feed() const noexcept { return feed_.get(); }
    [[nodiscard]] std::uint64_t sounds_begun() const noexcept { return sounds_begun_; }

private:
    void run(std::int32_t steps);
    void draw();
    [[nodiscard]] Input input();

    omui::Services*                          services_ = nullptr;
    omui::NodeId                             node_{};
    xengine::ui::Viewport                    view_;
    std::optional<Standalone>                game_;
    xengine::Sampler                         sampler_;
    xengine::ui::Pacer                       pacer_;
    std::optional<picture::Picture>          picture_;
    std::optional<picture::OnDevice>         on_device_;
    mlibs::rhi::CommandList                  list_;
    std::optional<Sound>                     sound_;
    std::unique_ptr<xengine::audio::Feed>    feed_;
    std::optional<xengine::audio::Device>    output_;   // after the feed it drains: it goes first
    std::uint64_t                            sounds_begun_ = 0;
    double                                   turning_ = 0, looking_ = 0;  // radians of mouse look not yet given to a step
};

// The application: the harbour, and under it the watchkeeper's health, rounds and the tide.
[[nodiscard]] omui::AppSpec app(Options options = {});
// The HUD the harbour keeps (one window a process).
[[nodiscard]] const std::shared_ptr<Hud>& hud();
// The options the harbour was made with (set by `app`; one window a process).
[[nodiscard]] Options& options();
// Each step's input of the game a window played, as `quay-play replay` reads it: the level's name (`level NAME`,
// a level other than the harbour), then a line a step.
[[nodiscard]] std::string recorded(const Standalone& game);
// Those lines played by the game with no window, from the harbour's start: its state after them, or why not.
[[nodiscard]] std::expected<State, std::string> replayed(std::string_view lines);

}  // namespace quay::window
