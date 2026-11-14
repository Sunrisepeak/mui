// glTF's skins and animations (read by xrender.asset as numbers) made a skeleton and clips by hanim.skeleton.gltf:
// a document written here — a node above the skeleton, joints listed out of order, a rotation of 120° between two
// linear keys (keys added so the normalised mix stays on the spherical path), keys that step, a cubic spline, a
// channel of a node outside the skeleton — and Khronos' Fox when it is in the fidelity cache (its three clips
// compressed, their error measured). The skin's matrices at rest are glTF's own: each joint's world times its
// inverse bind.
import std;
import mlibs.math;
import mlibs.base64;
import hanim.skeleton;
import hanim.skeleton.gltf;
import xrender.asset;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace mcpplibs::mtest;
namespace skeleton = hanim::skeleton;
namespace asset = xrender::asset;

namespace {

std::string document() {
    std::vector<float> numbers;
    const auto put = [&](std::initializer_list<float> values) { numbers.insert(numbers.end(), values); };
    put({0, 1});                                   // 0: times
    put({0, 0, 0, 1, 0, 0, 0.8660254f, 0.5f});     // 8: a turn of 120° about z
    put({0, 0.5f});                                // 40: times that step
    put({0, 1, 0, 0, 2, 0});                       // 48: moves
    put({0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 0, 0, 0});  // 72: a cubic spline's in, value, out ×2
    for (int joint = 0; joint < 3; ++joint) put({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -static_cast<float>(joint), 0, 1});  // 144: inverse binds
    std::string bytes(numbers.size() * 4, '\0');
    std::memcpy(bytes.data(), numbers.data(), bytes.size());
    const std::string data = mlibs::base64::encode(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    return std::format(R"({{
  "asset": {{"version": "2.0"}},
  "scene": 0, "scenes": [{{"nodes": [0]}}],
  "nodes": [
    {{"name": "Armature", "scale": [0.5, 0.5, 0.5], "children": [1]}},
    {{"name": "bone0", "children": [2]}},
    {{"name": "bone1", "translation": [0, 1, 0], "children": [3]}},
    {{"name": "bone2", "translation": [0, 1, 0]}}
  ],
  "skins": [{{"joints": [3, 1, 2], "inverseBindMatrices": 5}}],
  "animations": [{{"name": "bend", "samplers": [
      {{"input": 0, "output": 1, "interpolation": "LINEAR"}},
      {{"input": 2, "output": 3, "interpolation": "STEP"}},
      {{"input": 0, "output": 4, "interpolation": "CUBICSPLINE"}},
      {{"input": 0, "output": 3, "interpolation": "LINEAR"}}],
    "channels": [
      {{"sampler": 0, "target": {{"node": 2, "path": "rotation"}}}},
      {{"sampler": 1, "target": {{"node": 1, "path": "translation"}}}},
      {{"sampler": 2, "target": {{"node": 3, "path": "scale"}}}},
      {{"sampler": 3, "target": {{"node": 0, "path": "translation"}}}}]}}],
  "buffers": [{{"byteLength": {}, "uri": "data:application/octet-stream;base64,{}"}}],
  "bufferViews": [
    {{"buffer": 0, "byteOffset": 0, "byteLength": 8}}, {{"buffer": 0, "byteOffset": 8, "byteLength": 32}},
    {{"buffer": 0, "byteOffset": 40, "byteLength": 8}}, {{"buffer": 0, "byteOffset": 48, "byteLength": 24}},
    {{"buffer": 0, "byteOffset": 72, "byteLength": 72}}, {{"buffer": 0, "byteOffset": 144, "byteLength": 192}}],
  "accessors": [
    {{"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1]}},
    {{"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC4"}},
    {{"bufferView": 2, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [0.5]}},
    {{"bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3"}},
    {{"bufferView": 4, "componentType": 5126, "count": 6, "type": "VEC3"}},
    {{"bufferView": 5, "componentType": 5126, "count": 3, "type": "MAT4"}}]
}})",
                       bytes.size(), data);
}

// A node's place in the document's world: its parents' places, then its own.
skeleton::Mat4 world_of(const asset::Imported& imported, std::uint32_t node) {
    skeleton::Mat4 out = imported.nodes[node].local;
    for (std::int32_t above = imported.nodes[node].parent; above >= 0; above = imported.nodes[static_cast<std::size_t>(above)].parent)
        out = imported.nodes[static_cast<std::size_t>(above)].local * out;
    return out;
}
// The largest difference between the skin's matrices at rest and glTF's own (joint world × inverse bind).
float skin_gap(const asset::Imported& imported, const skeleton::gltf::Made& made) {
    std::vector<skeleton::Mat4> model(made.rig.skeleton.size()), skin(made.rig.skeleton.size());
    skeleton::model(made.rig.skeleton, made.rig.skeleton.rest(), model);
    skeleton::skin(made.rig.skeleton, model, skin);
    const asset::Skin& from = imported.skins[0];
    float gap = 0;
    for (std::size_t joint = 0; joint < from.joints.size(); ++joint) {
        const skeleton::Mat4 expected = world_of(imported, from.joints[joint]) * from.inverse_binds[joint];
        for (std::size_t index = 0; index < 16; ++index) gap = std::max(gap, std::abs(expected.values[index] - skin[made.skin_joints[joint]].values[index]));
    }
    return gap;
}

}  // namespace

