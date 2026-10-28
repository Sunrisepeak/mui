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
export module openmui.host.headless;

import std;
export import openmui.core;
export import openmui.view;
export import openmui.semantic;
export import openmui.agent;
export import openmui.raster;
export import openmui.image;  // recordings: imageio::AnimatedWebpOptions

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
using RecordingOptions = imageio::AnimatedWebpOptions;

struct HeadlessOptions {
    HeadlessLevel           level = HeadlessLevel::pixels;
    Size                    size{800, 600};
    float                   scale = 1;
    hanim::FrameRate         fps{60, 1};
    Color                   background = Color::white();
    Theme                   theme = Theme::light();
    typeset::FontCollection* fonts = nullptr;  // nullptr: the default fonts
    bool                    reduce_motion = false;
    std::string             app = "openmui-app";  // the application id (endpoint name, descriptor)
    std::filesystem::path   endpoint_directory;   // H3; default: agent::default_directory()
    bool                    parallel_raster = true;  // bands of rows at the same time (the same bytes)
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
    // One frame, one frame period later (the first frame is at 0).
    FrameOutput step(std::vector<InputEvent> events = {});
    // Frames until `duration` has passed.
    FrameOutput advance(seconds duration);
    // Frames until nothing animates, no timer is due within `limit`, and no
    // agent waits; answers how many frames ran.
    std::size_t settle(seconds limit = seconds(10));
    [[nodiscard]] seconds now() const noexcept;
    [[nodiscard]] std::int64_t frame_index() const noexcept;

    // ── input, as a person gives it ──
    void move(Point p);
    void click(Point p, MouseButton button = MouseButton::left);
    // Clicks the centre of a node; false when there is no such node or it is not visible.
    bool click(std::string_view id);
    void key(Key key, Modifiers mods = {});
    void type(std::string_view text);
    void scroll(Point at, Point delta, bool precise = true);
    void focus_next(bool backward = false);

    // ── pixels (H2 and up) ──
    // The current frame's pixels (rendered when the display list changed).
    [[nodiscard]] const raster::Surface& pixels();
    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> png(std::optional<Rect> region = {});
    std::expected<void, std::string> save_png(const std::filesystem::path& file, std::optional<Rect> region = {});
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
    // A capture function for the agent protocol.
    [[nodiscard]] agent::CaptureFn capture();

    // ── H3 ──
    [[nodiscard]] agent::Endpoint* endpoint() noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui
