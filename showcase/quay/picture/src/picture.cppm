// Quay's picture: the harbour at night as the watchkeeper sees it, drawn from the game's state by xrender — the
// quay wet after rain, rows of containers, crates, lamps on posts and a floodlight, the beacon at the end of the
// breakwater (brighter as it is worked), the wardens bent by their skeletons as they walk, aim or lie where they
// fell, their eyes the colour of their minds, the watchkeeper's gun and the flashes of shots, the marks shots left
// on what they met; rain falling through the lamps' light and splashing on the quay; fog, the light from everywhere
// and the glow of what is brighter than the display.
//
//   auto picture = quay::picture::Picture::make(quay::level::harbour());
//   picture->draw(state);                        // the CPU reference (xrender.mesh's Pass), then the glow
//   picture->shown();                            // rgba8, what a person sees
//   picture->warden_at(pixel);                   // which warden drew a pixel (1 …), or 0: one tree, two views
//
// It only reads the state. The scene, the view, the assets and the options of the last frame are what a device
// draws the same frame from.
export module omui.showcase.quay.picture;

import std;
export import omui.showcase.quay.sim;
export import xrender.mesh;
export import xrender.mesh.gpu;
export import xrender.particle;
export import xrender.particle.gpu;
export import hanim.skeleton;

export namespace quay::picture {

namespace mesh = xrender::mesh;
namespace scene = xrender::scene;

struct Frame {
    int  width = 480, height = 270;
    bool shadows = true;  // the lights' shadows, by rays through the scene
    bool glow = true;
};

// The instances' ids: what drew a pixel, by number.
inline constexpr scene::Id sea_id = 1, box_ids = 100, crate_ids = 300, warden_ids = 400, eye_ids = 500, lamp_ids = 600, beacon_ids = 700, gun_id = 800, flash_id = 801;
inline constexpr scene::Id mark_ids = 900;                    // a mark of a shot: 900 + its place among `State::marks` × 2 (+1 its hole)
inline constexpr scene::Id rain_id = 1000, splash_id = 1001;  // the rain's batches
inline constexpr std::uint32_t drops = 900, splashes = 160;   // falling at once, splashing at once

class Picture {
public:
    [[nodiscard]] static std::expected<Picture, std::string> make(const Level& level);

    // The frame of a state made ready to draw — the wardens posed and bent, the lights, the view, what it sees —
    // for the reference here or a device (`OnDevice`).
    void prepare(const State& state);
    // The state as the watchkeeper sees it: prepared, then drawn by the reference and glowing.
    const mesh::Targets& draw(const State& state, const Frame& frame = {});
    [[nodiscard]] const mesh::Targets& targets() const noexcept { return targets_; }
    [[nodiscard]] std::span<const std::uint8_t> shown() const noexcept { return shown_; }  // rgba8 after the glow
    [[nodiscard]] std::uint32_t bright() const noexcept { return bright_; }               // pixels brighter than the display
    // Who drew a pixel, as the game numbers them: a warden (1 …) or 0; and the id of the instance (0: none).
    [[nodiscard]] std::uint32_t warden_at(std::size_t pixel) const noexcept;
    [[nodiscard]] scene::Id instance_at(std::size_t pixel) const noexcept;

    [[nodiscard]] const scene::Scene& world() const noexcept { return world_; }
    [[nodiscard]] const scene::View& view() const noexcept { return view_; }
    [[nodiscard]] const scene::Extracted& seen() const noexcept { return seen_; }
    [[nodiscard]] mesh::Assets assets(bool shadows = true) const noexcept;
    [[nodiscard]] const mesh::Options& options() const noexcept { return options_; }
    [[nodiscard]] static mesh::Bloom bloom(int width) noexcept { return {.threshold = 1, .radius = 7.0f * static_cast<float>(width) / 480, .strength = 0.6f}; }
    // The wardens' bodies as this frame bent them: the mesh each is (its number in `assets().meshes`).
    [[nodiscard]] std::size_t bodies() const noexcept { return made_.size() - first_body_; }
    [[nodiscard]] std::uint32_t body_mesh(std::size_t warden) const noexcept { return static_cast<std::uint32_t>(first_body_ + warden); }
    // The rain this frame: the drops and the splashes, each lit by the lamps near it (drawn over the light).
    [[nodiscard]] std::span<const xrender::particle::Batch> rain() const noexcept { return batches_; }
    // What the reference drew of it the last frame.
    [[nodiscard]] const xrender::particle::Drawn& rained() const noexcept { return rained_; }

private:
    void pose(const State& state);
    void lights(const State& state);
    void marks(const State& state);
    void weather(const State& state);
    static constexpr std::size_t first_body_ = 6;

