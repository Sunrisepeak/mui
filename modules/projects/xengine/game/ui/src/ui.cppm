// xengine.ui — a game inside openmui.
//
//     // in a component:
//     void mounted() { pacer_.start(services(), 120, [this](std::int32_t n) { while (n--) timeline_.step(sampler_.sample()); invalidate(); }); }
//     bool on_event(EventContext&, const InputEvent& e) { return xengine::ui::feed(sampler_, e, services().bounds(node())); }
//
// The pacer wakes at the next step due on the runtime's clock (virtual
// headless, so a headless second is exactly 120 steps), takes every step due
// (a stall past its catch-up is not owed) and asks for frames while it runs.
// Keys reach the sampler by their code (`code(Key)` for bindings), any
// gamepad's buttons and axes by their place (`pad(GamepadButton)`); the
// pointer within the game's area (0 … 1 across and down).
export module xengine.ui;

import std;
import omui.core;
import omui.view;
import xengine.input;
import mlibs.simulation;
import mlibs.agent;
import mlibs.layout;
import xrender.raster;
import xrender.vector;

export namespace xengine::ui {

[[nodiscard]] constexpr std::uint32_t code(omui::Key k) noexcept { return static_cast<std::uint32_t>(k); }
// A gamepad's button and axis by their place in the standard set (spec §6.1), for bindings.
[[nodiscard]] constexpr std::int32_t pad(omui::GamepadButton b) noexcept { return static_cast<std::int32_t>(b); }
[[nodiscard]] constexpr std::int32_t pad(omui::GamepadAxis a) noexcept { return static_cast<std::int32_t>(a); }

// An openmui event for the game: a key by its code, the pointer within
// `area` (window coordinates); focus lost lets everything go. Whether it took it.
bool feed(Sampler& in, const omui::InputEvent& e, omui::Rect area);

class Pacer {
public:
    Pacer() = default;
    Pacer(const Pacer&) = delete;
    Pacer& operator=(const Pacer&) = delete;
    ~Pacer() { stop(); }

    // `run(n)`: take n steps now. `hz`: steps a second.
    void start(omui::Services& services, double hz, std::function<void(std::int32_t)> run, std::int32_t catch_up = 8);
    void stop();
    [[nodiscard]] bool running() const noexcept { return services_ != nullptr; }
    // Its clock: `scale` for slow motion, `alpha` between two steps for drawing.
    [[nodiscard]] mlibs::simulation::Clock& clock() noexcept { return clock_; }

private:
    void wake();
    omui::Services*                    services_ = nullptr;
    mlibs::simulation::Clock           clock_;
    omui::FrameDemandLease             frames_;
    std::uint64_t                      timer_ = 0;
    std::function<void(std::int32_t)>  run_;
};

// A world shown in an interface: the node of the UI tree where it is (the plan's §4.3, R3). Layout, hit
// testing and semantics know where the world is; the node says which subject it shows, and the world's own
// questions are asked of that subject, not of the interface. Its picture is drawn elsewhere — the renderer's
// mesh pass, a field — and shown over the node's box.
//
//     omui::Element e;  e.type = &omui::component_type<xengine::ui::Viewport>();  e.id = "world";
//     // once mounted:
//     auto* viewport = runtime.component<xengine::ui::Viewport>(runtime.find("main/world"));
//     viewport->show({.label = "the yard", .subject = &world_subject, .pixels = [&] { return picture.view(); }});
//     // each time the picture is drawn again:
//     viewport->changed();
struct Viewport {
    static constexpr std::string_view name = "viewport";
    static constexpr bool focusable = true;

    struct Shown {
        std::string            label;               // what a person is told it is
        mlibs::agent::Subject* subject = nullptr;   // the world's own subject: found beside the interface while this is mounted
        std::function<std::optional<xrender::raster::ImageView>()> pixels;  // its picture (premultiplied); none: nothing yet
        float                  width = 480, height = 270;  // its size when the layout does not say
        bool                   pixels_stay = false;        // shown by the nearest pixel (a field of cells), not smoothed
    };
    Viewport() = default;
    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;
    ~Viewport() { clear(); }

    void show(Shown what);
    void clear();
    // The picture's content changed: the next frame shows it.
    void changed();
    [[nodiscard]] const Shown& shown() const noexcept { return shown_; }
    // A point of the node (its own coordinates) as a place of the picture, 0 to 1 across and down; outside: none.
    [[nodiscard]] std::optional<omui::Point> within(omui::Point local) const;

    void mount(omui::Services& services, omui::NodeId node) { services_ = &services, node_ = node; }
    omui::Size measure(omui::MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available across, mlibs::layout::Available down);
    void paint(omui::PaintContext& ctx) const;
    void semantics(omui::SemanticsContext&, omui::spec::SemanticNode& node) const;

private:
    omui::Services* services_ = nullptr;
    omui::NodeId    node_{};
    Shown           shown_;
    std::uint64_t   number_ = 0;   // what the display list names the picture by
    mutable omui::Size size_{};
};

}  // namespace xengine::ui
