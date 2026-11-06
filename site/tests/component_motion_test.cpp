import std;
import omui;
import omui.site;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;
namespace {
constexpr std::string_view root="main/body/view/motion/page/motion_gallery";
const std::filesystem::path& media(){static const auto path=scratch_dir("site-component-motion");return path;}
struct Run {
    Headless h;
    explicit Run(float width=1100,std::string_view lang="en"):h(HeadlessOptions{.size={width,1000},.parallel_raster=false}) {
        auto app=site::app("motion",lang);h.runtime().set_theme(app.theme);app.setup(h.runtime());
        for(auto& verb:app.verbs)(void)h.runtime().verbs().add(verb.desc,verb.fn);
        h.mount("main",app.view());for(int i=0;i<6;++i)step();
        act("choose");act("browse/choices/component-motion");
        for(int i=0;i<120 && !bound();++i)step();
        check(bound(),"the real site attaches its public Hanim binding");
    }
    std::string path(std::string_view suffix) const{return std::string(root)+"/"+std::string(suffix);}
    NodeId node(std::string_view suffix){return h.runtime().find(path(suffix));}
    void step(double dt=1./60){check(h.advance(seconds(dt)).failure.empty());}
    void act(std::string_view suffix,std::string_view action="activate",Value args={}) {
        Invocation call;call.actor={"agent","component-motion-review",{}};call.args=std::move(args);
        check(h.tree().act(path(suffix),action,call).exit_code()==0,std::string(suffix));step();
    }
    bool bound() {
        const auto state=h.runtime().presentation(node("stage/scene/composition/card"));
        return state && state->channels!=PresentationChannel::none && state->applied && !state->error;
    }
    void details() {act("transport/buttons/details");for(int i=0;i<4;++i)step();}
    void seek(double progress) {
        // Setting an unchanged slider value is intentionally not a transport
        // command. Endpoints use their explicit actions, which always pause.
        if(progress==0)act("transport/buttons/state-a");
        else if(progress==1)act("transport/buttons/state-b");
        else act("transport/seek","set_value",Value{}.set("value",progress));
        step();
    }
    std::uint64_t pixels() {
        const auto area=h.runtime().drawn_bounds(node("stage"));const auto& pixels=h.pixels();Hash hash;
        const int x0=std::clamp(static_cast<int>(area.x),0,pixels.width()),x1=std::clamp(static_cast<int>(area.right()),x0,pixels.width());
        const int y0=std::clamp(static_cast<int>(area.y),0,pixels.height()),y1=std::clamp(static_cast<int>(area.bottom()),y0,pixels.height());
        for(int y=y0;y<y1;++y)hash.bytes(std::as_bytes(std::span(pixels.row(y)+x0,static_cast<std::size_t>(x1-x0))));
        return hash.value();
    }
};
}
int main(int argc,char** argv) {
    Suite suite{"site.component-motion"};
    suite.test("the authored recipes animate an unrelated UI hierarchy through public part bindings",[] {
        for(const auto selection:{"together","parts","title"}) {
            Headless h(HeadlessOptions{.size={480,240},.parallel_raster=false});
            h.mount("other",column({.padding=24},{row({.id="toolbar",.padding=10,.gap=20,.background="surface"},{
                text({.id="caption",.text="Another application"}),button({.id="publish",.text="Publish"})})}));
            h.settle(seconds(1));
            auto engine=hanim::Engine::make();check(engine.has_value());if(!engine)return;
            const std::array targets{animation::Target{"card",h.runtime().find("other/toolbar")},
                animation::Target{"title",h.runtime().find("other/toolbar/caption")},
                animation::Target{"save",h.runtime().find("other/toolbar/publish")}};
            auto binding=animation::Binding::attach(h.runtime(),targets,*engine);check(binding.has_value());if(!binding)return;
            auto recipe=site::component_motion_program(binding->object(),selection);check(recipe.has_value());if(!recipe)return;
            auto played=engine->play(*recipe);check(played.has_value());if(!played)return;
            std::vector<std::uint64_t> hashes;
            for(const auto progress:{0.,.25,.5,.75,1.}) {
                check(engine->seek_progress(*played,progress).has_value());
                auto frame=engine->frame(*played);check(frame.has_value());if(!frame)return;
                check(binding->present(*frame).has_value());check(h.step().failure.empty());
                hashes.push_back(site::frame_hash(h.pixels()));
                check(h.runtime().presentation(targets.front().node)->applied);
            }
            check(hashes.front()!=hashes[2] && hashes[2]!=hashes.back(),"recipe produces real intermediate pixels outside the website scene");
            check(engine->release(*played).has_value());const auto object=binding->object();binding->detach();
            check(engine->destroy_object(object).has_value());
        }
    });
    suite.test("one live stage exposes three public recipes only after details and samples the whole process",[] {
        Run r;check(!r.h.tree().node(r.path("details/selection")));r.details();
        for(const std::string_view selection:{"together","parts","title"}) {
            r.act("details/selection","set_value",Value{}.set("value",selection));r.seek(0);
            const auto card=r.h.runtime().drawn_bounds(r.node("stage/scene/composition/card"));
            const auto title=r.h.runtime().drawn_bounds(r.node("stage/scene/composition/card/title"));
            const auto action=r.h.runtime().drawn_bounds(r.node("stage/scene/composition/card/actions/save"));
            std::vector<std::uint64_t> frames;
            for(int i=0;i<=60;++i) {
                r.seek(i/60.);check(r.bound());frames.push_back(r.pixels());
                check(r.h.save_png(media()/std::format("{}-{:03}.png",selection,i),r.h.runtime().drawn_bounds(r.node("stage"))).has_value());
                const auto actual=r.h.runtime().drawn_bounds(r.node("stage/scene/composition/card"));
                check_near(actual.x,card.x+(selection=="together"?24*i/60.:0),.1);
            }
            check(frames.front()!=frames[30] && frames[30]!=frames.back(),"actual intermediate images change");
            if(selection!="together") {
                check(r.h.runtime().drawn_bounds(r.node("stage/scene/composition/card/title"))!=title);
                if(selection=="title")check(r.h.runtime().drawn_bounds(r.node("stage/scene/composition/card/actions/save"))==action);
            }
            for(int i=60;i>=0;--i){r.seek(i/60.);check(r.pixels()==frames[static_cast<std::size_t>(i)],"all reversed process frames reproduce");}
            r.act("details/copy");const auto source=r.h.runtime().clipboard_text();
            check(source==site::component_motion_source(selection,true,1));
            check(source.find("playback.play")!=std::string::npos && source.find("hanim::parallel")!=std::string::npos);
        }
    });
    suite.test("the public feedback combination samples its swings and pulse and returns the real note",[] {
        Run r;r.details();r.act("details/selection","set_value",Value{}.set("value","feedback"));r.seek(0);
        const auto card=r.h.runtime().drawn_bounds(r.node("stage/scene/composition/card"));
        std::vector<std::uint64_t> frames;
        for(int i=0;i<=60;++i) {
            r.seek(i/60.);check(r.bound());frames.push_back(r.pixels());
            check(r.h.save_png(media()/std::format("feedback-{:03}.png",i),r.h.runtime().drawn_bounds(r.node("stage"))).has_value());
        }
        check(frames.front()==frames.back() && frames[15]!=frames.front() && frames[30]!=frames.front());
        check(r.h.runtime().drawn_bounds(r.node("stage/scene/composition/card"))==card);
        for(int i=60;i>=0;--i){r.seek(i/60.);check(r.pixels()==frames[static_cast<std::size_t>(i)]);}
        r.act("details/copy");const auto source=r.h.runtime().clipboard_text();
        check(source==site::component_motion_source("feedback",true,1));
        check(source.find("hanim::anim::pulse")!=std::string::npos && source.find("hanim::anim::shake")!=std::string::npos &&
            source.find("hanim::anim::wobble")!=std::string::npos);
        r.seek(.25);const auto save=r.node("stage/scene/composition/card/actions/save");
        r.h.click(r.h.runtime().drawn_bounds(save).center());r.step();
        check(parse_json(r.h.runtime().clipboard_text()).has_value(),"the pulsing button keeps its real pointer action");
    });
    suite.test("public timed waypoints drive the site through semantic control and compose image effects",[] {
        Run r;r.details();r.act("details/selection","set_value",Value{}.set("value","path"));
        auto access=std::make_shared<AgentAccess>(std::make_shared<mlibs::control::Access>(mlibs::control::policy_for_profile("dev")));
        r.h.runtime().set_agent_access(access);const auto session=access->open_session();
        auto seek=[&](double value){
            const auto reply=r.h.runtime().verbs().invoke("motion.seek",Invocation{.args=Value{}.set("progress",value),
                .actor={"agent","path-review",{}},.remote=true,.access=access,.session=session});
            check(reply.exit_code()==0);r.step();r.step();
        };
        seek(0);const auto initial=r.pixels();std::vector<std::uint64_t> images;
        for(int i=0;i<=60;++i) {
            seek(i/60.);check(r.bound());images.push_back(r.pixels());
            check(r.h.save_png(media()/std::format("path-{:03}.png",i),r.h.runtime().drawn_bounds(r.node("stage"))).has_value());
        }
        check(images.front()==initial && images.back()==initial && images[20]!=initial && images[40]!=initial);
        for(int i=60;i>=0;--i){seek(i/60.);check(r.pixels()==images[std::size_t(i)]);}
        r.act("details/copy");const auto source=r.h.runtime().clipboard_text();
        check(source==site::component_motion_source("path",true,1) && source.find("hanim::anim::move_through")!=std::string::npos);
        r.act("details/effect","set_value",Value{}.set("value","pixel"));seek(.5);
        check(r.pixels()!=images[30]);
        check(r.h.save_png(media()/"path-pixel-midpoint.png",r.h.runtime().drawn_bounds(r.node("stage"))).has_value());
        const auto save=r.node("stage/scene/composition/card/actions/save");r.h.click(r.h.runtime().drawn_bounds(save).center());r.step();
        check(parse_json(r.h.runtime().clipboard_text()).has_value(),"the path-animated styled note retains its real control");
    });
    suite.test("public wait intervals hold the card while independent parts animate",[] {
        Run r;r.details();r.act("details/selection","set_value",Value{}.set("value","rhythm"));
        r.seek(0);const auto card=r.node("stage/scene/composition/card"),title=r.node("stage/scene/composition/card/title");
        const auto initial=r.h.runtime().drawn_bounds(card);std::vector<std::uint64_t> frames;
        for(int i=0;i<=60;++i) {
            r.seek(i/60.);const double t=1.2*i/60.;
            const double offset=t<.2?0:t<.5?60*(t-.2):t<.9?18:18-60*(t-.9);
            check_near(r.h.runtime().drawn_bounds(card).x,initial.x+offset,.1);
            frames.push_back(r.pixels());
            check(r.h.save_png(media()/std::format("rhythm-{:03}.png",i),r.h.runtime().drawn_bounds(r.node("stage"))).has_value());
        }
        check(frames.front()==frames.back() && frames[0]==frames[5] && frames[25]!=frames[30]);
        for(int i=60;i>=0;--i){r.seek(i/60.);check(r.pixels()==frames[std::size_t(i)]);}
        r.seek(.5);const auto held=r.h.runtime().drawn_bounds(card),moving=r.h.runtime().drawn_bounds(title);
        r.seek(.6);check(r.h.runtime().drawn_bounds(card)==held && r.h.runtime().drawn_bounds(title)!=moving);
        r.act("details/copy");check(r.h.runtime().clipboard_text()==site::component_motion_source("rhythm",true,1));
        r.act("details/effect","set_value",Value{}.set("value","pixel"));r.seek(.5);check(r.pixels()!=frames[30]);
        check(r.h.save_png(media()/"rhythm-pixel-midpoint.png",r.h.runtime().drawn_bounds(r.node("stage"))).has_value());
        const auto save=r.node("stage/scene/composition/card/actions/save");r.h.click(r.h.runtime().drawn_bounds(save).center());r.step();
        check(parse_json(r.h.runtime().clipboard_text()).has_value());
    });
    suite.test("finite repeated child programs accumulate steps and keep effect and input composition",[] {
        Run r;r.details();r.act("details/selection","set_value",Value{}.set("value","steps"));
        r.seek(0);const auto card=r.node("stage/scene/composition/card");const auto initial=r.h.runtime().drawn_bounds(card);
        std::vector<std::uint64_t> frames;
        for(int i=0;i<=60;++i) {
            r.seek(i/60.);const double t=1.2*i/60.;const double cycle=std::min(2.,std::floor(t/.3));
            const double offset=t<.9?8*(cycle+std::min(1.,(t-cycle*.3)/.2)):24-80*(t-.9);
            check_near(r.h.runtime().drawn_bounds(card).x,initial.x+offset,.1);frames.push_back(r.pixels());
            check(r.h.save_png(media()/std::format("steps-{:03}.png",i),r.h.runtime().drawn_bounds(r.node("stage"))).has_value());
        }
        check(frames.front()==frames.back() && frames[15]!=frames[30] && frames[30]!=frames[45]);
        for(int i=60;i>=0;--i){r.seek(i/60.);check(r.pixels()==frames[std::size_t(i)]);}
        r.act("details/copy");check(r.h.runtime().clipboard_text()==site::component_motion_source("steps",true,1));
        r.act("details/effect","set_value",Value{}.set("value","pixel"));r.seek(.5);check(r.pixels()!=frames[30]);
        check(r.h.save_png(media()/"steps-pixel-midpoint.png",r.h.runtime().drawn_bounds(r.node("stage"))).has_value());
        const auto save=r.node("stage/scene/composition/card/actions/save");r.h.click(r.h.runtime().drawn_bounds(save).center());r.step();
        check(parse_json(r.h.runtime().clipboard_text()).has_value());
    });
    suite.test("the public distance route follows both closed lobes and composes local image style",[] {
        Run r;r.details();r.act("details/selection","set_value",Value{}.set("value","route"));
        r.seek(0);const auto card=r.node("stage/scene/composition/card");const auto initial=r.h.runtime().drawn_bounds(card);
        hanim::MotionPath path;path.cubic_to({28,-18},{28,18},{0,0}).cubic_to({-28,-18},{-28,18},{0,0});
        std::vector<std::uint64_t> frames;
        for(int i=0;i<=60;++i) {
            r.seek(i/60.);const auto point=path.point(i/60.);const auto bounds=r.h.runtime().drawn_bounds(card);
            check_near(bounds.x,initial.x+point.x,.1);check_near(bounds.y,initial.y+point.y,.1);frames.push_back(r.pixels());
            check(r.h.save_png(media()/std::format("route-{:03}.png",i),r.h.runtime().drawn_bounds(r.node("stage"))).has_value());
        }
        check(frames.front()==frames.back() && frames[15]!=frames[45]);
        for(int i=60;i>=0;--i){r.seek(i/60.);check(r.pixels()==frames[std::size_t(i)]);}
        r.act("details/copy");check(r.h.runtime().clipboard_text()==site::component_motion_source("route",true,1));
        r.act("details/effect","set_value",Value{}.set("value","pixel"));r.seek(.25);check(r.pixels()!=frames[15]);
        check(r.h.save_png(media()/"route-pixel-quarter.png",r.h.runtime().drawn_bounds(r.node("stage"))).has_value());
        const auto save=r.node("stage/scene/composition/card/actions/save");r.h.click(r.h.runtime().drawn_bounds(save).center());r.step();
        check(parse_json(r.h.runtime().clipboard_text()).has_value());
    });
    suite.test("real input takes over without changing data and the same controls remain agent operable",[] {
        Run r;r.details();r.seek(.5);
        const auto card=r.node("stage/scene/composition/card"),save=r.node("stage/scene/composition/card/actions/save");
        const auto before=r.h.runtime().drawn_bounds(card);
        r.act("stage/scene/composition/card/editor","set_value",Value{}.set("value","An idea worth keeping"));
        r.act("stage/scene/composition/card/pin","toggle");
        r.h.click(r.h.runtime().drawn_bounds(save).center());r.step();
        const auto copied=parse_json(r.h.runtime().clipboard_text());check(copied.has_value());
        if(copied)check(copied->get("title")->as_string()=="An idea worth keeping" && copied->get("pinned")->as_bool());
        r.step(.8);check_near(r.h.runtime().drawn_bounds(card).x,before.x,.1);
        const auto popup=r.node("stage/scene/composition/card/actions/receipt");
        check(r.h.runtime().drawn_bounds(popup).width>0);
        r.act("stage/scene/composition/card/actions/receipt","dismiss");
        r.act("stage/scene/composition/card/actions/save");
        check(r.h.tree().node(r.path("stage/scene/composition/card/actions/saved"))->name.find('2')!=std::string::npos);
        check(r.h.save_png(media()/"real-note-and-receipt.png").has_value());
    });
    suite.test("public image scopes combine with component motion and local style without changing note data",[] {
        Run r;r.details();r.act("stage/scene/composition/card/editor","set_value",Value{}.set("value","Keep the same idea"));
        r.step(1); // Let the real text-edit feedback finish before replaying a different animation.
        r.seek(1);const auto ordinary_end=r.pixels();r.seek(0);const auto ordinary=r.pixels();
        Value::List samples;
        for(const auto effect:{"blur","tone","pixel","opacity","layered"}) {
            r.act("details/effect","set_value",Value{}.set("value",effect));
            std::vector<std::uint64_t> frames;
            for(int i=0;i<=60;++i) {
                r.seek(i/60.);check(r.bound());frames.push_back(r.pixels());
                const auto state=r.h.runtime().presentation(r.node("stage/scene/composition/card"));
                check(state && state->image_effects==(std::string_view(effect)=="layered"?2:1));
                check(r.h.save_png(media()/std::format("effect-{}-{:03}.png",effect,i),r.h.runtime().drawn_bounds(r.node("stage"))).has_value());
                const auto position=r.h.tree().node(r.path("transport/seek"))->value.get("value")->as_double();
                samples.push_back(Value{}.set("effect",effect).set("frame",i).set("requested_progress",i/60.)
                    .set("sampled_progress",position).set("host_time",r.h.runtime().now().count()));
            }
            check(frames.front()==ordinary,std::string(effect)+": the source image is an exact identity, with no extra clipping");
            check(frames.back()!=ordinary_end,std::string(effect)+": the effect changes actual pixels at the same final pose");
            check(frames.front()!=frames[30] && frames[30]!=frames.back());
            for(int i=60;i>=0;--i){r.seek(i/60.);check(r.pixels()==frames[static_cast<std::size_t>(i)],std::format("{} frame {} replays backwards",effect,i));}
            r.act("details/copy");check(r.h.runtime().clipboard_text()==site::component_motion_source("together",true,1,effect));
            check(r.h.runtime().clipboard_text().find("hanim::composite")!=std::string::npos);
        }
        std::ofstream(media()/"effects.report.omui.json")<<Value{}.set("backend","cpu").set("scale",1).set("samples",Value(std::move(samples))).json();
        r.act("details/style","set_value",Value{}.set("value","retro"));r.seek(.5);
        check(r.h.tree().node(r.path("stage/scene/composition/card/editor"))->value.as_string()=="Keep the same idea");
        r.act("stage/scene/composition/card/actions/save");
        const auto note=parse_json(r.h.runtime().clipboard_text());check(note && note->get("title")->as_string()=="Keep the same idea");
        check(r.h.save_png(media()/"effect-style-live-note.png").has_value());
        r.act("details/effect","set_value",Value{}.set("value","none"));r.seek(.5);
        check(r.h.runtime().presentation(r.node("stage/scene/composition/card"))->image_effects==0);
    });
    suite.test("a first pointer press takes over a moving component in the same input frame",[] {
        Run r;r.details();r.seek(.25);r.act("transport/buttons/play");r.step(.1);
        const auto card=r.node("stage/scene/composition/card");
        const auto before=r.h.runtime().drawn_bounds(card);
        const auto point=r.h.runtime().drawn_bounds(r.node("stage/scene/composition/card/actions/save")).center();
        PointerEvent move;move.kind=PointerKind::move;move.position=point;
        PointerEvent down=move;down.kind=PointerKind::down;down.button=MouseButton::left;down.buttons=1;
        PointerEvent up=down;up.kind=PointerKind::up;up.buttons=0;
        check(r.h.step({move,down,up}).failure.empty());
        check_near(r.h.runtime().drawn_bounds(card).x,before.x,.1);
        check(parse_json(r.h.runtime().clipboard_text()).has_value(),"the first click still copies the actual note");
        r.step(.5);check_near(r.h.runtime().drawn_bounds(card).x,before.x,.1);
        check(r.h.tree().node(r.path("transport/hint"))->name.find("real interface")!=std::string::npos);
    });
    suite.test("preferences, hidden pages and teardown release the preview demand while explicit sampling stays available",[] {
        Run r;r.details();r.seek(.25);const auto card=r.node("stage/scene/composition/card");
        const auto held=r.h.runtime().drawn_bounds(card);
        r.h.runtime().set_motion_level(MotionLevel::off);r.act("transport/buttons/play");r.step(.8);
        check_near(r.h.runtime().drawn_bounds(card).x,held.x,.1);
        r.seek(.75);check(r.h.runtime().drawn_bounds(card).x>held.x+8);
        r.step(.8);check(r.h.runtime().frame_demand().idle());
        r.h.runtime().set_route(site::canonical_route("home","en"));r.h.settle(seconds(1));
        check(!r.h.runtime().valid(card));check(r.h.runtime().frame_demand().idle());
    });
    suite.test("the same note and controls fit narrow English and Chinese stages",[] {
        for(const auto lang:{"en","zh"}) {
            Run r(360,lang);r.details();r.seek(.5);
            const auto stage=r.h.runtime().drawn_bounds(r.node("stage"));
            for(const auto tail:{"card","card/title","card/editor","card/pin","card/actions/save"}) {
                const auto bounds=r.h.runtime().drawn_bounds(r.node(std::string("stage/scene/composition/")+tail));
                check(bounds.x>=stage.x-.5 && bounds.right()<=stage.right()+.5,std::string(tail));
            }
            check(r.h.save_png(media()/std::format("phone-{}.png",lang)).has_value());
        }
    });
    return suite.run(argc,argv);
}
