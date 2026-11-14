module hanim.skeleton.gltf;

import std;
import mlibs.math;

namespace hanim::skeleton::gltf {

namespace {

namespace asset = xrender::asset;
namespace det = mlibs::math::det;

// The rotation of a matrix whose columns are turned and scaled axes (no shear): its scale divided out.
Transform decomposed(const Mat4& matrix) noexcept {
    Transform out;
    out.translation = {matrix.at(3, 0), matrix.at(3, 1), matrix.at(3, 2)};
    const auto column = [&](std::size_t index) { return Vec3{matrix.at(index, 0), matrix.at(index, 1), matrix.at(index, 2)}; };
    out.scale = {mlibs::math::length(column(0)), mlibs::math::length(column(1)), mlibs::math::length(column(2))};
    const auto at = [&](std::size_t col, std::size_t row) {
        const float size = col == 0 ? out.scale.x : col == 1 ? out.scale.y : out.scale.z;
        return size > 0 ? matrix.at(col, row) / size : 0.0f;
    };
    // Shepperd's method: the largest of the four first.
    const float trace = at(0, 0) + at(1, 1) + at(2, 2);
    Quat turn;
    if (trace > 0) {
        const float root = std::sqrt(trace + 1.0f) * 2;
        turn = {(at(1, 2) - at(2, 1)) / root, (at(2, 0) - at(0, 2)) / root, (at(0, 1) - at(1, 0)) / root, 0.25f * root};
    } else if (at(0, 0) > at(1, 1) && at(0, 0) > at(2, 2)) {
        const float root = std::sqrt(1.0f + at(0, 0) - at(1, 1) - at(2, 2)) * 2;
        turn = {0.25f * root, (at(1, 0) + at(0, 1)) / root, (at(2, 0) + at(0, 2)) / root, (at(1, 2) - at(2, 1)) / root};
    } else if (at(1, 1) > at(2, 2)) {
        const float root = std::sqrt(1.0f + at(1, 1) - at(0, 0) - at(2, 2)) * 2;
        turn = {(at(1, 0) + at(0, 1)) / root, 0.25f * root, (at(2, 1) + at(1, 2)) / root, (at(2, 0) - at(0, 2)) / root};
    } else {
        const float root = std::sqrt(1.0f + at(2, 2) - at(0, 0) - at(1, 1)) * 2;
        turn = {(at(2, 0) + at(0, 2)) / root, (at(2, 1) + at(1, 2)) / root, 0.25f * root, (at(0, 1) - at(1, 0)) / root};
    }
    out.rotation = mlibs::math::normalized(turn);
    return out;
}

// The spherical way between two rotations of length 1 (the shorter arc), by the deterministic functions.
Quat spherical(const Quat& from, Quat to, double share) noexcept {
    double cosine = mlibs::math::dot(from, to);
    if (cosine < 0) to = {-to.x, -to.y, -to.z, -to.w}, cosine = -cosine;
    const double arc = det::atan2(det::sqrt(std::max(0.0, 1.0 - cosine * cosine)), cosine);
    if (arc < 1e-6) return mlibs::math::mix(from, to, static_cast<float>(share));
    const double sine = det::sin(arc), first = det::sin((1 - share) * arc) / sine, second = det::sin(share * arc) / sine;
    return mlibs::math::normalized({static_cast<float>(from.x * first + to.x * second), static_cast<float>(from.y * first + to.y * second),
                                    static_cast<float>(from.z * first + to.z * second), static_cast<float>(from.w * first + to.w * second)});
}

// How far (radians of rotation) a normalised mix over a quaternion arc of `arc` strays from the spherical one, at
// its worst (near a fifth of the way: (3 − √3) / 6).
double straying(double arc) noexcept {
    constexpr double at = 0.21132486540518713;
    const double mixed = det::atan2(at * det::sin(arc), (1 - at) + at * det::cos(arc));
    return 2 * std::abs(mixed - at * arc);
}

// Linear rotation keys, with keys added between two where a normalised mix strays more than `tolerance`.
Keys<Quat> rotations(const asset::Channel& channel, float tolerance, std::size_t& added) {
    Keys<Quat> out;
    const std::size_t keys = channel.times.size();
    const auto value = [&](std::size_t key) {
        return mlibs::math::normalized({channel.values[key * 4], channel.values[key * 4 + 1], channel.values[key * 4 + 2], channel.values[key * 4 + 3]});
    };
    for (std::size_t key = 0; key < keys; ++key) {
        const Quat here = value(key);
        out.times.push_back(channel.times[key]), out.values.push_back(here);
        if (key + 1 == keys) break;
        const Quat next = value(key + 1);
        const double cosine = std::min(1.0, std::abs(static_cast<double>(mlibs::math::dot(here, next))));
        const double arc = det::atan2(det::sqrt(1.0 - cosine * cosine), cosine);
        std::size_t pieces = 1;
        while (pieces < 64 && straying(arc / static_cast<double>(pieces)) > tolerance) pieces *= 2;
        for (std::size_t piece = 1; piece < pieces; ++piece) {
            const double share = static_cast<double>(piece) / static_cast<double>(pieces);
            out.times.push_back(static_cast<float>(channel.times[key] + (channel.times[key + 1] - channel.times[key]) * share));
            out.values.push_back(spherical(here, next, share));
            ++added;
        }
    }
    return out;
}

Interpolation interpolation_of(asset::Interpolation from) noexcept {
    return from == asset::Interpolation::step ? Interpolation::step : from == asset::Interpolation::cubic ? Interpolation::cubic : Interpolation::linear;
}

}  // namespace

std::expected<Made, Error> rig(const asset::Imported& imported, const Options& options) {
    if (options.skin >= imported.skins.size()) return std::unexpected(Error{std::format("skin {} was asked for; the document has {}", options.skin, imported.skins.size())});
    const asset::Skin& skin = imported.skins[options.skin];
    const auto& nodes = imported.nodes;
    Made made;
    const auto note = [&](std::string where, std::string what, std::string instead) { made.notes.push_back({std::move(where), std::move(what), std::move(instead)}); };
    for (const std::uint32_t joint : skin.joints)
        if (joint >= nodes.size()) return std::unexpected(Error{std::format("/skins/{}: joint node {} is not in the document", options.skin, joint)});

    // The skin's joints and every node between two of them.
    std::vector<bool> in(nodes.size(), false);
    for (const std::uint32_t joint : skin.joints) in[joint] = true;
    for (const std::uint32_t joint : skin.joints) {
        std::vector<std::uint32_t> path;
        std::int32_t above = nodes[joint].parent;
        std::size_t steps = 0;
        while (above >= 0 && !in[static_cast<std::size_t>(above)] && steps++ < nodes.size()) path.push_back(static_cast<std::uint32_t>(above)), above = nodes[static_cast<std::size_t>(above)].parent;
        if (above >= 0 && in[static_cast<std::size_t>(above)])
            for (const std::uint32_t between : path) in[between] = true;
    }
    const auto depth = [&](std::uint32_t node) {
        std::size_t count = 0;
        for (std::int32_t above = nodes[node].parent; above >= 0 && count <= nodes.size(); above = nodes[static_cast<std::size_t>(above)].parent) ++count;
        return count;
    };
    for (std::uint32_t node = 0; node < nodes.size(); ++node)
        if (in[node]) made.nodes.push_back(node);
    std::ranges::stable_sort(made.nodes, [&](std::uint32_t one, std::uint32_t other) { return depth(one) < depth(other); });
    std::vector<std::int32_t> joint_of(nodes.size(), -1);
    for (std::size_t index = 0; index < made.nodes.size(); ++index) joint_of[made.nodes[index]] = static_cast<std::int32_t>(index);

    // What is above the roots, at rest: the first root's; another root under other nodes is a note.
    std::vector<JointDef> joints;
    Mat4 base;
    bool based = false;
    for (std::size_t index = 0; index < made.nodes.size(); ++index) {
        const asset::Node& node = nodes[made.nodes[index]];
        const std::int32_t parent = node.parent >= 0 ? joint_of[static_cast<std::size_t>(node.parent)] : -1;
        Transform rest{.translation = node.translation, .rotation = node.rotation, .scale = node.scale};
        if (node.matrix) {
            rest = decomposed(node.local);
            note(std::format("/nodes/{}/matrix", made.nodes[index]), "a joint placed by a matrix", "taken apart into a move, a turn and a scale (a shear is lost)");
        }
        if (parent < 0) {
            Mat4 above;
            for (std::int32_t up = node.parent; up >= 0; up = nodes[static_cast<std::size_t>(up)].parent) above = nodes[static_cast<std::size_t>(up)].local * above;
            if (!based) base = above, based = true;
            else if (!(above == base)) note(std::format("/nodes/{}", made.nodes[index]), "a second root of the skeleton under other nodes than the first", "placed under the first root's");
        }
        joints.push_back({.name = node.name.empty() ? std::format("node{}", made.nodes[index]) : node.name, .parent = parent, .rest = rest});
    }
    // Names are a joint's handle: one used twice gets its node's number.
    std::unordered_map<std::string, int> uses;
    for (const JointDef& joint : joints) ++uses[joint.name];
    for (std::size_t index = 0; index < joints.size(); ++index)
        if (uses[joints[index].name] > 1) joints[index].name = std::format("{}#{}", joints[index].name, made.nodes[index]);

    // Inverse binds: the skin's for its joints; for the nodes between, their own rest.
    auto at_rest = Skeleton::make(joints, {}, base);
    if (!at_rest) return std::unexpected(at_rest.error());
    std::vector<Mat4> binds(at_rest->inverse_binds().begin(), at_rest->inverse_binds().end());
    for (std::size_t index = 0; index < skin.joints.size(); ++index) {
        const auto joint = static_cast<std::size_t>(joint_of[skin.joints[index]]);
        made.skin_joints.push_back(static_cast<std::uint32_t>(joint));
        if (index < skin.inverse_binds.size()) binds[joint] = skin.inverse_binds[index];
    }
    auto skeleton = Skeleton::make(std::move(joints), std::move(binds), base);
    if (!skeleton) return std::unexpected(skeleton.error());
    made.rig.skeleton = std::move(*skeleton);

    std::optional<RootMotion> root_motion;
    if (options.root_motion) {
        const auto joint = made.rig.skeleton.find(*options.root_motion);
        if (!joint) return std::unexpected(Error{std::format("root motion asked of {}: no joint of that name", *options.root_motion)});
        root_motion = RootMotion{*joint};
    }

    for (std::size_t index = 0; index < imported.animations.size(); ++index) {
        const asset::Animation& animation = imported.animations[index];
        Clip clip{.name = animation.name.empty() ? std::format("animation{}", index) : animation.name};
        clip.joints.resize(made.rig.skeleton.size());
        clip.root_motion = root_motion;
        std::size_t outside = 0, added = 0;
        // The rate the document's keys were made at: their finest spacing (keys added below do not count).
        float finest = 0;
        for (const asset::Channel& channel : animation.channels)
            for (std::size_t key = 1; key < channel.times.size(); ++key)
                if (const float gap = channel.times[key] - channel.times[key - 1]; gap >= 1.0f / 240 - 1e-6f && (finest == 0 || gap < finest)) finest = gap;
        if (finest > 0) clip.key_rate = 1.0f / finest;
        for (std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index) {
            const asset::Channel& channel = animation.channels[channel_index];
            const std::string where = std::format("/animations/{}/channels/{}", index, channel_index);
            if (channel.path == asset::Path::weights) {
                note(where, "a morph target's weights", "left out: what is drawn has no morph targets yet");
                continue;
            }
            if (channel.node >= nodes.size() || joint_of[channel.node] < 0) {
                ++outside;
                continue;
            }
            if (channel.times.empty()) continue;
            JointChannels& joint = clip.joints[static_cast<std::size_t>(joint_of[channel.node])];
            const Interpolation interpolation = interpolation_of(channel.interpolation);
            if (channel.path == asset::Path::rotation) {
                if (interpolation == Interpolation::linear) joint.rotation = rotations(channel, options.tolerance, added);
                else {
                    joint.rotation = {.times = channel.times, .interpolation = interpolation};
                    for (std::size_t at = 0; at + 4 <= channel.values.size(); at += 4)
                        joint.rotation.values.push_back({channel.values[at], channel.values[at + 1], channel.values[at + 2], channel.values[at + 3]});
                }
            } else {
                Keys<Vec3> keys{.times = channel.times, .interpolation = interpolation};
                for (std::size_t at = 0; at + 3 <= channel.values.size(); at += 3) keys.values.push_back({channel.values[at], channel.values[at + 1], channel.values[at + 2]});
                (channel.path == asset::Path::translation ? joint.translation : joint.scale) = std::move(keys);
            }
            clip.duration = std::max(clip.duration, channel.times.back());
        }
        if (outside) note(std::format("/animations/{}", index), std::format("{} channels of nodes that are not the skin's skeleton", outside), "left out: a node outside a skeleton is placed by its owner");
        if (added) note(std::format("/animations/{}", index), "linear rotations, spherical in glTF", std::format("{} keys added so a normalised mix is within {} radians", added, options.tolerance));
        if (options.compress) {
            if (auto compressed = clip.compress(made.rig.skeleton, *options.compress); !compressed) return std::unexpected(Error{std::format("/animations/{}: {}", index, compressed.error().text)});
        }
        made.rig.clips.push_back(std::move(clip));
    }
    return made;
}


std::expected<Bent, Error> Bent::of(const asset::Imported& imported, const Made& made, std::uint32_t skin) {
    if (skin >= imported.skins.size()) return std::unexpected(Error{std::format("skin {} was asked for; the document has {}", skin, imported.skins.size())});
    if (made.skin_joints.size() != imported.skins[skin].joints.size())
        return std::unexpected(Error{std::format("/skins/{}: {} joints, and the skeleton was made of a skin of {}", skin, imported.skins[skin].joints.size(), made.skin_joints.size())});
    Bent out;
    for (const mlibs::geom::ClusteredMesh& mesh : imported.meshes) out.meshes_.push_back(&mesh);
    out.before_.assign(imported.meshes.size(), nullptr);
    out.kept_.reserve(imported.skinned.size());
    for (const asset::Skinning& skinning : imported.skinned) {
        if (skinning.skin != skin || skinning.instance >= imported.instances.size() || skinning.rest >= imported.meshes.size()) continue;
        out.kept_.push_back({.rest = &imported.meshes[skinning.rest], .mesh = imported.instances[skinning.instance].mesh});
        out.changed_.push_back(out.kept_.back().mesh);
    }
    out.skin_joints_ = made.skin_joints;
    out.matrices_.resize(made.skin_joints.size());
    return out;
}

void Bent::bend(std::span<const Mat4> skin) {
    for (std::size_t joint = 0; joint < skin_joints_.size(); ++joint) matrices_[joint] = skin_joints_[joint] < skin.size() ? skin[skin_joints_[joint]] : Mat4{};
    const std::size_t now = frames_ % 2;
    for (Kept& one : kept_) {
        xrender::mesh::bend(*one.rest, matrices_, one.turns[now], room_);
        meshes_[one.mesh] = &one.turns[now];
        before_[one.mesh] = frames_ ? &one.turns[1 - now] : nullptr;
    }
    ++frames_;
}

}  // namespace hanim::skeleton::gltf
