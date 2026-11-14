module hanim.skeleton;

import std;

namespace hanim::skeleton {

namespace {

using mlibs::math::conjugate;
using mlibs::math::cross;
using mlibs::math::dot;
using mlibs::math::mix;
using mlibs::math::normalized;
using mlibs::math::rotate;
using mlibs::math::scaled;

bool finite(Vec3 value) noexcept { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
bool finite(const Quat& value) noexcept { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w); }

// The key at or before `time` and how far toward the next: the index of the first key, the share.
std::pair<std::size_t, float> bracket(std::span<const float> times, float time) noexcept {
    if (times.size() < 2 || !(time > times.front())) return {0, 0.0f};
    if (!(time < times.back())) return {times.size() - 1, 0.0f};
    const auto after = std::upper_bound(times.begin(), times.end(), time);
    const std::size_t index = static_cast<std::size_t>(after - times.begin()) - 1;
    const float span = times[index + 1] - times[index];
    return {index, span > 0 ? (time - times[index]) / span : 0.0f};
}

// Cubic Hermite between two values with their tangents (per second) over `span` seconds.
template <typename Value, typename Combine>
Value hermite(const Value& from, const Value& out_tangent, const Value& to, const Value& in_tangent, float share, float span, Combine combine) noexcept {
    const float s2 = share * share, s3 = s2 * share;
    const float h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + share, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
    return combine(from, h00, out_tangent, h10 * span, to, h01, in_tangent, h11 * span);
}

Quat aligned(const Quat& rotation, const Quat& toward) noexcept {
    return dot(rotation, toward) < 0 ? Quat{-rotation.x, -rotation.y, -rotation.z, -rotation.w} : rotation;
}

}  // namespace

// ── transforms ───────────────────────────────────────────────────────────

Mat4 matrix(const Transform& transform) noexcept {
    Mat4 out = mlibs::math::rotation(transform.rotation);
    for (std::size_t row = 0; row < 3; ++row) {
        out.at(0, row) *= transform.scale.x;
        out.at(1, row) *= transform.scale.y;
        out.at(2, row) *= transform.scale.z;
    }
    out.at(3, 0) = transform.translation.x, out.at(3, 1) = transform.translation.y, out.at(3, 2) = transform.translation.z;
    return out;
}

Transform compose(const Transform& outer, const Transform& inner) noexcept {
    return {.translation = outer.translation + rotate(outer.rotation, scaled(inner.translation, outer.scale)),
            .rotation = normalized(outer.rotation * inner.rotation),
            .scale = scaled(outer.scale, inner.scale)};
}

Quat yaw(const Quat& rotation) noexcept {
    const float size = std::sqrt(rotation.y * rotation.y + rotation.w * rotation.w);
    if (!(size > 1e-6f)) return {};
    return {0, rotation.y / size, 0, rotation.w / size};
}

Motion Motion::then(const Motion& after) const noexcept {
    return {.moved = moved + rotate(turned, after.moved), .turned = normalized(turned * after.turned)};
}

// ── the skeleton ─────────────────────────────────────────────────────────

std::expected<Skeleton, Error> Skeleton::make(std::vector<JointDef> joints, std::vector<Mat4> inverse_binds, Mat4 base) {
    Skeleton out;
    out.base_ = base;
    std::unordered_set<std::string_view> seen;
    for (std::size_t index = 0; index < joints.size(); ++index) {
        const JointDef& joint = joints[index];
        if (joint.parent >= static_cast<std::int32_t>(index) || joint.parent < -1)
            return std::unexpected(Error{std::format("joint {} ({}): its parent {} is not an earlier joint", index, joint.name, joint.parent)});
        if (!joint.name.empty() && !seen.insert(joint.name).second) return std::unexpected(Error{std::format("two joints are named {}", joint.name)});
        if (!finite(joint.rest.translation) || !finite(joint.rest.rotation) || !finite(joint.rest.scale))
            return std::unexpected(Error{std::format("joint {} ({}): a rest that is not a number", index, joint.name)});
    }
    if (!inverse_binds.empty() && inverse_binds.size() != joints.size())
        return std::unexpected(Error{std::format("{} inverse binds for {} joints", inverse_binds.size(), joints.size())});
    for (JointDef& joint : joints) {
        out.names_.push_back(std::move(joint.name));
        out.parents_.push_back(joint.parent);
        out.rest_.push_back(joint.rest);
    }
    if (inverse_binds.empty()) {
        std::vector<Mat4> at_rest(out.size());
        model(out, out.rest_, at_rest);
        for (std::size_t index = 0; index < at_rest.size(); ++index) {
            const auto inverse = mlibs::math::inverse(at_rest[index]);
            if (!inverse) return std::unexpected(Error{std::format("joint {} ({}): its rest has no inverse (a scale of 0)", index, out.names_[index])});
            inverse_binds.push_back(*inverse);
        }
    }
    out.inverse_binds_ = std::move(inverse_binds);
    return out;
}

std::optional<std::uint32_t> Skeleton::find(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < names_.size(); ++index)
        if (names_[index] == name) return static_cast<std::uint32_t>(index);
    return std::nullopt;
}

