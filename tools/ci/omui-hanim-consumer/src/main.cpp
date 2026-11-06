import std;
import omui.hanim;
import hanim.control;
import openmui.ui.base;
import xrender.raster;
import mlibs.image;
import mlibs.hash;
import mcpplibs.os;
import xrender.text;
using namespace omui;
using namespace std::chrono_literals;
namespace raster=xrender::raster;

namespace {
Value::List checks;
Value::List driver_frames;
Value::List effect_frames;
Value::List feedback_frames;
Value::List bridge_frames;
bool check(bool ok,std::string_view name) {checks.push_back(Value{}.set("name",name).set("ok",ok));return ok;}
int finish(Value::List frames={}) {
    const bool ok=std::ranges::all_of(checks,[](const Value& v){return v.get("ok")->as_bool();});
    std::println("{}",Value{}.set("kind","omui.hanim-consumer").set("ok",ok).set("checks",Value(std::move(checks)))
        .set("frames",Value(std::move(frames))).set("driver_frames",Value(std::move(driver_frames)))
        .set("effect_frames",Value(std::move(effect_frames))).set("feedback_frames",Value(std::move(feedback_frames)))
        .set("bridge_frames",Value(std::move(bridge_frames))).json());return ok?0:1;
}
PointerEvent pointer(PointerKind kind,Point at) {
    PointerEvent event;event.kind=kind;event.position=at;event.button=MouseButton::left;
    event.buttons=kind==PointerKind::down?1u:0u;return event;
}
std::string pixel_hash(const raster::Surface& pixels) {return mlibs::hash::Fnv1a{}.bytes(std::as_bytes(pixels.pixels())).hex();}

void bridge_checks(mlibs::text::FontCollection& fonts,const std::filesystem::path& output) {
    auto authority=std::make_shared<mlibs::control::Access>(mlibs::control::policy_for_profile("dev"));
    auto ui=std::make_shared<AgentAccess>(authority);
    RuntimeOptions options;options.fonts=&fonts;options.size={400,220};options.stages=Stages::paint;options.access=ui;
    Runtime runtime(options);xrender::text::GlyphCache glyphs(fonts);xrender::CpuRenderer renderer;raster::Surface pixels(400,220);
    bool clicked=false;
    runtime.mount("main",column({.padding=20,.gap=16},{
        column({.id="card",.width=220,.height=80,.padding=12,.background="surface",.radius=12},{text({.id="title",.text="Shared authority"})}),
        button({.id="sample",.text="Sample midpoint",.on_activated=[&]{
            Invocation local;local.args=Value{}.set("progress",.5);
            clicked=runtime.verbs().invoke("animation.sample",local).exit_code()==0;
        }})}));
    bool rendered=true;
    auto paint=[&](double t){const auto frame=runtime.frame({hanim::seconds(t),{}});
        const auto result=renderer.render(runtime.display_list(),pixels,{&runtime.images(),&glyphs});
        rendered=rendered && frame.failure.empty() && !result.missing_glyphs && !result.effects_skipped;return pixel_hash(pixels);};
    paint(0);paint(1);
    auto engine=hanim::Engine::make();if(!check(bool(engine),"bridge_engine_created"))return;
    const auto card=runtime.find("main/card");
    auto binding=animation::Binding::attach(runtime,std::array{animation::Target{"card",card}},*engine);
    if(!check(bool(binding),"bridge_binds_real_ui_content"))return;
    auto playback=engine->play(hanim::anim::move_to(binding->object(),{60,0}));
    if(!check(bool(playback),"bridge_program_started_by_the_host"))return;
    mlibs::control::InvocationContext context;hanim::control::Controller controller(*engine,authority,context);
    int callbacks=0;bool revoke_inside=false;std::optional<mlibs::control::Invocation> carried;
    std::optional<hanim::control::Failure> rejected;
    const auto added=runtime.verbs().add({.name="animation.sample"},[&](const Value& args)->VerbResult {
        ++callbacks;const auto call=runtime.current_invocation();
        if(!call)return std::unexpected("Missing UI invocation");
        auto converted=to_control_invocation(*call);if(!converted)return std::unexpected("Unbound UI authority");
        const auto* progress=args.get("progress");if(!progress || !progress->is_number())return std::unexpected("Missing progress");
        carried=*converted;
        if(revoke_inside && converted->session)(void)authority->revoke_session(*converted->session);
        const auto sampled=controller.seek_progress(*converted,*playback,progress->as_double());
        if(!sampled){rejected=sampled.error();return std::unexpected("Native seek refused");}
        if(!sampled->executed)return Value{}.set("executed",false);
        const auto frame=engine->frame(*playback);
        if(!frame || !binding->present(*frame))return std::unexpected("Presentation failed");
        return Value{}.set("executed",true);
    });
    if(!check(bool(added),"bridge_registers_a_real_ui_verb"))return;
    auto policy=authority->policy();policy.user.operations=std::vector<std::string>{"invoke","animation.seek-progress"};authority->set_policy(policy);
    const auto session=ui->open_session();
    Invocation request{.actor={"agent","ui-bridge",{"person:forged"}},.remote=true,.access=ui,.session=session};
    auto invoke=[&](double progress){auto call=request;call.args=Value{}.set("progress",progress);return runtime.verbs().invoke("animation.sample",call).exit_code()==0;};
    bool good=true;std::vector<std::string> hashes;
    for(int i=0;i<=60;++i) {
        good=invoke(i/60.) && good;hashes.push_back(paint(3+i/60.));
        good=good && engine->inspect(*playback)->position==hanim::seconds(i/60.);
        const auto file=output/std::format("bridge-{:03}.png",i);
        good=bool(mlibs::image::save_png(file,{reinterpret_cast<const std::uint8_t*>(pixels.pixels().data()),400,220,400*4})) && good;
        bridge_frames.push_back(Value{}.set("index",i).set("time",i/60.).set("pixel_fnv1a",hashes.back()).set("png",mcpplibs::os::fs::utf8(file)));
    }
    check(good && rendered && hashes[0]!=hashes[30] && hashes[30]!=hashes[60],"61_ui_invocations_drive_native_control_and_actual_pixels");
    for(int i=60;i>=0;--i)good=invoke(i/60.) && paint(3+i/60.)==hashes[static_cast<std::size_t>(i)] && good;
    check(good && rendered,"ui_native_bridge_replays_all_process_pixels_backwards");
    check(carried && carried->access==authority && carried->session==session->control_session() && carried->remote &&
        carried->operation=="invoke" && carried->actor.via==std::vector{session->owner()},"bridge_keeps_issuer_session_origin_and_host_attribution");
    const auto before=engine->inspect(*playback)->position;const auto before_calls=callbacks;
    auto dry=request;dry.dry_run=true;dry.args=Value{}.set("progress",.9);
    check(runtime.verbs().invoke("animation.sample",dry).exit_code()==0 && callbacks==before_calls && engine->inspect(*playback)->position==before,
        "ui_dry_run_does_not_execute_the_native_callback");
    policy.user.operations=std::vector<std::string>{"invoke"};authority->set_policy(policy);
    check(!invoke(.9) && rejected && rejected->code==hanim::control::Error::admission && engine->inspect(*playback)->position==before,
        "transport_permission_does_not_expand_to_native_domain_permission");
    policy.user.operations=std::vector<std::string>{"animation.seek-progress"};authority->set_policy(policy);
    const auto before_domain=callbacks;
    check(!invoke(.9) && callbacks==before_domain,"native_domain_permission_does_not_bypass_the_ui_entry");
    policy.user.operations=std::vector<std::string>{"invoke","animation.seek-progress"};authority->set_policy(policy);
    revoke_inside=true;rejected.reset();
    check(!invoke(.9) && rejected && rejected->code==hanim::control::Error::admission && engine->inspect(*playback)->position==before,
        "revocation_after_ui_admission_is_rechecked_by_the_native_controller");
    revoke_inside=false;paint(5);
    const auto button=runtime.find("main/sample");const auto at=runtime.drawn_bounds(button).center();
    (void)runtime.frame({6s,{pointer(PointerKind::down,at),pointer(PointerKind::up,at)}});paint(6.1);
    check(clicked && carried && !carried->remote && engine->inspect(*playback)->position==500ms,
        "the_same_bridge_accepts_a_real_local_pointer_action");
    (void)runtime.verbs().remove("animation.sample");
    check(bool(engine->release(*playback)),"bridge_playback_released");
}

void image_checks(mlibs::text::FontCollection& fonts,const std::filesystem::path& output) {
    RuntimeOptions options;options.fonts=&fonts;options.size={400,220};options.stages=Stages::paint;
    Runtime runtime(options);xrender::text::GlyphCache glyphs(fonts);xrender::CpuRenderer renderer;raster::Surface pixels(400,220);
    int activated=0;
    runtime.mount("main",column({.padding=20},{column({.id="card",.width=210,.padding=12,.gap=12,.background="surface",.radius=12},{
        text({.id="title",.text="Real text, shared effects"}),button({.id="action",.text="Use",.on_activated=[&]{++activated;}})})}));
    bool rendered=true;double time=0;
    auto paint=[&]{time+=1;const auto frame=runtime.frame({hanim::seconds(time),{}});
        const auto result=renderer.render(runtime.display_list(),pixels,{&runtime.images(),&glyphs});
        rendered=rendered && frame.failure.empty() && !result.effects_skipped && !result.missing_glyphs;return pixel_hash(pixels);};
    paint();const auto plain=paint();const auto card=runtime.find("main/card"),action=runtime.find("main/card/action");
    const auto base=runtime.drawn_bounds(card);
    auto made=hanim::Engine::make();if(!check(bool(made),"image_engine_created"))return;
    auto engine=std::move(*made);auto binding=animation::Binding::attach(runtime,std::array{animation::Target{"card",card}},engine);
    if(!check(bool(binding),"image_binding_attaches_to_actual_ui_content"))return;
    const auto object=binding->object();
    auto recipe=hanim::composite(hanim::composite(hanim::anim::move_to(object,{60,12}).duration(1s)
        .with_effect(hanim::fx::blur(0,3))).with_effect(hanim::fx::pixelate({3,0},{3,.45f},{hanim::fx::PixelUnit::device})))
        .with_effect(hanim::fx::tone({.amount=0},{.amount=.2f}));
    auto plan=engine.prepare(recipe);
    if(!check(plan && binding->prepare(*plan),"leaf_and_two_nested_scopes_prepare_through_the_public_adapter"))return;
    auto playback=engine.play(*plan);if(!check(bool(playback),"prepared_image_program_plays"))return;
    auto sample=[&](double progress) {if(!engine.seek_progress(*playback,progress))return false;
        const auto frame=engine.program_frame(*playback);return frame && bool(binding->present(*frame));};
    bool good=true;std::vector<std::string> hashes;
    for(int i=0;i<=60 && good;++i) {
        good=sample(i/60.);if(!good)break;hashes.push_back(paint());
        const auto state=runtime.presentation(card);const auto bounds=runtime.drawn_bounds(card);
        good=rendered && state && state->applied && !state->error && state->image_effects==3 &&
            std::abs(bounds.x-base.x-i)<.03 && std::abs(bounds.y-base.y-i/5.)<.03;
        const auto file=output/std::format("effect-{:03}.png",i);
        good=good && bool(mlibs::image::save_png(file,{reinterpret_cast<const std::uint8_t*>(pixels.pixels().data()),400,220,400*4}));
        effect_frames.push_back(Value{}.set("index",i).set("time",i/60.).set("pixel_fnv1a",hashes.back()).set("png",mcpplibs::os::fs::utf8(file)));
    }
    if(!check(good && effect_frames.size()==61,"61_real_ui_frames_combine_motion_leaf_effect_and_nested_scopes"))return;
    check(hashes.front()==plain && hashes[0]!=hashes[30] && hashes[30]!=hashes[60],"zero_strength_is_exact_identity_and_intermediate_pixels_change");
    for(int i=60;i>=0;--i)good=sample(i/60.) && paint()==hashes[static_cast<std::size_t>(i)] && good;
    check(good,"all_61_nested_effect_frames_replay_exactly_in_reverse");
    (void)sample(.5);const auto accepted=paint();const auto pose=runtime.presentation(card)->pose;
    (void)engine.seek_progress(*playback,.75);auto frame=engine.program_frame(*playback);
    if(!check(bool(frame),"program_frame_exposes_typed_scope_samples"))return;
    auto foreign=*frame;foreign.scene.preparation=nullptr;
    check(!binding->present(foreign) && runtime.presentation(card)->pose==pose && paint()==accepted,"foreign_plan_cannot_publish_matching_sized_buffers");
    std::vector<float> bad(frame->scene.scope_effect_values.begin(),frame->scene.scope_effect_values.end());bad.back()=std::numeric_limits<float>::quiet_NaN();
    auto invalid=*frame;invalid.scene.scope_effect_values=bad;
    check(!binding->present(invalid) && runtime.presentation(card)->pose==pose && paint()==accepted,"late_invalid_scope_preserves_the_complete_accepted_ui_batch");
    const auto limited=binding->prepare(*plan,{.max_prepared_effect_layers=1});
    check(!limited && sample(.5) && paint()==accepted,"failed_capacity_preparation_keeps_the_previous_binding_usable");
    const auto tone=*xrender::effects().find("tone");const auto original=*xrender::effects().info(tone);
    check(xrender::effects().replace(tone,original),"effect_provider_can_reload_without_replacing_the_ui");
    const auto stale=binding->present(*frame);
    check(!stale && stale.error().code==animation::BindingError::rendering && runtime.presentation(card)->pose==pose,"stale_generation_is_rejected_before_pose_publication");
    check(binding->prepare(*plan) && sample(.5) && paint()==accepted,"explicit_rebind_restores_identical_pixels_after_provider_reload");
    raster::Surface high_dpi(800,440);const auto dpi=renderer.render(runtime.display_list(),high_dpi,{&runtime.images(),&glyphs},{.scale=2});
    check(!dpi.effects_skipped && !dpi.missing_glyphs,"the_same_live_glyph_and_effect_chain_renders_at_two_times_dpi");
    const auto center=runtime.drawn_bounds(action).center();
    const auto clicked=runtime.frame({hanim::seconds(++time),{pointer(PointerKind::move,center),pointer(PointerKind::down,center),pointer(PointerKind::up,center)}});
    Invocation invoke;invoke.actor={"agent","image-review",{}};const auto activated_by_agent=runtime.act(action,"activate",invoke);
    check(clicked.failure.empty() && activated_by_agent.exit_code()==0 && activated==2,"people_and_agents_operate_the_same_effected_standard_button");
    check(runtime.inspect(card).get("presentation")->get("image_effects")->list()->size()==3,"agent_observation_exposes_the_actual_native_effect_chain");
    (void)engine.release(*playback);
    const auto pure=engine.prepare(hanim::anim::move_to(object,{0,0}).duration(1s));
    if(!check(pure && binding->prepare(*pure),"pure_recipe_prepares_after_an_image_program"))return;
    const auto plain_play=engine.play(*pure);const auto plain_frame=plain_play?engine.frame(*plain_play):std::expected<hanim::EngineFrame,hanim::EngineFailure>(std::unexpected(hanim::EngineFailure{}));
    check(plain_frame && binding->present(*plain_frame) && runtime.presentation(card)->image_effects==0 &&
        (static_cast<unsigned>(runtime.presentation(card)->channels)&16u)==0,"pure_recipe_clears_images_and_releases_only_its_dynamic_image_channel");
    if(plain_play)(void)engine.release(*plain_play);
    const auto constant=engine.play(hanim::anim::move_to(object,{5,0}).with_effect(hanim::EffectSpec{"tone",{}}));
    const auto constant_frame=constant?engine.frame(*constant):std::expected<hanim::EngineFrame,hanim::EngineFailure>(std::unexpected(hanim::EngineFailure{}));
    check(constant_frame && constant_frame->effect_values.empty() && !binding->present(*constant_frame),
        "legacy_pose_frame_cannot_silently_discard_a_zero_parameter_default_effect");
    if(constant) {
        const auto constant_plan=engine.plan(*constant);const auto sampled=engine.program_frame(*constant);
        check(constant_plan && sampled && binding->prepare(*constant_plan) && binding->present(*sampled) &&
            runtime.presentation(card)->image_effects==1,"prepared_program_preserves_zero_parameter_effects_with_renderer_defaults");
    }
    if(constant)(void)engine.release(*constant);
    for(std::string_view unsupported:{"displace","liquid_glass"}) {
        const auto unsupported_plan=engine.prepare(hanim::anim::move_to(object,{5,0}).with_effect(hanim::EffectSpec{std::string(unsupported),{}}));
        check(unsupported_plan && !binding->prepare(*unsupported_plan),"unsupported_spatial_or_backdrop_scope_is_refused_explicitly");
    }
    binding->detach();check(bool(engine.destroy_object(object)),"image_objects_and_ui_leases_release_independently");
    check(rendered,"image_process_pixels_render_without_missing_resources_or_skipped_effects");
}

class DriveStage final : public mlibs::pipeline::Stage {
public:
    DriveStage(std::weak_ptr<animation::Playback> playback,animation::PlaybackGate& gate):playback_(std::move(playback)),gate_(gate){}
    std::vector<mlibs::pipeline::Port> reads() const override {return {mlibs::pipeline::port<FrameTree>(std::string(frame_resource::tree)),mlibs::pipeline::port<FrameLayout>(std::string(frame_resource::layout))};}
    std::vector<mlibs::pipeline::Port> writes() const override {return {mlibs::pipeline::port<FrameTree>(std::string(frame_resource::tree))};}
    std::expected<void,std::string> run(mlibs::pipeline::Context&) override {
        if(auto playback=playback_.lock())if(auto result=playback->update(gate_);!result)return std::unexpected("public playback update failed");
        return {};
    }
private:
    std::weak_ptr<animation::Playback> playback_;
    animation::PlaybackGate& gate_;
};

void driver_checks(mlibs::text::FontCollection& fonts,const std::filesystem::path& output) {
    std::shared_ptr<animation::Playback> retained;
    {
        animation::PlaybackGate gate;
        RuntimeOptions options;options.fonts=&fonts;options.size={400,220};options.stages=Stages::paint;
        Runtime runtime(options);
        xrender::text::GlyphCache glyphs(fonts);xrender::CpuRenderer renderer;raster::Surface pixels(400,220);
        runtime.mount("main",column({.padding=20},{column({.id="card",.width=150,.height=100,.background="surface"},{
            text({.id="title",.text="Same frame"}),button({.id="action",.text="Use"})})}));
        (void)runtime.frame({0s,{}});(void)runtime.frame({2s,{}});
        const auto card=runtime.find("main/card");const auto baseline=runtime.drawn_bounds(card);
        auto attached=animation::Playback::attach(runtime,std::array{animation::Target{"card",card}});
        if(!check(bool(attached),"host_adapter_attaches_without_a_window"))return;
        retained=std::make_shared<animation::Playback>(std::move(*attached));
        check(!retained->rebind(),"rebind_requires_an_existing_program");
        const auto registered=runtime.stage_plugins().add({.name="validation.playback",.kind="stage",
            .description="public playback before current UI bounds",.trace_category="omui.hanim.playback",.cost=mlibs::pipeline::Cost{.cpu_us=50}},
            [weak=std::weak_ptr(retained),&gate](const mlibs::pipeline::Config&)->std::expected<std::unique_ptr<mlibs::pipeline::Stage>,std::string>{return std::make_unique<DriveStage>(weak,gate);});
        if(!check(bool(registered),"host_stage_declares_cost_and_registers"))return;
        auto graph=runtime.frame_pipeline().description();
        const auto before=std::ranges::find_if(graph.stages,[](const auto& stage){return stage.name=="bounds";});
        before->after.push_back("public_playback");graph.stages.insert(before,{"public_playback","validation.playback",{"layout"},{}});
        const std::array inputs{mlibs::pipeline::port<FrameInput>(std::string(frame_resource::input)),mlibs::pipeline::port<FrameTree>(std::string(frame_resource::tree))};
        auto built=mlibs::pipeline::Pipeline::build(graph,runtime.stage_plugins(),inputs);
        if(!built)std::println(std::cerr,"{}",mlibs::pipeline::to_text(built.error()));
        if(!check(bool(built),"host_update_stage_is_before_bounds_and_paint"))return;
        runtime.frame_pipeline()=std::move(*built);
        const auto recipe=hanim::anim::move_to(retained->object(),{100,0}).duration(1s);
        if(!check(bool(retained->play(recipe)),"host_plays_a_public_recipe"))return;
        bool frames_ok=true;
        auto frame=[&](double time){const auto output=runtime.frame({hanim::seconds(time),{}});
            frames_ok=frames_ok && output.failure.empty();
            const auto rendered=renderer.render(runtime.display_list(),pixels,{&runtime.images(),&glyphs});
            frames_ok=frames_ok && rendered.missing_glyphs==0 && rendered.effects_skipped==0;return output;};
        auto pose=[&](double position){return std::abs(retained->status().native->position.count()-position)<1e-7 &&
            std::abs(runtime.drawn_bounds(card).x-baseline.x-100*position)<.03 && runtime.presentation(card)->applied;};
        bool sampled=true;std::vector<std::string> reference;
        for(int i=0;i<=60;++i) {
            frame(2+i/60.);sampled=sampled && pose(i/60.);
            const auto file=output/std::format("driver-{:03}.png",i);
            sampled=sampled && bool(mlibs::image::save_png(file,{reinterpret_cast<const std::uint8_t*>(pixels.pixels().data()),400,220,400*4}));
            reference.push_back(pixel_hash(pixels));
            driver_frames.push_back(Value{}.set("index",i).set("time",i/60.).set("pixel_fnv1a",reference.back()).set("png",mcpplibs::os::fs::utf8(file)));
        }
        for(int i=60;i>=0;--i){frame(2+i/60.);sampled=sampled && pose(i/60.) && pixel_hash(pixels)==reference[static_cast<std::size_t>(i)];}
        check(sampled,"host_clock_renders_all_61_current_frames_and_exact_reverse_replay");
        frame(2.25);check(pose(.25) && retained->status().demanding_frames,"host_time_publishes_the_current_pose_in_the_same_frame");
        check(!retained->play(recipe),"a_second_program_cannot_silently_replace_the_first");
        (void)retained->pause();const auto paused=frame(10);
        check(pose(.25) && paused.demand.idle() && !retained->status().demanding_frames,"pause_preserves_pose_and_releases_continuous_frame_demand");
        (void)retained->resume();frame(10.25);check(pose(.5),"resume_keeps_continuity");
        gate.visible=false;frame(11);frame(19);check(pose(.5) && !retained->status().demanding_frames,"hidden_time_does_not_advance_the_native_player");
        gate.visible=true;frame(20);frame(20.1);check(pose(.6),"visible_again_without_catching_up_hidden_time");
        frame(20.05);check(pose(.55),"a_backward_host_clock_uses_the_same_time_mapping");
        (void)retained->reverse();frame(20.15);check(pose(.45),"reverse_is_continuous");
        (void)retained->rate(-2);frame(20.25);check(pose(.25),"rate_changes_are_continuous");
        runtime.set_motion_level(MotionLevel::off);(void)retained->seek_progress(.4);frame(21);
        check(pose(.4) && !retained->status().requested,"explicit_sampling_remains_available_with_automatic_motion_off");
        (void)retained->resume();frame(22);
        check(pose(.4) && retained->status().suspended==animation::Suspension::motion_preference && !retained->status().demanding_frames,
              "automatic_playback_respects_the_runtime_motion_preference");
        runtime.set_motion_level(MotionLevel::full);frame(30);frame(30.1);check(pose(.2),"motion_preference_resume_does_not_jump");
        runtime.pause_time(true);const auto frozen=frame(31);
        check(pose(.2) && retained->status().clock_paused && frozen.demand.idle(),"a_paused_runtime_clock_does_not_request_busy_frames");
        runtime.seek_time(hanim::seconds(30.05));frame(31);check(pose(.3),"explicit_runtime_time_seek_still_samples_while_the_clock_is_paused");
        runtime.release_time();(void)retained->pause();frame(32);
        check(!retained->seek_progress(std::numeric_limits<double>::quiet_NaN()) && pose(.3),"invalid_sampling_keeps_the_accepted_pose");
        (void)retained->stop();frame(32.1);check(std::abs(runtime.drawn_bounds(card).x-baseline.x)<.03,"stop_restores_the_initial_offset");
        check(bool(retained->play(recipe,{.alternate=true})),"alternating_transport_uses_the_public_time_map");
        frame(34.35);check(pose(.25),"large_time_steps_preserve_repeated_phase");
        frame(35.35);check(pose(.75) && retained->status().native->rate<0,"alternate_boundary_direction_is_observable");
        gate.interacting=true;frame(36);check(pose(.75) && !retained->status().demanding_frames,"interaction_gate_preserves_the_visible_pose");
        (void)retained->pause();gate.interacting=false;frame(40);check(pose(.75) && !retained->status().requested,"leaving_interaction_cannot_undo_an_explicit_pause");
        check(bool(retained->plan()),"the_host_exposes_the_actual_immutable_hanim_plan");
        check(frames_ok,"host_pipeline_frames_render_without_failures");
        (void)retained->stop(hanim::CancelMode::hold);
        const auto image_recipe=hanim::composite(hanim::anim::move_to(retained->object(),{30,0}).duration(1s))
            .with_effect(hanim::fx::tone({.amount=0},{.amount=.3f}));
        if(!check(bool(retained->play(image_recipe)) && bool(retained->seek_progress(.4)),"host_image_program_is_ready_for_provider_reload"))return;
        frame(41);const auto accepted=pixel_hash(pixels);const auto identity=retained->plan()->identity();
        const auto pose_before=runtime.presentation(card)->pose;
        const auto tone=*xrender::effects().find("tone");const auto original=*xrender::effects().info(tone);
        check(xrender::effects().replace(tone,original),"host_provider_reload_advances_generation");
        check(!retained->seek_progress(.6) && retained->status().native->position==hanim::seconds(.4) &&
            runtime.presentation(card)->pose==pose_before,"stale_host_sampling_preserves_player_position_and_accepted_pose");
        check(bool(retained->rebind()),"owning_host_rebinds_without_restarting_the_program");frame(42);
        check(pixel_hash(pixels)==accepted && retained->plan()->identity()==identity &&
            !retained->status().requested && !retained->status().demanding_frames &&
            retained->status().native->position==hanim::seconds(.4),"rebind_preserves_pixels_plan_position_and_pause_intent");
        auto incompatible=original;incompatible.preserves_hit_geometry=false;
        check(xrender::effects().replace(tone,incompatible),"provider_can_publish_an_incompatible_capability");
        check(!retained->rebind() && runtime.presentation(card)->pose==pose_before &&
            retained->status().native->position==hanim::seconds(.4),"incompatible_rebind_retains_accepted_state");
        check(xrender::effects().replace(tone,original) && bool(retained->rebind()),"compatible_provider_recovers_after_a_failed_rebind");
        frame(43);check(pixel_hash(pixels)==accepted,"recovered_provider_restores_the_actual_image");
        (void)retained->resume();frame(43.1);
        check(std::abs(retained->status().native->position.count()-.5)<1e-7,"explicit_resume_continues_from_the_preserved_position");
        check(xrender::effects().replace(tone,original) && bool(retained->rebind()) && retained->status().requested,
              "rebind_also_preserves_running_intent");
        frame(43.2);check(std::abs(retained->status().native->position.count()-.6)<1e-7 && frames_ok,
              "running_rebind_keeps_the_existing_host_clock_mapping");
        (void)retained->stop();
        check(bool(retained->play(recipe)) && bool(retained->rebind()),"pure_property_programs_keep_the_same_rebind_contract");
    }
    check(retained && !retained->attached() && !retained->update() && !retained->rebind() && !retained->status().demanding_frames,
          "retained_host_adapter_is_safe_after_runtime_destruction");
}
}
int main(int argc,char** argv) {
    if(!check(argc==2,"output_directory_argument"))return finish();
    const auto output=mcpplibs::os::fs::from_utf8(argv[1]);
    mlibs::text::FontCollection fonts;const auto loaded=fonts.add_defaults();
    if(!check(loaded && fonts.face_count()>0,"font_resources_loaded")) {
        if(!loaded)std::println(std::cerr,"{}",loaded.error());return finish();
    }
    RuntimeOptions options;options.fonts=&fonts;options.size={480,260};options.stages=Stages::paint;
    Runtime runtime(options);xrender::text::GlyphCache glyphs(fonts);xrender::CpuRenderer renderer;
    int activated=0;auto open=signal(false);
    auto save=button({.id="save",.text="Save changes",.variant=ButtonVariant::primary,.on_activated=[&]{++activated;open.set(true);},.width=116});
    auto card=column({.id="card",.width=204,.height=150,.padding=Edges(16),.gap=14,.background="#f1f5f9",.radius=16,.clip=true},
        {text({.id="title",.text="A live component"}),std::move(save),
         popover({.id="popup",.label="Saved changes",.open=open},{text({.text="Ready to share"})})});
    runtime.mount("main",stack({.width=480,.height=260,.padding=Edges(20),.background="#ffffff"},{std::move(card)}));
    (void)runtime.frame({0s,{}});(void)runtime.frame({2s,{}});
    const auto root=runtime.find("main/card"),title=runtime.find("main/card/title"),button_node=runtime.find("main/card/save");
    const auto base=runtime.drawn_bounds(root);
    auto made=hanim::Engine::make();if(!check(bool(made),"native_engine_created"))return finish();
    auto hengine=std::move(*made);
    const std::array targets{
        animation::Target{"card",root,PresentationChannel::translation},
        animation::Target{"title",title,PresentationChannel::rotation},
        animation::Target{"save",button_node,PresentationChannel::scale}};
    auto binding=animation::Binding::attach(runtime,targets,hengine);
    if(!check(binding && binding->attached(),"explicit_root_text_and_component_channels_attached"))return finish();
    const auto snapshot=hengine.capture_object(binding->object());
    check(snapshot && snapshot->parts().size()==3,"presentation_object_retains_selected_hierarchy");
    const auto program=hanim::parallel(
        hanim::anim::move_to(binding->object(),{100,20}).duration(1s),
        hanim::sequence(
            hanim::anim::to(binding->object(),{.part="title",.rotation_offset=.12}).duration(500ms),
            hanim::anim::to(binding->object(),{.part="title",.rotation_offset=-.12}).duration(500ms)),
        hanim::anim::to(binding->object(),{.part="save",.scale_factor=hanim::Vec2{1.1,1.1}}).duration(750ms));
    auto played=hengine.play(program);
    if(!check(bool(played),"public_independent_channel_program_plays"))return finish();
    Value::List frames;std::vector<std::string> hashes;bool good=true,has_glyphs=false;
    for(int i=0;good && i<=60;++i) {
        good=bool(hengine.seek_progress(*played,i/60.));const auto frame=hengine.frame(*played);
        good=good && frame && bool(binding->present(*frame));if(!good)break;
        const auto result=runtime.frame({hanim::seconds(3+i/60.),{}});
        const auto bounds=runtime.drawn_bounds(root);const auto state=runtime.presentation(root);
        good=result.failure.empty() && state && state->applied && !state->error &&
            std::abs(bounds.x-(base.x+100*i/60.))<.03 && std::abs(bounds.y-(base.y+20*i/60.))<.03;
        has_glyphs=has_glyphs || std::ranges::any_of(runtime.display_list().commands(),[](const auto& command){
            const auto* run=std::get_if<xrender::DrawGlyphs>(&command);return run && !run->glyphs.empty();});
        raster::Surface pixels(480,260);const auto rendered=renderer.render(runtime.display_list(),pixels,{&runtime.images(),&glyphs});
        const auto title_box=runtime.drawn_bounds(title);std::size_t ink=0;
        for(int y=std::max(0,static_cast<int>(std::floor(title_box.y)));y<std::min(260,static_cast<int>(std::ceil(title_box.bottom())));++y)
            for(int x=std::max(0,static_cast<int>(std::floor(title_box.x)));x<std::min(480,static_cast<int>(std::ceil(title_box.right())));++x) {
                const auto pixel=pixels.at(x,y);if(pixel.r<100 && pixel.g<100 && pixel.b<100 && pixel.a>200)++ink;
            }
        good=good && rendered.missing_glyphs==0 && ink>10;
        const auto file=output/std::format("frame-{:03}.png",i);
        good=good && bool(mlibs::image::save_png(file,{reinterpret_cast<const std::uint8_t*>(pixels.pixels().data()),480,260,480*4}));
        hashes.push_back(pixel_hash(pixels));
        frames.push_back(Value{}.set("index",i).set("time",i/60.).set("pixel_fnv1a",hashes.back()).set("png",mcpplibs::os::fs::utf8(file))
            .set("x",bounds.x).set("y",bounds.y).set("title_ink_pixels",static_cast<std::int64_t>(ink)));
    }
    if(!check(good && frames.size()==61 && has_glyphs,"61_real_frames_include_shaped_text_component_and_current_bounds"))return finish(std::move(frames));
    good=true;
    for(int i=60;i>=0 && good;--i) {
        good=bool(hengine.seek_progress(*played,i/60.));const auto frame=hengine.frame(*played);
        good=good && frame && bool(binding->present(*frame));if(!good)break;
        good=runtime.frame({hanim::seconds(5+(60-i)/60.),{}}).failure.empty();
        raster::Surface pixels(480,260);(void)renderer.render(runtime.display_list(),pixels,{&runtime.images(),&glyphs});
        good=good && pixel_hash(pixels)==hashes[static_cast<std::size_t>(i)];
    }
    check(good,"reverse_sampling_reproduces_all_61_pixels_without_advancing_business_state");
    const auto seek=hengine.seek_progress(*played,.5);const auto frame=hengine.frame(*played);
    if(!check(seek && frame && binding->present(*frame),"midpoint_pose_published_for_real_input"))return finish(std::move(frames));
    (void)runtime.frame({7s,{}});const auto center=runtime.drawn_bounds(button_node).center();
    const auto input=runtime.frame({hanim::seconds(7.01),{pointer(PointerKind::move,center),pointer(PointerKind::down,center),pointer(PointerKind::up,center)}});
    check(input.failure.empty() && activated==1,"real_pointer_activates_the_moving_standard_button");
    Invocation call;call.actor={"agent","presentation-review",{}};
    const auto action=runtime.act(button_node,"activate",call);(void)runtime.frame({hanim::seconds(7.02),{}});
    check(action.exit_code()==0 && activated==2,"agent_activates_the_same_component_through_native_semantics");
    const auto info=runtime.inspect(root);
    check(info.get("presentation") && info.get("presentation")->get("representation")->as_string()=="local-presentation-offset",
        "agent_inspection_describes_the_actual_representation");
    (void)runtime.frame({8s,{}});
    const auto popup=runtime.find("main/card/popup");
    const auto popup_before=runtime.drawn_bounds(popup),button_before=runtime.drawn_bounds(button_node);
    check(open.peek() && popup_before.width>0 && std::abs(popup_before.x-button_before.x)<.03 &&
        popup_before.y>=button_before.bottom(),"actual_popover_anchors_to_the_projected_button");
    const auto retarget=hengine.retarget(*played,hanim::PropertyTargets{.offset=hanim::Vec2{20,10}});
    const auto moved=retarget && hengine.seek_progress(*played,1);
    const auto moved_frame=hengine.frame(*played);
    if(!check(moved && moved_frame && binding->present(*moved_frame),"retarget_keeps_current_child_channels"))return finish(std::move(frames));
    (void)runtime.frame({hanim::seconds(8.01),{}});
    const auto popup_after=runtime.drawn_bounds(popup),button_after=runtime.drawn_bounds(button_node);
    check(std::abs(button_after.x-button_before.x-20)<.03 && std::abs(button_after.y-button_before.y-10)<.03 &&
        std::abs(popup_after.x-popup_before.x-20)<.03 && std::abs(popup_after.y-popup_before.y-10)<.03,
        "popover_follows_new_anchor_in_the_same_frame_without_a_second_easing");
    check(runtime.inspect(popup).get("presentation") &&
        runtime.inspect(popup).get("presentation")->get("live_anchor_placement")->as_bool(),"anchor_following_is_observable");
    const auto dismissed=runtime.act(popup,"dismiss",call);(void)runtime.frame({hanim::seconds(8.02),{}});
    check(dismissed.exit_code()==0 && !open.peek(),"agent_dismisses_the_real_anchored_component");
    auto occupied=runtime.acquire_presentation(std::array{PresentationTarget{root,PresentationChannel::translation}});
    check(!occupied && occupied.error().code==PresentationError::occupied,"overlapping_writer_is_explicitly_refused");
    check(bool(hengine.release(*played)),"original_playback_released");
    const auto before=runtime.presentation(root)->pose;
    auto effect=hengine.play(hanim::anim::move_to(binding->object(),{60,0}).with_effect(hanim::fx::blur(0,2)));
    const auto effect_frame=effect?hengine.frame(*effect):std::expected<hanim::EngineFrame,hanim::EngineFailure>(std::unexpected(hanim::EngineFailure{}));
    const auto unsupported=effect_frame?binding->present(*effect_frame):std::expected<void,animation::BindingFailure>{};
    check(effect && !unsupported && unsupported.error().code==animation::BindingError::unsupported_frame && runtime.presentation(root)->pose==before,
        "unprepared_legacy_image_frame_preserves_all_published_ui_fields");
    if(effect)(void)hengine.release(*effect);
    const auto object=binding->object();binding->detach();(void)runtime.frame({9s,{}});
    check(!binding->attached() && std::abs(runtime.drawn_bounds(root).x-base.x)<.03,"detach_restores_live_base_presentation");
    check(bool(hengine.destroy_object(object)) && !hengine.capture_object(object) && hengine.size()==0,"engine_objects_and_playbacks_released");
    {
    (void)runtime.frame({10s,{}});(void)runtime.frame({11s,{}});
    auto feedback=animation::Playback::attach(runtime,targets);
    if(!check(bool(feedback),"public_feedback_attaches_to_real_text_and_controls"))return finish(std::move(frames));
    const auto feedback_program=hanim::parallel(
        hanim::anim::shake(feedback->object(),{8,0},{.duration=1s}),
        hanim::anim::wobble(feedback->object(),.12,{.duration=1s,.part="title"}),
        hanim::anim::pulse(feedback->object(),{1.125,1.125},{.duration=1s,.part="save"}));
    if(!check(bool(feedback->play(feedback_program)),"public_feedback_composes_without_a_site_dependency"))return finish(std::move(frames));
    raster::Surface feedback_pixels(480,260);std::vector<std::string> feedback_hashes;bool feedback_ok=true;
    auto feedback_sample=[&](int i) {
        if(!feedback->seek_progress(i/60.))return false;
        const auto frame=runtime.frame({hanim::seconds(12+i/60.),{}});
        const auto rendered=renderer.render(runtime.display_list(),feedback_pixels,{&runtime.images(),&glyphs});
        const auto state=runtime.presentation(root);
        return frame.failure.empty() && rendered.missing_glyphs==0 && rendered.effects_skipped==0 && state && state->applied && !state->error;
    };
    for(int i=0;i<=60;++i) {
        feedback_ok=feedback_sample(i) && feedback_ok;feedback_hashes.push_back(pixel_hash(feedback_pixels));
        const auto file=output/std::format("feedback-{:03}.png",i);
        feedback_ok=bool(mlibs::image::save_png(file,{reinterpret_cast<const std::uint8_t*>(feedback_pixels.pixels().data()),480,260,480*4})) && feedback_ok;
        feedback_frames.push_back(Value{}.set("index",i).set("time",i/60.).set("pixel_fnv1a",feedback_hashes.back()).set("png",mcpplibs::os::fs::utf8(file)));
    }
    check(feedback_ok && feedback_hashes.front()==feedback_hashes.back() && feedback_hashes[15]!=feedback_hashes.front() &&
        feedback_hashes[30]!=feedback_hashes.front(),"61_feedback_frames_cover_swings_pulse_and_return");
    for(int i=60;i>=0;--i)feedback_ok=feedback_sample(i) && pixel_hash(feedback_pixels)==feedback_hashes[static_cast<std::size_t>(i)] && feedback_ok;
    check(feedback_ok,"public_feedback_replays_all_intermediate_pixels_backwards");
    (void)feedback->stop();
    }
    driver_checks(fonts,output);
    image_checks(fonts,output);
    bridge_checks(fonts,output);
    return finish(std::move(frames));
}
