// openmui.render:pool — transient textures, pooled from frame to frame.
//
// A frame asks for textures by what they are (size, format, usage); the pool
// hands out one it already made when it has one free, and makes one when it
// has not. At the next frame every texture is free again, and one nobody
// asked for in `keep` frames is destroyed (a size the window no longer has).
// So a steady interface makes no textures at all from frame to frame.
//
// A history is the other kind: a texture kept by name from one frame to the
// next, for what gathers over time (anti-aliasing from frames taken a little
// aside, anything accumulated). A frame asks for it by its name and is given
// two — `now`, to write, and `before`, what was written under that name the
// last time — and how many frames ago that was. Nobody else is handed either.
export module xrender.graph.api:pool;

import std;
import mlibs.rhi;

export namespace xrender {

// What a transient texture is; the pool hands out one of the same again.
struct TextureNeed {
    std::uint32_t     width = 0, height = 0;
    mlibs::rhi::Format       format = mlibs::rhi::Format::rgba8_unorm;
    mlibs::rhi::TextureUsage usage = mlibs::rhi::TextureUsage::sampled;
    friend constexpr bool operator==(const TextureNeed&, const TextureNeed&) = default;
};

// A history as one frame has it. `past`: how many frames ago `before` was
// written — 1 when that was the last frame; 0 when it holds nothing (the name
// is new, or what it is changed), and what gathers begins again.
struct History {
    mlibs::rhi::Texture now, before;
    std::uint32_t       past = 0;
};

class TexturePool {
public:
    explicit TexturePool(mlibs::rhi::Device& device, std::uint32_t keep = 120) : device_(&device), keep_(keep) {}
    TexturePool(const TexturePool&) = delete;
    TexturePool& operator=(const TexturePool&) = delete;
    TexturePool(TexturePool&& other) noexcept;
    TexturePool& operator=(TexturePool&& other) noexcept;
    ~TexturePool();

    // A frame begins: every texture is free again; one unused for `keep`
    // frames is destroyed.
    void begin_frame();

    // The frame starts over (encoded again from the beginning): what it was
    // handed is free again, nothing is destroyed.
    void restart_frame() noexcept;

    // A texture of what `need` says, free until the next begin_frame().
    [[nodiscard]] mlibs::rhi::Result<mlibs::rhi::Texture> acquire(const TextureNeed& need, std::string_view label);

    // The history called `name`, of what `need` says: asked again in the same
    // frame (or after restart_frame) it is the same pair; in a later frame the
    // two have changed places. One nobody asked for in `keep` frames is
    // destroyed, and so is one whose need changed (its past is of another size).
    [[nodiscard]] mlibs::rhi::Result<History> history(std::string_view name, const TextureNeed& need, std::string_view label);
    // What a history gathered is no longer true (a cut, a jump): the next
    // time it is asked for its past is 0. Of a name nobody asked for: nothing.
    void forget(std::string_view name) noexcept;
    [[nodiscard]] std::size_t histories() const noexcept { return histories_.size(); }

    // Whether `t` is one of the pool's.
    [[nodiscard]] bool owns(mlibs::rhi::Texture t) const noexcept;

    [[nodiscard]] std::size_t held() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t made() const noexcept { return made_; }
    [[nodiscard]] std::size_t released() const noexcept { return released_; }

private:
    struct Entry {
        mlibs::rhi::Texture  texture;
        TextureNeed   need;
        bool          used = false;
        std::uint64_t last = 0;
    };
    struct Kept {
        std::string                        name;
        TextureNeed                        need;
        std::array<mlibs::rhi::Texture, 2> textures;
        std::uint32_t                      now = 0;      // which of the two this frame writes
        std::uint64_t                      asked = 0;    // the frame it was last asked for
        std::uint32_t                      past = 0;     // as that frame was told
        bool                               written = false;  // `before` holds a frame
    };
    void destroy_all() noexcept;

    mlibs::rhi::Device*       device_;
    std::uint32_t      keep_;
    std::vector<Entry> entries_;
    std::vector<Kept>  histories_;
    std::uint64_t      frame_ = 0;
    std::size_t        made_ = 0, released_ = 0;
};

}  // namespace xrender