std::vector<float> Skeleton::mask(std::uint32_t joint, float weight) const {
    std::vector<float> out(size(), 0.0f);
    if (joint >= size()) return out;
    out[joint] = weight;
    // Parents come before their children: one pass down the list finds the subtree.
    std::vector<bool> under(size(), false);
    under[joint] = true;
    for (std::size_t index = joint + 1; index < size(); ++index)
        if (parents_[index] >= 0 && under[static_cast<std::size_t>(parents_[index])]) under[index] = true, out[index] = weight;
    return out;
}

void model(const Skeleton& skeleton, std::span<const Transform> pose, std::span<Mat4> out) noexcept {
    const auto parents = skeleton.parents();
    for (std::size_t index = 0; index < skeleton.size(); ++index) {
        const Mat4 local = matrix(pose[index]);
        out[index] = parents[index] < 0 ? skeleton.base() * local : out[static_cast<std::size_t>(parents[index])] * local;
    }
}

void skin(const Skeleton& skeleton, std::span<const Mat4> model_space, std::span<Mat4> out) noexcept {
    const auto inverse_binds = skeleton.inverse_binds();
    for (std::size_t index = 0; index < skeleton.size(); ++index) out[index] = model_space[index] * inverse_binds[index];
}

// ── channels ─────────────────────────────────────────────────────────────

Vec3 sample(const Keys<Vec3>& keys, float time) noexcept {
    if (keys.times.empty()) return {};
    const bool cubic = keys.interpolation == Interpolation::cubic;
    const auto value = [&](std::size_t key) { return keys.values[cubic ? key * 3 + 1 : key]; };
    const auto [index, share] = bracket(keys.times, time);
    if (share == 0 || keys.interpolation == Interpolation::step) return value(index);
    if (!cubic) return mix(keys.values[index], keys.values[index + 1], share);
    const float span = keys.times[index + 1] - keys.times[index];
    return hermite(value(index), keys.values[index * 3 + 2], value(index + 1), keys.values[(index + 1) * 3], share, span,
                   [](Vec3 from, float a, Vec3 out, float b, Vec3 to, float c, Vec3 in, float d) { return from * a + out * b + to * c + in * d; });
}

Quat sample(const Keys<Quat>& keys, float time) noexcept {
    if (keys.times.empty()) return {};
    const bool cubic = keys.interpolation == Interpolation::cubic;
    const auto value = [&](std::size_t key) { return keys.values[cubic ? key * 3 + 1 : key]; };
    const auto [index, share] = bracket(keys.times, time);
    if (share == 0 || keys.interpolation == Interpolation::step) return normalized(value(index));
    if (!cubic) return mix(keys.values[index], keys.values[index + 1], share);
    const float span = keys.times[index + 1] - keys.times[index];
    return normalized(hermite(value(index), keys.values[index * 3 + 2], value(index + 1), keys.values[(index + 1) * 3], share, span,
                              [](const Quat& from, float a, const Quat& out, float b, const Quat& to, float c, const Quat& in, float d) {
                                  return Quat{from.x * a + out.x * b + to.x * c + in.x * d, from.y * a + out.y * b + to.y * c + in.y * d,
                                              from.z * a + out.z * b + to.z * c + in.z * d, from.w * a + out.w * b + to.w * c + in.w * d};
                              }));
}

