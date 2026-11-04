// Immutable binding expressions and prepared per-object schedules, no clock.
export module hanim.program;
import std;
export import hanim.library;
export import hanim.effect;

namespace hanim::detail { struct ProgramNode; struct ProgramData; }
export namespace hanim {
inline constexpr std::uint32_t program_contract_version = 1;
inline constexpr std::size_t invalid_program_index = std::numeric_limits<std::size_t>::max();
struct SegmentOptions { seconds duration{1}; Ease easing = ease::linear; };
// The first binding always uses its frozen source. Subsequent default bindings
// of that logical ObjectRef use the preceding segment's actual boundary pose.
enum class ProgramSource : std::uint8_t { previous_endpoint, snapshot };
enum class ProgramKind : std::uint8_t { empty, binding, sequence, parallel, stagger };
struct ProgramBinding {
    Snapshot from, to;
    Effects effects;
    SegmentOptions options;
    MorphOptions morph;
    ProgramSource source = ProgramSource::previous_endpoint;
};
// Live inputs resolve together at preparation; explicit snapshots retain their
// captured revision. Neither form owns or mutates the application's registry.
using ObjectInput = std::variant<Snapshot, ObjectRef>;
struct ProgramInputs { ObjectInput from, to; };
class Program {
public:
    Program() noexcept = default;
    [[nodiscard]] explicit operator bool() const noexcept { return bool(node_); }
    [[nodiscard]] ProgramKind kind() const noexcept;
    [[nodiscard]] std::span<const Program> children() const & noexcept;
    std::span<const Program> children() const && = delete;
    [[nodiscard]] const ProgramBinding* binding() const & noexcept;
    const ProgramBinding* binding() const && = delete;
    // Non-null for deferred authoring. binding() still supplies timing/effects;
    // its from/to stay empty; PreparedProgram owns the resolved snapshots.
    [[nodiscard]] const ProgramInputs* inputs() const & noexcept;
    const ProgramInputs* inputs() const && = delete;
    [[nodiscard]] seconds step() const noexcept;
private:
    friend Program bind(Snapshot, Snapshot, Effects, SegmentOptions, MorphOptions, ProgramSource);
    friend Program bind(ProgramInputs, Effects, SegmentOptions, MorphOptions, ProgramSource);
    friend Program sequence(std::span<const Program>);
    friend Program parallel(std::span<const Program>);
    friend Program stagger(seconds, std::span<const Program>);
    explicit Program(std::shared_ptr<const detail::ProgramNode> node) noexcept : node_(std::move(node)) {}
    std::shared_ptr<const detail::ProgramNode> node_;
};
[[nodiscard]] Program bind(Snapshot from, Snapshot to, Effects effects = {}, SegmentOptions options = {},
                           MorphOptions morph = {}, ProgramSource source = ProgramSource::previous_endpoint);
[[nodiscard]] Program bind(ProgramInputs inputs, Effects effects = {}, SegmentOptions options = {},
                           MorphOptions morph = {}, ProgramSource source = ProgramSource::previous_endpoint);
[[nodiscard]] Program sequence(std::span<const Program> children);
[[nodiscard]] Program sequence(std::initializer_list<Program> children);
[[nodiscard]] Program parallel(std::span<const Program> children);
[[nodiscard]] Program parallel(std::initializer_list<Program> children);
[[nodiscard]] Program stagger(seconds step, std::span<const Program> children);
[[nodiscard]] Program stagger(seconds step, std::initializer_list<Program> children);

namespace anim {
// A reusable leaf value. Group/image effect scopes are intentionally not
// represented as leaf setters; they require their own typed IR contract.
class Morph {
public:
    Morph(ObjectInput from, ObjectInput to, MorphOptions options = {})
        : inputs_{std::move(from), std::move(to)} { binding_.morph = options; }
    [[nodiscard]] Morph duration(seconds value) const {
        auto result = *this; result.binding_.options.duration = value; return result;
    }
    [[nodiscard]] Morph easing(Ease value) const {
        auto result = *this; result.binding_.options.easing = value; return result;
    }
    [[nodiscard]] Morph with_effect(EffectSpec value) const {
        auto result = *this; result.binding_.effects.push_back(std::move(value)); return result;
    }
    // Deduction keeps legacy with_effect({}) unambiguous (EffectSpec).
    template<class Process> requires (std::same_as<Process, fx::VertexPath> || std::same_as<Process, fx::TranslationPath>)
    [[nodiscard]] Morph with_effect(Process value) const {
        auto result = *this;
        if constexpr (std::same_as<Process, fx::VertexPath>)
            result.binding_.morph.shape.point_path = result.binding_.morph.shape.point_path.added(value.path);
        else result.binding_.morph.translation_path = result.binding_.morph.translation_path.added(value.path);
        return result;
    }
    [[nodiscard]] Morph source(ProgramSource value) const {
        auto result = *this; result.binding_.source = value; return result;
    }
    [[nodiscard]] const ProgramBinding& binding() const & noexcept { return binding_; }
    const ProgramBinding& binding() const && = delete;
    [[nodiscard]] operator Program() const {
        return bind(inputs_, binding_.effects, binding_.options, binding_.morph, binding_.source);
    }
private:
    ProgramInputs inputs_;
    ProgramBinding binding_;
};
[[nodiscard]] inline Morph morph(ObjectInput from, ObjectInput to, MorphOptions options = {}) {
    return Morph{std::move(from), std::move(to), options};
}
}
template<class T> concept Animation = std::same_as<std::remove_cvref_t<T>, Program> ||
                                    std::same_as<std::remove_cvref_t<T>, anim::Morph>;
template<Animation... T> requires (sizeof...(T) > 0)
[[nodiscard]] Program sequence(const T&... values) { return sequence({Program(values)...}); }
template<Animation... T> requires (sizeof...(T) > 0)
[[nodiscard]] Program parallel(const T&... values) { return parallel({Program(values)...}); }
template<Animation... T> requires (sizeof...(T) > 0)
[[nodiscard]] Program stagger(seconds step, const T&... values) { return stagger(step, {Program(values)...}); }
struct ProgramLimits {
    std::size_t max_nodes = 4096, max_depth = 64, max_tracks = 1024, max_segments = 4096;
    // Retained preparation counts across all segments, not bytes or AST input.
    std::size_t max_total_parts = 65536, max_total_cubics = 1048576, max_total_effect_parameters = 65536;
    EffectLimits effects;
};
enum class ProgramError : std::uint8_t {
    unprepared, invalid_limits, empty_program, invalid_snapshot, invalid_source,
    invalid_time, invalid_ease, node_budget, depth_budget, track_budget,
    segment_budget, part_budget, cubic_budget, effect_parameter_budget,
    source_version_mismatch, binding_conflict, animation, effect,
    invalid_track, output_too_small, unresolved_object, object
};
struct ProgramFailure {
    ProgramError code;
    std::size_t node = invalid_program_index, track = invalid_program_index, segment = invalid_program_index;
    std::optional<MorphFailure> animation;
    std::optional<EffectError> effect;
    std::optional<ObjectError> object;
};
[[nodiscard]] std::string_view program_error_name(ProgramError error) noexcept;
struct PreparedSegment {
    std::size_t track = 0, node = 0;
    std::string path;
    seconds start{0};
    SegmentOptions options;
    PreparedObjectMorph animation;
    PreparedEffects effects;
    bool anchor_source = false;
};
struct ProgramTrack {
    ObjectRef object;
    std::uint64_t source_revision = 0;
    FrameRequirements requirements;
    std::size_t max_effect_parameters = 0, first_segment = 0, segment_count = 0;
};
struct ProgramSample {
    FrameView geometry;
    std::size_t segment_index = 0;
    double linear_progress = 0, sample_progress = 0;
    std::size_t effect_parameters = 0;
};
class PreparedProgram {
public:
    PreparedProgram() noexcept = default;
    [[nodiscard]] static std::expected<PreparedProgram, ProgramFailure> prepare(const Program&, ProgramLimits limits = {});
    // Constrained deduction preserves legacy prepare(program, {}) calls:
    // braces must remain ProgramLimits, not an uninitialized ObjectRegistry.
    template<class Registry> requires std::same_as<std::remove_cvref_t<Registry>, ObjectRegistry>
    [[nodiscard]] static std::expected<PreparedProgram, ProgramFailure>
    prepare(const Program& program, Registry&& registry, ProgramLimits limits = {}) {
        return prepare_with(program, &registry, limits);
    }
    // Normalization path for the existing strict morph API and current-pose
    // retarget plans whose from() Snapshot can deliberately be empty.
    [[nodiscard]] static std::expected<PreparedProgram, ProgramFailure> single(
        PreparedObjectMorph, PreparedEffects, SegmentOptions options = {}, bool anchor_source = false, ProgramLimits limits = {});
    [[nodiscard]] explicit operator bool() const noexcept { return bool(data_); }
    [[nodiscard]] seconds duration() const noexcept;
    [[nodiscard]] std::span<const ProgramTrack> tracks() const & noexcept;
    std::span<const ProgramTrack> tracks() const && = delete;
    [[nodiscard]] std::span<const PreparedSegment> segments() const & noexcept;
    std::span<const PreparedSegment> segments() const && = delete;
    // Finite local time clamps per track. Gaps hold the preceding segment;
    // before the first start, hold its exact source. Later starts win at a
    // shared boundary. exact_source selects the first segment and raw zero.
    // Buffers may be track maxima; returned spans contain actual segment data.
    // Failures preserve both output buffers. No matching, mutation or allocation.
    [[nodiscard]] std::expected<ProgramSample, ProgramFailure> sample(
        std::size_t track, seconds time, FrameBuffer geometry, std::span<float> effects,
        bool exact_source = false) const noexcept;
private:
    [[nodiscard]] static std::expected<PreparedProgram, ProgramFailure>
        prepare_with(const Program&, const ObjectRegistry*, ProgramLimits);
    std::shared_ptr<const detail::ProgramData> data_;
};
} // namespace hanim
