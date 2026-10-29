// openmui.raster:paint — what fills the covered pixels.
//
// Solid colours, gradients (linear, radial, conic; pad, repeat, reflect) and
// image patterns. Gradients are evaluated at pixel centres through a 256-entry
// lookup table interpolated in premultiplied sRGB, as browsers do; images are
// sampled bilinearly with 8-bit fixed-point weights. An image can also be a
// mask (its alpha tinted with the paint's colour), and either can be lit by a
// Material: its normal map, sampled where the colour is, turned by the
// paint's transform and shaded by a light in device space.
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

// Stylised (cel) lighting of an image paint (render's Material, resolved):
// a tangent-space normal map aligned with the image (openmui.image's
// normal_map: R = x right, G = y down, B = out of the picture, 128 = 0), its
// xy turned by the rotation (and mirror) of the paint's transform, lit by a
// light whose direction is in device space (y down). Lambert, cut into
// `bands` levels with edges `softness` wide, darkens toward `shadow`
// (multiplied in) by `shadow_strength` where unlit; a rim where the normal
// grazes toward the light adds `rim`. See Shader::lit for the exact math.
struct Material {
    ImageView normal;                 // the normal map (any size: it is stretched over the image)
    float     light_x = -0.6f, light_y = -0.6f, light_z = 0.5f;  // toward the light; normalised by the shader
    Color     shadow{0.35f, 0.3f, 0.5f, 1};  // straight; multiplied into the unlit colour
    float     shadow_strength = 0.6f;
    int       bands = 2;              // 0 or 1: smooth lambert; n ≥ 2: n levels (at most 16)
    float     softness = 0.08f;       // a band edge's width, in lambert (0: hard)
    Color     rim{1, 1, 1, 1};        // straight; its alpha scales it
    float     rim_strength = 0.6f;
    float     rim_width = 0.35f;      // where n·z (the normal toward the viewer) is below this
    Color     key{1, 1, 1, 1};        // straight; the light's colour, multiplied into the lit part (white: none)
};

// What the lighting computes with, once per fill (the light normalised, the
// transform's rotation): exported so a GPU renderer shades with exactly the
// CPU's constants.
struct MaterialSetup {
    float lx = 0, ly = 0, lz = 1;     // unit, toward the light
    float fx = 0, fy = 0;             // the light's direction in the screen's plane, unit (0 when overhead)
    float r00 = 1, r01 = 0, r10 = 0, r11 = 1;  // image space to device space, a rotation or a rotation with a mirror
    float shadow_r = 1, shadow_g = 1, shadow_b = 1, shadow_strength = 0;
    float rim_r = 0, rim_g = 0, rim_b = 0, rim_strength = 0;  // rim premultiplied by its alpha
    float rim_width = 0;              // the rim where n·z is below this
    float inv_softness = 0;           // 1 / softness (0: hard edges)
    int   bands = 0;                  // 0: smooth
    float inv_levels = 0, inv_steps = 0;  // 1 / bands, 1 / (bands − 1)
    float key_r = 1, key_g = 1, key_b = 1;  // the light's colour
};
[[nodiscard]] MaterialSetup material_setup(const Material& m, const Affine& transform) noexcept;
// The orthogonal part of a linear map (polar decomposition, 2 × 2): a
// rotation, or a reflection when the map mirrors (its determinant < 0) —
// how a normal in image space turns into device space.
[[nodiscard]] std::array<float, 4> orthogonal_part(const Affine& m) noexcept;

struct Paint {
    // image_mask: the image's alpha as coverage of `color` (an ink stroke's
    // silhouette in any colour).
    enum class Kind : std::uint8_t { solid, gradient, image, image_mask };
    Kind kind = Kind::solid;
    Color color;  // solid; the tint (alpha) of an image

    // Gradient, in the paint's own space (mapped by `transform`).
    GradientKind          gradient = GradientKind::linear;
    Point                 start, end;        // linear: from/to; radial: centre and focal point; conic: centre
    float                 radius = 0;        // radial: radius; conic: start angle in radians
    Spread                spread = Spread::pad;
    std::span<const Stop> stops;

    // Image (and image_mask).
    ImageView       image;
    Sampling        sampling = Sampling::linear;
    const Material* material = nullptr;  // lit (image and image_mask); none: as before

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
    // A gradient's lookup table: entry i is the colour at t = i / 255 (the
    // paint's opacity applied), looked up at int(t × 255 + 0.5) — what a GPU
    // renderer uploads to shade exactly as this one does.
    [[nodiscard]] std::span<const Rgba8, 256> table() const noexcept { return lut_; }

    // A premultiplied colour lit by a normal map's texel (the material's
    // shading, exactly: what the GPU's shader computes the same way).
    [[nodiscard]] static Rgba8 lit(Rgba8 color, Rgba8 normal, const MaterialSetup& m) noexcept;

private:
    [[nodiscard]] Rgba8 gradient_at(float px, float py) const;
    [[nodiscard]] Rgba8 image_at(float px, float py) const;
    [[nodiscard]] Rgba8 texel_at(const ImageView& img, Point q) const;
    [[nodiscard]] Rgba8 shaded_at(float px, float py) const;  // image_mask, or a material

    const Paint*          paint_;
    std::optional<MaterialSetup> material_;
    bool                  solid_ = false;
    Rgba8                 solid_color_{};
    std::optional<Affine> inverse_;
    std::array<Rgba8, 256> lut_{};
    std::uint32_t         opacity8_ = 255;
};

}  // namespace omui::raster