// ── clips ────────────────────────────────────────────────────────────────

std::size_t Clip::bytes() const noexcept {
    std::size_t total = 0;
    for (const JointChannels& joint : joints)
        total += (joint.translation.times.size() + joint.rotation.times.size() + joint.scale.times.size()) * sizeof(float) +
                 joint.translation.values.size() * sizeof(Vec3) + joint.rotation.values.size() * sizeof(Quat) + joint.scale.values.size() * sizeof(Vec3);
    return total;
}

namespace {

// The root's place across the ground and its turn about the way up, at a time: what root motion is taken between.
Motion root_at(const Clip& clip, const Skeleton& skeleton, float time) noexcept {
    const std::uint32_t joint = clip.root_motion->joint;
    const JointChannels& channels = clip.joints[joint];
    const Transform& rest = skeleton.rest()[joint];
    const Vec3 at = channels.translation.empty() ? rest.translation : sample(channels.translation, time);
    const Quat turn = channels.rotation.empty() ? rest.rotation : sample(channels.rotation, time);
    return {.moved = {at.x, 0, at.z}, .turned = yaw(turn)};
}

// What takes the root from `from` to `to` (both within the clip), in the space of the character at `from`.
Motion between(const Motion& from, const Motion& to) noexcept {
    const Quat back = conjugate(from.turned);
    return {.moved = rotate(back, to.moved - from.moved), .turned = normalized(back * to.turned)};
}

// The root in place: what root motion carries taken out of it.
void in_place(const Clip& clip, const Skeleton& skeleton, float time, std::span<Transform> out) noexcept {
    if (!clip.root_motion || clip.root_motion->joint >= out.size()) return;
    const Motion at = root_at(clip, skeleton, time);
    Transform& root = out[clip.root_motion->joint];
    const Quat back = conjugate(at.turned);
    root.translation = rotate(back, root.translation - at.moved);
    root.rotation = normalized(back * root.rotation);
}

}  // namespace

void sample_raw(const Clip& clip, const Skeleton& skeleton, float time, std::span<Transform> out) noexcept {
    const auto rest = skeleton.rest();
    const float at = std::clamp(time, 0.0f, clip.duration);
    for (std::size_t index = 0; index < out.size() && index < rest.size(); ++index) {
        if (index >= clip.joints.size()) {
            out[index] = rest[index];
            continue;
        }
        const JointChannels& channels = clip.joints[index];
        out[index] = {.translation = channels.translation.empty() ? rest[index].translation : sample(channels.translation, at),
                      .rotation = channels.rotation.empty() ? rest[index].rotation : sample(channels.rotation, at),
                      .scale = channels.scale.empty() ? rest[index].scale : sample(channels.scale, at)};
    }
}

void sample(const Clip& clip, const Skeleton& skeleton, float time, std::span<Transform> out) noexcept {
    if (clip.compressed) clip.compressed->sample(std::clamp(time, 0.0f, clip.duration), out);
    else sample_raw(clip, skeleton, time, out);
    in_place(clip, skeleton, std::clamp(time, 0.0f, clip.duration), out);
}

Motion motion(const Clip& clip, const Skeleton& skeleton, float from, float to, std::uint32_t laps) noexcept {
    if (!clip.root_motion || clip.root_motion->joint >= skeleton.size() || clip.root_motion->joint >= clip.joints.size()) return {};
    const Motion start = root_at(clip, skeleton, 0), end = root_at(clip, skeleton, clip.duration);
    const Motion begun = root_at(clip, skeleton, std::clamp(from, 0.0f, clip.duration));
    const Motion now = root_at(clip, skeleton, std::clamp(to, 0.0f, clip.duration));
    if (laps == 0) return between(begun, now);
    const Motion lap = between(start, end);
    Motion total = between(begun, end);
    for (std::uint32_t lapped = 1; lapped < laps; ++lapped) total = total.then(lap);
    return total.then(between(start, now));
}

