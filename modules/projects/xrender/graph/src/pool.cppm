// openmui.render:pool — transient textures, pooled from frame to frame.
//
// A frame asks for textures by what they are (size, format, usage); the pool
// hands out one it already made when it has one free, and makes one when it
// has not. At the next frame every texture is free again, and one nobody
// asked for in `keep` frames is destroyed (a size the window no longer has).
// So a steady interface makes no textures at all from frame to frame.
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
    void destroy_all() noexcept;

    mlibs::rhi::Device*       device_;
    std::uint32_t      keep_;
    std::vector<Entry> entries_;
    std::uint64_t      frame_ = 0;
    std::size_t        made_ = 0, released_ = 0;
};

}  // namespace xrender
