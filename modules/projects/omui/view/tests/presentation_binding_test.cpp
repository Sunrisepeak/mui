import std;
import omui.view;
import xrender.raster;
import mlibs.image;
import mlibs.testing;
import xrender.text;
using namespace omui;
using namespace mlibs::testing;
namespace raster=xrender::raster;

namespace {
template<class T,class E>T take(std::expected<T,E> value) {
    check(value.has_value());if(!value)std::abort();
    if constexpr(!std::is_void_v<T>)return std::move(*value);
}
bool near(Rect a,Rect b,float tolerance=.03f) {
    return std::abs(a.x-b.x)<tolerance && std::abs(a.y-b.y)<tolerance &&
        std::abs(a.width-b.width)<tolerance && std::abs(a.height-b.height)<tolerance;
}
PointerEvent pointer(PointerKind kind,Point at) {
    PointerEvent event;event.kind=kind;event.position=at;event.button=MouseButton::left;
    event.buttons=kind==PointerKind::down?1u:0u;return event;
}
mlibs::text::FontCollection& default_fonts() {
    static mlibs::text::FontCollection value=[]{mlibs::text::FontCollection fonts;(void)fonts.add_defaults();return fonts;}();
    return value;
}
struct Fixture {
    mlibs::text::FontCollection& fonts=default_fonts();
    Runtime rt{[&]{RuntimeOptions o;o.size={360,220};o.fonts=&fonts;o.stages=Stages::paint;
        o.theme=Theme::light();o.theme.motion["motion.normal"]=Transition::tween(seconds(1),hanim::ease::linear);return o;}()};
    xrender::text::GlyphCache glyphs{fonts};
    Signal<std::string> color=signal(std::string("#e11d48"));
    int presses=0;Point local{};
    Fixture() {
        auto control=box({.id="control",.width=70,.height=32,.background="#e11d48",
            .on={{"press",[this](const Value&){++presses;}},{"pointer_down",[this](const Value& value){
                local={static_cast<float>(value.get("x")->as_double()),static_cast<float>(value.get("y")->as_double())};}}}});
        control.prop("background",Source(derive([c=color]{return Value(c.get());})));
        control.prop("motion",Source(Value("morph motion.normal")));
        auto label=text({.id="title",.text="Shared motion"});
        auto card=column({.id="card",.width=150,.height=105,.padding=Edges(10),.gap=8,.background="#f1f5f9",.radius=12,.clip=true},
            {std::move(label),std::move(control)});
        auto content=stack({.id="clip",.width=300,.height=180,.padding=Edges(12),.radius=16,.clip=true},{std::move(card)});
        rt.mount("main",stack({.width=360,.height=220,.background="#ffffff"},{std::move(content)}));frame(0);frame(2);
    }
    NodeId card() const{return rt.find("main/clip/card");}
    NodeId label() const{return rt.find("main/clip/card/title");}
    NodeId control() const{return rt.find("main/clip/card/control");}
    void frame(double time){check(rt.frame({seconds(time),{}}).failure.empty());}
    raster::Surface pixels() {
        check(std::ranges::any_of(rt.display_list().commands(),[](const auto& command){
            const auto* glyphs=std::get_if<xrender::DrawGlyphs>(&command);return glyphs && !glyphs->glyphs.empty();}));
        raster::Surface result(360,220);xrender::CpuRenderer renderer;
        const auto report=renderer.render(rt.display_list(),result,{&rt.images(),&glyphs});check(report.missing_glyphs==0 && report.effects_skipped==0);return result;
    }
    void click(Point point,double time) {
        check(rt.frame({seconds(time),{pointer(PointerKind::move,point),pointer(PointerKind::down,point),pointer(PointerKind::up,point)}}).failure.empty());
    }
};
}
int main(int argc,char** argv) {
    Suite suite{"view.presentation_binding"};
    suite.test("native image chains and selected poses publish together and replay real text through every frame",[] {
        Fixture f;const auto ordinary=f.pixels().hash();
        auto lease=take(f.rt.acquire_presentation(std::array{
            PresentationTarget{f.card(),PresentationChannel::translation},PresentationTarget{f.label(),PresentationChannel::rotation}}));
        take(lease.reserve_effects(0,2,16));
        const auto blur=*xrender::effects().find("blur"),tone=*xrender::effects().find("tone");
        auto blur_values=xrender::effects().params(blur,{}),tone_values=xrender::effects().params(tone,{});
        auto index=[](xrender::EffectRef ref,std::string_view name){const auto& fields=xrender::effects().info(ref)->params;
            return static_cast<std::size_t>(std::ranges::find(fields,name,&xrender::EffectParam::name)-fields.begin());};
        const auto radius=index(blur,"radius"),amount=index(tone,"amount");
        const std::array layers{PresentationImageLayer{blur,xrender::effects().generation(blur),blur_values},
            PresentationImageLayer{tone,xrender::effects().generation(tone),tone_values}};
        const std::array effects{PresentationEffects{layers},PresentationEffects{}};
        std::vector<std::uint64_t> hashes;
        auto sample=[&](int frame) {
            const double t=frame/60.;blur_values[radius]=static_cast<float>(3*t);tone_values[amount]=static_cast<float>(.3*t);
            take(lease.publish(std::array{hanim::Transform2D{.tx=20*t},hanim::Transform2D{.rotate=.08*t}},effects));
            f.frame(3+t);return f.pixels();
        };
        const auto images=scratch_dir("presentation-image-scopes");
        for(int i=0;i<=60;++i) {
            auto pixels=sample(i);hashes.push_back(pixels.hash());
            take(mlibs::image::save_png(images/std::format("frame-{:03}.png",i),
                {reinterpret_cast<const std::uint8_t*>(pixels.pixels().data()),360,220,360*4}));
            const auto state=f.rt.presentation(f.card());check(state && state->applied && !state->error && state->image_effects==2);
        }
        check(hashes.front()==ordinary,"identity image chains do not add another antialiased clip layer");
        check(hashes[0]!=hashes[30] && hashes[30]!=hashes[60]);
        for(int i=60;i>=0;--i)check(sample(i).hash()==hashes[static_cast<std::size_t>(i)],"native image parameters replay the whole process exactly");
        (void)sample(60);const auto accepted=f.pixels().hash();tone_values[amount]=std::numeric_limits<float>::quiet_NaN();
        const auto bad=lease.publish(std::array{hanim::Transform2D{.tx=99},hanim::Transform2D{}},effects);
        check(!bad && bad.error().code==PresentationError::invalid_effect);check(f.rt.presentation(f.card())->pose.tx==20);
        f.frame(5);check(f.pixels().hash()==accepted,"a late invalid effect leaves both prior geometry and every parameter intact");
        tone_values[amount]=.3f;
        f.click(f.rt.drawn_bounds(f.control()).center(),5.1);check(f.presses==1);
        check(f.rt.inspect(f.card()).get("presentation")->get("image_effects")->list()->size()==2);
    });
    suite.test("image ownership is independent of transform writers and releases only its own state",[] {
        Fixture f;
        auto move=take(f.rt.acquire_presentation(std::array{PresentationTarget{f.card(),PresentationChannel::translation,{0,0}}}));
        auto image=take(f.rt.acquire_presentation(std::array{PresentationTarget{f.card(),PresentationChannel::image}}));
        take(move.publish(std::array{hanim::Transform2D{.tx=25}}));
        check(!image.reserve_effects(0,65,4));take(image.reserve_effects(0,1,8));
        const auto conflict=move.reserve_effects(0,1,8);check(!conflict && conflict.error().code==PresentationError::occupied);
        {
            const auto blur=*xrender::effects().find("blur");
            const auto parameters=xrender::effects().params(blur,std::array{std::pair<std::string_view,float>{"radius",2}});
            const std::array layers{PresentationImageLayer{blur,xrender::effects().generation(blur),parameters}};
            take(image.publish(std::array{hanim::Transform2D{}},std::array{PresentationEffects{layers}}));
        }
        f.frame(3);check(f.rt.presentation(f.card())->pose.tx==25 && f.rt.presentation(f.card())->image_effects==1);
        const auto blurred=f.pixels().hash();image.release();f.frame(3.1);
        check(f.rt.presentation(f.card())->pose.tx==25 && f.rt.presentation(f.card())->image_effects==0);
        check(f.pixels().hash()!=blurred);move.release();
    });
    suite.test("a text only image selection captures real glyphs without styling its sibling control",[] {
        Fixture plain,styled;
        auto lease=take(styled.rt.acquire_presentation(std::array{PresentationTarget{styled.label(),PresentationChannel::image}}));
        take(lease.reserve_effects(0,1,1));const auto blur=*xrender::effects().find("blur");
        const std::array<float,1> values{3};const std::array layers{PresentationImageLayer{blur,xrender::effects().generation(blur),values}};
        take(lease.publish(std::array{hanim::Transform2D{}},std::array{PresentationEffects{layers}}));styled.frame(3);plain.frame(3);
        for(float dpi:{1.f,2.f}) {
            raster::Surface a(static_cast<int>(360*dpi),static_cast<int>(220*dpi)),b(a.width(),a.height());xrender::CpuRenderer cpu;
            const auto reference=cpu.render(plain.rt.display_list(),a,{&plain.rt.images(),&plain.glyphs},{.scale=dpi});
            const auto result=cpu.render(styled.rt.display_list(),b,{&styled.rt.images(),&styled.glyphs},{.scale=dpi});
            check(!reference.missing_glyphs && !result.missing_glyphs && !result.effects_skipped && a.hash()!=b.hash());
            const auto box=styled.rt.drawn_bounds(styled.control());bool same=true;
            for(int y=static_cast<int>(std::ceil(box.y*dpi));y<static_cast<int>(box.bottom()*dpi);++y)
                for(int x=static_cast<int>(std::ceil(box.x*dpi));x<static_cast<int>(box.right()*dpi);++x)same=same && a.at(x,y)==b.at(x,y);
            check(same,"the sibling control retains every pixel at either DPI");
            take(mlibs::image::save_png(scratch_dir("presentation-image-scopes")/std::format("title-{}x.png",static_cast<int>(dpi)),
                {reinterpret_cast<const std::uint8_t*>(b.pixels().data()),b.width(),b.height(),b.width()*4}));
        }
        styled.click(styled.rt.drawn_bounds(styled.control()).center(),3.1);check(styled.presses==1);
    });
    suite.test("a whole group moves actual text controls pixels and hit geometry through the same frame",[] {
        Fixture f;const auto card=f.rt.drawn_bounds(f.card()),label=f.rt.drawn_bounds(f.label()),button=f.rt.drawn_bounds(f.control());
        auto lease=take(f.rt.acquire_presentation(std::array{PresentationTarget{f.card(),PresentationChannel::translation}}));
        const auto images=scratch_dir("presentation-binding");
        for(int i=0;i<=60;++i) {
            const float x=static_cast<float>(i);take(lease.publish(std::array{hanim::Transform2D{.tx=x}}));f.frame(3+i/60.);
            check(near(f.rt.drawn_bounds(f.card()),card.offset({x,0})));
            check(near(f.rt.drawn_bounds(f.label()),label.offset({x,0})));
            check(near(f.rt.drawn_bounds(f.control()),button.offset({x,0})));
            check(f.rt.presentation(f.card())->applied);
            auto pixels=f.pixels();
            if(i==0 || i==30 || i==60)take(mlibs::image::save_png(images/std::format("group-{:02}.png",i),
                {reinterpret_cast<const std::uint8_t*>(pixels.pixels().data()),360,220,360*4}));
        }
        f.click(f.rt.drawn_bounds(f.control()).center(),4.2);check(f.presses==1);
        check(std::abs(f.local.x-35)<.03f && std::abs(f.local.y-16)<.03f);
        check(f.rt.inspect(f.card()).get("presentation")->get("applied")->as_bool());
        lease.release();f.frame(4.4);check(near(f.rt.drawn_bounds(f.control()),button));
    });
    suite.test("selected children compose with their parent and siblings keep their own local pose",[] {
        Fixture f;const auto title=f.rt.drawn_bounds(f.label()),control=f.rt.drawn_bounds(f.control());
        auto lease=take(f.rt.acquire_presentation(std::array{
            PresentationTarget{f.card(),PresentationChannel::translation},PresentationTarget{f.label(),PresentationChannel::translation}}));
        take(lease.publish(std::array{hanim::Transform2D{.tx=40},hanim::Transform2D{.tx=15,.ty=3}}));f.frame(3);
        check(near(f.rt.drawn_bounds(f.label()),title.offset({55,3})));
        check(near(f.rt.drawn_bounds(f.control()),control.offset({40,0})));
        f.click(f.rt.drawn_bounds(f.control()).center(),3.1);check(f.presses==1);
    });
    suite.test("disjoint writers share a node and an invalid batch leaves every published field intact",[] {
        Fixture f;
        auto move=take(f.rt.acquire_presentation(std::array{PresentationTarget{f.card(),PresentationChannel::translation}}));
        auto rotate=take(f.rt.acquire_presentation(std::array{PresentationTarget{f.card(),PresentationChannel::rotation}}));
        const auto conflict=f.rt.acquire_presentation(std::array{PresentationTarget{f.card(),PresentationChannel::translation}});
        check(!conflict && conflict.error().code==PresentationError::occupied);
        take(move.publish(std::array{hanim::Transform2D{.tx=30}}));take(rotate.publish(std::array{hanim::Transform2D{.rotate=.1}}));
        check(f.rt.presentation(f.card())->pose.tx==30 && f.rt.presentation(f.card())->pose.rotate==.1);
        rotate.release();check(f.rt.presentation(f.card())->pose.tx==30 && f.rt.presentation(f.card())->pose.rotate==0);
        move.release();
        auto batch=take(f.rt.acquire_presentation(std::array{PresentationTarget{f.card()},PresentationTarget{f.label()}}));
        const auto bad=batch.publish(std::array{hanim::Transform2D{.tx=20},hanim::Transform2D{.tx=std::numeric_limits<double>::infinity()}});
        check(!bad && bad.error().target==1 && f.rt.presentation(f.card())->pose==hanim::Transform2D{});
        check(f.rt.presentation(f.label())->pose==hanim::Transform2D{});
    });
    suite.test("default state feedback continues and projected frames never become automatic morph sources",[] {
        Fixture reference,projected;
        auto lease=take(projected.rt.acquire_presentation(std::array{PresentationTarget{projected.card(),PresentationChannel::translation}}));
        take(lease.reserve_effects(0,1,8));const auto tone=*xrender::effects().find("tone");
        const auto values=xrender::effects().params(tone,std::array{std::pair<std::string_view,float>{"amount",.2f}});
        const std::array layers{PresentationImageLayer{tone,xrender::effects().generation(tone),values}};
        take(lease.publish(std::array{hanim::Transform2D{.tx=40}},std::array{PresentationEffects{layers}}));projected.frame(2.1);reference.frame(2.1);
        projected.color.set("#2563eb");reference.color.set("#2563eb");projected.frame(3);reference.frame(3);
        for(double t:{3.1,3.25,3.5,3.75,4.1}) {
            projected.frame(t);reference.frame(t);
            xrender::DisplayList expected;expected.push_layer(1,xrender::BlendMode::normal,tone,values,{0,0,360,220});
            expected.append(reference.rt.display_list());expected.pop_layer();
            raster::Surface a(360,220);xrender::CpuRenderer cpu;
            check(cpu.render(expected,a,{&reference.rt.images(),&reference.glyphs}).effects_skipped==0);
            const auto b=projected.pixels();const auto box=reference.rt.drawn_bounds(reference.control());
            const int x=static_cast<int>(box.center().x),y=static_cast<int>(box.center().y);
            check(a.row(y)[x]==b.row(y)[x+40]);
            check(std::ranges::count_if(projected.rt.display_list().commands(),[tone](const auto& command){
                const auto* layer=std::get_if<xrender::PushLayer>(&command);return layer && layer->effect.id==tone.id;
            })==1,"the default morph never captures and reapplies the external image effect");
            check(near(projected.rt.drawn_bounds(projected.control()),box.offset({40,0})));
        }
        lease.release();projected.frame(4.2);reference.frame(4.2);
        check(near(projected.rt.drawn_bounds(projected.control()),reference.rt.drawn_bounds(reference.control())));
        check(projected.pixels().hash()==reference.pixels().hash(),"detach restores the live ordinary UI, with no baked image effect");
    });
    suite.test("ancestor clipping remains fixed while local clips and pointer targets follow the selected group",[] {
        Fixture f;auto lease=take(f.rt.acquire_presentation(std::array{PresentationTarget{f.card(),PresentationChannel::translation}}));
        take(lease.publish(std::array{hanim::Transform2D{.tx=230}}));f.frame(3);
        const auto border=f.rt.drawn_bounds(f.control()),clip=f.rt.drawn_bounds(f.rt.find("main/clip"));
        check(border.right()>clip.right());
        check(f.rt.hit_test({clip.right()+1,border.center().y})!=f.control());
        check(f.rt.hit_test({clip.right()-3,border.center().y})==f.control());
    });
    suite.test("node generations and runtime lifetime invalidate retained leases safely",[] {
        PresentationLease lease;
        {Fixture f;lease=take(f.rt.acquire_presentation(std::array{PresentationTarget{f.card()}}));
         f.rt.unmount("main");check(!lease.attached());check(!lease.publish(std::array{hanim::Transform2D{}}));}
        check(!lease.attached());lease.release();
        {Fixture f;lease=take(f.rt.acquire_presentation(std::array{PresentationTarget{f.card()}}));}
        check(!lease.attached());check(!lease.publish(std::array{hanim::Transform2D{}}));lease.release();
    });
    suite.test("IME caret geometry follows the committed presentation without a new text edit",[] {
        Fixture f;const Rect local{4,6,1,18};const auto origin=f.rt.drawn_bounds(f.control()).origin();
        f.rt.text_input(f.control(),local);
        auto lease=take(f.rt.acquire_presentation(std::array{PresentationTarget{f.card(),PresentationChannel::translation}}));
        take(lease.publish(std::array{hanim::Transform2D{.tx=50,.ty=8}}));f.frame(3);
        check(f.rt.text_input_rect() && near(*f.rt.text_input_rect(),local.offset({origin.x+50,origin.y+8})));
        take(lease.publish(std::array{hanim::Transform2D{.tx=70,.ty=10}}));f.frame(3.1);
        check(f.rt.text_input_rect() && near(*f.rt.text_input_rect(),local.offset({origin.x+70,origin.y+10})));
        lease.release();f.frame(3.2);check(near(*f.rt.text_input_rect(),local.offset(origin)));
    });
    suite.test("first attachment during a flattened state morph is refused without interrupting it",[] {
        Fixture f;f.color.set("#2563eb");f.frame(3);f.frame(3.2);
        const auto busy=f.rt.acquire_presentation(std::array{PresentationTarget{f.control()}});
        check(!busy && busy.error().code==PresentationError::busy);
        check(f.rt.presentation(f.control())->channels==PresentationChannel::none);
        f.frame(4.2);
        auto settled=take(f.rt.acquire_presentation(std::array{PresentationTarget{f.control(),PresentationChannel::scale}}));
        const auto before=f.rt.drawn_bounds(f.control());
        take(settled.publish(std::array{hanim::Transform2D{.sx=1.4,.sy=1.2}}));f.frame(4.3);
        const auto after=f.rt.drawn_bounds(f.control());
        check(std::abs(after.width-before.width*1.4f)<.03f && std::abs(after.height-before.height*1.2f)<.03f);
        f.click(after.center(),4.4);check(f.presses==1);
        check(std::abs(f.local.x-35)<.03f && std::abs(f.local.y-16)<.03f);
    });
    return suite.run(argc,argv);
}
