// Kiln in a window (the release plan's RC2.5): the cavern shown in an interface, played with keys, the mouse or
// a gamepad.
//
//   return omui::run(kiln::window::app({.record = "session.kiln"}), options);
//
// The component `Cavern` is the game's window: it holds the same `kiln::Standalone` a headless program holds,
// steps it on the interface's clock (`meng.ui`'s pacer, 60 steps a second) and gives each step the person's
// input — A and D (or the arrows) walk, W or Space jumps (and swims up), the left button casts toward the pointer,
// the right button or Q chooses the next spell; or a gamepad: the left stick walks, A jumps, the right trigger
// casts the way she faces, Y chooses the next spell. The picture is Kiln's display list drawn on the CPU each frame
// and shown cell for cell; each frame's events are heard (`kiln::Sound`) through the system's output. Every
// step's input is recorded: what the window played is the game with no window (`replayed`), the same bits.
export module omui.showcase.kiln.window;

import std;
export import omui;
export import meng.ui;
export import omui.showcase.kiln;
export import omui.showcase.kiln.picture;
export import omui.showcase.kiln.sound;
export import meng.audio.device;
export import mrender.raster;
export import mrender.vector;

export namespace kiln::window {

// What the interface shows beside the cavern, kept by the cavern as it steps.
struct Hud {
    omui::Signal<std::string> health = omui::signal(std::string()), mana = omui::signal(std::string()), spell = omui::signal(std::string()),
                              said = omui::signal(std::string("A D walk, W jumps, the left button casts toward the pointer, Q the next spell — or a gamepad"));
};
// Where the cavern's sound goes.
enum class Hearing : std::uint8_t {
    speakers,  // the system's output
    silent,    // the null output: heard at real time, nothing sounds (tests, a machine with no output)
    none,      // not made at all
};
struct Options {
    std::filesystem::path record;  // none: not written; else each step's input, when the window closes
    Hearing               hearing = Hearing::speakers;
    std::string           level = "first";  // `kiln::level::named`
};

class Cavern {
public:
    static constexpr std::string_view name = "kiln-cavern";
    static constexpr bool focusable = true;
    // The picture's cells: 6 pixels a cell (768 × 384 for the first cavern).
    static constexpr int scale = 6;

    Cavern();
    Cavern(const Cavern&) = delete;
    Cavern& operator=(const Cavern&) = delete;
    ~Cavern();

    void mount(omui::Services& services, omui::NodeId node);
    omui::Size measure(omui::MeasureContext& ctx, std::optional<float> width, std::optional<float> height, mlibs::layout::Available across, mlibs::layout::Available down) {
        return view_.measure(ctx, width, height, across, down);
    }
    void paint(omui::PaintContext& ctx) const { view_.paint(ctx); }
    void semantics(omui::SemanticsContext& ctx, omui::spec::SemanticNode& node) const { view_.semantics(ctx, node); }
    bool on_event(omui::EventContext& ctx, const omui::InputEvent& event);

    // The game as played so far (none before it is mounted, or where the level could not be made).
    [[nodiscard]] const Standalone* game() const noexcept { return game_ ? &*game_ : nullptr; }
    [[nodiscard]] const meng::ui::Viewport& viewport() const noexcept { return view_; }
    [[nodiscard]] const mrender::raster::Surface& picture() const noexcept { return frame_; }
    [[nodiscard]] const Sound* sound() const noexcept { return sound_ ? &*sound_ : nullptr; }
    [[nodiscard]] const meng::audio::Device* output() const noexcept { return output_ ? &*output_ : nullptr; }
    [[nodiscard]] std::uint64_t sounds_begun() const noexcept { return sounds_begun_; }

private:
    void run(std::int32_t steps);
    void draw();

    omui::Services*                       services_ = nullptr;
    omui::NodeId                          node_{};
    meng::ui::Viewport                 view_;
    std::optional<Standalone>             game_;
    meng::Sampler                      sampler_;
    meng::ui::Pacer                    pacer_;
    mrender::ImageStore                   images_;
    std::optional<Picture>                picture_;
    mrender::DisplayList                  list_;
    mrender::CpuRenderer                  renderer_;
    mrender::raster::Surface              frame_{1, 1};
    std::optional<Sound>                  sound_;
    std::unique_ptr<meng::audio::Feed> feed_;
    std::optional<meng::audio::Device> output_;  // after the feed it drains: it goes first
    std::uint64_t                         sounds_begun_ = 0;
};

// The application: the cavern, and under it Mui's health, mana and spell.
[[nodiscard]] omui::AppSpec app(Options options = {});
// The HUD the cavern keeps (one window a process).
[[nodiscard]] const std::shared_ptr<Hud>& hud();
// The options the cavern was made with (set by `app`; one window a process).
[[nodiscard]] Options& options();
// Each step's input of the game a window played: `level NAME`, then a line a step (meng.input's `to_text`).
[[nodiscard]] std::string recorded(const Standalone& game);
// Those lines played by the game with no window: its state after them, or why not.
[[nodiscard]] std::expected<State, std::string> replayed(std::string_view lines);

}  // namespace kiln::window