    std::vector<mlibs::geom::ClusteredMesh>       made_;    // box, sphere, post, sea, then a warden's bent body each
    std::vector<const mlibs::geom::ClusteredMesh*> meshes_;
    std::vector<mlibs::geom::Bvh>                 grown_;
    std::vector<const mlibs::geom::Bvh*>          trees_;
    std::vector<mesh::Material>                   materials_;
    std::vector<mesh::Texture>                    pictures_; // the containers' corrugated steel (which way it faces): on the heap, kept where it is as the picture moves
    std::vector<const mesh::Texture*>             textures_;
    mlibs::geom::ClusteredMesh                    body_;    // a warden at rest
    hanim::skeleton::Skeleton                     skeleton_;
    hanim::skeleton::Pose                         posed_;
    std::vector<mlibs::math::Mat4>                model_, skin_;
    std::vector<mlibs::math::Vec3>                room_;
    std::vector<mlibs::math::Vec3>                crate_halves_;
    std::vector<scene::Id>                        flashes_;  // the lights of this frame's shots
    std::vector<xrender::particle::Particle>      drops_, splashes_;
    mesh::Texture                                 streak_;   // a drop's shape: a thin line down the middle
    std::array<xrender::particle::Batch, 2>       batches_{};
    xrender::particle::Drawn                      rained_;
    scene::Scene                                  world_;
    scene::View                                   view_;
    scene::Extracted                              seen_;
    mesh::Options                                 options_;
    mesh::Pass                                    pass_;
    mesh::Targets                                 targets_;
    mesh::Glow                                    glow_;
    std::vector<std::uint8_t>                     shown_;
    std::uint32_t                                 bright_ = 0;
};

// The picture on a device (mlibs.rhi; xrender.mesh.gpu): the frame a `Picture` prepared, drawn by the pass the
// reference is held to — the wardens bent on the device, the lamps' and the moon's shadows by maps, the glow — to
// an rgba8 target of its own. What a window shows and the frame budget is measured on.
class OnDevice {
public:
    [[nodiscard]] static mlibs::rhi::Result<OnDevice> make(mlibs::rhi::Device& device, const Picture& picture, std::uint32_t width, std::uint32_t height);
    OnDevice(OnDevice&& other) noexcept;
    OnDevice& operator=(OnDevice&& other) noexcept;
    ~OnDevice();
    // The frame `picture` last prepared, recorded on `list`.
    [[nodiscard]] mlibs::rhi::Result<void> record(mlibs::rhi::CommandList& list, const Picture& picture);
    // Drawn at another size from now on (a window's node resized, its pixels' ratio changed): the frame's own
    // targets made again; the passes and the lights' maps kept.
    [[nodiscard]] mlibs::rhi::Result<void> resize(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] mlibs::rhi::Texture colour() const noexcept { return colour_; }
    [[nodiscard]] const mesh::gpu::Stats& last() const noexcept { return pass_->last(); }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

private:
    OnDevice() = default;
    void release() noexcept;
    mlibs::rhi::Device*            device_ = nullptr;
    std::optional<mesh::gpu::Pass> pass_;
    std::optional<mesh::gpu::Glow> glow_;
    std::optional<xrender::particle::gpu::Pass> rain_;
    mesh::gpu::Shadows             shadows_;
    mlibs::rhi::Texture            colour_, depth_, light_, one_, other_, surface_;
    std::uint32_t                  width_ = 0, height_ = 0;
};

// A game played on a device as Quay's frame is timed (tests/bench's game/quay-frame-1080p-gpu, the page's
// tests/web_frame_test.cpp): the guide playing from a second in; each `frame` the step, the guide's choice, the
// picture prepared, and on the device the frame recorded, submitted and waited for. A game that has ended is
// begun again by `again_if_over` — the caller's to leave out of what it times.
class Played {
public:
    [[nodiscard]] static std::expected<Played, std::string> make(mlibs::rhi::Device& device, std::uint32_t width, std::uint32_t height);
    // Whether the game has ended; whether it had and was begun again.
    [[nodiscard]] bool over() const noexcept { return state_.outcome != Outcome::playing; }
    bool again_if_over();
    // One frame; the milliseconds until it was submitted (what the CPU spent: step, choice, picture, recording).
    [[nodiscard]] std::expected<double, std::string> frame();
    [[nodiscard]] const OnDevice& drawn() const noexcept { return *drawn_; }
    [[nodiscard]] std::uint64_t games() const noexcept { return games_; }

private:
    Played() = default;
    mlibs::rhi::Device*     device_ = nullptr;
    std::optional<Picture>  picture_;
    std::optional<OnDevice> drawn_;
    State                   state_;
    Guide                   guide_;
    mlibs::rhi::CommandList list_;  // kept: a list cleared keeps its room
    std::uint64_t           games_ = 0;
};

}  // namespace quay::picture
