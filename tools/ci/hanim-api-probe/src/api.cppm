// Consumer-local authoring experiment, not an exported hanim product API.
export module hanim.validation.api_probe;
import std;
export import hanim.engine;

export namespace api_probe {
using Operand = std::variant<hanim::Snapshot, hanim::ObjectRef>;
struct Binding {
    Operand from, to;
    hanim::Effects effects;
    hanim::SegmentOptions options;
};

// Setters return new values; they neither capture nor change a live object.
// Effects are deliberately leaf-only: this experiment has no group FX scope.
class Morph {
public:
    Morph(Operand from, Operand to) : binding_{std::move(from), std::move(to), {}, {}} {}
    [[nodiscard]] Morph duration(hanim::seconds value) const {
        auto result = *this; result.binding_.options.duration = value; return result;
    }
    [[nodiscard]] Morph easing(hanim::Ease value) const {
        auto result = *this; result.binding_.options.easing = value; return result;
    }
    [[nodiscard]] Morph with_effect(hanim::EffectSpec value) const {
        auto result = *this; result.binding_.effects.push_back(std::move(value)); return result;
    }
    [[nodiscard]] const Binding& binding() const & noexcept { return binding_; }
    const Binding& binding() const && = delete;
private:
    Binding binding_;
};
namespace anim {
[[nodiscard]] inline Morph morph(hanim::ObjectRef source, hanim::ObjectRef target) {
    return Morph{source, target};
}
[[nodiscard]] inline Morph morph(hanim::Snapshot source, hanim::Snapshot target) {
    return Morph{std::move(source), std::move(target)};
}
}

namespace detail { struct Node; }
// Immutable, cold-path authoring tree, retaining refs until preparation.
class Description {
public:
    explicit Description(Morph leaf);
    [[nodiscard]] static Description group(hanim::ProgramKind, std::vector<Description>, hanim::seconds = {});
private:
    friend class HEngine;
    explicit Description(std::shared_ptr<const detail::Node> node) : node_(std::move(node)) {}
    std::shared_ptr<const detail::Node> node_;
};
namespace detail {
struct Node {
    hanim::ProgramKind kind;
    std::optional<Morph> leaf;
    std::vector<Description> children;
    hanim::seconds step{0};
};
}
inline Description::Description(Morph leaf)
    : node_(std::make_shared<const detail::Node>(detail::Node{hanim::ProgramKind::binding, std::move(leaf), {}, {}})) {}
inline Description Description::group(hanim::ProgramKind kind, std::vector<Description> children, hanim::seconds step) {
    return Description{std::make_shared<const detail::Node>(detail::Node{kind, {}, std::move(children), step})};
}
[[nodiscard]] inline Description describe(const Morph& value) { return Description{value}; }
[[nodiscard]] inline Description describe(const Description& value) { return value; }
template<class T> concept Animation = std::same_as<std::remove_cvref_t<T>, Morph> ||
                                    std::same_as<std::remove_cvref_t<T>, Description>;
template<Animation... T> requires (sizeof...(T) > 0)
[[nodiscard]] Description sequence(const T&... values) {
    return Description::group(hanim::ProgramKind::sequence, {describe(values)...});
}
template<Animation... T> requires (sizeof...(T) > 0)
[[nodiscard]] Description parallel(const T&... values) {
    return Description::group(hanim::ProgramKind::parallel, {describe(values)...});
}
template<Animation... T> requires (sizeof...(T) > 0)
[[nodiscard]] Description stagger(hanim::seconds delay, const T&... values) {
    return Description::group(hanim::ProgramKind::stagger, {describe(values)...}, delay);
}

// Borrows the existing Engine; it must outlive this facade and stay on one
// owning thread. No second registry, matcher, timeline compiler or player.
class HEngine {
public:
    explicit HEngine(hanim::Engine& engine, hanim::ProgramLimits limits = {}) noexcept
        : engine_(engine), limits_(limits) {}
    template<Animation T>
    [[nodiscard]] std::expected<hanim::PreparedProgram, hanim::EngineFailure> prepare(const T& description) const {
        return prepare(description, limits_);
    }
    template<Animation T>
    [[nodiscard]] std::expected<hanim::PreparedProgram, hanim::EngineFailure>
    prepare(const T& description, hanim::ProgramLimits limits) const {
        // Bound this adapter's recursive lowering too. The upper bound is the
        // existing Program v1 depth contract, not a second compilation policy.
        if (!limits.max_depth || limits.max_depth > 256 || !limits.max_nodes)
            return std::unexpected(program_failure(hanim::ProgramError::invalid_limits));
        std::size_t nodes = 0;
        auto lowered = lower(describe(description), limits, nodes, 1);
        if (!lowered) return std::unexpected(lowered.error());
        auto prepared = hanim::PreparedProgram::prepare(*lowered, limits);
        if (!prepared) return std::unexpected(hanim::EngineFailure{.code=hanim::EngineError::program, .program=prepared.error()});
        return std::move(*prepared);
    }
    template<Animation T>
    [[nodiscard]] std::expected<hanim::PlaybackRef, hanim::EngineFailure> play(const T& description) {
        auto prepared = prepare(description);
        if (!prepared) return std::unexpected(prepared.error());
        return engine_.play(std::move(*prepared));
    }
    [[nodiscard]] std::expected<hanim::PlaybackRef, hanim::EngineFailure> play(hanim::PreparedProgram prepared) {
        return engine_.play(std::move(prepared));
    }
private:
    [[nodiscard]] static hanim::EngineFailure program_failure(hanim::ProgramError code,
            std::size_t node = hanim::invalid_program_index) {
        return {.code=hanim::EngineError::program, .program=hanim::ProgramFailure{.code=code, .node=node}};
    }
    [[nodiscard]] std::expected<hanim::Snapshot, hanim::EngineFailure> capture(const Operand& operand) const {
        if (const auto* snapshot = std::get_if<hanim::Snapshot>(&operand)) return *snapshot;
        auto result = engine_.capture_object(std::get<hanim::ObjectRef>(operand));
        if (!result) return std::unexpected(hanim::EngineFailure{.code=hanim::EngineError::object, .object=result.error()});
        return std::move(*result);
    }
    [[nodiscard]] std::expected<hanim::Program, hanim::EngineFailure> lower(const Description& description,
            hanim::ProgramLimits limits, std::size_t& nodes, std::size_t depth) const {
        const auto index = nodes++;
        if (index >= limits.max_nodes) return std::unexpected(program_failure(hanim::ProgramError::node_budget, index));
        if (depth > limits.max_depth) return std::unexpected(program_failure(hanim::ProgramError::depth_budget, index));
        if (!description.node_) return std::unexpected(program_failure(hanim::ProgramError::empty_program, index));
        const auto& node = *description.node_;
        if (node.leaf) {
            const auto& binding = node.leaf->binding();
            auto from = capture(binding.from); if (!from) return std::unexpected(from.error());
            auto to = capture(binding.to); if (!to) return std::unexpected(to.error());
            return hanim::bind(std::move(*from), std::move(*to), binding.effects, binding.options);
        }
        std::vector<hanim::Program> children;
        // No schedule or matching here: preserve the tree for the real compiler.
        for (const auto& child : node.children) {
            auto result = lower(child, limits, nodes, depth + 1);
            if (!result) return std::unexpected(result.error());
            children.push_back(std::move(*result));
        }
        const std::span<const hanim::Program> span{children};
        switch (node.kind) {
            case hanim::ProgramKind::sequence: return hanim::sequence(span);
            case hanim::ProgramKind::parallel: return hanim::parallel(span);
            case hanim::ProgramKind::stagger: return hanim::stagger(node.step, span);
            default: return std::unexpected(hanim::EngineFailure{.code=hanim::EngineError::unsupported_operation});
        }
    }
    hanim::Engine& engine_;
    hanim::ProgramLimits limits_;
};
} // namespace api_probe
