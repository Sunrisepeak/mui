module omui.hanim;
import std;
import mlibs.trace;

namespace omui::animation {
namespace {
using Status=std::expected<void,PlaybackFailure>;
PlaybackFailure failure(hanim::EngineFailure value) {return {.code=PlaybackError::engine,.engine=std::move(value)};}
PlaybackFailure failure(BindingFailure value) {return {.code=PlaybackError::binding,.binding=std::move(value)};}
bool endpoint(const hanim::PlaybackInfo& info) {
    return info.duration.count()<=0 || (info.rate>0 ? info.position>=info.duration : info.position.count()<=0);
}
}

std::expected<Playback,PlaybackFailure> Playback::attach(Runtime& runtime,std::span<const Target> targets) {
    auto& engine=runtime.engine();
    auto binding=Binding::attach(runtime,targets,engine);if(!binding)return std::unexpected(failure(binding.error()));
    Playback result;result.engine_=&engine;result.binding_=std::move(*binding);result.runtime_=&runtime;
    return result;
}
Playback::~Playback() {
    // The shared Engine outlives no runtime: touch it only while attached.
    if(engine_ && attached()) {
        if(playback_)(void)engine_->release(*playback_);
        const auto object=binding_.object();
        binding_.detach();
        if(object)(void)engine_->destroy_object(object);
    }
    demand_.release();
}
Playback::Playback(Playback&& other) noexcept
    : engine_(std::exchange(other.engine_,nullptr)),binding_(std::move(other.binding_)),demand_(std::move(other.demand_)),
      runtime_(std::exchange(other.runtime_,nullptr)),playback_(std::exchange(other.playback_,std::nullopt)),options_(other.options_),
      anchor_time_(other.anchor_time_),anchor_position_(other.anchor_position_),direction_(other.direction_),suspended_(other.suspended_),
      requested_(other.requested_),demanding_(std::exchange(other.demanding_,false)) {}
Playback& Playback::operator=(Playback&& other) noexcept {
    if(this!=&other){this->~Playback();new(this) Playback(std::move(other));}
    return *this;
}
Status Playback::ready() const {
    if(!attached())return std::unexpected(PlaybackFailure{PlaybackError::stale});
    if(!playback_)return std::unexpected(PlaybackFailure{PlaybackError::no_program});
    return {};
}
void Playback::demand(bool wanted) {
    wanted=wanted && attached();
    if(wanted==demanding_)return;
    demand_.release();demanding_=wanted;
    if(wanted)demand_=runtime_->demand_frames(FrameDemand::fastest("hanim"));
}
void Playback::reanchor() {
    const auto info=engine_->inspect(*playback_);
    anchor_time_=runtime_->now();anchor_position_=info->position;direction_=info->rate>0?1:-1;
}
Status Playback::publish() {
    if(binding_.images_) {
        const auto frame=engine_->program_frame(*playback_);if(!frame)return std::unexpected(failure(frame.error()));
        const auto result=binding_.present(*frame);if(!result)return std::unexpected(failure(result.error()));return {};
    }
    const auto frame=engine_->frame(*playback_);if(!frame)return std::unexpected(failure(frame.error()));
    const auto result=binding_.present(*frame);if(!result)return std::unexpected(failure(result.error()));
    return {};
}
Status Playback::sample(hanim::seconds position,double rate_value) {
    const auto before=engine_->inspect(*playback_);
    if(auto r=engine_->rate(*playback_,rate_value);!r)return std::unexpected(failure(r.error()));
    const auto sought=engine_->seek(*playback_,position);
    const auto shown=sought?publish():Status(std::unexpected(failure(sought.error())));
    if(!shown) {
        // Presentation validates the complete batch before writing. Restore
        // the player's sampled position too, so a refusal leaves both sides
        // on the last accepted pose.
        (void)engine_->rate(*playback_,before->rate);(void)engine_->seek(*playback_,before->position);
    }
    return shown;
}
Status Playback::play(const hanim::Program& program,PlaybackOptions options) {
    mlibs::trace::Span span("play","omui.hanim.playback");
    if(!attached())return std::unexpected(PlaybackFailure{PlaybackError::stale});
    if(playback_)return std::unexpected(PlaybackFailure{PlaybackError::occupied});
    if(!std::isfinite(options.rate) || options.rate==0)return std::unexpected(PlaybackFailure{PlaybackError::invalid_time});
    const auto prepared=engine_->prepare(program);if(!prepared)return std::unexpected(failure(prepared.error()));
    if(auto bound=binding_.prepare(*prepared);!bound)return std::unexpected(failure(bound.error()));
    const auto played=engine_->play(*prepared);if(!played)return std::unexpected(failure(played.error()));
    playback_=*played;
    const auto info=engine_->inspect(*playback_);
    const auto shown=sample(options.rate<0?info->duration:hanim::seconds(0),options.rate);
    if(!shown){(void)engine_->release(*playback_);playback_.reset();return shown;}
    options_=options;options_.rate=std::abs(options.rate);requested_=true;suspended_=Suspension::none;
    reanchor();
    // The next update applies host gates before requesting a continuing
    // stream. A one-shot wake lets a previously idle host reach that stage.
    runtime_->request_frame();return {};
}
Status Playback::pause() {
    if(auto r=ready();!r)return r;
    const auto paused=engine_->pause(*playback_);if(!paused)return std::unexpected(failure(paused.error()));
    requested_=false;reanchor();demand(false);runtime_->request_frame();return {};
}
Status Playback::rebind() {
    mlibs::trace::Span span("rebind","omui.hanim.playback");
    if(auto r=ready();!r)return r;
    const auto prepared=engine_->plan(*playback_);if(!prepared)return std::unexpected(failure(prepared.error()));
    if(auto bound=binding_.prepare(*prepared);!bound)return std::unexpected(failure(bound.error()));
    if(auto shown=publish();!shown)return shown;
    runtime_->request_frame();return {};
}
Status Playback::resume() {
    if(auto r=ready();!r)return r;
    const auto info=engine_->inspect(*playback_);
    if(endpoint(*info) && info->duration.count()>0) {
        if(options_.alternate) {if(auto r=engine_->reverse(*playback_);!r)return std::unexpected(failure(r.error()));}
        else if(auto r=sample(info->rate>0?hanim::seconds(0):info->duration,info->rate);!r)return r;
    }
    if(auto r=engine_->resume(*playback_);!r)return std::unexpected(failure(r.error()));
    requested_=true;reanchor();runtime_->request_frame();return {};
}
Status Playback::reverse() {
    if(auto r=ready();!r)return r;
    if(auto r=engine_->reverse(*playback_);!r)return std::unexpected(failure(r.error()));
    reanchor();runtime_->request_frame();return {};
}
Status Playback::rate(double value) {
    if(auto r=ready();!r)return r;
    if(!std::isfinite(value) || value==0)return std::unexpected(PlaybackFailure{PlaybackError::invalid_time});
    if(auto r=engine_->rate(*playback_,value);!r)return std::unexpected(failure(r.error()));
    options_.rate=std::abs(value);reanchor();runtime_->request_frame();return {};
}
Status Playback::seek(hanim::seconds position) {
    if(auto r=ready();!r)return r;
    if(!std::isfinite(position.count()))return std::unexpected(PlaybackFailure{PlaybackError::invalid_time});
    const auto info=engine_->inspect(*playback_);
    if(auto r=sample(position,info->rate);!r)return r;
    return pause();
}
Status Playback::seek_progress(double progress) {
    if(auto r=ready();!r)return r;
    if(!std::isfinite(progress))return std::unexpected(PlaybackFailure{PlaybackError::invalid_time});
    return seek(engine_->inspect(*playback_)->duration*std::clamp(progress,0.0,1.0));
}
Status Playback::stop(hanim::CancelMode mode) {
    if(auto r=ready();!r)return r;
    if(mode!=hanim::CancelMode::hold && mode!=hanim::CancelMode::restore)
        return std::unexpected(failure(hanim::EngineFailure{.code=hanim::EngineError::invalid_argument}));
    if(mode==hanim::CancelMode::restore)if(auto r=sample(hanim::seconds(0),1);!r)return r;
    if(auto r=engine_->release(*playback_);!r)return std::unexpected(failure(r.error()));
    playback_.reset();requested_=false;suspended_=Suspension::none;demand(false);runtime_->request_frame();return {};
}
Status Playback::update(PlaybackGate gate) {
    mlibs::trace::Span span("update","omui.hanim.playback");
    // A mounted host stage remains installed between programs. Stopping a
    // program must still allow the following frame to paint the restored pose.
    if(attached() && !playback_){demand(false);return {};}
    if(auto r=ready();!r){demand(false);return r;}
    Suspension next=Suspension::none;
    if(!gate.visible)next=Suspension::hidden;
    else if(!gate.foreground)next=Suspension::background;
    else if(gate.interacting)next=Suspension::interaction;
    else if(runtime_->reduce_motion() || runtime_->motion_level()!=MotionLevel::full)next=Suspension::motion_preference;
    if(next!=suspended_) {
        // Gate transitions preserve the displayed pose. Hidden time is not
        // replayed when the same scene becomes active again.
        reanchor();suspended_=next;
    }
    if(!requested_ || suspended_!=Suspension::none) {
        if(engine_->inspect(*playback_)->state!=hanim::PlaybackState::paused)(void)engine_->pause(*playback_);
        demand(false);return {};
    }
    const auto info=engine_->inspect(*playback_);
    const hanim::TimeMap mapping{.rate=options_.rate,
        .repeat=options_.alternate?std::numeric_limits<double>::infinity():1,
        .alternate=options_.alternate,.reverse=direction_<0,.fill=hanim::Fill::both};
    const auto phase=(direction_>0?anchor_position_:info->duration-anchor_position_)/options_.rate;
    const auto elapsed=runtime_->now()-anchor_time_;
    const auto at=hanim::sample_time(mapping,hanim::seconds(0),info->duration,elapsed+phase);
    if(!at || !std::isfinite(at->position.count())){demand(false);return std::unexpected(PlaybackFailure{PlaybackError::invalid_time});}
    const double signed_rate=at->backwards?-options_.rate:options_.rate;
    if(info->position!=at->position || info->rate!=signed_rate)if(auto r=sample(at->position,signed_rate);!r){demand(false);return r;}
    if(info->state==hanim::PlaybackState::paused)(void)engine_->resume(*playback_);
    demand(!at->ended && info->duration.count()>0 && !runtime_->time_paused());return {};
}
PlaybackStatus Playback::status() const noexcept {
    PlaybackStatus status;
    if(playback_ && attached())if(auto info=engine_->inspect(*playback_);info)status.native=*info;  // the runtime's Engine: only while it lives
    status.suspended=suspended_;status.requested=attached() && requested_;status.demanding_frames=attached() && demanding_;
    status.clock_paused=attached() && runtime_->time_paused();
    return status;
}
std::expected<hanim::PreparedProgram,PlaybackFailure> Playback::plan() const {
    if(auto r=ready();!r)return std::unexpected(r.error());
    auto result=engine_->plan(*playback_);if(!result)return std::unexpected(failure(result.error()));return *result;
}
}
