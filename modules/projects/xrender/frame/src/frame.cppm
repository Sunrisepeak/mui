// xrender.frame — the frame's entry (D-162; the plan's §5.6).
//
// A frame is a target and the layers drawn into it in order, from the back:
// an interface's display list, a world's, an overlay's. Whoever makes content
// hands its layers here; the composition, the capture and the way from a
// pixel back to what drew it are the renderer's, the same for all of them.
//
//     xrender::CpuFrame frame;
//     const std::array layers{xrender::FrameLayer{.name = "world", .list = &world, .resources = {&images}},
//                             xrender::FrameLayer{.name = "ui", .list = &interface, .resources = {&ui_images, &glyphs}, .object_ids = true}};
//     frame.render(layers, surface, {.background = black});
//     frame.at(120, 80);   // {layer 1 "ui", object 17}
//
// On a device the same layers go through `GpuFrame` into a colour target the
// host made and presents.
//
// The frame is a subject of the agent kernel too (`FrameSubject`): which
// layers it is made of, its pixels, what drew a pixel — whatever made the
// content, an agent reviews the same picture a person sees.
export module xrender.frame;

import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.rhi;
export import xrender.vector;
export import xrender.raster;
export import :catalog;

export namespace xrender {

// A layer drawn elsewhere — a 3D pass, a decoded picture — given as its pixels, the frame's size. All of it is
// the caller's, alive while the frame is asked about (as the target is).
struct PictureLayer {
    std::span<const std::uint8_t>  rgba;     // straight alpha, sRGB: width × height × 4, rows from the top
    int                            width = 0, height = 0;
    std::span<const std::uint32_t> objects;  // none, or who drew each pixel: 0 where nothing did
    // None, or more of who drew a pixel, as members of a JSON object (`"instance":17,"triangle":80`) appended to
    // `members`: what `frame.at` says beside the object.
    std::function<void(int x, int y, std::string& members)> describe;
};

// One source of a frame's content: a display list, or a picture (PictureLayer).
struct FrameLayer {
    std::string_view      name;              // how a capture or an explanation addresses it: "ui", "world"
    const DisplayList*    list = nullptr;
    Resources             resources;         // the images and glyphs its commands name
    float                 scale = 1.0f;      // physical pixels per logical pixel
    std::optional<Affine> transform;         // the device transform it starts under (default: its scale)
    std::optional<float>  effect_scale;      // the logical-pixel scale its image filters work at (default: its scale)
    bool                  object_ids = false;  // keep which of its objects drew each pixel
    const PictureLayer*        picture = nullptr;   // instead of a list: laid over what is there (it keeps ids where it has `objects`)
};

struct FrameOptions {
    Color background = Color::transparent();  // what the first layer is drawn over
    bool  clear = true;                       // false: over what the target holds
    // Only these rows (sorted, not overlapping), over what the target holds; empty: every row.
    std::span<const RowSpan> rows;
    Parallel*    parallel = nullptr;  // bands drawn at the same time, the bytes a serial render's
    std::int32_t rows_per_task = 64;
    bool         measure = false;
};

struct LayerReport {
    std::string  name;
    bool         object_ids = false;
    RenderReport report;
};
struct FrameReport {
    int                      width = 0, height = 0;
    std::vector<LayerReport> layers;  // from the back
};

// What drew a pixel: the topmost layer that keeps ids and has one there (layer −1: none).
struct PixelSource {
    std::int32_t  layer = -1;
    std::uint32_t object = 0;
};

// Layers composed by the CPU reference renderer into a surface.
class CpuFrame {
public:
    // The layers drawn into `target` in order: the first over the background, each next over what is there. The
    // bytes are those of the reference renderer drawing each list in turn.
    const FrameReport& render(std::span<const FrameLayer> layers, raster::Surface& target, const FrameOptions& options = {});
    // The last render: its size and what each layer drew.
    [[nodiscard]] const FrameReport& report() const noexcept { return report_; }
    // After a render: what drew a pixel of the target.
    [[nodiscard]] PixelSource at(int x, int y) const noexcept;
    // The ids one layer kept, a pixel each, row by row (empty when it kept none): 0 where it drew nothing.
    [[nodiscard]] std::span<const std::uint32_t> object_ids(std::size_t layer) const noexcept;
    // What else a layer says of who drew a pixel (a picture's `describe`), as JSON members; nothing: unchanged.
    void describe(std::size_t layer, int x, int y, std::string& members) const;

private:
    std::vector<CpuRenderer> renderers_;  // one a layer: each keeps its ids and its scratch from frame to frame
    std::vector<const PictureLayer*> pictures_;  // one a layer: the picture it was, or none
    FrameReport              report_;
};

struct GpuFrameOptions {
    Color background = Color::transparent();  // what the first layer is drawn over
    bool  clear = true;
    // The textures a layer's external_texture() commands name, made on the same device (a video frame, an
    // engine's view); none for an id: skipped and counted.
    std::function<std::optional<mlibs::rhi::Texture>(std::uint64_t id)> external;
};

// Layers composed by the device's renderer into a colour target the host made (and presents, or reads back).
// One renderer for every layer: they share its pipelines and its atlas of masks.
class GpuFrame {
public:
    // The renderer's pipelines and textures on `device`; why not, when the device cannot take them.
    [[nodiscard]] static mlibs::rhi::Result<GpuFrame> create(mlibs::rhi::Device& device);
    // The layers drawn into `target` (an rgba8_unorm colour target of `width` × `height`) in order, the work
    // submitted: the first over the background, each next over what is there. A layer's `scale` is taken; a
    // transform of its own and its objects' ids are the reference renderer's (CpuFrame).
    [[nodiscard]] mlibs::rhi::Result<const FrameReport*> render(std::span<const FrameLayer> layers, mlibs::rhi::Texture target, std::uint32_t width, std::uint32_t height,
                                                                const GpuFrameOptions& options = {});
    [[nodiscard]] const FrameReport& report() const noexcept { return report_; }
    [[nodiscard]] mlibs::rhi::Device& device() const noexcept;

private:
    explicit GpuFrame(GpuRenderer renderer) : renderer_(std::move(renderer)) {}
    GpuRenderer renderer_;
    FrameReport report_;
};

// The frame as a subject of the agent kernel: the last frame a compositor rendered and the pixels it left.
class FrameSubject : public mlibs::agent::Subject {
public:
    // What it shows from now on; both outlive the subject or are shown again. Nothing shown: an empty frame.
    // `before`: an owner that draws only when asked (a host without a window) draws now, so what is read is
    // the frame as it would be shown.
    void show(const CpuFrame& frame, const raster::Surface& target, std::function<void()> before = {}) {
        frame_ = &frame;
        target_ = &target;
        before_ = std::move(before);
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "frame"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xrender.frame@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true, .provenance = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return xrender::listing; }
    // The pixels' hash, with the frame's size: the same picture, the same hash.
    [[nodiscard]] std::uint64_t state_hash() const override;
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    const CpuFrame*        frame_ = nullptr;
    const raster::Surface* target_ = nullptr;
    std::function<void()>  before_;
};

}  // namespace xrender
