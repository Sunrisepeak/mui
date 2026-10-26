// openmui.raster:paint — what fills the covered pixels.
//
// Solid colours, gradients (linear, radial, conic; pad, repeat, reflect) and
// image patterns. Gradients are evaluated at pixel centres through a 256-entry
// lookup table interpolated in premultiplied sRGB, as browsers do; images are
// sampled bilinearly with 8-bit fixed-point weights.
export module openmui.raster:paint;

import std;
import openmui.geom;
import :surface;

export namespace omui::raster {

struct Stop {
    float offset = 0;
    Color color;
};

enum class GradientKind : std::uint8_t { linear, radial, conic };
enum class Spread : std::uint8_t { pad, repeat, reflect };
enum class Sampling : std::uint8_t { nearest, linear };

struct Paint {
    enum class Kind : std::uint8_t { solid, gradient, image };
    Kind kind = Kind::solid;
    Color color;  // solid; the tint (alpha) of an image

    // Gradient, in the paint's own space (mapped by `transform`).
    GradientKind          gradient = GradientKind::linear;
    Point                 start, end;        // linear: from/to; radial: centre and focal point; conic: centre
    float                 radius = 0;        // radial: radius; conic: start angle in radians
    Spread                spread = Spread::pad;
    std::span<const Stop> stops;

    // Image.
    ImageView image;
    Sampling  sampling = Sampling::linear;

    Affine transform;  // paint space to device space
    float  opacity = 1;

    [[nodiscard]] static Paint solid(Color c) noexcept {
        Paint p;
        p.color = c;
        return p;
    }
};

// A paint ready to produce pixels: the inverse transform and lookup table
// computed once per fill.
class Shader {
public:
    explicit Shader(const Paint& paint);
    [[nodiscard]] bool opaque_solid() const noexcept { return solid_ && solid_color_.a == 255; }
    [[nodiscard]] bool solid() const noexcept { return solid_; }
    [[nodiscard]] Rgba8 solid_color() const noexcept { return solid_color_; }
    // The premultiplied colours of `count` pixels of row y from column x.
    void shade(int x, int y, int count, Rgba8* out) const;

private:
    [[nodiscard]] Rgba8 gradient_at(float px, float py) const;
    [[nodiscard]] Rgba8 image_at(float px, float py) const;

    const Paint*          paint_;
    bool                  solid_ = false;
    Rgba8                 solid_color_{};
    std::optional<Affine> inverse_;
    std::array<Rgba8, 256> lut_{};
    std::uint32_t         opacity8_ = 255;
};

}  // namespace omui::raster
