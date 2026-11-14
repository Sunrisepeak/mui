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

}  // namespace hanim::skeleton::gltf
