import std;
import omui.hanim;
import openmui.ui.base;
import mlibs.raster;
import mlibs.image;
import mlibs.hash;
import mcpplibs.os;
using namespace omui;
using namespace std::chrono_literals;
namespace raster=mlibs::raster;

namespace {
Value::List checks;
Value::List driver_frames;
bool check(bool ok,std::string_view name) {checks.push_back(Value{}.set("name",name).set("ok",ok));return ok;}
int finish(Value::List frames={}) {
    const bool ok=std::ranges::all_of(checks,[](const Value& v){return v.get("ok")->as_bool();});
    std::println("{}",Value{}.set("kind","omui.hanim-consumer").set("ok",ok).set("checks",Value(std::move(checks)))
        .set("frames",Value(std::move(frames))).set("driver_frames",Value(std::move(driver_frames))).json());return ok?0:1;
}
PointerEvent pointer(PointerKind kind,Point at) {
    PointerEvent event;event.kind=kind;event.position=at;event.button=MouseButton::left;
    event.buttons=kind==PointerKind::down?1u:0u;return event;
}
std::string pixel_hash(const raster::Surface& pixels) {return mlibs::hash::Fnv1a{}.bytes(std::as_bytes(pixels.pixels())).hex();}

class DriveStage final : public pipeline::Stage {
public:
    DriveStage(std::weak_ptr<animation::Playback> playback,animation::PlaybackGate& gate):playback_(std::move(playback)),gate_(gate){}
    std::vector<pipeline::Port> reads() const override {return {pipeline::port<FrameTree>(std::string(frame_resource::tree)),pipeline::port<FrameLayout>(std::string(frame_resource::layout))};}
    std::vector<pipeline::Port> writes() const override {return {pipeline::port<FrameTree>(std::string(frame_resource::tree))};}
    std::expected<void,std::string> run(pipeline::Context&) override {
        if(auto playback=playback_.lock())if(auto result=playback->update(gate_);!result)return std::unexpected("public playback update failed");
        return {};
    }
private:
    std::weak_ptr<animation::Playback> playback_;
    animation::PlaybackGate& gate_;
};

void driver_checks(typeset::FontCollection& fonts,const std::filesystem::path& output) {
    std::shared_ptr<animation::Playback> retained;
    {
        animation::PlaybackGate gate;
        RuntimeOptions options;options.fonts=&fonts;options.size={400,220};options.stages=Stages::paint;
        Runtime runtime(options);
        typeset::GlyphCache glyphs(fonts);render::CpuRenderer renderer;raster::Surface pixels(400,220);
        runtime.mount("main",column({.padding=20},{column({.id="card",.width=150,.height=100,.background="surface"},{
            text({.id="title",.text="Same frame"}),button({.id="action",.text="Use"})})}));
        (void)runtime.frame({0s,{}});(void)runtime.frame({2s,{}});
        const auto card=runtime.find("main/card");const auto baseline=runtime.drawn_bounds(card);
        auto attached=animation::Playback::attach(runtime,std::array{animation::Target{"card",card}});
        if(!check(bool(attached),"host_adapter_attaches_without_a_window"))return;
        retained=std::make_shared<animation::Playback>(std::move(*attached));
        const auto registered=runtime.stage_plugins().add({.name="validation.playback",.kind="stage",
            .description="public playback before current UI bounds",.trace_category="omui.hanim.playback",.cost=pipeline::Cost{.cpu_us=50}},
            [weak=std::weak_ptr(retained),&gate](const pipeline::Config&)->std::expected<std::unique_ptr<pipeline::Stage>,std::string>{return std::make_unique<DriveStage>(weak,gate);});
        if(!check(bool(registered),"host_stage_declares_cost_and_registers"))return;
        auto graph=runtime.frame_pipeline().description();
        const auto before=std::ranges::find_if(graph.stages,[](const auto& stage){return stage.name=="bounds";});
        before->after.push_back("public_playback");graph.stages.insert(before,{"public_playback","validation.playback",{"layout"},{}});
        const std::array inputs{pipeline::port<FrameInput>(std::string(frame_resource::input)),pipeline::port<FrameTree>(std::string(frame_resource::tree))};
        auto built=pipeline::Pipeline::build(graph,runtime.stage_plugins(),inputs);
        if(!built)std::println(std::cerr,"{}",pipeline::to_text(built.error()));
        if(!check(bool(built),"host_update_stage_is_before_bounds_and_paint"))return;
        runtime.frame_pipeline()=std::move(*built);
        const auto recipe=hanim::anim::move_to(retained->object(),{100,0}).duration(1s);
        if(!check(bool(retained->play(recipe)),"host_plays_a_public_recipe"))return;
        bool frames_ok=true;
        auto frame=[&](double time){const auto output=runtime.frame({hanim::seconds(time),{}});
            frames_ok=frames_ok && output.failure.empty();
            const auto rendered=renderer.render(runtime.display_list(),pixels,{&runtime.images(),&glyphs});
            frames_ok=frames_ok && rendered.missing_glyphs==0;return output;};
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
    }
    check(retained && !retained->attached() && !retained->update() && !retained->status().demanding_frames,
          "retained_host_adapter_is_safe_after_runtime_destruction");
}
}
int main(int argc,char** argv) {
    if(!check(argc==2,"output_directory_argument"))return finish();
    const auto output=mcpplibs::os::fs::from_utf8(argv[1]);
    typeset::FontCollection fonts;const auto loaded=fonts.add_defaults();
    if(!check(loaded && fonts.face_count()>0,"font_resources_loaded")) {
        if(!loaded)std::println(std::cerr,"{}",loaded.error());return finish();
    }
    RuntimeOptions options;options.fonts=&fonts;options.size={480,260};options.stages=Stages::paint;
    Runtime runtime(options);typeset::GlyphCache glyphs(fonts);render::CpuRenderer renderer;
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
            hanim::anim::to(binding->object(),{.rotation=.12,.part="title"}).duration(500ms),
            hanim::anim::to(binding->object(),{.rotation=0,.part="title"}).duration(500ms)),
        hanim::anim::to(binding->object(),{.scale=hanim::Vec2{1.1,1.1},.part="save"}).duration(750ms));
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
            const auto* run=std::get_if<render::DrawGlyphs>(&command);return run && !run->glyphs.empty();});
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
        "unsupported_image_effect_preserves_all_published_ui_fields");
    if(effect)(void)hengine.release(*effect);
    const auto object=binding->object();binding->detach();(void)runtime.frame({9s,{}});
    check(!binding->attached() && std::abs(runtime.drawn_bounds(root).x-base.x)<.03,"detach_restores_live_base_presentation");
    check(bool(hengine.destroy_object(object)) && !hengine.capture_object(object) && hengine.size()==0,"engine_objects_and_playbacks_released");
    driver_checks(fonts,output);
    return finish(std::move(frames));
}
