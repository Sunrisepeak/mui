// xengine.cellular.present.world — a world of matter on screen by ladders (D-178; the package's feature `world`).
// What the world IS is authoritative and the same on every machine; how it is SHOWN is laddered: a machine goes
// down a step when its frames cost too much, and up again when there is room. Two ladders, each its own:
//
//     light     glow    what emits, blurred and added over the field
//               baked   no blur computed: over what emits, frames of a flame baked once from the glow's own form
//               none    the shade alone
//     scenery   alive   the decorative layer stepped beside the world (scenery that catches from a real fire)
//               still   scenery drawn as placed
//
//     auto picture = WorldPicture::make(images, program, look, shape, {.fill = "air"});
//     picture->scenery({23, 40, 8, 4}, wood);
//     picture->bake(*small_world_with_a_fire);                  // once: the frames the baked step draws
//     picture->show(world);  picture->draw(list, dest);         // each frame
//
// The ladders are mlibs.pipeline's (`WorldPicture::ladders()`, the dearer first — on a CPU the blur costs tens of
// times what the layer does, so it is the first to go): a governor that weighs frame costs lowers and raises
// them with whatever else a host has laddered; `set` is where its decision, a recording's, or a person's pin
// lands, and `best_light` / `scenery_alive` what the device allows at most. Nothing is lowered in silence:
// `describe()` says where each ladder stands, why, and what each part is doing — for a person and an agent alike.
//
// One way: `show` takes the world as const. A step of a ladder changes pixels and nothing of the world.
export module xengine.cellular.present.world;

import std;
export import xengine.cellular.present;
export import xengine.matter.appearance;
export import xengine.cellular.rigid;
export import xengine.cellular.decor;
export import mlibs.pipeline;

export namespace xengine::cellular {

enum class Light : std::uint8_t { glow, baked, none };
inline constexpr std::array<std::string_view, 3> light_names{"glow", "baked", "none"};
inline constexpr std::array<std::string_view, 2> scenery_names{"alive", "still"};

struct PictureOptions {
    Shape tile{16, 16};
    std::string fill;                // the world's fill (beyond the decorative layer's edges)
    std::uint32_t seed = 0;          // of the decorative layer's own choices
    float glow = 2.0f;               // the glow's blur, in cells
    float strength = 0.9f;
    int warm = 30;                   // frames the decorative layer is run when it starts again
    Light best_light = Light::glow;  // what the device allows at most
    bool scenery_alive = true;
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

    // The ladders a governor is given, in the order they are lowered: the light (two steps down), then the scenery (one).
    static constexpr std::size_t light_ladder = 0, scenery_ladder = 1;
    [[nodiscard]] static constexpr std::array<mlibs::pipeline::Ladder, 2> ladders() noexcept { return {{{"world.light", 2, 0}, {"world.scenery", 1, 0}}}; }
    // Where a ladder stands — a governor's change as it gives it, a recording's, a pin — and why (said back by
    // describe()). No richer than the device allows.
    void set(std::size_t ladder, std::uint8_t lowered, std::string reason = {});
    void set_light(Light light, std::string reason = {}) { set(light_ladder, static_cast<std::uint8_t>(light), std::move(reason)); }
    void set_scenery(bool alive, std::string reason = {}) { set(scenery_ladder, alive ? 0 : 1, std::move(reason)); }
    [[nodiscard]] Light light() const noexcept;
    [[nodiscard]] bool scenery_alive() const noexcept;

    // Scenery: solid matter that is only shown — simulated where the scenery is alive, drawn as placed where not.
    void scenery(Tile region, std::uint32_t material);
    // The baked step's frames, from the glow's own form on `scene` (a small world with what emits in it, the caller's:
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
    // {"light":"baked","light_why":"…","best_light":"glow","scenery":"still","scenery_why":"…","flames":4,"baked_frames":12,"placed":52,"differing":51}
    [[nodiscard]] std::string describe() const;

private:
    struct Data;
    explicit WorldPicture(std::unique_ptr<Data>) noexcept;
    std::unique_ptr<Data> data_;
};

}  // namespace xengine::cellular
