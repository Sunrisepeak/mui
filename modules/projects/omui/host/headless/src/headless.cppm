// openmui.host.headless — openmui without a display (plan §8.2).
//
// A Headless owns a runtime on a clock it steps itself (one frame per step,
// at the frame rate it was given), injects input the way a person would
// (by point, or by id: "click main/save"), renders with the CPU reference
// renderer into a surface, writes PNG, and, at H3, serves the agent endpoint
// between frames. The same view gives the same semantic tree at every level,
// and the same pixels on every platform.
//
//     omui::Headless h({.size = {640, 480}});
//     h.mount("main", app());
//     h.click("main/save");
//     h.settle();
//     h.save_png("after-save.png");
export module omui.host.headless;

import std;
export import omui.core;
export import omui.view;
export import omui.semantic;
export import omui.agent;
export import xrender.raster;
export import mlibs.image;  // recordings: imageio::AnimatedWebpOptions

export namespace omui {

// The four headless levels (unified-semantics research §7.3).
enum class HeadlessLevel : std::uint8_t {
    semantics,  // H0: build and reconcile, no layout
    layout,     // H1: bounds and hit testing, no pixels
    pixels,     // H2: CPU raster, PNG
    session,    // H3: pixels and an agent endpoint
};

// A recording's timing (each frame `delay_num / delay_den` seconds, `loops`
// 0 forever) and, written to a `.webp` file, its coding: an animated WebP,
// lossy at quality 80 unless asked otherwise (a README loop a fraction of its
// APNG's size). Any other name is an animated PNG, which takes the timing alone.
using RecordingOptions = mlibs::image::AnimatedWebpOptions;

struct HeadlessOptions {
    HeadlessLevel           level = HeadlessLevel::pixels;
    Size                    size{800, 600};
    float                   scale = 1;
    mlibs::time::Rate         fps{60, 1};
    Color                   background = Color::white();
    Theme                   theme = Theme::light();
    mlibs::text::FontCollection* fonts = nullptr;  // nullptr: the default fonts
    bool                    reduce_motion = false;
    std::string             app = "openmui-app";  // the application id (endpoint name, descriptor)
    std::filesystem::path   endpoint_directory;   // H3; default: agent::default_directory()
    std::string             endpoint_name;        // H3; default: the application id (two of one application: another name)
    // H3: agents may give input as a person's devices would (the protocol's
    // `input`), decided as their requests over the bus are (S-10, S-11).
    bool                    agent_input = true;
    bool                    parallel_raster = true;  // bands of rows at the same time (the same bytes)
    // Every frame as it would be at rest (Services::complete_frames): any
    // frame rendered directly is the one the frames before it lead to —
    // a video's frames rendered out of order, a still.
    bool                    complete_frames = false;
    // The tier it draws as (performance tiers §1: a test of how an ESP32 or a
    // phone draws says T0 or T2). Its governor stays off: frames are the test's.
    mlibs::pipeline::Tier          tier = mlibs::pipeline::Tier::t3;
};

class Headless {
public:
    explicit Headless(HeadlessOptions options = {});
    ~Headless();
    Headless(const Headless&) = delete;
    Headless& operator=(const Headless&) = delete;

    [[nodiscard]] Runtime& runtime() noexcept;
    [[nodiscard]] semantic::Tree& tree() noexcept;
    [[nodiscard]] const HeadlessOptions& options() const noexcept;

    NodeId mount(std::string_view slot, View view);

    // ── time ──
    // One frame, one frame period later (the first frame is at 0) — or at
    // `at`, a recorded frame's time (a replay, W12).
    FrameOutput step(std::vector<InputEvent> events = {}, std::optional<seconds> at = std::nullopt);
    // Frames until `duration` has passed.
    FrameOutput advance(seconds duration);
    // Frames until nothing animates, no timer is due within `limit`, and no
    // agent waits; answers how many frames ran.
    std::size_t settle(seconds limit = seconds(10));
    [[nodiscard]] seconds now() const noexcept;
    [[nodiscard]] std::int64_t frame_index() const noexcept;

    // ── input, as a person gives it ──
    void move(Point p);
    // The hand moved this far, the pointer where it is: what a pointer kept to a node is told (`movement`).
    void move_by(Point movement);
    // Whether the interface holds the pointer (Services::lock_pointer): this host always lets it.
    [[nodiscard]] bool pointer_held() const noexcept;
    void click(Point p, MouseButton button = MouseButton::left);
    // Clicks the centre of a node; false when there is no such node or it is not visible.
    bool click(std::string_view id);
    void key(Key key, Modifiers mods = {});
    void type(std::string_view text);
    void scroll(Point at, Point delta, bool precise = true);
    void focus_next(bool backward = false);

    // ── pixels (H2 and up) ──
    // The current frame's pixels (rendered when the display list changed).
    [[nodiscard]] const xrender::raster::Surface& pixels();
    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> png(std::optional<Rect> region = {});
    // The window's picture with a camera on `frame` (logical pixels): that region filling the
    // window, its shape kept and centred, drawn at the window's resolution (a film's close-up).
    // What the slot `fixed` holds (a caption) is drawn over it as it is, outside the camera.
    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> framed_png(Rect frame, std::string_view fixed = {});
    std::expected<void, std::string> save_png(const std::filesystem::path& file, std::optional<Rect> region = {});
    // The same frame as vectors (xrender.vector.svg): SVG at any size, each
    // node's drawing a group marked with its path (data-node), and the
    // pieces SVG cannot say kept as the reference renderer's pixels, listed.
    [[nodiscard]] std::expected<agent::VectorCapture, std::string> svg(std::optional<Rect> region = {});
    // A recording for an animation (a README's loop, a bug report): each
    // `record()` keeps the current frame (of `region`, the first's for the
    // rest); `save_recording` writes them and starts afresh: an animated WebP
    // for a `.webp` file (imageio::encode_animated_webp), else an animated
    // PNG (imageio::encode_apng).
    void record(std::optional<Rect> region = {});
    [[nodiscard]] std::size_t recorded() const noexcept;
    std::expected<void, std::string> save_recording(const std::filesystem::path& file, RecordingOptions options = {});
    // The node that drew a pixel (logical coordinates), through the object-id buffer.
    [[nodiscard]] NodeId node_at_pixel(Point p);
    // Capture functions for the agent protocol: pixels, and vectors.
    [[nodiscard]] agent::CaptureFn capture();
    [[nodiscard]] agent::CaptureSvgFn capture_svg();

    // ── H3 ──
    [[nodiscard]] agent::Endpoint* endpoint() noexcept;
    // Held, no agent's request is served: one that came while the application was not ready (its scripts
    // still loading) waits, and is answered from the application as it stands once released — never from
    // a window with nothing in it yet.
    void hold_requests(bool held) noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui
