// The one unit that names ACL (D-83): a clip's channels read at a rate, compressed within an error, and played back.
module hanim.skeleton;

import std;
import mcpplibs.mwrap.acl;
import mlibs.hash;

namespace hanim::skeleton {

namespace {

// Writes what ACL plays back straight into a pose.
struct Into final : acl::track_writer {
    std::span<Transform> out;
    void write_rotation(std::uint32_t joint, rtm::quatf_arg0 rotation) {
        if (joint < out.size()) out[joint].rotation = {rtm::quat_get_x(rotation), rtm::quat_get_y(rotation), rtm::quat_get_z(rotation), rtm::quat_get_w(rotation)};
    }
    void write_translation(std::uint32_t joint, rtm::vector4f_arg0 translation) {
        if (joint < out.size()) out[joint].translation = {rtm::vector_get_x(translation), rtm::vector_get_y(translation), rtm::vector_get_z(translation)};
    }
    void write_scale(std::uint32_t joint, rtm::vector4f_arg0 scale) {
        if (joint < out.size()) out[joint].scale = {rtm::vector_get_x(scale), rtm::vector_get_y(scale), rtm::vector_get_z(scale)};
    }
};

struct Aligned {
    void operator()(const std::byte* data) const noexcept { ::operator delete[](const_cast<std::byte*>(data), std::align_val_t{16}); }
};

}  // namespace

struct Compressing {
    static std::expected<Compressed, Error> run(const Clip& clip, const Skeleton& skeleton, Compression settings) {
        const std::size_t joints = skeleton.size();
        if (joints == 0) return std::unexpected(Error{"a skeleton with no joints"});
        if (clip.joints.size() != joints) return std::unexpected(Error{std::format("the clip has channels for {} joints; the skeleton has {}", clip.joints.size(), joints)});
        if (!(clip.duration >= 0) || !std::isfinite(clip.duration)) return std::unexpected(Error{"a clip's duration is a number, 0 or more"});
        if (!(settings.rate >= 0) || !(settings.precision > 0) || !(settings.shell > 0)) return std::unexpected(Error{"a rate of 0 or more, a precision and a shell above 0"});
        if (settings.rate == 0 && clip.key_rate > 0) settings.rate = std::min(240.0f, clip.key_rate);
        if (settings.rate == 0) {
            // The finest spacing of the clip's keys (a key closer than 1/240 s to the one before is not a rate).
            float finest = std::max(clip.duration, 1.0f / 240);
            const auto spacing = [&](std::span<const float> times) {
                for (std::size_t key = 1; key < times.size(); ++key)
                    if (const float gap = times[key] - times[key - 1]; gap >= 1.0f / 240 - 1e-6f) finest = std::min(finest, gap);
            };
            for (const JointChannels& channels : clip.joints) spacing(channels.translation.times), spacing(channels.rotation.times), spacing(channels.scale.times);
            settings.rate = std::min(240.0f, 1.0f / finest);
        }
        // Samples at even times from the start to exactly the end: the rate as asked, nudged so the last is the end.
        // (A rate a hair over a whole number of samples is that number.)
        const std::uint32_t samples = static_cast<std::uint32_t>(std::ceil(clip.duration * settings.rate - 0.001f)) + 1;
        const float rate = samples > 1 ? static_cast<float>(samples - 1) / clip.duration : settings.rate;

        acl::ansi_allocator allocator;
        acl::track_array_qvvf tracks(allocator, static_cast<std::uint32_t>(joints));
        Pose pose(joints);
        std::vector<acl::track_qvvf> made;
        for (std::uint32_t joint = 0; joint < joints; ++joint) {
            acl::track_desc_transformf desc;
            desc.output_index = joint;
            desc.parent_index = skeleton.parents()[joint] < 0 ? mcpplibs::mwrap::acl::k_invalid_track_index : static_cast<std::uint32_t>(skeleton.parents()[joint]);
            desc.precision = settings.precision;
            desc.shell_distance = settings.shell;
            tracks[joint] = acl::track_qvvf::make_reserve(desc, allocator, samples, rate);
        }
        // Each rotation on the side of the one before it (q and −q are one turn; a track that flips between them is
        // not smooth to a compressor that mixes neighbouring samples).
        std::vector<Quat> before(joints);
        for (std::uint32_t sample = 0; sample < samples; ++sample) {
            const float time = sample + 1 == samples ? clip.duration : static_cast<float>(sample) / rate;
            sample_raw(clip, skeleton, time, pose);
            for (std::uint32_t joint = 0; joint < joints; ++joint) {
                Transform& at = pose[joint];
                if (sample > 0 && mlibs::math::dot(at.rotation, before[joint]) < 0) at.rotation = {-at.rotation.x, -at.rotation.y, -at.rotation.z, -at.rotation.w};
                before[joint] = at.rotation;
                tracks[joint][sample] = rtm::qvv_set(rtm::quat_normalize(rtm::quat_set(at.rotation.x, at.rotation.y, at.rotation.z, at.rotation.w)),
                                                     rtm::vector_set(at.translation.x, at.translation.y, at.translation.z), rtm::vector_set(at.scale.x, at.scale.y, at.scale.z));
            }
        }
        acl::compression_settings compression = acl::get_default_compression_settings();
        acl::qvvf_transform_error_metric metric;
        compression.error_metric = &metric;
        acl::output_stats stats;
        acl::compressed_tracks* packed = nullptr;
        if (const acl::error_result result = acl::compress_track_list(allocator, tracks, compression, packed, stats); !result.empty() || !packed)
            return std::unexpected(Error{std::format("ACL refused the clip: {}", result.empty() ? "no output" : result.c_str())});
        acl::decompression_context<acl::default_transform_decompression_settings> context;
        context.initialize(*packed);
        const acl::track_error error = acl::calculate_compression_error(allocator, tracks, context, metric);

        Compressed out;
        out.size_ = packed->get_size();
        auto* bytes = static_cast<std::byte*>(::operator new[](out.size_, std::align_val_t{16}));
        std::memcpy(bytes, packed, out.size_);
        allocator.deallocate(packed, packed->get_size());
        out.data_ = std::shared_ptr<const std::byte>(bytes, Aligned{});
        out.joints_ = joints;
        out.duration_ = clip.duration;
        out.error_ = error.error;
        out.settings_ = settings;
        return out;
    }
};

std::expected<void, Error> Clip::compress(const Skeleton& skeleton, Compression settings) {
    auto made = Compressing::run(*this, skeleton, settings);
    if (!made) return std::unexpected(made.error());
    compressed = std::move(*made);
    return {};
}

void Compressed::sample(float time, std::span<Transform> out) const noexcept {
    if (!data_) return;
    const auto& tracks = *reinterpret_cast<const acl::compressed_tracks*>(data_.get());
    acl::decompression_context<acl::default_transform_decompression_settings> context;
    context.initialize(tracks);
    context.seek(std::clamp(time, 0.0f, duration_), acl::sample_rounding_policy::none);
    Into writer;
    writer.out = out.first(std::min(out.size(), joints_));
    context.decompress_tracks(writer);
}

std::uint64_t Compressed::hash() const noexcept {
    if (!data_) return 0;
    return mlibs::hash::fnv1a(std::span<const std::byte>(data_.get(), size_));
}

}  // namespace hanim::skeleton
