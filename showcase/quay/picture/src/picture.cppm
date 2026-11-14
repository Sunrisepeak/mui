// Quay's picture: the harbour at night as the watchkeeper sees it, drawn from the game's state by xrender — the
// quay wet after rain, rows of containers, crates, lamps on posts and a floodlight, the beacon at the end of the
// breakwater (brighter as it is worked), the wardens bent by their skeletons as they walk, aim or lie where they
// fell, their eyes the colour of their minds, the watchkeeper's gun and the flashes of shots; fog, the light from
// everywhere and the glow of what is brighter than the display.
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

class Picture {
public:
    [[nodiscard]] static std::expected<Picture, std::string> make(const Level& level);

    // The state as the watchkeeper sees it.
    const mesh::Targets& draw(const State& state, const Frame& frame = {});
    [[nodiscard]] const mesh::Targets& targets() const noexcept { return targets_; }
    [[nodiscard]] std::span<const std::uint8_t> shown() const noexcept { return shown_; }  // rgba8 after the glow
    [[nodiscard]] std::uint32_t bright() const noexcept { return bright_; }               // pixels brighter than the display
    // Who drew a pixel, as the game numbers them: a warden (1 …) or 0; and the id of the instance (0: none).
    [[nodiscard]] std::uint32_t warden_at(std::size_t pixel) const noexcept;
    [[nodiscard]] scene::Id instance_at(std::size_t pixel) const noexcept;

    [[nodiscard]] const scene::Scene& world() const noexcept { return world_; }
    [[nodiscard]] const scene::View& view() const noexcept { return view_; }
    [[nodiscard]] mesh::Assets assets(bool shadows = true) const noexcept;
    [[nodiscard]] const mesh::Options& options() const noexcept { return options_; }

private:
    void pose(const State& state);
    void lights(const State& state);

    std::vector<mlibs::geom::ClusteredMesh>       made_;    // box, sphere, post, sea, then a warden's bent body each
    std::vector<const mlibs::geom::ClusteredMesh*> meshes_;
    std::vector<mlibs::geom::Bvh>                 grown_;
    std::vector<const mlibs::geom::Bvh*>          trees_;
    std::vector<mesh::Material>                   materials_;
    mlibs::geom::ClusteredMesh                    body_;    // a warden at rest
    hanim::skeleton::Skeleton                     skeleton_;
    hanim::skeleton::Pose                         posed_;
    std::vector<mlibs::math::Mat4>                model_, skin_;
    std::vector<mlibs::math::Vec3>                room_;
    std::vector<mlibs::math::Vec3>                crate_halves_;
    std::vector<scene::Id>                        flashes_;  // the lights of this frame's shots
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

}  // namespace quay::picture
