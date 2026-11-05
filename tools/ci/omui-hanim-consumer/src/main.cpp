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
bool check(bool ok,std::string_view name) {checks.push_back(Value{}.set("name",name).set("ok",ok));return ok;}
int finish(Value::List frames={}) {
    const bool ok=std::ranges::all_of(checks,[](const Value& v){return v.get("ok")->as_bool();});
    std::println("{}",Value{}.set("kind","omui.hanim-consumer").set("ok",ok).set("checks",Value(std::move(checks)))
        .set("frames",Value(std::move(frames))).json());return ok?0:1;
}
PointerEvent pointer(PointerKind kind,Point at) {
    PointerEvent event;event.kind=kind;event.position=at;event.button=MouseButton::left;
    event.buttons=kind==PointerKind::down?1u:0u;return event;
}
std::string pixel_hash(const raster::Surface& pixels) {return mlibs::hash::Fnv1a{}.bytes(std::as_bytes(pixels.pixels())).hex();}
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
    return finish(std::move(frames));
}
