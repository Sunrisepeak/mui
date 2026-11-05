module openmui.site;
import std;
import openmui;
import :component_motion;

namespace omui::site {
namespace {
struct Recipe {double move,title_x,title_y,turn,scale;};
std::optional<Recipe> recipe(std::string_view selection) {
    if(selection=="together")return Recipe{24,0,0,0,1};
    if(selection=="parts")return Recipe{0,12,-3,.06,1.08};
    if(selection=="title")return Recipe{0,12,-3,.06,1};
    return std::nullopt;
}
}
std::expected<hanim::Program,std::string> component_motion_program(hanim::ObjectRef object,std::string_view selection) {
    using namespace std::chrono_literals;
    const auto values=recipe(selection);if(!values)return std::unexpected("Unknown animation selection");
    return hanim::parallel(
        hanim::anim::move_to(object,{values->move,0}).duration(1200ms),
        hanim::anim::to(object,{.offset=hanim::Vec2{values->title_x,values->title_y},.rotation=values->turn,.part="title"}).duration(1200ms),
        hanim::anim::to(object,{.scale=hanim::Vec2{values->scale,values->scale},.part="save"}).duration(800ms));
}
std::string component_motion_source(std::string_view selection,bool loop,double speed) {
    const auto values=recipe(selection);if(!values)return {};
    return std::format(R"(// Reusable C++ recipe. Bind card, title and save to UI nodes first.
using namespace std::chrono_literals;
auto animation = hanim::parallel(
    hanim::anim::move_to(object, {{{}, 0}}).duration(1200ms),
    hanim::anim::to(object, {{.offset=hanim::Vec2{{{}, {}}},
        .rotation={}, .part="title"}}).duration(1200ms),
    hanim::anim::to(object, {{.scale=hanim::Vec2{{{}, {}}},
        .part="save"}}).duration(800ms));
auto started = playback.play(animation, {{.alternate={}, .rate={}}});
// Check started; call playback.update() before bounds/paint.
)",values->move,values->title_x,values->title_y,values->turn,values->scale,values->scale,loop,speed);
}
Element component_motion_scene(const MotionSceneContext& context) {
    auto& state=*context.state;
    const bool zh=context.language && context.language->get()=="zh";
    const float width=std::min(296.f,std::max(190.f,context.width-56));
    const std::string saved=zh?"已复制":"Copied";
    auto save=button({.id="save",.text=zh?"复制便笺":"Copy note",.variant=ButtonVariant::primary,
        .on_activated=[title=state.text,pinned=state.checked,saves=state.count,receipt=state.choice,copy=context.clipboard] {
            if(copy)copy(Value{}.set("title",title.peek()).set("pinned",pinned.peek()).json());
            saves.set(saves.peek()+1);receipt.set("saved");
        }});
    if(!context.clipboard)save.prop("disabled",Source(Value(true)));
    auto paper=column({.id="card",.width=Dim(width),.padding=Edges(16),.gap=12,
        .background="surface",.border_color="border",.border_width=1,.radius=20}, {
        text({.id="title",.text=computed([title=state.text,zh]{return title.get().empty()?std::string(zh?"无标题":"Untitled"):title.get();}),
              .style="heading",.max_width=Dim(width-32)}),
        text_box({.id="editor",.label=zh?"便笺标题":"Note title",.text=state.text,.width=Dim(width-32)}),
        checkbox({.id="pin",.checked=state.checked,.label=zh?"保留在手边":"Keep it close"}),
        row({.id="actions",.gap=12,.align="center"},{std::move(save),
            popover({.id="receipt",.label=zh?"复制结果":"Copy result",.open=Source{std::nullopt,
                [receipt=state.choice]{return Value(receipt.get()=="saved");},[receipt=state.choice](const Value& v){receipt.set(v.as_bool()?"saved":"");}}}, {
                text({.id="message",.text=zh?"便笺 JSON 已复制。":"Note JSON copied."})}),
            badge({.id="saved",.text=computed([s=state.count,saved]{return std::format("{} {}",saved,s.get());})})
        })
    });
    // The outer stage provides the clip/portal boundary. Its layout remains
    // stable; the public Hanim program owns only explicit presentation offsets.
    return column({.id="composition",.width=Dim(context.width),.height=Dim(context.height),
        .justify="center",.align="center"},{std::move(paper)});
}
bool prepare_component_motion(ComponentMotion& state,Runtime& runtime,std::string_view root,
                              double progress,int direction,double speed,bool loop,bool requested) {
    if(state.playback && state.playback->attached() &&
       !runtime.state(state.playback->targets().front().node).removing &&
       state.configured_selection==state.selection.peek() && state.configured_loop==loop && state.configured_speed==speed)return true;
    const auto card=runtime.find(std::string(root)+"/composition/card");
    if(!runtime.valid(card)){state.playback.reset();return false;}
    if(state.playback && (!state.playback->attached() || state.playback->targets().front().node!=card))state.playback.reset();
    bool fresh=false;
    if(!state.playback) {
        const std::array targets{
            animation::Target{"card",card},
            animation::Target{"title",runtime.find(std::string(root)+"/composition/card/title")},
            animation::Target{"save",runtime.find(std::string(root)+"/composition/card/actions/save")}};
        auto attached=animation::Playback::attach(runtime,targets);
        if(!attached) {
            const auto& error=attached.error();
            const bool busy=error.binding && error.binding->ui && error.binding->ui->code==PresentationError::busy;
            if(!busy)state.error.set("This selection is unavailable.");
            return false;
        }
        state.playback=std::move(*attached);fresh=true;
    }
    const auto selection=state.selection.peek();
    if(fresh || selection!=state.configured_selection || loop!=state.configured_loop) {
        const auto program=component_motion_program(state.playback->object(),selection);
        if(!program){state.error.set(program.error());return false;}
        const bool changed=!fresh && selection!=state.configured_selection;
        if(!fresh)if(auto stopped=state.playback->stop();!stopped){state.error.set("The previous animation could not stop.");return false;}
        auto started=state.playback->play(*program,{.alternate=loop,.rate=(changed?1:direction)*speed});
        if(!started){state.error.set("This animation could not start.");return false;}
        if(!state.playback->seek_progress(changed?0:progress) || (requested && !state.playback->resume())) {
            state.error.set("This animation position is unavailable.");return false;
        }
        state.configured_selection=selection;state.configured_loop=loop;state.configured_speed=speed;state.error.set("");
    } else if(speed!=state.configured_speed) {
        const auto info=state.playback->status().native;
        if(!state.playback->rate((info && info->rate<0?-1:1)*speed)){state.error.set("This playback rate is unavailable.");return false;}
        state.configured_speed=speed;
    }
    return true;
}
}
