// hanim.skeleton.gltf — a glTF document's skin and animations (as xrender.asset reads them: numbers, not played)
// as a skeleton and its clips.
//
//     const auto asset = xrender::asset::gltf_file("Fox.glb");
//     auto made = hanim::skeleton::gltf::rig(*asset, {.compress = hanim::skeleton::Compression{}});
//     // made->rig.clips: "Survey", "Walk", "Run"; made->skin_joints: the skin's joint k is the skeleton's joint skin_joints[k]
//
// The skeleton is the skin's joints and every node between two of them, parents first; what is above its root, at
// rest, is the skeleton's base. A channel of a node of the skeleton becomes that joint's channel; one of any other
// node, a morph target's weights, and a joint given by a matrix are notes, with where they are in the document.
// Rotation keys are added where a normalised mix between two would leave the spherical one by more than
// `tolerance` (glTF's linear rotation is spherical; hanim's is normalised, as ozz's and ACL's are).
export module hanim.skeleton.gltf;

import std;
export import hanim.skeleton;
export import xrender.asset;

export namespace hanim::skeleton::gltf {

struct Options {
    std::uint32_t                skin = 0;
    float                        tolerance = 0.0001f;  // radians a rotation key may be off the spherical path
    std::optional<std::string>   root_motion;          // the joint (its node's name) whose motion becomes a channel
    std::optional<Compression>   compress;             // every clip compressed by ACL
};

struct Made {
    Rig                                rig;
    std::vector<std::uint32_t>         skin_joints;  // the skin's joint k is the skeleton's joint skin_joints[k]
    std::vector<std::uint32_t>         nodes;        // the skeleton's joint j is the document's node nodes[j]
    std::vector<xrender::asset::Note>  notes;
};

[[nodiscard]] std::expected<Made, Error> rig(const xrender::asset::Imported& asset, const Options& options = {});

// The document's instances its skin bends, kept bent by the skeleton's pose — what joins the two owners: hanim
// plays, xrender draws (its `bend`, the one bending; D-93). Each frame:
//
//     player.step(dt);
//     bent.bend(player.skin());                 // one a joint of the skeleton, as a player gives them
//     reference.draw(world, seen, view, {.meshes = bent.meshes(), .materials = …, .before = bent.before()}, frame);
//     for (const std::uint32_t mesh : bent.changed()) device_pass.bend(list, mesh, *bent.meshes()[mesh]);
//
// `meshes()` is the document's, each skinned instance's in place of the one posed at rest; `before()` is the
// frame before's of each bent mesh (none for the others, and none before the second frame) — its velocity.
// Two meshes an instance in turn: after the first frame nothing is allocated.
class Bent {
public:
    // The instances of `asset` that `made`'s skin (`Options::skin`, `skin`) bends.
    [[nodiscard]] static std::expected<Bent, Error> of(const xrender::asset::Imported& asset, const Made& made, std::uint32_t skin = 0);
    void bend(std::span<const Mat4> skin);
    [[nodiscard]] std::span<const mlibs::geom::ClusteredMesh* const> meshes() const noexcept { return meshes_; }
    [[nodiscard]] std::span<const mlibs::geom::ClusteredMesh* const> before() const noexcept { return before_; }
    [[nodiscard]] std::span<const std::uint32_t> changed() const noexcept { return changed_; }  // the meshes bent: the asset's numbers
    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

private:
    struct Kept {
        const mlibs::geom::ClusteredMesh* rest = nullptr;
        std::uint32_t                     mesh = 0;  // what its instance draws
        std::array<mlibs::geom::ClusteredMesh, 2> turns;
    };
    std::vector<Kept>                              kept_;
    std::vector<const mlibs::geom::ClusteredMesh*> meshes_, before_;
    std::vector<std::uint32_t>                     changed_, skin_joints_;
    std::vector<Mat4>                              matrices_;
    std::vector<Vec3>                              room_;
    std::uint64_t                                  frames_ = 0;
};

}  // namespace hanim::skeleton::gltf
