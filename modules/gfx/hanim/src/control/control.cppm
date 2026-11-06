export module hanim.control;
import std;
export import hanim.engine;
export import mlibs.control;

export namespace hanim::control {
struct OperationInfo {
    mlibs::control::Operation operation;
    std::string_view purpose;
};
[[nodiscard]] std::span<const OperationInfo> operations() noexcept;
enum class Error : std::uint8_t { admission, engine };
struct Failure {
    Error code = Error::admission;
    openas::Decision decision = openas::Decision::refuse;
    std::optional<EngineFailure> engine;
};
template<class T> struct Receipt { bool executed = false; std::optional<T> value; };
template<> struct Receipt<void> { bool executed = false; };
template<class T> using Reply = std::expected<Receipt<T>,Failure>;

// Borrowed Engine/context; both must outlive this serial control adapter.
// Trusted hosts supply Access and session handles; no sessions or transport
// are created here. Share the context with dependent renderer/tool operations.
class Controller {
public:
    Controller(Engine& engine,std::shared_ptr<const mlibs::control::Access> access,
               mlibs::control::InvocationContext& context) : engine_(engine),access_(std::move(access)),context_(context) {}
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    [[nodiscard]] Reply<PreparedProgram> prepare(const mlibs::control::Invocation&,const Program&,ProgramLimits = {});
    [[nodiscard]] Reply<PlaybackRef> play(const mlibs::control::Invocation&,const Program&,ProgramLimits = {});
    [[nodiscard]] Reply<PlaybackRef> play(const mlibs::control::Invocation&,PreparedProgram);
    [[nodiscard]] Reply<std::vector<PlaybackInfo>> list(const mlibs::control::Invocation&);
    [[nodiscard]] Reply<PlaybackInfo> inspect(const mlibs::control::Invocation&,PlaybackRef);
    [[nodiscard]] Reply<PreparedProgram> plan(const mlibs::control::Invocation&,PlaybackRef);
    [[nodiscard]] Reply<void> seek(const mlibs::control::Invocation&,PlaybackRef,seconds);
    [[nodiscard]] Reply<void> seek_progress(const mlibs::control::Invocation&,PlaybackRef,double);
    [[nodiscard]] Reply<void> pause(const mlibs::control::Invocation&,PlaybackRef);
    [[nodiscard]] Reply<void> resume(const mlibs::control::Invocation&,PlaybackRef);
    [[nodiscard]] Reply<void> reverse(const mlibs::control::Invocation&,PlaybackRef);
    [[nodiscard]] Reply<void> rate(const mlibs::control::Invocation&,PlaybackRef,double);
    [[nodiscard]] Reply<void> cancel(const mlibs::control::Invocation&,PlaybackRef,CancelMode = CancelMode::hold);
    [[nodiscard]] Reply<void> release(const mlibs::control::Invocation&,PlaybackRef);
private:
    Engine& engine_;
    std::shared_ptr<const mlibs::control::Access> access_;
    mlibs::control::InvocationContext& context_;
};
}