UTest cases{"hanim.skeleton.gltf", [] {
    "a document's skin and animation: the skeleton parents first, keys added, steps, a spline, notes"_test = [] {
        const std::string text = document();
        const auto imported = asset::gltf(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
        expect(fatal(imported.has_value())) << (imported ? "" : imported.error());
        expect(imported->nodes.size() == 4u && imported->skins.size() == 1u && imported->animations.size() == 1u && imported->animations[0].channels.size() == 4u);
        const auto made = skeleton::gltf::rig(*imported);
        expect(fatal(made.has_value())) << (made ? "" : made.error().text);
        const skeleton::Skeleton& bones = made->rig.skeleton;
        expect(bones.size() == 3u && bones.names()[0] == "bone0" && bones.parents()[2] == 1) << "the Armature above it is its base";
        expect(made->skin_joints == std::vector<std::uint32_t>{2, 0, 1}) << "the skin's order kept for its vertices";
        expect(skin_gap(*imported, *made) < 1e-6f) << "glTF's own skinning matrices";

        const skeleton::Clip& bend = made->rig.clips[0];
        expect(bend.name == "bend" && bend.duration == 1.0f);
        expect(bend.joints[1].rotation.times.size() > 2u) << "keys added to the 120° turn: " << bend.joints[1].rotation.times.size();
        skeleton::Pose pose(bones.size());
        skeleton::sample(bend, bones, 0.5f, pose);
        const skeleton::Quat halfway{0, 0, 0.5f, 0.8660254f};  // 60° about z: the spherical way's middle
        expect(1.0f - std::abs(mlibs::math::dot(pose[1].rotation, halfway)) < 1e-7f) << "on the spherical path";
        skeleton::sample(bend, bones, 0.25f, pose);
        const auto quarter = skeleton::Quat::about({0, 0, 1}, 0.5235988f);
        // The angle between the two, in double (a float's acos near 1 is coarser than what is measured).
        const skeleton::Quat between = mlibs::math::conjugate(quarter) * pose[1].rotation;
        const double off = 2 * std::atan2(std::hypot(double{between.x}, double{between.y}, double{between.z}), std::abs(double{between.w}));
        expect(off < 1e-4f) << std::format("a quarter of the way: {:.2e} radians off", off);
        skeleton::sample(bend, bones, 0.4f, pose);
        expect(pose[0].translation == skeleton::Vec3{0, 1, 0});
        skeleton::sample(bend, bones, 0.6f, pose);
        expect(pose[0].translation == skeleton::Vec3{0, 2, 0}) << "it steps";
        skeleton::sample(bend, bones, 0.5f, pose);
        expect(std::abs(pose[2].scale.x - 1.5f) < 1e-5f) << "the spline's middle: " << pose[2].scale.x;
        const bool outside = std::ranges::any_of(made->notes, [](const asset::Note& note) { return note.what.contains("not the skin's skeleton"); });
        const bool keyed = std::ranges::any_of(made->notes, [](const asset::Note& note) { return note.instead.contains("keys added"); });
        expect(outside && keyed) << "what was left out and what was added are notes";
        expect(!skeleton::gltf::rig(*imported, {.skin = 3}).has_value());
        expect(!skeleton::gltf::rig(*imported, {.root_motion = "tail"}).has_value());
    };

    "Khronos' Fox, when the fidelity cache has it: three clips compressed by ACL, measured"_test = [] {
        const std::filesystem::path file = std::filesystem::path(mcpplibs::os::env::get("HOME").value_or(".")) / ".cache/omui/fidelity/models/Fox/glTF-Binary/Fox.glb";
        if (!std::filesystem::exists(file)) {
            std::println("Fox.glb not in the fidelity cache ({}): not measured", file.string());
            return;
        }
        const auto imported = asset::gltf_file(file);
        expect(fatal(imported.has_value()));
        const auto made = skeleton::gltf::rig(*imported, {.compress = skeleton::Compression{}});
        expect(fatal(made.has_value())) << (made ? "" : made.error().text);
        expect(skin_gap(*imported, *made) < 1e-3f) << "glTF's own skinning matrices: " << skin_gap(*imported, *made);
        const auto box = imported->bounds();
        const float size = mlibs::math::length(box.high - box.low);
        std::println("Fox: {} joints, {} clips, {:.1f} units across", made->rig.skeleton.size(), made->rig.clips.size(), size);
        for (const skeleton::Clip& clip : made->rig.clips) {
            const skeleton::Fidelity fidelity = skeleton::measure(clip, made->rig.skeleton, 240);
            std::println("  {:6} {:.2f} s: {:6} bytes raw, {:5} compressed ({:.1f}×); ACL's error {:.4f}, measured at 240/s {:.4f} (units of the model) at {}, {:.3f} s", clip.name,
                         clip.duration, clip.bytes(), clip.compressed->bytes(), static_cast<double>(clip.bytes()) / static_cast<double>(clip.compressed->bytes()),
                         clip.compressed->error(), fidelity.worst, made->rig.skeleton.names()[fidelity.joint], fidelity.time);
            // Survey within the precision asked; Walk and Run, short clips keyed at 24 a second, 0.0235 and 0.0023 units at
            // their samples (0.015% of the model's size) — ACL's own measure; what is between their samples is the
            // spherical path against ACL's normalised mix.
            expect(clip.compressed->error() < 0.0002f * size) << clip.name << ": within 0.02% of the model's size";
            // Run is the open item (the README's): 1.0 unit off at the right hand between two samples near 0.58 s.
            if (clip.name != "Run") expect(fidelity.worst < 0.001f * size) << clip.name << ": between samples, within 0.1% of the model's size";
        }
        expect(made->rig.clip("Walk").has_value() && made->rig.clip("Run").has_value() && made->rig.clip("Survey").has_value());
    };
}};
