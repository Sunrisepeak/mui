// A layout resize must not make a selected label point at another option.
// Sample production paint and real font coverage; semantic target boxes alone
// cannot detect a lens still travelling through the previous allocation.
import std;
import openmui.ui.base;
import openmui.testing;
import xrender.text;

using namespace omui;
using namespace omui::testing;

namespace {
bool color_is(Color a, Color b) {
    return std::abs(a.r-b.r)<0.001f && std::abs(a.g-b.g)<0.001f && std::abs(a.b-b.b)<0.001f;
}
bool inside(Rect outer, Rect inner, float rounding = 0.75f) {
    return inner.x >= outer.x-rounding && inner.y >= outer.y-rounding &&
        inner.right() <= outer.right()+rounding && inner.bottom() <= outer.bottom()+rounding;
}
void unite(std::optional<Rect>& all, Rect rect) { if(!rect.empty()) all=all?all->united(rect):rect; }
std::string rect_text(Rect r) { return std::format("[{:.3f},{:.3f},{:.3f},{:.3f}]",r.x,r.y,r.width,r.height); }
struct Paint {
    std::vector<Rect> lenses;
    std::vector<Rect> selected_glyphs;
    std::optional<Rect> selected_ink;
};
Paint painted(Runtime& runtime, xrender::text::GlyphCache& cache, NodeId node) {
    Paint result;
    const auto& theme=runtime.theme();
    Affine transform;
    float opacity=1;
    std::optional<Rect> clip;
    std::vector<Affine> transforms;
    std::vector<float> layers;
    std::vector<std::optional<Rect>> clips;
    std::vector<NodeId> owners;
    for(const auto& command:runtime.display_list().commands()) {
        if(const auto* c=std::get_if<render::BeginNode>(&command)) {
            const auto owner=runtime.node_at_index(c->node);
            owners.push_back(runtime.valid(owner)?owner:owners.empty()?NodeId{}:owners.back());
        } else if(std::holds_alternative<render::EndNode>(command)) {
            check(!owners.empty());if(!owners.empty())owners.pop_back();
        } else if(const auto* c=std::get_if<render::PushTransform>(&command)) {
            transforms.push_back(transform);transform=transform*c->transform;
        } else if(std::holds_alternative<render::PopTransform>(command)) {
            check(!transforms.empty());if(transforms.empty())continue;
            transform=transforms.back();transforms.pop_back();
        } else if(const auto* c=std::get_if<render::PushLayer>(&command)) {
            layers.push_back(opacity);opacity*=c->opacity;
        } else if(std::holds_alternative<render::PopLayer>(command)) {
            check(!layers.empty());if(layers.empty())continue;
            opacity=layers.back();layers.pop_back();
        } else if(const auto* c=std::get_if<render::PushClipRect>(&command)) {
            clips.push_back(clip);const auto rect=transform.map(c->rect);clip=clip?clip->intersection(rect):rect;
        } else if(std::holds_alternative<render::PushClipPath>(command)) {
            // The production segmented has no path clip. Refuse to silently
            // replace a future arbitrary shape by an approximate rectangle.
            check(false,"selection sample requires an explicit path-clip oracle if production adds one");clips.push_back(clip);
        } else if(std::holds_alternative<render::PopClip>(command)) {
            check(!clips.empty());if(clips.empty())continue;
            clip=clips.back();clips.pop_back();
        } else if(!owners.empty() && owners.back()==node && opacity>0.05f) {
            if(const auto* c=std::get_if<render::FillRoundRect>(&command);
               c && c->paint.kind==render::Paint::Kind::solid && c->paint.color.a*opacity>0.05f && color_is(c->paint.color,theme.color("surface"))) {
                const auto raw=transform.map(c->rect);
                if(clip) check(inside(*clip,raw),"the actual selection lens itself is not hidden by allocation clipping");
                result.lenses.push_back(raw);
            } else if(const auto* c=std::get_if<render::DrawGlyphs>(&command);
                      c && c->paint.kind==render::Paint::Kind::solid && color_is(c->paint.color,theme.color("text"))) {
                for(std::size_t i=0;i<c->glyphs.size();++i) {
                    const auto style=c->styles.empty()?render::GlyphStyle{}:c->styles[i];
                    if(opacity*style.opacity*c->paint.color.a<=0.05f)continue;
                    const auto& glyph=c->glyphs[i];const auto mask=cache.mask(c->font,glyph.id,c->size,{});
                    check(mask.coverage!=nullptr,"the selected text is measured using an actual font glyph, never a guessed advance box");
                    if(!mask.coverage || mask.coverage->empty())continue;
                    const auto bounds=mask.coverage->bounds;
                    const Rect local{static_cast<float>(bounds.x),static_cast<float>(bounds.y),static_cast<float>(bounds.width),static_cast<float>(bounds.height)};
                    const auto positioned=transform*Affine::translate(c->origin.x+glyph.position.x,c->origin.y+glyph.position.y)*style.transform;
                    const auto raw=positioned.map(local);
                    // Keep pre-clip ink: clipping a missing tail is not proof
                    // that a complete selected word fits its actual lens.
                    if(clip)check(inside(*clip,raw),"every intended selected glyph remains visible before clipping");
                    result.selected_glyphs.push_back(raw);unite(result.selected_ink,raw);
                }
            }
        }
    }
    check(owners.empty() && transforms.empty() && layers.empty() && clips.empty(),"post-motion production display state is balanced");
    return result;
}
struct Fixture {
    typeset::FontCollection fonts;
    Runtime runtime;
    xrender::text::GlyphCache glyphs;
    Signal<float> width=signal(320.0f);
    Signal<std::string> value;
    Fixture(std::string initial)
        :fonts(make_fonts()),runtime(make_options(fonts)),glyphs(fonts),value(signal(std::move(initial))) {
        auto control=segmented({.id="modes",.label="Agent mode",.labels=std::vector<std::string>{"Use","Develop","Design"},
            .options=std::vector<std::string>{"use","develop","design"},.value=value});
        control.prop("width",Source(derive([room=width]{return Value(static_cast<double>(room.get()));})));
        runtime.mount("main",column({.padding=Edges(20)},{std::move(control)}));frame(0);frame(1);
        check(runtime.valid(node()),"the fixture mounts the actual production segmented under a concrete root");
        check(runtime.motion_level()==MotionLevel::full && !runtime.reduce_motion(),"normal framework default motion remains enabled");
        check(runtime.motions().empty(),"the initial selected value is actually settled");
    }
    static typeset::FontCollection make_fonts() {
        typeset::FontCollection f;check(f.add_defaults().has_value(),"actual default TTF is available");return f;
    }
    static RuntimeOptions make_options(typeset::FontCollection& fonts) {
        RuntimeOptions o;o.fonts=&fonts;o.theme=Theme::light();o.size={360,120};return o;
    }
    NodeId node() const {return runtime.find("main/modes");}
    void frame(double time) {check(runtime.frame({seconds(time),{}}).failure.empty());}
    Paint sample() {return painted(runtime,glyphs,node());}
    void choose(std::string_view option) {
        Invocation call;call.actor={"person","user",{}};
        check_eq(runtime.act_item(node(),option,"activate",call).exit_code().value_or(-1),std::int64_t{0});
    }
};
void supported_label(const Paint& sample, std::size_t glyphs, std::string_view context) {
    check(sample.lenses.size() == 1, std::string(context)+": one real surface-coloured lens is painted");
    check(sample.selected_glyphs.size() == glyphs, std::string(context)+": the complete selected label remains painted in selected text colour");
    check(sample.selected_ink.has_value(),std::string(context)+": selected glyph coverage is nonempty");
    if(sample.lenses.size()!=1 || !sample.selected_ink)return;
    const auto lens=sample.lenses.front();
    for(const auto glyph:sample.selected_glyphs)
        check(inside(lens,glyph),std::format("{}: actual lens {} carries selected glyph ink {}",context,rect_text(lens),rect_text(glyph)));
}
}

