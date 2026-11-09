module hanim.control;
import std;
import mlibs.trace;
import mlibs.log;

namespace hanim::control {
namespace {
constexpr mlibs::log::Log diagnostic{"hanim.control"};
enum class Op {prepare,play,list,inspect,plan,seek,seek_progress,pause,resume,reverse,rate,cancel,release};
static_assert(catalog.size()==13,"one Op a catalog entry, in its order (hanim.ops.omui.ts)");
template<class Fn> auto invoke(mlibs::control::InvocationContext& context,
    const std::shared_ptr<const mlibs::control::Access>& access,const mlibs::control::Invocation& call,
    Op operation,Fn&& execute) -> Reply<typename std::invoke_result_t<Fn>::value_type> {
    using T=typename std::invoke_result_t<Fn>::value_type;
    const auto descriptor=catalog[static_cast<std::size_t>(operation)].operation;
    mlibs::control::OperationScope scope(context,access,call,descriptor);
    // Catalog names are complete static string literals, retained by trace.
    mlibs::trace::Span trace(descriptor.name.data(),"hanim.control");
    if(scope.decision()!=openas::Decision::run) {
        diagnostic.warn("{} not admitted",descriptor.name);
        return std::unexpected(Failure{.code=Error::admission,.decision=scope.decision()});
    }
    if(scope.dry_run())return Receipt<T>{};
    auto value=execute();
    if(!value) {
        diagnostic.warn("{} failed: {}",descriptor.name,value.error().text());
        return std::unexpected(Failure{.code=Error::engine,.decision=openas::Decision::run,.engine=value.error()});
    }
    if constexpr(std::is_void_v<T>)return Receipt<void>{true};
    else return Receipt<T>{true,std::move(*value)};
}
}
std::span<const mlibs::control::CatalogEntry> operations() noexcept {return catalog;}
Reply<PreparedProgram> Controller::prepare(const mlibs::control::Invocation& call,const Program& p,ProgramLimits limits) {
    return invoke(context_,access_,call,Op::prepare,[&]{return engine_.prepare(p,limits);});
}
Reply<PlaybackRef> Controller::play(const mlibs::control::Invocation& call,const Program& p,ProgramLimits limits) {
    return invoke(context_,access_,call,Op::play,[&]{return engine_.play(p,limits);});
}
Reply<PlaybackRef> Controller::play(const mlibs::control::Invocation& call,PreparedProgram p) {
    return invoke(context_,access_,call,Op::play,[&]{return engine_.play(std::move(p));});
}
Reply<std::vector<PlaybackInfo>> Controller::list(const mlibs::control::Invocation& call) {
    return invoke(context_,access_,call,Op::list,[&]() -> std::expected<std::vector<PlaybackInfo>,EngineFailure> {
        std::vector<PlaybackInfo> result(engine_.size());auto count=engine_.list(result);
        if(!count)return std::unexpected(count.error());result.resize(*count);return result;
    });
}
Reply<PlaybackInfo> Controller::inspect(const mlibs::control::Invocation& c,PlaybackRef p) {return invoke(context_,access_,c,Op::inspect,[&]{return engine_.inspect(p);});}
Reply<PreparedProgram> Controller::plan(const mlibs::control::Invocation& c,PlaybackRef p) {return invoke(context_,access_,c,Op::plan,[&]{return engine_.plan(p);});}
Reply<void> Controller::seek(const mlibs::control::Invocation& c,PlaybackRef p,seconds t) {return invoke(context_,access_,c,Op::seek,[&]{return engine_.seek(p,t);});}
Reply<void> Controller::seek_progress(const mlibs::control::Invocation& c,PlaybackRef p,double t) {return invoke(context_,access_,c,Op::seek_progress,[&]{return engine_.seek_progress(p,t);});}
Reply<void> Controller::pause(const mlibs::control::Invocation& c,PlaybackRef p) {return invoke(context_,access_,c,Op::pause,[&]{return engine_.pause(p);});}
Reply<void> Controller::resume(const mlibs::control::Invocation& c,PlaybackRef p) {return invoke(context_,access_,c,Op::resume,[&]{return engine_.resume(p);});}
Reply<void> Controller::reverse(const mlibs::control::Invocation& c,PlaybackRef p) {return invoke(context_,access_,c,Op::reverse,[&]{return engine_.reverse(p);});}
Reply<void> Controller::rate(const mlibs::control::Invocation& c,PlaybackRef p,double rate) {return invoke(context_,access_,c,Op::rate,[&]{return engine_.rate(p,rate);});}
Reply<void> Controller::cancel(const mlibs::control::Invocation& c,PlaybackRef p,CancelMode mode) {return invoke(context_,access_,c,Op::cancel,[&]{return engine_.cancel(p,mode);});}
Reply<void> Controller::release(const mlibs::control::Invocation& c,PlaybackRef p) {return invoke(context_,access_,c,Op::release,[&]{return engine_.release(p);});}
}
