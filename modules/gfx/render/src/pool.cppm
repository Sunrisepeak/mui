// openmui.render:pool — transient textures, pooled from frame to frame.
//
// A frame asks for textures by what they are (size, format, usage); the pool
// hands out one it already made when it has one free, and makes one when it
// has not. At the next frame every texture is free again, and one nobody
// asked for in `keep` frames is destroyed (a size the window no longer has).
// So a steady interface makes no textures at all from frame to frame.
export module openmui.render:pool;

import std;
import openmui.rhi;

export namespace omui::render {

// What a transient texture is; the pool hands out one of the same again.
struct TextureNeed {
    std::uint32_t     width = 0, height = 0;
    rhi::Format       format = rhi::Format::rgba8_unorm;
    rhi::TextureUsage usage = rhi::TextureUsage::sampled;
    friend constexpr bool operator==(const TextureNeed&, const TextureNeed&) = default;
};

class TexturePool {
public:
    explicit TexturePool(rhi::Device& device, std::uint32_t keep = 120) : device_(&device), keep_(keep) {}
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
    [[nodiscard]] rhi::Result<rhi::Texture> acquire(const TextureNeed& need, std::string_view label);

    // Whether `t` is one of the pool's.
    [[nodiscard]] bool owns(rhi::Texture t) const noexcept;

    [[nodiscard]] std::size_t held() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t made() const noexcept { return made_; }
    [[nodiscard]] std::size_t released() const noexcept { return released_; }

private:
    struct Entry {
        rhi::Texture  texture;
        TextureNeed   need;
        bool          used = false;
        std::uint64_t last = 0;
    };
    void destroy_all() noexcept;

    rhi::Device*       device_;
    std::uint32_t      keep_;
    std::vector<Entry> entries_;
    std::uint64_t      frame_ = 0;
    std::size_t        made_ = 0, released_ = 0;
};

}  // namespace omui::render
