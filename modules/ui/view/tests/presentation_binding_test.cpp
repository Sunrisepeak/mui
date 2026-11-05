import std;
import openmui.view;
import mlibs.raster;
import mlibs.image;
import mlibs.testing;
using namespace omui;
using namespace mlibs::testing;
namespace raster=mlibs::raster;

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
typeset::FontCollection& default_fonts() {
    static typeset::FontCollection value=[]{typeset::FontCollection fonts;(void)fonts.add_defaults();return fonts;}();
    return value;
}
struct Fixture {
    typeset::FontCollection& fonts=default_fonts();
    Runtime rt{[&]{RuntimeOptions o;o.size={360,220};o.fonts=&fonts;o.stages=Stages::paint;
        o.theme=Theme::light();o.theme.motion["motion.normal"]=Transition::tween(seconds(1),hanim::ease::linear);return o;}()};
    typeset::GlyphCache glyphs{fonts};
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
            const auto* glyphs=std::get_if<render::DrawGlyphs>(&command);return glyphs && !glyphs->glyphs.empty();}));
        raster::Surface result(360,220);render::CpuRenderer renderer;
        const auto report=renderer.render(rt.display_list(),result,{&rt.images(),&glyphs});check(report.missing_glyphs==0);return result;
    }
    void click(Point point,double time) {
        check(rt.frame({seconds(time),{pointer(PointerKind::move,point),pointer(PointerKind::down,point),pointer(PointerKind::up,point)}}).failure.empty());
    }
};
}
int main(int argc,char** argv) {
    Suite suite{"view.presentation_binding"};
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
        take(lease.publish(std::array{hanim::Transform2D{.tx=40}}));projected.frame(2.1);reference.frame(2.1);
        projected.color.set("#2563eb");reference.color.set("#2563eb");projected.frame(3);reference.frame(3);
        for(double t:{3.1,3.25,3.5,3.75,4.1}) {
            projected.frame(t);reference.frame(t);
            const auto a=reference.pixels(),b=projected.pixels();const auto box=reference.rt.drawn_bounds(reference.control());
            const int x=static_cast<int>(box.center().x),y=static_cast<int>(box.center().y);
            check(a.row(y)[x]==b.row(y)[x+40]);
            check(near(projected.rt.drawn_bounds(projected.control()),box.offset({40,0})));
        }
        lease.release();projected.frame(4.2);check(near(projected.rt.drawn_bounds(projected.control()),reference.rt.drawn_bounds(reference.control())));
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