int main(int argc,char** argv) {
    Suite suite{"ui.base.selection-layout"};register_base_components();
    suite.test("a settled selected Design lens carries its real glyphs throughout an external 320 to 1120 allocation",[] {
        Fixture f("design");const auto start=f.sample();check(start.selected_glyphs.size()>=5,"Design has actual visible letter ink");
        supported_label(start,start.selected_glyphs.size(),"before resize");
        if(start.lenses.size()!=1 || start.selected_glyphs.empty())return;
        f.width.set(1120);f.runtime.resize({1160,120},1);
        for(int frame=0;frame<=60;++frame) {
            f.frame(2+static_cast<double>(frame)/60);const auto sample=f.sample();
            check(f.value.peek() == "design", "external allocation never changes the selected model value");
            supported_label(sample,start.selected_glyphs.size(),std::format("resize frame {}",frame));
            if(sample.lenses.size()==1 && sample.selected_ink && !inside(sample.lenses.front(),*sample.selected_ink))
                std::println("selection resize frame={} lens={} selected_ink={} glyphs={}",frame,rect_text(sample.lenses.front()),rect_text(*sample.selected_ink),sample.selected_glyphs.size());
        }
        check(f.runtime.motions().empty(),"the normal framework motion settles without an independent timer");
        const auto end=f.sample();check(end.lenses.size()==1 && end.lenses.front().x>start.lenses.front().x+400,
            "the test really changed the production allocation and lens position");
    });
    suite.test("ordinary Use to Design selection still moves a real intermediate lens",[] {
        Fixture f("use");const auto start=f.sample();check_eq(start.lenses.size(),std::size_t{1});
        if(start.lenses.size()!=1)return;const auto before=start.lenses.front();
        f.choose("design");bool active=false;std::vector<Rect> sampled;
        for(int frame=0;frame<=60;++frame) {
            f.frame(2+static_cast<double>(frame)/60);const auto sample=f.sample();
            check(sample.lenses.size() == 1, "ordinary selection paints one live lens");
            if(sample.lenses.size()==1)sampled.push_back(sample.lenses.front());
            for(const auto& motion:f.runtime.motions())if(motion.node==f.node() && motion.progress>0 && motion.progress<1)active=true;
        }
        const auto end=f.sample();check_eq(end.lenses.size(),std::size_t{1});if(end.lenses.size()!=1)return;
        const auto after=end.lenses.front();std::size_t middle=0;
        for(const auto rect:sampled) if(rect.center().x>before.center().x+1 && rect.center().x<after.center().x-1)++middle;
        check(after.center().x>before.center().x+100,"Use and Design really have distinct painted lens endpoints");
        check(active && middle>=3,"default selection retains real moving geometry across multiple intermediate frames");
        check_eq(f.value.peek(),std::string("design"));supported_label(end,end.selected_glyphs.size(),"ordinary selection endpoint");
        check(end.selected_glyphs.size()>=5 && f.runtime.motions().empty(),"real selected label and lens finish together");
    });
    suite.test("an explicit parent reorder owns default Button allocation and stagger without losing label ink",[] {
        auto fonts=Fixture::make_fonts();auto options=Fixture::make_options(fonts);options.size={300,240};
        options.theme.motion["motion.normal"]=Transition::tween(seconds(1),hanim::ease::linear);
        Runtime runtime(options);xrender::text::GlyphCache glyphs(fonts);
        auto order=signal(std::vector<std::string>{"alpha","beta","gamma"});
        auto list=column({.id="list",.width=200,.gap=12},{dynamic([order]{
            std::vector<Element> children;
            for(const auto& key:order.get()) {
                auto item=button({.id=key,.text=std::format("Action {}",key),.width=200,.height=36});
                item.key=key;children.push_back(std::move(item));
            }
            return children;
        })});
        list.prop("motion",Source(Value(Value::Map{{"update",Value("none")},{"reorder",Value{}.set("use","morph")
            .set("curve","motion.normal").set("order","document").set("stagger",0.08)}})));
        runtime.mount("main",column({.padding=Edges(20)},{std::move(list)}));
        auto frame=[&](double t){check(runtime.frame({seconds(t),{}}).failure.empty());};frame(0);frame(1);
        check(runtime.motion_level()==MotionLevel::full && !runtime.reduce_motion(),"the actual default Button recipes remain enabled");
        const auto alpha=runtime.find("main/list/alpha");check(runtime.valid(alpha));
        const auto initial=painted(runtime,glyphs,alpha);check_eq(initial.lenses.size(),std::size_t{1});
        check(!initial.selected_glyphs.empty(),"the production Button paints real label glyphs");
        if(initial.lenses.size()!=1 || initial.selected_glyphs.empty())return;
        const auto before=initial.lenses.front();
        order.set({"gamma","alpha","beta"});
        std::vector<Rect> bodies;bool parent_active=false;
        for(int index=0;index<=78;++index) {
            frame(2+static_cast<double>(index)/60);
            check(runtime.find("main/list/alpha")==alpha,"the stable Button instance survives model reordering");
            const auto sample=painted(runtime,glyphs,alpha);
            supported_label(sample,initial.selected_glyphs.size(),std::format("parent reorder frame {}",index));
            if(sample.lenses.size()==1)bodies.push_back(sample.lenses.front());
            for(const auto& motion:runtime.motions())
                if(motion.path=="main/list" && motion.progress>0 && motion.progress<1)parent_active=true;
            if(index==24 && sample.lenses.size()==1)
                check(runtime.hit_test(sample.lenses.front().center())==alpha,"the genuinely moving Button retains the same actual pointer owner");
        }
        frame(4);const auto settled=painted(runtime,glyphs,alpha);check_eq(settled.lenses.size(),std::size_t{1});
        if(settled.lenses.size()!=1 || bodies.size()!=79)return;
        const auto after=settled.lenses.front();
        check(std::abs(after.y-before.y)>40,"the real list layout endpoints are different");
        const auto expected=before.y+(after.y-before.y)*0.32f; // second document item: .40 - .08 delay
        std::println("parent Button reorder before={} at.05={} at.40={} after={} expected_y_at.40={}",
            rect_text(before),rect_text(bodies[3]),rect_text(bodies[24]),rect_text(after),expected);
        check(std::abs(bodies[3].y-before.y)<0.75f,"the parent's document stagger holds Alpha before its own delay");
        check(std::abs(bodies[24].y-expected)<0.75f,"default child feedback cannot replace the explicit parent's one-second allocation curve");
        std::size_t middle=0;
        for(const auto body:bodies)if(body.y>std::min(before.y,after.y)+0.75f && body.y<std::max(before.y,after.y)-0.75f)++middle;
        check(parent_active && middle>=3,"an active parent motion must produce multiple real intermediate Button bodies");
        check(runtime.motions().empty(),"parent and child motion settle without component clocks");
    });
    return suite.run(argc,argv);
}
