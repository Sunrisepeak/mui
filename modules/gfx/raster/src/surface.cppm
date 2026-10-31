// openmui.raster:surface — pixels and coverage masks.
//
// A Surface is premultiplied RGBA, 8 bits per channel, rows top to bottom
// (the render interface's format, spec §7 R-1; a host that wants BGRA swaps
// at presentation). A Mask is 8-bit coverage. Both own their memory; views
// borrow it.
export module openmui.raster:surface;

import std;
import openmui.geom;

export namespace omui::raster {

using geom::Affine;
using geom::Color;
using geom::PixelRect;
using geom::Point;
using geom::Rect;

// One premultiplied pixel.
struct Rgba8 {  // four bytes, R G B A: a surface row is a byte row
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
    friend constexpr bool operator==(Rgba8 x, Rgba8 y) noexcept { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; }
};

[[nodiscard]] constexpr Rgba8 premultiply(const Color& c) noexcept {
    auto p = c.premultiplied8();
    return {p[0], p[1], p[2], p[3]};
}

// x·y/255 rounded to nearest, exactly, for 8-bit values.
[[nodiscard]] constexpr std::uint32_t mul255(std::uint32_t x, std::uint32_t y) noexcept {
    std::uint32_t t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

struct ImageView {
    const Rgba8* pixels = nullptr;
    int          width = 0;
    int          height = 0;
    int          stride = 0;  // in pixels

    [[nodiscard]] const Rgba8& at(int x, int y) const noexcept { return pixels[static_cast<std::size_t>(y) * stride + x]; }
};

class Surface {
public:
    Surface() = default;
    Surface(int width, int height);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] Rgba8* row(int y) noexcept { return pixels_.data() + static_cast<std::size_t>(y) * width_; }
    [[nodiscard]] const Rgba8* row(int y) const noexcept { return pixels_.data() + static_cast<std::size_t>(y) * width_; }
    [[nodiscard]] Rgba8& at(int x, int y) noexcept { return row(y)[x]; }
    [[nodiscard]] const Rgba8& at(int x, int y) const noexcept { return row(y)[x]; }
    [[nodiscard]] std::span<Rgba8> pixels() noexcept { return pixels_; }
    [[nodiscard]] std::span<const Rgba8> pixels() const noexcept { return pixels_; }
    // The pixels as bytes, R G B A in order.
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept {
        return {reinterpret_cast<const std::uint8_t*>(pixels_.data()), pixels_.size() * 4};
    }
    [[nodiscard]] ImageView view() const noexcept { return {pixels_.data(), width_, height_, width_}; }

    void clear(Rgba8 value = {});
    void resize(int width, int height);
    // A hash of the pixels (FNV-1a), for golden tests.
    [[nodiscard]] std::uint64_t hash() const noexcept;

private:
    int                width_ = 0;
    int                height_ = 0;
    std::vector<Rgba8> pixels_;
};

class Mask {
public:
    Mask() = default;
    Mask(int width, int height, std::uint8_t fill = 0);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::uint8_t* row(int y) noexcept { return data_.data() + static_cast<std::size_t>(y) * width_; }
    [[nodiscard]] const std::uint8_t* row(int y) const noexcept { return data_.data() + static_cast<std::size_t>(y) * width_; }
    [[nodiscard]] std::uint8_t at(int x, int y) const noexcept { return row(y)[x]; }
    [[nodiscard]] std::span<std::uint8_t> data() noexcept { return data_; }
    [[nodiscard]] std::span<const std::uint8_t> data() const noexcept { return data_; }
    void fill(std::uint8_t v);
    void resize(int width, int height, std::uint8_t fill = 0);

private:
    int                       width_ = 0;
    int                       height_ = 0;
    std::vector<std::uint8_t> data_;
};

// Coverage of a shape, in a rectangle of a surface (a mask with an origin):
// what rasterising produces and what compositing consumes.
struct Coverage {
    PixelRect                 bounds;   // where `values` lies on the target
    std::vector<std::uint8_t> values;   // bounds.width × bounds.height

    [[nodiscard]] std::uint8_t at(int x, int y) const noexcept {
        return values[static_cast<std::size_t>(y - bounds.y) * bounds.width + (x - bounds.x)];
    }
    [[nodiscard]] bool empty() const noexcept { return bounds.empty(); }
};

}  // namespace omui::raster
