// openmui.site:reference — the page's reference run: a few fixed seconds of
// the site (a flight, a language switch, a lens), played offscreen at CI's
// size, each sampled frame's pixels hashed the same way in CI and in a
// visitor's browser. The footer compares the browser's hash of a frame with
// the one CI pinned (reference/ci.frames.omui.json, embedded): the same
// pixels, bit for bit, or not.
export module openmui.site:reference;

import std;
import openmui;

export namespace omui::site {

// A step of the reference run: at a second of it, what a person does.
struct ReferenceStep {
    double           at;
    std::string_view node;  // the control pressed (the site's own ids)
};
[[nodiscard]] std::span<const ReferenceStep> reference_script();

inline constexpr double reference_fps = 10;          // frames sampled a second
inline constexpr Size   reference_size{1280, 800};   // CI's window
[[nodiscard]] std::int32_t reference_frames();        // how many

// A frame's hash: FNV-1a over its rows' RGBA bytes, top to bottom.
[[nodiscard]] std::uint64_t frame_hash(const raster::Surface& pixels) noexcept;

// CI's hashes of the reference frames, in order (empty: none pinned yet).
[[nodiscard]] std::span<const std::uint64_t> reference_pinned();

// The reference run played on a headless runtime of its own: a frame asked
// for is played to (from the start again when it lies behind) and drawn as
// it would be at rest (complete frames: the canvas's textures in, whatever
// the order frames are asked for), with exactly the fonts the page carries.
class ReferenceReplay {
public:
    ReferenceReplay();
    ~ReferenceReplay();
    ReferenceReplay(const ReferenceReplay&) = delete;
    ReferenceReplay& operator=(const ReferenceReplay&) = delete;

    // The frame's pixels' hash.
    [[nodiscard]] std::uint64_t hash(std::int32_t frame);
    // The pixels of the frame last asked for, and as a PNG.
    [[nodiscard]] const raster::Surface& pixels();
    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> png();

private:
    void restart();
    struct State;
    std::unique_ptr<State> s_;
};

}  // namespace omui::site
