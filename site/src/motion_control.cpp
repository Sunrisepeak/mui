module openmui.site;
import std;
import openmui;
import mlibs.trace;
import :motion_gallery;

namespace omui::site {
std::span<const VerbDesc> motion_control_verbs() {
    static const std::array<VerbDesc,6> verbs{{
        {.name="motion.inspect",.summary="Inspect the active public-program animation and its host gates",.effect=Effect::read},
        {.name="motion.seek",.summary="Pause and sample the active public-program animation at progress 0..1",.args={{"progress","number"}}},
        {.name="motion.pause",.summary="Pause the active public-program animation"},
        {.name="motion.resume",.summary="Resume the active public-program animation, respecting visibility and interaction"},
        {.name="motion.reverse",.summary="Reverse the active public-program animation without changing its play intent"},
        {.name="motion.rate",.summary="Set the active public-program animation speed, keeping direction",.args={{"rate","number"}}}
    }};
    return verbs;
}
namespace {
std::string_view suspension(animation::Suspension value) {
    switch(value) {
    case animation::Suspension::none:return "none";
    case animation::Suspension::hidden:return "hidden";
    case animation::Suspension::background:return "background";
    case animation::Suspension::interaction:return "interaction";
    case animation::Suspension::motion_preference:return "motion-preference";
    }
    return "unknown";
}
Value snapshot(const MotionGallery& g) {
    Value out;out.set("selected",g.active_id).set("representation","ui-presentation")
        .set("available",false).set("authored_program",g.authored_program);
    if(!g.authored_program || !g.component || !g.component->playback || !g.component->playback->attached())return out;
    const auto state=g.component->playback->status();if(!state.native)return out;
    const auto& info=*state.native;
    return out.set("available",true).set("position",info.position.count()).set("duration",info.duration.count())
        .set("progress",info.duration.count()>0?info.position.count()/info.duration.count():0)
        .set("rate",info.rate).set("requested",state.requested).set("suspension",suspension(state.suspended))
        .set("demanding_frames",state.demanding_frames).set("clock_paused",state.clock_paused);
}
}
VerbResult motion_control(MotionGallery& g,const MotionGalleryContext& c,std::string_view operation,const Value& args) {
    mlibs::trace::Span span("command","site.motion.control");
    const auto verbs=motion_control_verbs();
    const auto found=std::ranges::find(verbs,operation,&VerbDesc::name);
    if(found==verbs.end())return std::unexpected("Unknown animation operation");
    auto* rt=c.runtime?*c.runtime:nullptr;if(!rt)return std::unexpected("Animation host is unavailable");
    const auto call=rt->current_invocation();if(!call)return std::unexpected("Missing bound UI invocation");
    auto native=to_control_invocation(*call);if(!native)return std::unexpected("Unbound animation authority");
    const auto& access=rt->agent_access();
    // The UI verb checks the transport entry; this scope checks the specific
    // host operation. Do not expose the inner Engine: Playback must reanchor
    // its clock, publish the pose and retain its visibility/interaction gates.
    mlibs::control::InvocationContext context;
    mlibs::control::OperationScope scope(context,access.control_binding(),*native,
        {found->name,operation=="motion.inspect"?openas::Effect::read:openas::Effect::reversible});
    if(scope.decision()!=openas::Decision::run)return std::unexpected(
        scope.decision()==openas::Decision::ask?"Animation operation requires authorization":"Animation operation refused");
    if(!scope.execute())return Value{}.set("executed",false);
    if(operation=="motion.inspect")return snapshot(g);
    if(!g.authored_program || !g.component || !g.component->playback || !g.component->playback->attached())
        return std::unexpected("Select an available public-program animation first");
    auto& p=*g.component->playback;
    double value=0;
    if(operation=="motion.seek" || operation=="motion.rate") {
        const auto* arg=args.get(operation=="motion.seek"?"progress":"rate");
        if(!arg || !arg->is_number() || !std::isfinite(arg->as_double()))return std::unexpected("Expected a finite numeric animation parameter");
        value=arg->as_double();
        if(operation=="motion.seek" && (value<0 || value>1))return std::unexpected("Progress must be within 0..1");
        if(operation=="motion.rate" && (value<.25 || value>2))return std::unexpected("Rate must be within 0.25..2");
    }
    std::expected<void,animation::PlaybackFailure> result;
    if(operation=="motion.seek")result=p.seek_progress(value);
    else if(operation=="motion.pause")result=p.pause();
    else if(operation=="motion.resume")result=p.resume();
    else if(operation=="motion.reverse")result=p.reverse();
    else if(operation=="motion.rate") {
        const auto state=p.status();if(!state.native)return std::unexpected("Animation has no program");
        result=p.rate((state.native->rate<0?-1:1)*value);
    }
    if(!result)return std::unexpected("Animation host rejected the operation");
    // Commit UI intent only after the public host accepts the command.
    if(operation=="motion.seek" || operation=="motion.pause") {
        g.playing.set(false);g.resume_on_view=false;g.resume_after_interaction=false;
        if(operation=="motion.seek")g.progress.set(value);
    } else if(operation=="motion.resume") {
        g.playing.set(true);g.resume_on_view=false;g.interactive.set(false);
    } else if(operation=="motion.rate") {
        g.speed.set(value);g.component->configured_speed=value;
    }
    if(const auto state=p.status();state.native)g.direction=state.native->rate<0?-1:1;
    rt->request_frame();return snapshot(g).set("executed",true);
}
}
