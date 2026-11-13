// xengine.cellular.present.world — a world of matter on screen by a ladder (D-178; the package's feature `world`).
// What the world IS is authoritative and the same on every machine; how it is SHOWN is a ladder a machine goes
// down when its frames cost too much, and up again when there is room:
//
//     rich    the decorative layer stepped beside the world (scenery that catches from a real fire), the light
//             as a blurred glow
//     lit     the world as it is, scenery as placed (not simulated), the glow
//     baked   no glow computed: over what emits light, frames of a flame baked once from the rich form
//     plain   the shade alone
//
//     auto picture = WorldPicture::make(images, program, look, shape, {.fill = "air"});
//     picture->scenery({23, 40, 8, 4}, wood);
//     picture->bake(*small_world_with_a_fire);                  // once: the frames the baked step draws
//     picture->show(world);  picture->draw(list, dest);         // each frame
//
// The ladder is one of mlibs.pipeline's (`WorldPicture::ladder()`): a governor that weighs frame costs lowers and
// raises it with whatever else a host has laddered; `set_step` is where its decision, a recording's, or a
// person's pin lands, and `richest` what the device allows at most. Nothing is lowered in silence: `describe()`
// says the step, why, and what each part is doing — for a person and an agent alike.
//
// One way: `show` takes the world as const. A step of the ladder changes pixels and nothing of the world.
export module xengine.cellular.present.world;

import std;
export import xengine.cellular.present.matter;
export import xengine.cellular.rigid;
export import xengine.cellular.decor;
export import mlibs.pipeline;

export namespace xengine::cellular {

enum class PictureStep : std::uint8_t { rich, lit, baked, plain };
inline constexpr std::array<std::string_view, 4> picture_step_names{"rich", "lit", "baked", "plain"};

struct PictureOptions {
    Shape tile{16, 16};
    std::string fill;                // the world's fill (beyond the decorative layer's edges)
    std::uint32_t seed = 0;          // of the decorative layer's own choices
    float glow = 2.0f;               // the glow's blur, in cells
    float strength = 0.9f;
    int warm = 30;                   // frames the decorative layer is run when it starts again
    PictureStep richest = PictureStep::rich;  // what the device allows at most
};

// Frames of a flame, baked from the rich form: each an image of light (added over the field), `cells` a side.
struct Flipbook {
    std::vector<xrender::ImageRef> frames;
    float cells_wide = 0, cells_high = 0;   // what a frame covers, in cells
    float centre_x = 0, centre_y = 0;       // where its light comes from, in cells from the frame's corner: put over what emits
    int every = 1;                          // world frames a baked frame is shown for
    std::uint64_t key = 0;                  // of what it was baked from: another look, scene or option is another key
};

class WorldPicture {
public:
    [[nodiscard]] static std::expected<WorldPicture, std::string> make(xrender::ImageStore& store, const xengine::matter::Program& program, const xengine::matter::Look& look,
                                                                       Shape shape, PictureOptions options = {});
    WorldPicture(WorldPicture&&) noexcept;
    WorldPicture& operator=(WorldPicture&&) noexcept;
    ~WorldPicture();

    // The ladder a governor is given: three steps down from `rich`.
    [[nodiscard]] static constexpr mlibs::pipeline::Ladder ladder() noexcept { return {"world.presentation", 3, 0}; }
    // Where the ladder stands (no richer than `richest`), and why — said back by describe().
    void set_step(PictureStep step, std::string reason = {});
    [[nodiscard]] PictureStep step() const noexcept;

    // Scenery: solid matter that is only shown — simulated at `rich`, drawn as placed below it.
    void scenery(Tile region, std::uint32_t material);
    // The baked step's frames, from the rich form of `scene` (a small world with what emits in it, the caller's:
    // the kernel names no material): `frames` of them, one every `every` steps after `warm`. Drawn with this
    // picture's look and glow, on the CPU, the same bits each time.
    std::expected<void, std::string> bake(const rigid::MatterWorld& scene, int frames = 12, int every = 4, int warm = 120);
    [[nodiscard]] const Flipbook& flames() const noexcept;

    // A frame: the world read, the step's own work done (the layer stepped, the images updated).
    void show(const rigid::MatterWorld& world);
    void draw(xrender::DisplayList& list, mlibs::geom::Rect dest) const;
    // The cells shown (the world's, with the layer's own over them), for whoever draws more over the field.
    [[nodiscard]] std::span<const std::uint32_t> shown() const noexcept;
    [[nodiscard]] const FieldPresenter& view() const noexcept;
    // {"step":"lit","richest":"rich","why":"…","layer":"still","glow":true,"flames":0,"scenery":32,"differing":0}
    [[nodiscard]] std::string describe() const;

private:
    struct Data;
    explicit WorldPicture(std::unique_ptr<Data>) noexcept;
    std::unique_ptr<Data> data_;
};

}  // namespace xengine::cellular
