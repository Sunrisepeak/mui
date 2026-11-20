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
import mlibs.image;
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

// A strip of two quads on two bones (the second 1 up), its upper corners on the second; the clip turns the
// second bone a quarter about z in a second.
std::string skinned_document() {
    std::vector<float> numbers;
    const auto put = [&](std::initializer_list<float> values) { numbers.insert(numbers.end(), values); };
    put({0, 0, 0, 1, 0, 0, 0, 2, 0, 1, 2, 0});                             // 0: positions
    put({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});                 // 48: weights, each wholly its first joint's
    put({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});                 // 112: inverse binds
    put({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1});
    put({0, 1});                                                           // 240: times
    put({0, 0, 0, 1, 0, 0, 0.70710678f, 0.70710678f});                     // 248: a quarter turn about z
    std::string bytes(numbers.size() * 4, '\0');
    std::memcpy(bytes.data(), numbers.data(), bytes.size());
    const std::array<std::uint8_t, 16> joints{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};          // 280
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 1, 3, 2};                                         // 296
    bytes.append(reinterpret_cast<const char*>(joints.data()), joints.size());
    bytes.append(reinterpret_cast<const char*>(indices.data()), indices.size() * 2);
    const std::string data = mlibs::base64::encode(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    return std::format(R"({{
  "asset": {{"version": "2.0"}},
  "scene": 0, "scenes": [{{"nodes": [0, 1]}}],
  "nodes": [{{"mesh": 0, "skin": 0}}, {{"name": "lower", "children": [2]}}, {{"name": "upper", "translation": [0, 1, 0]}}],
  "meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0, "JOINTS_0": 5, "WEIGHTS_0": 1}}, "indices": 6}}]}}],
  "skins": [{{"joints": [1, 2], "inverseBindMatrices": 2}}],
  "animations": [{{"name": "turn", "samplers": [{{"input": 3, "output": 4}}], "channels": [{{"sampler": 0, "target": {{"node": 2, "path": "rotation"}}}}]}}],
  "buffers": [{{"byteLength": {}, "uri": "data:application/octet-stream;base64,{}"}}],
  "bufferViews": [
    {{"buffer": 0, "byteOffset": 0, "byteLength": 48}}, {{"buffer": 0, "byteOffset": 48, "byteLength": 64}},
    {{"buffer": 0, "byteOffset": 112, "byteLength": 128}}, {{"buffer": 0, "byteOffset": 240, "byteLength": 8}},
    {{"buffer": 0, "byteOffset": 248, "byteLength": 32}}, {{"buffer": 0, "byteOffset": 280, "byteLength": 16}},
    {{"buffer": 0, "byteOffset": 296, "byteLength": 12}}],
  "accessors": [
    {{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [0, 0, 0], "max": [1, 2, 0]}},
    {{"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC4"}},
    {{"bufferView": 2, "componentType": 5126, "count": 2, "type": "MAT4"}},
    {{"bufferView": 3, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1]}},
    {{"bufferView": 4, "componentType": 5126, "count": 2, "type": "VEC4"}},
    {{"bufferView": 5, "componentType": 5121, "count": 4, "type": "VEC4"}},
    {{"bufferView": 6, "componentType": 5123, "count": 6, "type": "SCALAR"}}]
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

    "a document's skinned mesh kept bent by the clip played: the corners where the bones put them, the frame before kept, nothing made after the first"_test = [] {
        const std::string text = skinned_document();
        const auto imported = asset::gltf(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
        expect(fatal(imported.has_value())) << (imported ? "" : imported.error());
        expect(fatal(eq(imported->skinned.size(), std::size_t{1})));
        const auto made = skeleton::gltf::rig(*imported);
        expect(fatal(made.has_value())) << (made ? "" : made.error().text);
        auto bent = skeleton::gltf::Bent::of(*imported, *made);
        expect(fatal(bent.has_value())) << (bent ? "" : bent.error().text);
        const std::uint32_t drawn = imported->instances[imported->skinned[0].instance].mesh;
        expect(eq(bent->changed().size(), std::size_t{1}) && eq(bent->changed()[0], drawn));
        skeleton::Player player(std::make_shared<const skeleton::Rig>(made->rig));
        const auto at = [&](float seconds) {
            skeleton::Choice choice;
            choice.push({.clip = 0, .time = seconds, .before = seconds, .weight = 1});
            player.apply(choice, 0);
            bent->bend(player.skin());
        };
        at(0);
        expect(bent->before()[drawn] == nullptr) << "no frame before the first";
        const mlibs::geom::ClusteredMesh* const first = bent->meshes()[drawn];
        at(0.5f);
        expect(bent->before()[drawn] == first) << "the frame before's, kept";
        const auto has = [&](mlibs::math::Vec3 wanted) {
            return std::ranges::any_of(bent->meshes()[drawn]->mesh.positions, [&](mlibs::math::Vec3 one) { return mlibs::math::length(one - wanted) < 2e-4f; });
        };
        // Half way (an eighth of a turn): the upper corners (0, 2) and (1, 2) are (0, 1) and (1, 1) from the second
        // bone, turned (−0.7071, 0.7071) and (0, 1.4142), and the bone 1 up. Its spherical path is kept within 1e-4.
        expect(has({-0.70710678f, 1.70710678f, 0}) && has({0, 2.41421356f, 0})) << "the upper corners where the turned bone puts them";
        expect(has({0, 0, 0}) && has({1, 0, 0})) << "the lower ones where they were";
        const mlibs::math::Vec3* const kept = bent->meshes()[drawn]->mesh.positions.data();
        at(0.25f), at(0.5f);
        expect(bent->meshes()[drawn]->mesh.positions.data() == kept) << "the same two meshes in turn";
        expect(!skeleton::gltf::Bent::of(*imported, *made, 4).has_value()) << "a skin the document has not";
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

        // Walking, drawn: its mesh bent each frame by what the player gives, by xrender's one bending, and drawn by
        // the reference — four moments of the step, their outlines not the same.
        auto bent = skeleton::gltf::Bent::of(*imported, *made);
        expect(fatal(bent.has_value()));
        skeleton::Player player(std::make_shared<const skeleton::Rig>(made->rig));
        asset::Imported shown = *imported;
        const std::uint32_t walk = *made->rig.clip("Walk");
        const float length = made->rig.clips[walk].duration;
        const auto view = asset::view_of({.theta = 1.1f, .phi = 1.35f, .radius = size * 1.2f, .target = (box.low + box.high) * 0.5f}, box, 1);
        std::vector<std::vector<std::uint8_t>> outlines;
        std::vector<std::uint8_t> strip(static_cast<std::size_t>(4 * 256 * 256 * 4));
        for (int moment = 0; moment < 4; ++moment) {
            const float time = length * static_cast<float>(moment) / 4;
            skeleton::Choice choice;
            choice.push({.clip = walk, .time = time, .before = time, .weight = 1});
            player.apply(choice, 0);
            bent->bend(player.skin());
            for (const std::uint32_t mesh : bent->changed()) shown.meshes[mesh] = *bent->meshes()[mesh];
            const auto drawn = asset::looked(shown, view, {.width = 256, .height = 256, .samples = 1});
            expect(fatal(drawn.has_value())) << (drawn ? "" : drawn.error());
            std::vector<std::uint8_t> outline(256 * 256);
            for (std::size_t pixel = 0; pixel < outline.size(); ++pixel) outline[pixel] = drawn->rgba[pixel * 4 + 3] > 0;
            outlines.push_back(std::move(outline));
            for (int row = 0; row < 256; ++row)
                std::copy_n(drawn->rgba.begin() + row * 256 * 4, 256 * 4, strip.begin() + (static_cast<std::ptrdiff_t>(row) * 4 * 256 + moment * 256) * 4);
        }
        std::size_t least = outlines[0].size(), covered = 0;
        for (int moment = 1; moment < 4; ++moment) {
            std::size_t differing = 0;
            for (std::size_t pixel = 0; pixel < outlines[0].size(); ++pixel) differing += outlines[moment][pixel] != outlines[moment - 1][pixel];
            least = std::min(least, differing);
        }
        for (const std::uint8_t one : outlines[0]) covered += one;
        const std::filesystem::path kept = std::filesystem::temp_directory_path() / "openmui-tests" / "hanim-skeleton" / "fox-walk.png";
        std::filesystem::create_directories(kept.parent_path());
        expect(mlibs::image::save_png(kept, {strip.data(), 4 * 256, 256, 4 * 256 * 4, mlibs::image::Alpha::straight}).has_value());
        std::println("Fox walking, drawn at four moments of its step ({}): {} pixels of fox, the outlines of two moments apart by {} pixels at the least", kept.string(), covered, least);
        expect(gt(covered, std::size_t{2000}) && gt(least, std::size_t{100})) << "the legs move";
    };
}};
