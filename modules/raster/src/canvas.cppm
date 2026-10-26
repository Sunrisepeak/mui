// openmui.raster:canvas — drawing onto a surface.
//
// Fills, strokes, images and shadows composite source-over in premultiplied
// sRGB-encoded 8-bit arithmetic with exact /255 rounding (spec R-1); layers
// composite with the W3C Compositing and Blending modes. Clips are a scissor
// rectangle and, for anything else, a coverage mask. An optional object-id
// buffer records which node drew each pixel — the CPU half of pixel
// provenance (devtools trace a pixel to the source that produced it).
export module openmui.raster:canvas;

import std;
import openmui.geom;
import :surface;
import :rasterizer;
import :paint;
import :stroke;

export namespace omui::raster {

using geom::CornerRadii;

enum class Blend : std::uint8_t {
    normal, multiply, screen, overlay, darken, lighten, color_dodge, color_burn, hard_light, soft_light,
    difference, exclusion, plus, erase,
};

class Canvas {
public:
    explicit Canvas(Surface& target);
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    // Starts over on `target`: no clips, no layers, no row restriction. The
    // buffers (rasteriser cells, layer surfaces, clip masks) are kept, so a
    // renderer that reuses its canvas allocates nothing per frame.
    void reset(Surface& target);

    // Draws only rows [top, bottom) and leaves the others as they are. The
    // pixels in those rows are byte-for-byte what drawing the whole surface
    // gives: rows rasterise independently, and the columns are clipped as
    // they are for the whole surface. This is what damage-region rendering
    // stands on. Call before any clip or layer is pushed.
    void restrict_rows(std::int32_t top, std::int32_t bottom);

    void fill_path(PathData path, FillRule rule, const Paint& paint, const Affine& m = {});
    void stroke_path(PathData path, const Stroke& stroke, const Paint& paint, const Affine& m = {});
    void fill_rect(const Rect& r, const Paint& paint, const Affine& m = {});
    void fill_round_rect(const Rect& r, const CornerRadii& radii, const Paint& paint, const Affine& m = {});
    // Composites a coverage mask, its bounds moved by (dx, dy) device pixels
    // (glyph masks are made once and placed many times).
    void fill_coverage(const Coverage& coverage, const Paint& paint, int dx = 0, int dy = 0);
    void draw_image(const ImageView& image, const Rect& source, const Rect& dest, Sampling sampling, float opacity,
                    const Affine& m = {});
    // CSS box-shadow: blur is the blur radius (σ = blur / 2).
    void box_shadow(const Rect& r, const CornerRadii& radii, Point offset, float blur, float spread, Color color,
                    bool inset, const Affine& m = {});

    void push_clip_rect(const Rect& r, const Affine& m = {});
    void push_clip_round_rect(const Rect& r, const CornerRadii& radii, const Affine& m = {});
    void push_clip_path(PathData path, FillRule rule, const Affine& m = {});
    void pop_clip();

    // Subsequent drawing goes to a transparent layer, composited by pop_layer.
    void push_layer(float opacity, Blend blend = Blend::normal);
    void pop_layer();

    // The node id written into the object-id buffer by subsequent drawing.
    void set_object_id(std::uint32_t id) noexcept { object_id_ = id; }
    // An object-id buffer the size of the target; kept across reset() while
    // the size is the same, so a partial render updates only its rows
    // (clear_object_ids clears them first).
    void enable_object_ids();
    void clear_object_ids(std::int32_t top, std::int32_t bottom) noexcept;
    [[nodiscard]] std::span<const std::uint32_t> object_ids() const noexcept { return ids_; }

    [[nodiscard]] PixelRect bounds() const noexcept { return {0, 0, base_->width(), base_->height()}; }
    [[nodiscard]] std::size_t depth() const noexcept { return clips_.size() + layers_.size(); }

private:
    struct Clip {
        PixelRect             scissor;
        std::shared_ptr<Mask> mask;  // nullptr: the scissor alone
    };
    struct Layer {
        std::unique_ptr<Surface> surface;  // from layer_pool_, back to it at pop_layer
        float                    opacity;
        Blend                    blend;
        std::size_t              clip_depth;
    };
    std::shared_ptr<Mask> take_mask();

    [[nodiscard]] Surface& target() noexcept { return layers_.empty() ? *base_ : *layers_.back().surface; }
    [[nodiscard]] const Clip& clip() const noexcept { return clips_.back(); }
    void composite(const Coverage& coverage, const Shader& shader, int dx = 0, int dy = 0);
    void push_mask(const Coverage& coverage);

    Surface*                   base_;
    std::vector<Clip>          clips_;
    std::vector<Layer>         layers_;
    std::vector<std::uint32_t> ids_;
    std::uint32_t              object_id_ = 0;
    Rasterizer                 rasterizer_;
    Polygons                   polygons_;
    Coverage                   coverage_;
    std::vector<Rgba8>         row_;
    std::vector<std::unique_ptr<Surface>> layer_pool_;
    std::vector<std::shared_ptr<Mask>>    mask_pool_;
    PathBuilder                path_;      // scratch for rectangles and rounded rectangles
    Coverage                   scratch_;   // scratch for clips and shadows
    Coverage                   scratch2_;
};

// Blurs a coverage mask (three box passes approximating a Gaussian of
// standard deviation sigma, W3C Filter Effects §9.2), growing its bounds.
void blur(Coverage& coverage, float sigma);

}  // namespace omui::raster
