export module omui.hanim:binding;
import std;
export import hanim;
export import hanim.render.vector;
export import openmui.view;

namespace omui::animation::detail {struct BindingImages;}

export namespace omui::animation {
struct Target {
    std::string part;
    NodeId node;
    PresentationChannel channels=PresentationChannel::transform;
    geom::Point anchor{.5f,.5f};
};
enum class BindingError : std::uint8_t { ui,object,invalid_selection,foreign_frame,unsupported_frame,invalid_pose,unprepared,rendering,capacity };
struct BindingFailure {
    BindingError code;
    std::size_t target=0;
    std::optional<PresentationFailure> ui;
    std::optional<hanim::ObjectError> object;
    std::optional<hanim::vector_render::VectorFailure> rendering;
};
// Property-tier UI adapter. The HObject describes explicitly selected local
// presentation offsets, initially identity, rather than a copy of UI layout,
// text, materials or business state. The first target is the rendered root;
// others are descendants in that same rendered subtree (no portal crossing).
// Ordinary state feedback remains active; any node kind can be selected.
class Binding {
public:
    Binding() noexcept = default;
    Binding(Binding&&) noexcept = default;
    Binding& operator=(Binding&&) noexcept = default;
    Binding(const Binding&)=delete;
    Binding& operator=(const Binding&)=delete;
    [[nodiscard]] static std::expected<Binding,BindingFailure>
        attach(Runtime&,std::span<const Target>,hanim::Engine&);
    [[nodiscard]] hanim::ObjectRef object() const noexcept {return object_;}
    [[nodiscard]] bool attached() const noexcept {return lease_.attached();}
    [[nodiscard]] std::span<const Target> targets() const & noexcept {return targets_;}
    std::span<const Target> targets() const && = delete;
    // Explicit sampled-pose publication, with no second player or clock.
    // Every target is checked before the UI batch changes. No source spans
    // are retained; call between native Engine sampling and Runtime::frame.
    [[nodiscard]] std::expected<void,BindingFailure> present(const hanim::EngineFrame&);
    // Cold binding of one object's leaf/nested image scopes. Source geometry
    // remains live UI content; effect order and timing come from this plan.
    [[nodiscard]] std::expected<void,BindingFailure> prepare(const hanim::PreparedProgram&,
        hanim::vector_render::ProgramVectorLimits = {});
    [[nodiscard]] std::expected<void,BindingFailure> present(const hanim::EngineProgramFrame&);
    void detach() noexcept {lease_.release();}
private:
    friend class Playback;
    [[nodiscard]] std::expected<void,BindingFailure> load_pose(const hanim::FrameView&);
    [[nodiscard]] std::expected<void,BindingFailure> publish();
    PresentationLease lease_;
    hanim::ObjectRef object_;
    std::vector<Target> targets_;
    std::vector<std::size_t> parts_,parents_;
    std::vector<hanim::Transform2D> poses_;
    hanim::PreparedProgram prepared_;
    std::shared_ptr<const detail::BindingImages> images_;
    std::vector<float> image_parameters_;
    std::vector<PresentationImageLayer> image_layers_;
    std::vector<PresentationEffects> image_frames_;
    bool image_reserved_=false;
};
// Engine owns object(): release its playbacks, detach, then destroy_object.
// Binding destruction only releases UI channels; it never dereferences a
// possibly destroyed Engine. Geometry and cross-object UI scopes still need
// their own representation/binding contracts.
}