Fidelity measure(const Clip& clip, const Skeleton& skeleton, float per_second) noexcept {
    Fidelity out;
    if (!clip.compressed || skeleton.size() == 0) return out;
    const float shell = clip.compressed->settings().shell;
    Pose raw(skeleton.size()), packed(skeleton.size());
    std::vector<Mat4> raw_model(skeleton.size()), packed_model(skeleton.size());
    const std::size_t count = static_cast<std::size_t>(std::ceil(clip.duration * per_second));
    for (std::size_t step = 0; step <= count; ++step) {
        const float time = std::min(clip.duration, static_cast<float>(step) / per_second);
        sample_raw(clip, skeleton, time, raw);
        clip.compressed->sample(time, packed);
        model(skeleton, raw, raw_model);
        model(skeleton, packed, packed_model);
        for (std::size_t joint = 0; joint < skeleton.size(); ++joint)
            for (const Vec3 point : {Vec3{0, 0, 0}, Vec3{shell, 0, 0}, Vec3{0, shell, 0}, Vec3{0, 0, shell}}) {
                const float off = mlibs::math::length(mlibs::math::point(raw_model[joint], point) - mlibs::math::point(packed_model[joint], point));
                if (off > out.worst) out = {.worst = off, .joint = static_cast<std::uint32_t>(joint), .time = time};
            }
    }
    return out;
}

// ── blending ─────────────────────────────────────────────────────────────

void blend(std::span<const Transform> from, std::span<const Transform> to, float weight, std::span<Transform> out) noexcept {
    // The ends exactly: what is not mixed is not normalised again.
    if (weight <= 0 || weight >= 1) {
        const auto source = weight <= 0 ? from : to;
        if (source.data() != out.data()) std::copy_n(source.begin(), out.size(), out.begin());
        return;
    }
    for (std::size_t index = 0; index < out.size(); ++index)
        out[index] = {.translation = mix(from[index].translation, to[index].translation, weight),
                      .rotation = mix(from[index].rotation, to[index].rotation, weight),
                      .scale = mix(from[index].scale, to[index].scale, weight)};
}

void blend(std::span<const Transform> from, std::span<const Transform> to, std::span<const float> weights, std::span<Transform> out) noexcept {
    for (std::size_t index = 0; index < out.size(); ++index) {
        const float weight = weights[index];
        if (weight <= 0) out[index] = from[index];
        else if (weight >= 1) out[index] = to[index];
        else out[index] = {.translation = mix(from[index].translation, to[index].translation, weight),
                           .rotation = mix(from[index].rotation, to[index].rotation, weight),
                           .scale = mix(from[index].scale, to[index].scale, weight)};
    }
}

void difference(std::span<const Transform> pose, std::span<const Transform> reference, std::span<Transform> out) noexcept {
    for (std::size_t index = 0; index < out.size(); ++index) {
        const Vec3 base = reference[index].scale;
        out[index] = {.translation = pose[index].translation - reference[index].translation,
                      .rotation = normalized(conjugate(reference[index].rotation) * pose[index].rotation),
                      .scale = {base.x != 0 ? pose[index].scale.x / base.x : 1, base.y != 0 ? pose[index].scale.y / base.y : 1, base.z != 0 ? pose[index].scale.z / base.z : 1}};
    }
}

void add(std::span<const Transform> base, std::span<const Transform> added, float weight, std::span<const float> weights, std::span<Transform> out) noexcept {
    for (std::size_t index = 0; index < out.size(); ++index) {
        const float share = weights.empty() ? weight : weight * weights[index];
        if (share <= 0) {
            out[index] = base[index];
            continue;
        }
        const Quat turn = mix(Quat{}, aligned(added[index].rotation, Quat{}), share);
        out[index] = {.translation = base[index].translation + added[index].translation * share,
                      .rotation = normalized(base[index].rotation * turn),
                      .scale = scaled(base[index].scale, mix(Vec3{1, 1, 1}, added[index].scale, share))};
    }
}

std::optional<std::uint32_t> Rig::clip(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < clips.size(); ++index)
        if (clips[index].name == name) return static_cast<std::uint32_t>(index);
    return std::nullopt;
}

}  // namespace hanim::skeleton
