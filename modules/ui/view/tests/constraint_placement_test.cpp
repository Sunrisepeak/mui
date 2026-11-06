// External allocation transactions preserve content motion without sweeping
// controls through their siblings. Ordinary model layout remains seekable.
import std;
import openmui.view;
import openmui.motion;
import openmui.raster;
import openmui.testing;
import xrender.text;

using namespace omui;
using namespace omui::testing;
namespace mo = omui::motion;
namespace {
bool near(Rect a, Rect b, float epsilon = 0.03f) {
    return std::abs(a.x-b.x)<epsilon && std::abs(a.y-b.y)<epsilon && std::abs(a.width-b.width)<epsilon && std::abs(a.height-b.height)<epsilon;
}
bool contains(Rect outer, Rect inner) {
    return inner.x >= outer.x-0.1f && inner.y >= outer.y-0.1f && inner.right() <= outer.right()+0.1f && inner.bottom() <= outer.bottom()+0.1f;
}
bool separated(Rect a, Rect b) { return a.right() <= b.x+0.1f || b.right() <= a.x+0.1f || a.bottom() <= b.y+0.1f || b.bottom() <= a.y+0.1f; }
Theme theme() { auto t = Theme::light(); t.motion["motion.normal"] = Transition::tween(seconds(1), hanim::ease::linear); return t; }
RuntimeOptions options(typeset::FontCollection& fonts, Size size = {700, 500}) {
    RuntimeOptions o; o.fonts = &fonts; o.size = size; o.theme = theme(); return o;
}
void frame(Runtime& rt, double time) { check(rt.frame({seconds(time), {}}).failure.empty()); }
Element moving(Element e, std::string_view policy = "automatic") {
    e.prop("motion", Source(Value{}.set("use", "morph").set("curve", "motion.normal").set("placement", std::string(policy)))); return e;
}
Element offset_box(std::string id, Signal<float> width) {
    auto e = box({.id = std::move(id), .height = 30});
    e.prop("width", Source(derive([width] {return Value(static_cast<double>(width.get()));}))); return e;
}
struct Responsive {
    static constexpr std::string_view name = "constraint-responsive";
    Services* services = nullptr; NodeId node;
    Signal<bool> compact = signal(false);
    Signal<bool> hot = signal(false);
    int activated = 0;
    void mount(Services& s, NodeId n) {services = &s; node = n;}
    void laid_out() { const bool next = services->bounds(node).width < 450; if (next != compact.get()) compact.set(next); }
    void style(const Theme&, layout::Style& s) const {s.direction = layout::Direction::column;}
    std::vector<Element> compose() {
        const bool small = compact.get(); const float width = small ? 280.0f : 640.0f;
        auto field = moving(box({.id="field", .width=width-24, .height=34, .background="#f1f5f9"}), "follow_layout");
        auto caption = moving(text({.id="caption", .text="One semantic object follows its current allocated surface.", .color="#334155", .width=width-24}), "follow_layout");
        auto carrier = moving(column({.id="specimen", .width=width, .padding=Edges(12), .gap=8, .background="#ddd6fe"}, {std::move(field), std::move(caption)}));
        auto action = [this](std::string id) {
            auto e = box({.id=std::move(id), .width=120, .height=34, .background=derive([state=hot] {return Fill(state.get()?"#1d4ed8":"#e11d48");}), .on={{"press", [this](const Value&) {++activated;}}}});
            return moving(std::move(e), "follow_layout");
        };
        std::vector<Element> actions; actions.push_back(action("primary")); actions.push_back(action("replay"));
        Element bar = small ? column({.id="actions", .gap=8}, std::move(actions)) : row({.id="actions", .gap=8}, std::move(actions));
        auto status = moving(text({.id="status", .text=small?"Ready on the narrow surface":"Ready on the wide surface", .width=width}), "follow_layout");
        return {std::move(carrier), text({.id="preview", .text="Simulated preview — real controls"}), std::move(bar), std::move(status)};
    }
};
mo::Resolver resolver() { return {.keys=[](std::uint32_t n, std::string& key, std::string&) {key=std::to_string(n);}}; }
render::DisplayList drawing(bool target, bool resize = true, bool changing_stroke = false) {
    const float width = target && resize ? 180.0f : 100.0f;
    render::DisplayList list; list.begin_node(1);
    list.push_transform(Affine::translate(target?120:15, target?30:10));
    list.node_frame(1, {0,0,width,40},1,{}, {}, Affine::translate(target?20:5,0));
    list.fill_round_rect({0,0,width,40},CornerRadii::all(target?8.0f:4.0f),render::Paint::solid(Color::hex(target?0x1d4ed8:0xe11d48)));
    list.fill_rect({target?50.0f:10.0f,10,10,10},render::Paint::solid(Color::white()));
    const render::Glyph glyph{.id=1,.position={0,0}};
    render::GlyphStyle style; style.transform=Affine::translate(3,0);style.opacity=0.6f;
    list.draw_glyphs({1},16,{target?70.0f:30.0f,24},std::span(&glyph,1),render::Paint::solid(Color::black()),std::span(&style,1));
    list.draw_glyphs({1},16,{target?150.0f:70.0f,24},std::span(&glyph,1),render::Paint::solid(Color::black()));
    const float stroke=changing_stroke?(target?2.0f:4.0f):1.0f;
    list.stroke_round_rect({stroke/2,stroke/2,width-stroke,40-stroke},CornerRadii::all(4),stroke,render::Paint::solid(Color::black()));
    list.pop_transform(); list.end_node(); return list;
}
}
int main(int argc, char** argv) {
    Suite suite{"view.constraint_placement"};
    suite.test("placement is typed policy, never inferred from a recipe name", [] {
        const auto t=theme();
        check(t.motion_library.at("default.action").placement==MotionSpec::PlacementPolicy::follow_layout);
        check(t.motion_library.at("morph").placement==MotionSpec::PlacementPolicy::automatic);
        auto copied=t; copied.motion_library["renamed"]=copied.motion_library.at("default.action");
        const auto renamed=parse_motion(Value("renamed"),copied); check(renamed && renamed->placement==MotionSpec::PlacementPolicy::follow_layout);
        const auto override=parse_motion(Value{}.set("use","default.action").set("placement","animate"),t);
        check(override && override->placement==MotionSpec::PlacementPolicy::animate);
        check(!parse_motion(Value{}.set("use","morph").set("placement","guess"),t));
    });
    suite.test("allocation projection keeps local shape state, paint and unscaled glyphs", [] {
        auto a=drawing(false), b=drawing(true); mo::Options config; config.rebase_layout=true;
        config.curve.kind=mo::Curve::Kind::tween; config.curve.duration=1; config.curve.ease=hanim::ease::linear;
        mo::Morph morph(mo::Matter::extract(a,resolver()),mo::Matter::extract(b,resolver()),config,0);
        const auto& source=morph.from(); check(source.frames().front().rect.width==180);
        check(std::abs(source.frames().front().transform.e-105)<0.01f,"new external origin 100 retains source visual translation 5");
        const auto commands=source.list().commands();
        check(std::get<render::FillRoundRect>(commands[source.pieces()[0].command]).rect.width==180,"full border follows allocation immediately");
        check(std::get<render::FillRect>(commands[source.pieces()[1].command]).rect.x==10,"internal state shape keeps source endpoint");
        check(std::get<render::DrawGlyphs>(commands[source.pieces()[2].command]).size==16,"font size is never width-scaled");
        const auto& label=std::get<render::DrawGlyphs>(commands[source.pieces()[2].command]);
        check(label.origin.x==70 && label.styles.front().transform.e==3 && label.styles.front().opacity==0.6f,
            "centered whole-run alignment adopts current allocation without discarding glyph state");
        check(std::get<render::DrawGlyphs>(commands[source.pieces()[3].command]).origin.x==150,"right aligned run also adopts its current origin");
        check(std::get<render::StrokeRoundRect>(commands[source.pieces()[4].command]).rect.width==179,
            "half-stroke inset border follows the same allocation as its full fill");
        render::DisplayList middle; morph.draw(0.5,middle); check(middle.balanced());
        bool thumb=false, glyph=false;
        for(const auto& c:middle.commands()) {
            if(const auto* f=std::get_if<render::FillRect>(&c); f && f->rect.width==10) thumb=f->rect.x>10 && f->rect.x<50;
            if(const auto* g=std::get_if<render::DrawGlyphs>(&c)) glyph=g->size==16;
        }
        check(thumb && glyph,"state shape still takes its real intermediate trajectory");
        render::DisplayList endpoint; morph.draw(1,endpoint); check(endpoint.balanced());
        check(std::ranges::none_of(endpoint.commands(),[](const render::Command& c){return std::holds_alternative<render::PushClipRect>(c);}),
            "temporary outgoing text crop cannot clip target ink at an exact held endpoint");
    });
    suite.test("allocation and border width can change together without an old inner line or outer overflow", [] {
        auto a=drawing(false,true,true),b=drawing(true,true,true);mo::Options config;config.rebase_layout=true;
        config.curve.kind=mo::Curve::Kind::tween;config.curve.duration=1;config.curve.ease=hanim::ease::linear;
        mo::Morph morph(mo::Matter::extract(a,resolver()),mo::Matter::extract(b,resolver()),config,0);
        for(const double time:{0.0,0.3,0.7,1.0}) {
            render::DisplayList out;morph.draw(time,out);check(out.balanced());bool found=false;
            for(const auto& command:out.commands()) if(const auto* border=std::get_if<render::StrokeRoundRect>(&command)) {
                found=true;check(std::abs(border->rect.x-border->width/2)<0.01f && std::abs(border->rect.right()+border->width/2-180)<0.01f,
                    "source half-width inset and target half-width inset keep the painted outer edge on the current allocation");
            }
            check(found);
        }
    });
    suite.test("fractional interrupted snapshot preserves ownership of a half-stroke inset border", [] {
        auto make=[](float width) {
            render::DisplayList list;list.begin_node(1);list.node_frame(1,{0,0,width,40});
            list.fill_round_rect({0,0,width,40},CornerRadii::all(4),render::Paint::solid(Color::white()));
            list.stroke_round_rect({0.5f,0.5f,width-1,39},CornerRadii::all(4),1,render::Paint::solid(Color::black()));
            list.end_node();return list;
        };
        auto a=make(590.8328857421875f),b=make(443.1992492675781f),next=make(179.3f);
        mo::Options config;config.curve.kind=mo::Curve::Kind::tween;config.curve.duration=1;config.curve.ease=hanim::ease::linear;
        mo::Morph first(mo::Matter::extract(a,resolver()),mo::Matter::extract(b,resolver()),config,0);
        auto snapshot=first.snapshot(0.5336417886554939);
        const auto& old_border=std::get<render::StrokeRoundRect>(snapshot.list().commands()[snapshot.pieces()[1].command]);
        check(old_border.rect.width!=snapshot.frames().front().rect.width-1,
            "fixture really exercises independently rounded frame and half-stroke interpolation");
        config.rebase_layout=true;
        mo::Morph second(std::move(snapshot),mo::Matter::extract(next,resolver()),config,1);
        const auto& from=second.from();const auto& border=std::get<render::StrokeRoundRect>(from.list().commands()[from.pieces()[1].command]);
        check(std::abs(border.rect.right()+border.width/2-179.3f)<0.01f,
            "fractional interrupted inset border still belongs to its owner and follows the new allocation");
    });
    suite.test("a content/alignment change in an unchanged border retains its glyph trajectory", [] {
        auto a=drawing(false,false), b=drawing(true,false);mo::Options config;config.rebase_layout=true;
        mo::Morph morph(mo::Matter::extract(a,resolver()),mo::Matter::extract(b,resolver()),config,0);
        const auto& from=morph.from();const auto& run=std::get<render::DrawGlyphs>(from.list().commands()[from.pieces()[2].command]);
        check(run.origin.x==30 && run.size==16 && run.styles.front().transform.e==3,
            "unchanged allocation cannot snap text alignment/content motion to its target");
    });
    suite.test("default action follows sibling allocation while state colour still morphs", [] {
        typeset::FontCollection fonts; Runtime rt(options(fonts)); auto width=signal(20.0f); auto hot=signal(false);
        auto button=box({.id="button",.width=60,.height=30,.background=derive([hot]{return Fill(hot.get()?"#1d4ed8":"#e11d48");})});
        button.prop("motion",Source(Value("default.action motion.normal")));
        rt.mount("main",row({}, {offset_box("spacer",width),std::move(button)})); frame(rt,0);frame(rt,0.1);
        const auto id=rt.find("main/button"); width.set(200);hot.set(true);frame(rt,1);frame(rt,1.3);
        check(near(rt.drawn_bounds(id),rt.screen_bounds(id)),"external placement is current allocation, without sibling crossing");
        check(!rt.motions().empty(),"state feedback remains active");
    });
    suite.test("resize transaction spans deferred laid_out reflow and contains parent field caption and status", [] {
        typeset::FontCollection fonts;check(fonts.add_defaults().has_value()); Runtime rt(options(fonts));
        Element responsive; responsive.id="responsive";responsive.type=&component_type<Responsive>();responsive.prop("width",Source(Value("100%")));
        rt.mount("main",std::move(responsive));frame(rt,0);frame(rt,0.1);frame(rt,0.2);
        auto* model=rt.component<Responsive>(rt.find("main"));check(model!=nullptr); if(!model)return;
        model->hot.set(true);frame(rt,1);frame(rt,1.2);
        double begin=2;
        for(const Size size:{Size{320,500},Size{700,500}}) {
            rt.resize(size,1);frame(rt,begin);frame(rt,begin+0.016);
            const auto specimen=rt.find("main/specimen"), field=rt.find("main/specimen/field"),caption=rt.find("main/specimen/caption");
            const auto primary=rt.find("main/actions/primary"),replay=rt.find("main/actions/replay"),status=rt.find("main/status"),preview=rt.find("main/preview");
            for(const double sample:{0.016,0.05,0.2,0.6}) {
                frame(rt,begin+sample);
                check(contains(rt.drawn_bounds(specimen),rt.drawn_bounds(field)),"actual field stays inside current parent carrier");
                check(contains(rt.drawn_bounds(specimen),rt.drawn_bounds(caption)),"caption allocation stays inside its current carrier");
                check(separated(rt.drawn_bounds(specimen),rt.drawn_bounds(preview)),"carrier does not sweep through subsequent preview text");
                check(separated(rt.drawn_bounds(primary),rt.drawn_bounds(replay)) && separated(rt.drawn_bounds(primary),rt.drawn_bounds(status)),"current actions and status never cross");
                check(rt.hit_test(rt.drawn_bounds(primary).center())==primary,"presented action center has the same live pointer owner");
            }
            frame(rt,begin+1.5); begin+=2;
        }
    });
    suite.test("same viewport auto-height list-grid model still has real intermediate allocation", [] {
        typeset::FontCollection fonts;Runtime rt(options(fonts));auto grid=signal(false);
        rt.mount("main",column({}, {dynamic([grid] {
            std::vector<Element> items;
            for(int i=0;i<3;++i) items.push_back(box({.id=std::format("item{}",i),.width=50,.height=30,.background="#e11d48"}));
            Element surface=grid.get()?row({.id="surface",.gap=10},std::move(items)):column({.id="surface",.gap=10},std::move(items));
            // A stable stack root owns the explicit model transition; its
            // auto height changes solely because the same model changed.
            return std::vector<Element>{moving(stack({.id="owner"}, {std::move(surface)}))};
        })}));frame(rt,0);frame(rt,0.1);
        const auto initial=rt.find("main/owner"); check(rt.valid(initial),"negative fixture has a real owner below a concrete mounted column");
        const auto before=rt.drawn_bounds(initial);grid.set(true);frame(rt,1);frame(rt,1.4);
        const auto id=rt.find("main/owner"); check(rt.valid(id),"model reorder retains the real explicit owner");
        const auto now=rt.drawn_bounds(id),target=rt.screen_bounds(id);
        std::println("list-grid owner={} before=[{},{},{},{}] now=[{},{},{},{}] target=[{},{},{},{}] motions={}",
            rt.path(id),before.x,before.y,before.width,before.height,now.x,now.y,now.width,now.height,target.x,target.y,target.width,target.height,rt.motions().size());
        for(const auto& motion:rt.motions()) std::println("  motion {} progress={} start={} end={} matched={} only-a={} only-b={}",motion.path,motion.progress,motion.start,motion.end,motion.matched,motion.from_only,motion.to_only);
        check(!near(before,target),"fixture must have genuinely different natural-height allocation endpoints");
        check(!near(now,before)&&!near(now,target),"auto-height change does not masquerade as external constraint change");
    });
    suite.test("explicit animate opts out and explicit subtree constraint leaves sibling motion alone", [] {
        typeset::FontCollection fonts;Runtime rt(options(fonts));auto width=signal(20.0f);
        auto lane=[&](std::string id,std::string policy) {
            return row({.id=std::move(id)}, {offset_box("spacer",width),moving(box({.id="control",.width=60,.height=30,.background="#e11d48"}),policy)});
        };
        rt.mount("main",column({}, {lane("scoped","automatic"),lane("outside","automatic"),lane("optout","animate")}));frame(rt,0);frame(rt,0.1);
        const auto scoped=rt.find("main/scoped/control"),outside=rt.find("main/outside/control"),optout=rt.find("main/optout/control");
        width.set(200);rt.constraints_changed(rt.find("main/scoped"));rt.constraints_changed(rt.find("main/optout"));frame(rt,1);frame(rt,1.4);
        check(near(rt.drawn_bounds(scoped),rt.screen_bounds(scoped)),"named boundary rebase applies to its descendants");
        check(!near(rt.drawn_bounds(outside),rt.screen_bounds(outside)),"unrelated sibling retains explicit model animation");
        check(!near(rt.drawn_bounds(optout),rt.screen_bounds(optout)),"animate is an explicit opt-out even inside transaction");
        frame(rt,3);width.set(80);frame(rt,4);frame(rt,4.4);
        check(!near(rt.drawn_bounds(scoped),rt.screen_bounds(scoped)),"ended transaction cannot leak into future ordinary model changes");
    });
    suite.test("natural width driven by model is not an external allocation transaction", [] {
        typeset::FontCollection fonts;Runtime rt(options(fonts));auto width=signal(50.0f);
        auto child=offset_box("child",width);
        auto surface=moving(row({.id="surface",.align_self="start",.background="#e11d48"},{std::move(child)}));
        rt.mount("main",column({}, {std::move(surface)}));frame(rt,0);frame(rt,0.1);
        const auto id=rt.find("main/surface"); const auto before=rt.drawn_bounds(id);width.set(180);frame(rt,1);frame(rt,1.4);
        const auto middle=rt.drawn_bounds(id),target=rt.screen_bounds(id);
        check(middle.width>before.width && middle.width<target.width,"child-driven natural width keeps its explicit model trajectory");
    });
    suite.test("held local motion rebases allocation without losing its pause or internal endpoint", [] {
        typeset::FontCollection fonts;Runtime rt(options(fonts));auto prefix=signal(20.0f);auto x=signal(0.0f);
        auto control=moving(box({.id="control",.width=60,.height=30,.background="#e11d48"}));
        control.prop("translate_x",Source(derive([x]{return Value(static_cast<double>(x.get()));})));
        rt.mount("main",row({}, {offset_box("prefix",prefix),std::move(control)}));frame(rt,0);frame(rt,0.1);
        const auto id=rt.find("main/control");x.set(40);frame(rt,1);frame(rt,1.4);check(rt.hold_motion(id));rt.pause_motion(id);
        const float own=rt.drawn_bounds(id).x-20;
        prefix.set(200);rt.constraints_changed(id);frame(rt,1.5);
        check(std::abs(rt.drawn_bounds(id).x-(200+own))<0.1f,"paused state is preserved at the new external allocation");
        const auto held=rt.drawn_bounds(id);frame(rt,1.8);check(near(held,rt.drawn_bounds(id)),"constraint retarget does not resume a paused handle");
        rt.release_motion(id);frame(rt,3);
    });
    suite.test("reduced and off use current allocation and remain within their motion policy", [] {
        for(const MotionLevel level:{MotionLevel::reduced,MotionLevel::off}) {
            typeset::FontCollection fonts;Runtime rt(options(fonts));auto width=signal(20.0f);
            rt.set_motion_level(level);rt.mount("main",row({}, {offset_box("prefix",width),moving(box({.id="control",.width=60,.height=30,.background="#e11d48"}))}));
            frame(rt,0);frame(rt,0.1);const auto id=rt.find("main/control");width.set(200);rt.constraints_changed(id);frame(rt,1);frame(rt,1.2);
            check(near(rt.drawn_bounds(id),rt.screen_bounds(id)),"policy never retains old external placement after constraints change");
            check(rt.motions().empty(),"off/reduced do not acquire a long default duration");
        }
    });
    suite.test("external rebase keeps inherited current rounded clip at exact zero and mid frame", [] {
        typeset::FontCollection fonts;Runtime rt(options(fonts,{200,100}));auto prefix=signal(60.0f);
        auto child=box({.id="control",.width=60,.height=30,.background="#e11d48"});child.prop("motion",Source(Value("none")));
        auto row_view=row({.id="lane"},{offset_box("prefix",prefix),std::move(child)});row_view.prop("motion",Source(Value("none")));
        rt.mount("main",moving(stack({.width=100,.height=60,.radius=10,.clip=true},{std::move(row_view)})));
        frame(rt,0);frame(rt,0.1);const auto id=rt.find("main/lane/control");
        prefix.set(20);rt.constraints_changed(rt.find("main"));frame(rt,1);
        xrender::text::GlyphCache glyphs(fonts);
        for(const double time:{1.0,1.3}) {
            frame(rt,time);const Point painted{75,15};
            raster::Surface surface(200,100);render::CpuRenderer renderer;render::CpuOptions cpu;cpu.object_ids=true;
            (void)renderer.render(rt.display_list(),surface,{&rt.images(),&glyphs},cpu);
            const auto owner=renderer.object_ids()[15*200+75];
            check(rt.node_at_index(owner)==id,"CPU body pixel is inside the current inherited clip");
            check(rt.hit_test(painted)==id,"rebasing a child never translates its ancestor clip away from a visible pixel");
            check(rt.hit_test({105,15})!=id,"current rounded carrier still clips both drawing and input");
        }
    });
    return suite.run(argc,argv);
}
