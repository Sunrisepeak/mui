import std;
import hanim.program;
import mlibs.testing;
import mlibs.trace;
using namespace hanim;
using namespace mlibs::testing;
namespace {
std::atomic<std::size_t> allocations{0};
void* allocate(std::size_t size) noexcept {allocations.fetch_add(1);return std::malloc(size?size:1);}
void* allocate_aligned(std::size_t size,std::align_val_t alignment) noexcept {
    const auto align=static_cast<std::size_t>(alignment);if(align<alignof(void*) || (align&(align-1)))return nullptr;
    const auto extra=align-1+sizeof(void*);if(size>std::numeric_limits<std::size_t>::max()-extra-1)return nullptr;
    auto* base=std::malloc((size?size:1)+extra);if(!base)return nullptr;
    auto address=reinterpret_cast<std::uintptr_t>(base)+sizeof(void*);
    address=(address+align-1)&~(static_cast<std::uintptr_t>(align)-1);
    auto* pointer=reinterpret_cast<void*>(address);static_cast<void**>(pointer)[-1]=base;allocations.fetch_add(1);return pointer;
}
void release_aligned(void* pointer) noexcept {if(pointer)std::free(static_cast<void**>(pointer)[-1]);}
void* require(void* pointer) noexcept {if(!pointer)std::abort();return pointer;}
}
void* operator new(std::size_t n){return require(allocate(n));}
void* operator new[](std::size_t n){return require(allocate(n));}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{return allocate(n);}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{return allocate(n);}
void* operator new(std::size_t n,std::align_val_t a){return require(allocate_aligned(n,a));}
void* operator new[](std::size_t n,std::align_val_t a){return require(allocate_aligned(n,a));}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{return allocate_aligned(n,a);}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{return allocate_aligned(n,a);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete(void* p,std::align_val_t)noexcept{release_aligned(p);}
void operator delete[](void* p,std::align_val_t)noexcept{release_aligned(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{release_aligned(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{release_aligned(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{release_aligned(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{release_aligned(p);}
namespace {
template<class T,class E>T take(std::expected<T,E> result){
    check(bool(result));if(!result)std::abort();
    if constexpr(!std::is_void_v<T>)return std::move(*result);
}
ObjectDescription shape(double x,bool circle=false) {
    constexpr double k=.5522847498307936;
    const auto edges=circle?std::vector<ShapeSegment>{ShapeSegment::cubic({k,-1},{1,-k},{1,0}),ShapeSegment::cubic({1,k},{k,1},{0,1}),
        ShapeSegment::cubic({-k,1},{-1,k},{-1,0}),ShapeSegment::cubic({-1,-k},{-k,-1},{0,-1})}:
        std::vector<ShapeSegment>{ShapeSegment::line({-1,1}),ShapeSegment::line({1,1})};
    return {{{.id="shape",.local={.tx=x},.contours={take(Contour::copy({{0,-1},edges,true}))},.fill=Rgba{1,0,0,1}}}};
}
struct Fixture {
    ObjectRegistry registry=take(ObjectRegistry::make());
    ObjectRef ar=take(registry.create(shape(0))),br=take(registry.create(shape(10,true))),cr=take(registry.create(shape(30)));
    Snapshot a=take(registry.capture(ar)),b=take(registry.capture(br)),c=take(registry.capture(cr));
};
struct Buffer {
    std::vector<FramePart> parts;std::vector<ShapeCubic> cubics;std::vector<float> effects;
    explicit Buffer(const ProgramTrack& track):parts(track.requirements.parts+1),cubics(track.requirements.cubics+1),effects(track.max_effect_parameters+1,-123) {
        parts.back().local.tx=-123;cubics.back().end.x=-123;
    }
    ProgramSample sample(const PreparedProgram& plan,std::size_t track,double time,bool exact=false) {
        return take(plan.sample(track,seconds(time),{parts,cubics},effects,exact));
    }
};
bool all_same(const FrameView& a,const FrameView& b) {
    if(a.parts.size()!=b.parts.size() || a.cubics.size()!=b.cubics.size())return false;
    for(std::size_t i=0;i<a.parts.size();++i)if(a.parts[i].local!=b.parts[i].local || a.parts[i].fill!=b.parts[i].fill)return false;
    for(std::size_t i=0;i<a.cubics.size();++i) {
        const auto& x=a.cubics[i];const auto& y=b.cubics[i];
        if(x.start!=y.start || x.control1!=y.control1 || x.control2!=y.control2 || x.end!=y.end)return false;
    }return true;
}
}
int main(int argc,char** argv) {
    Suite suite{"hanim.program"};
    suite.test("composite preserves author hierarchy timing and changing segment membership",[] {
        Fixture f;
        const auto scoped=composite(parallel(anim::morph(f.a,f.c).duration(seconds(4)),
            composite(anim::morph(f.b,f.c).duration(seconds(2))).with_effect(fx::blur(0,8))))
            .with_effect(fx::blur(2,10));
        const auto plan=take(PreparedProgram::prepare(sequence(anim::morph(f.a,f.b).duration(seconds(2)),scoped)));
        check(plan.duration()==seconds(6) && plan.nodes().size()==7 && plan.scopes().size()==2);
        check(plan.nodes()[0].subtree_end==7 && plan.nodes()[2].parent==0 && plan.nodes()[2].subtree_end==7);
        check(plan.scopes()[0].start==seconds(2) && plan.scopes()[0].duration==seconds(4));
        check(plan.scopes()[1].parent==0 && plan.scopes()[1].depth==2 && plan.scopes()[1].parameter_offset==1);
        check(plan.nodes()[4].track==0 && plan.nodes()[4].segment==1 && plan.nodes()[4].scope==0);
        std::array<float,3> values{-1,-1,123};
        take(plan.sample_scopes(seconds(1),values));check(values[0]==2 && values[1]==0 && values[2]==123);
        take(plan.sample_scopes(seconds(3),values));check(values[0]==4 && values[1]==4);
        take(plan.sample_scopes(seconds(100),values));check(values[0]==10 && values[1]==8);
        take(plan.sample_scopes(seconds(100),values,true));check(values[0]==2 && values[1]==0);
        Buffer buffer(plan.tracks()[0]);
        check(plan.segments()[buffer.sample(plan,0,1.99).segment_index].scope==invalid_program_index);
        check(plan.segments()[buffer.sample(plan,0,2).segment_index].scope==0);
        check(buffer.sample(plan,0,2).geometry.parts[0].local.tx==10,"source chaining survives scope boundaries");
    });
    suite.test("scope easing affects only effects with explicit restore and instant semantics",[] {
        Fixture f;
        const auto recipe=composite(anim::morph(f.a,f.b)).with_effect(fx::blur(0,8))
            .effect_easing(Ease::stepped(4,StepPosition::jump_start));
        const auto plan=take(PreparedProgram::prepare(recipe));Buffer buffer(plan.tracks()[0]);std::array<float,1> values;
        take(plan.sample_scopes(seconds(0),values));check(values[0]==2);
        check(buffer.sample(plan,0,0).geometry.parts[0].local.tx==0,"scope easing never warps the child clock");
        take(plan.sample_scopes(seconds(0),values,true));check(values[0]==0);
        const auto instant=take(PreparedProgram::prepare(composite(anim::morph(f.a,f.b).duration(seconds(0))).with_effect(fx::blur(2,7))));
        take(instant.sample_scopes(seconds(-1),values));check(values[0]==2);
        take(instant.sample_scopes(seconds(0),values));check(values[0]==7);
    });
    suite.test("scope budgets and invalid output preserve diagnostics and caller storage",[] {
        Fixture f;const auto recipe=composite(anim::morph(f.a,f.b).with_effect(fx::blur(0,2))).with_effect(fx::blur(1,4));
        const auto absent=PreparedProgram::prepare(recipe,{.max_scopes=0});
        check(!absent && absent.error().code==ProgramError::scope_budget && absent.error().where.node==0 && absent.error().where.scope==0);
        check(PreparedProgram::prepare(recipe,{.max_total_effect_parameters=1}).error().code==ProgramError::effect_parameter_budget);
        const auto plan=take(PreparedProgram::prepare(recipe));std::array<float,1> values{123};
        check(plan.sample_scopes(seconds(.5),{}).error().code==ProgramError::output_too_small);
        check(plan.sample_scopes(seconds(std::numeric_limits<double>::infinity()),values).error().code==ProgramError::invalid_time);
        check(values[0]==123);
        auto bad=ease::linear;bad.kind=static_cast<EaseKind>(255);
        check(PreparedProgram::prepare(composite(anim::morph(f.a,f.b)).effect_easing(bad)).error().code==ProgramError::invalid_ease);
    });
    suite.test("nested scope sampling is allocation free and leaves output tails untouched",[] {
        Fixture f;const auto plan=take(PreparedProgram::prepare(composite(composite(anim::morph(f.a,f.b))
            .with_effect(fx::blur(2,8))).with_effect(fx::tone({}, {.r=.2f,.amount=1}))));
        std::vector<float> values(plan.scope_parameter_count()+1,123);
        mlibs::trace::start();take(plan.sample_scopes(seconds(.5),values));const auto before=allocations.load();bool good=true;
        for(int i=0;i<1000;++i)good &= plan.sample_scopes(seconds(static_cast<double>(i%97)/96),values).has_value();
        const auto after=allocations.load();mlibs::trace::stop();check(good && before==after && values.back()==123);
    });
    suite.test("typed path targets compose with image effects and retain a single preparation path",[] {
        Fixture f;
        const auto base=anim::morph(f.a,f.b);
        const auto recipe=base.with_effect(fx::vertex_path(PointPath::bend(.35)))
                              .with_effect(fx::translation_path(PointPath::bend(.25)))
                              .with_effect(fx::blur(0,8));
        check(base.binding().morph.shape.point_path.linear() && base.binding().morph.translation_path.linear());
        const auto braced=base.with_effect({});
        check(braced.binding().effects.size()==1,"legacy braced image effect stays unambiguous");
        check(recipe.binding().effects.size()==1,"process effects are not silently passed to the image backend");
        const auto plan=take(PreparedProgram::prepare(recipe)), plain=take(PreparedProgram::prepare(base));
        Buffer output(plan.tracks()[0]), reference(plain.tracks()[0]);
        auto middle=output.sample(plan,0,.5);
        const auto original=reference.sample(plain,0,.5);
        check(middle.geometry.parts[0].local.tx==5 && middle.geometry.parts[0].local.ty==2.5);
        check(middle.geometry.parts[0].fill==original.geometry.parts[0].fill);
        check(!std::ranges::equal(middle.geometry.cubics,original.geometry.cubics,[](const auto& a,const auto& b){
            return a.start==b.start && a.control1==b.control1 && a.control2==b.control2 && a.end==b.end;
        }));
        check(middle.effect_parameters==1 && output.effects[0]==4);
        for(double time:{0.,1.})check(all_same(output.sample(plan,0,time).geometry,reference.sample(plain,0,time).geometry));
        const auto combined=take(PreparedProgram::prepare(base.with_effect(fx::translation_path(PointPath::bend(.125)))
                                                             .with_effect(fx::translation_path(PointPath::bend(.125)))));
        Buffer sum(combined.tracks()[0]);
        check(sum.sample(combined,0,.5).geometry.parts[0].local.ty==2.5);
        const auto bad=PreparedProgram::prepare(base.with_effect(fx::vertex_path({{std::numeric_limits<double>::quiet_NaN(),0},{}})));
        check(!bad && bad.error().code==ProgramError::animation && bad.error().where.node==0 &&
              bad.error().caused_by(MorphError::invalid_options));
    });
    suite.test("standalone live inputs capture once and retain geometry after registry changes",[] {
        Fixture f;
        const auto recipe=anim::morph(f.ar,f.br).duration(seconds(1));
        take(f.registry.update(f.ar,shape(4)));
        const auto plan=take(PreparedProgram::prepare(recipe,f.registry));
        check(plan.tracks()[0].source_revision==2);
        Buffer output(plan.tracks()[0]);
        check(output.sample(plan,0,.5).geometry.parts[0].local.tx==7);
        take(f.registry.update(f.ar,shape(8)));
        take(f.registry.destroy(f.ar));take(f.registry.destroy(f.br));
        check(output.sample(plan,0,.5).geometry.parts[0].local.tx==7);
        const auto stale=PreparedProgram::prepare(recipe,f.registry);
        check(!stale && stale.error().code==ProgramError::object && stale.error().caused_by(ObjectError::stale_object));
        const auto missing=PreparedProgram::prepare(recipe);
        check(!missing && missing.error().code==ProgramError::unresolved_object);
        check(PreparedProgram::prepare(bind(f.a,f.b),{}).has_value(),"legacy braced default limits stay unambiguous");
    });
    suite.test("immutable nested sequence parallel and stagger compile to owned bounded object schedules",[] {
        Fixture f;const auto first=bind(f.a,f.b);const auto copy=first;
        const auto input=sequence({parallel({first,stagger(seconds(.5),{bind(f.b,f.c),bind(f.c,f.a)})}),bind(f.a,f.c)});
        check(first.kind()==ProgramKind::binding && copy.binding()==first.binding());
        check(input.kind()==ProgramKind::sequence && input.children().size()==2);
        const auto plan=take(PreparedProgram::prepare(input));
        check(plan.duration()==seconds(2.5) && plan.tracks().size()==3 && plan.segments().size()==4);
        check(plan.tracks()[0].object==f.ar && plan.tracks()[0].segment_count==2);
        check(plan.segments()[0].path=="0/0/0" && plan.segments()[1].path=="0/1");
        check(plan.segments()[1].start==seconds(1.5));
        Buffer a(plan.tracks()[0]),b(plan.tracks()[1]),c(plan.tracks()[2]);
        check(a.sample(plan,0,2).geometry.parts[0].local.tx==20);
        check(b.sample(plan,1,.5).geometry.parts[0].local.tx==20);
        check(c.sample(plan,2,.25).geometry.parts[0].local.tx==30,"staggered track holds exact source before it begins");
        check(c.sample(plan,2,1).geometry.parts[0].local.tx==15);
    });
    suite.test("A to B to C keeps A identity and seeks independently of previous evaluation",[] {
        Fixture f;const auto plan=take(PreparedProgram::prepare(sequence({bind(f.a,f.b),bind(f.a,f.c)})));
        Buffer output(plan.tracks()[0]);
        for(const auto [time,position]:std::array<std::pair<double,double>,7>{{{1.5,20},{.25,2.5},{2,30},{0,0},{1,10},{9,30},{-2,0}}}) {
            const auto frame=output.sample(plan,0,time);check(frame.geometry.parts[0].local.tx==position);
            check(frame.geometry.source_object==f.ar);
        }
        check(plan.segments()[1].animation.from().parts().empty(),"chained boundary is deeply captured without an old snapshot chain");
        check(output.parts.back().local.tx==-123 && output.cubics.back().end.x==-123 && output.effects.back()==-123);
        const auto b_plan=take(PreparedProgram::prepare(sequence({bind(f.a,f.b),bind(f.b,f.c)})));
        check(b_plan.tracks().size()==2 && b_plan.tracks()[1].object==f.br);
    });
    suite.test("snapshot mode deliberately resets source while ordinary chained starts stay anchored",[] {
        Fixture f;const SegmentOptions steps{seconds(1),Ease::stepped(4,StepPosition::jump_start)};
        const auto chained=take(PreparedProgram::prepare(sequence({bind(f.a,f.b),bind(f.a,f.c,{},steps)})));
        Buffer a(chained.tracks()[0]);check(a.sample(chained,0,1).geometry.parts[0].local.tx==10);
        check(a.sample(chained,0,1.01).geometry.parts[0].local.tx>10);
        check(a.sample(chained,0,1).geometry.parts[0].local.tx==10);
        const auto fixed=take(PreparedProgram::prepare(sequence({bind(f.a,f.b),bind(f.a,f.c,{},steps,{},ProgramSource::snapshot)})));
        Buffer b(fixed.tracks()[0]);check(b.sample(fixed,0,1).geometry.parts[0].local.tx==7.5);
        check(b.sample(fixed,0,1.8,true).geometry.parts[0].local.tx==0,"exact restore selects the first raw source");
    });
    suite.test("same-object overlaps and parallel instantaneous writes refuse with structured locations",[] {
        Fixture f;const auto x=bind(f.a,f.b),y=bind(f.a,f.c);
        const auto overlap=PreparedProgram::prepare(parallel({x,y}));
        check(!overlap && overlap.error().code==ProgramError::binding_conflict && overlap.error().where.node==2 && overlap.error().where.track==0 && overlap.error().where.segment==1);
        const auto delayed=PreparedProgram::prepare(stagger(seconds(.5),{x,y}));check(!delayed && delayed.error().code==ProgramError::binding_conflict);
        const auto instant=bind(f.a,f.b,{}, {.duration=seconds(0)});
        const auto conflicting=PreparedProgram::prepare(parallel({instant,instant}));check(!conflicting && conflicting.error().code==ProgramError::binding_conflict);
        const auto serial=take(PreparedProgram::prepare(sequence({instant,bind(f.a,f.c,{}, {.duration=seconds(0)})})));
        Buffer output(serial.tracks()[0]);check(serial.duration()==seconds(0) && output.sample(serial,0,0).geometry.parts[0].local.tx==30);
        const auto followed=take(PreparedProgram::prepare(sequence({instant,bind(f.a,f.c)})));
        Buffer next(followed.tracks()[0]);check(next.sample(followed,0,0).geometry.parts[0].local.tx==10 && next.sample(followed,0,.5).geometry.parts[0].local.tx==20);
    });
    suite.test("gap fill holds old output and effect sampling uses only active segment ranges",[] {
        Fixture f;const auto wait=bind(f.b,f.c,{}, {.duration=seconds(2)});
        const auto input=parallel({bind(f.a,f.b,{fx::blur(0,8)}),sequence({wait,bind(f.a,f.c,{fx::blur(2,6),fx::tone({}, {})})})});
        const auto plan=take(PreparedProgram::prepare(input));Buffer out(plan.tracks()[0]);
        const auto gap=out.sample(plan,0,1.5);check(gap.segment_index==0 && gap.geometry.parts[0].local.tx==10 && out.effects[0]==8);
        const auto next=out.sample(plan,0,2.5);check(next.effect_parameters==5 && out.effects[0]==4 && next.geometry.parts[0].local.tx==20);
        const auto first=out.sample(plan,0,.5);check(first.effect_parameters==1 && out.effects[0]==4);
        check(out.effects.back()==-123,"spare output remains untouched");
    });
    suite.test("frozen snapshots and plans survive producers expression and earlier preparations",[] {
        PreparedProgram plan;ObjectRef source;
        {
            Fixture f;source=f.ar;auto expression=sequence({bind(f.a,f.b),bind(f.a,f.c)});
            plan=take(PreparedProgram::prepare(expression));
            take(f.registry.destroy(f.ar));take(f.registry.destroy(f.br));take(f.registry.destroy(f.cr));
        }
        const auto copy=plan;plan={};Buffer output(copy.tracks()[0]);
        check(output.sample(copy,0,1.5).geometry.parts[0].local.tx==20 && copy.tracks()[0].object==source);
    });
    suite.test("version mismatch needs explicit frozen-source mode",[] {
        Fixture f;take(f.registry.update(f.ar,shape(100)));const auto newer=take(f.registry.capture(f.ar));
        const auto bad=PreparedProgram::prepare(sequence({bind(f.a,f.b),bind(newer,f.c)}));
        check(!bad && bad.error().code==ProgramError::source_version_mismatch && bad.error().where.node==2);
        const auto good=take(PreparedProgram::prepare(sequence({bind(f.a,f.b),bind(newer,f.c,{}, {},{},ProgramSource::snapshot)})));
        Buffer output(good.tracks()[0]);check(output.sample(good,0,1).geometry.parts[0].local.tx==100);
        const auto frozen=take(PreparedProgram::prepare(bind(f.a,f.b)));Buffer old(frozen.tracks()[0]);
        check(old.sample(frozen,0,0).geometry.parts[0].local.tx==0,"source mutation does not rewrite the frozen plan");
    });
    suite.test("expanded graph and retained preparation limits are checked separately",[] {
        Fixture f;const auto two=sequence({bind(f.a,f.b),bind(f.a,f.c,{fx::blur(0,1)})});
        auto limits=ProgramLimits{};limits.max_nodes=2;
        auto result=PreparedProgram::prepare(two,limits);check(!result && result.error().code==ProgramError::node_budget);
        limits={};limits.max_depth=1;result=PreparedProgram::prepare(two,limits);check(!result && result.error().code==ProgramError::depth_budget);
        limits={};limits.max_segments=1;result=PreparedProgram::prepare(two,limits);check(!result && result.error().code==ProgramError::segment_budget);
        limits={};limits.max_tracks=1;result=PreparedProgram::prepare(parallel({bind(f.a,f.b),bind(f.b,f.c)}),limits);check(!result && result.error().code==ProgramError::track_budget);
        limits={};limits.max_total_parts=1;result=PreparedProgram::prepare(two,limits);check(!result && result.error().code==ProgramError::part_budget);
        limits={};limits.max_total_cubics=0;result=PreparedProgram::prepare(two,limits);check(!result && result.error().code==ProgramError::cubic_budget);
        limits={};limits.max_total_effect_parameters=0;result=PreparedProgram::prepare(two,limits);check(!result && result.error().code==ProgramError::effect_parameter_budget);
        limits={};limits.max_nodes=0;result=PreparedProgram::prepare(two,limits);check(!result && result.error().code==ProgramError::invalid_limits);
    });
    suite.test("invalid expressions times effects and easing cannot become prepared values",[] {
        Fixture f;const auto nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
        check(PreparedProgram::prepare(Program{}).error().code==ProgramError::empty_program);
        check(PreparedProgram::prepare(sequence({})).error().code==ProgramError::empty_program);
        check(PreparedProgram::prepare(bind({},f.b)).error().code==ProgramError::invalid_snapshot);
        for(double value:{-1.0,nan,inf})check(PreparedProgram::prepare(bind(f.a,f.b,{}, {.duration=seconds(value)})).error().code==ProgramError::invalid_time);
        check(PreparedProgram::prepare(stagger(seconds(-1),{bind(f.a,f.b)})).error().code==ProgramError::invalid_time);
        auto invalid=Ease::stepped(2);invalid.p[0]=1.5;
        check(PreparedProgram::prepare(bind(f.a,f.b,{}, {.easing=invalid})).error().code==ProgramError::invalid_ease);
        invalid={static_cast<EaseKind>(255)};check(PreparedProgram::prepare(bind(f.a,f.b,{}, {.easing=invalid})).error().code==ProgramError::invalid_ease);
        const auto overflow=sequence({bind(f.a,f.b,{}, {.duration=seconds(std::numeric_limits<double>::max())}),bind(f.b,f.c,{}, {.duration=seconds(std::numeric_limits<double>::max())})});
        check(PreparedProgram::prepare(overflow).error().code==ProgramError::invalid_time);
        const auto bad_effect=PreparedProgram::prepare(bind(f.a,f.b,{{"blur",{{"radius",0,std::numeric_limits<float>::infinity()}}}}));
        check(!bad_effect && bad_effect.error().code==ProgramError::effect && bad_effect.error().caused_by(EffectError::nonfinite_value));
    });
    suite.test("sampling errors preserve both geometry and effect buffers",[] {
        Fixture f;const auto plan=take(PreparedProgram::prepare(bind(f.a,f.b,{fx::blur(0,8)})));Buffer output(plan.tracks()[0]);
        output.sample(plan,0,.25);const auto local=output.parts[0].local;const auto cubic=output.cubics[0];const auto effect=output.effects[0];
        check(!plan.sample(0,seconds(.5),{output.parts,output.cubics},{}));
        check(!plan.sample(0,seconds(.5),{std::span(output.parts).first(0),output.cubics},output.effects));
        check(!plan.sample(0,seconds(std::numeric_limits<double>::quiet_NaN()),{output.parts,output.cubics},output.effects));
        check(!plan.sample(1,seconds(.5),{output.parts,output.cubics},output.effects));
        check(output.parts[0].local==local && output.cubics[0].control1==cubic.control1 && output.effects[0]==effect);
        const auto a=take(f.registry.create(shape(1e308))),b=take(f.registry.create(shape(-1e308)));
        const auto huge=take(PreparedProgram::prepare(bind(take(f.registry.capture(a)),take(f.registry.capture(b)),{fx::blur(0,8)})));
        Buffer unsafe(huge.tracks()[0]);unsafe.sample(huge,0,0);const auto original=unsafe.parts[0].local;
        const auto bad=huge.sample(0,seconds(.5),{unsafe.parts,unsafe.cubics},unsafe.effects);
        check(!bad && bad.error().code==ProgramError::animation && unsafe.parts[0].local==original && unsafe.effects[0]==0);
    });
    suite.test("single normalization retains prepared retargets and source restoration semantics",[] {
        Fixture f;const auto morph=take(PreparedObjectMorph::prepare(f.a,f.b));const auto effects=take(PreparedEffects::prepare({}));
        const auto first=take(PreparedProgram::single(morph,effects));Buffer output(first.tracks()[0]);const auto current=output.sample(first,0,.5);
        const auto retarget=take(PreparedObjectMorph::retarget(current.geometry,f.c));
        const auto plan=take(PreparedProgram::single(retarget,effects,{seconds(1),Ease::stepped(4,StepPosition::jump_start)},true));
        Buffer next(plan.tracks()[0]);check(next.sample(plan,0,0).geometry.parts[0].local.tx==5);
        next.sample(plan,0,.5);check(next.sample(plan,0,0).geometry.parts[0].local.tx==5);
        check(next.sample(plan,0,.5,true).geometry.parts[0].local.tx==5);
        check(PreparedProgram::single({},effects).error().code==ProgramError::unprepared);
        check(PreparedProgram::single(morph,effects,{.duration=seconds(-1)}).error().code==ProgramError::invalid_time);
    });
    suite.test("prepared arbitrary-time sampling adds no allocations with tracing enabled",[] {
        Fixture f;const auto plan=take(PreparedProgram::prepare(sequence({bind(f.a,f.b,{fx::blur(0,8)}),bind(f.a,f.c,{fx::blur(8,2)})})));
        Buffer output(plan.tracks()[0]);
        mlibs::trace::start();for(int i=0;i<32;++i)output.sample(plan,0,.5);
        const auto before=allocations.load();bool good=true;
        for(int i=0;i<1000;++i) {
            const auto value=plan.sample(0,seconds(static_cast<double>(i%197)/98),{output.parts,output.cubics},output.effects);
            good=good && value.has_value() && value->geometry.source_object==f.ar;
        }
        const auto after=allocations.load();mlibs::trace::stop();check(good && before==after);
    });
    if constexpr(mlibs::testing::threads)suite.test("immutable plan sampling is concurrent with separate caller storage",[] {
        Fixture f;const auto plan=take(PreparedProgram::prepare(sequence({bind(f.a,f.b),bind(f.a,f.c)})));
        Buffer a(plan.tracks()[0]),b(plan.tracks()[0]);std::atomic<bool> good{true};
        auto work=[&](Buffer& buffer,double time,double expected) {for(int i=0;i<1000;++i) {
            const auto sample=plan.sample(0,seconds(time),{buffer.parts,buffer.cubics},buffer.effects);
            if(!sample || sample->geometry.parts[0].local.tx!=expected)good=false;
        }};
        std::thread x(work,std::ref(a),.5,5),y(work,std::ref(b),1.5,20);x.join();y.join();check(good.load());
    });
    suite.test("compiled simple phase time is bit-identical to the shared core mapper",[] {
        Fixture f;
        for(double duration:{0.,1e-12,.3,1.,1e300}) {
            const auto plan=take(PreparedProgram::prepare(bind(f.a,f.b,{}, {.duration=seconds(duration)})));
            Buffer output(plan.tracks()[0]);
            for(double time:{-std::numeric_limits<double>::max(),-0.,0.,1e-13,.15,.3,.5,1.,1e299,std::numeric_limits<double>::max()}) {
                const auto actual=output.sample(plan,0,time);
                const auto mapped=map_time(TimeMap{.fill=Fill::both},seconds(0),seconds(duration),seconds(time));
                const auto expected=time<0?0:duration==0?1:mapped->count()/duration;
                check(std::bit_cast<std::uint64_t>(actual.linear_progress)==std::bit_cast<std::uint64_t>(expected));
            }
        }
    });
    suite.test("property sequences preserve the actual previous endpoint and explicit snapshot resets",[] {
        Fixture f;
        const auto plan=take(PreparedProgram::prepare(sequence(
            anim::move_to(f.a,{12,8}),anim::move_by(f.a,{6,-4}),anim::rotate_to(f.a,std::numbers::pi))));
        Buffer output(plan.tracks()[0]);
        const auto first=output.sample(plan,0,.5).geometry.parts[0].local;
        check(first.tx==6 && first.ty==4 && first.rotate==0);
        const auto second=output.sample(plan,0,1.5).geometry.parts[0].local;
        check(second.tx==15 && second.ty==6);
        const auto third=output.sample(plan,0,2.5).geometry.parts[0].local;
        check(third.tx==18 && third.ty==4 && third.rotate==std::numbers::pi/2 && third.sx==1 && third.sy==1);
        const auto reset=take(PreparedProgram::prepare(sequence(anim::move_to(f.a,{12,8}),
            anim::move_by(f.a,{6,4}).source(ProgramSource::snapshot))));
        Buffer reset_output(reset.tracks()[0]);
        check(reset_output.sample(reset,0,1).geometry.parts[0].local.tx==0);
        check(reset_output.sample(reset,0,2).geometry.parts[0].local.tx==6);
    });
    suite.test("relative rotation and scale compose actual channel endpoints with stateless zero allocation sampling",[] {
        Fixture f;auto description=shape(4);description.parts[0].local.rotate=.5;
        description.parts[0].local.sx=2;description.parts[0].local.sy=-3;
        const auto source=take(f.registry.capture(take(f.registry.create(description))));
        const auto plan=take(PreparedProgram::prepare(parallel(
            sequence(anim::rotate_by(source,1),anim::rotate_by(source,-.5)),
            sequence(anim::scale_by(source,{2,.5}),anim::scale_by(source,{.5,2})))));
        std::size_t channels=0;for(const auto& segment:plan.segments())channels+=segment.animation.channels().size();
        check(channels==4);
        Buffer output(plan.tracks()[0]);bool good=true;
        const auto before=allocations.load();
        for(int direction:{1,-1})for(int k=0;k<=120;++k) {
            const double t=(direction==1?k:120-k)/60.;
            const auto value=output.sample(plan,0,t).geometry.parts[0].local;
            const double rotate=t<=1?.5+t:1.5-.5*(t-1);
            const double sx=t<=1?2+2*t:4-2*(t-1),sy=t<=1?-3+1.5*t:-1.5-1.5*(t-1);
            good=good && std::abs(value.rotate-rotate)<1e-12 && std::abs(value.sx-sx)<1e-12 &&
                std::abs(value.sy-sy)<1e-12 && value.tx==4;
        }
        const auto after=allocations.load();check(good && before==after);
        const auto reset=take(PreparedProgram::prepare(sequence(anim::rotate_by(source,1),
            anim::rotate_by(source,-.5).source(ProgramSource::snapshot))));
        Buffer reset_output(reset.tracks()[0]);
        check(reset_output.sample(reset,0,2).geometry.parts[0].local.rotate==0);
        const auto snapshot=take(f.registry.capture(source.object()));
        check(snapshot.revision()==source.revision() && snapshot.parts()[0].local.sx==2);
    });
    suite.test("public feedback recipes lower to shared channels and return without a runtime clock",[] {
        Fixture f;auto description=shape(4);description.parts[0].local.rotate=.5;
        description.parts[0].local.sx=2;description.parts[0].local.sy=-3;
        const auto source=take(f.registry.capture(take(f.registry.create(description))));
        const anim::FeedbackOptions options{.duration=seconds(1),.easing=ease::linear};
        const auto recipe=parallel(anim::shake(source,{8,4},options),anim::wobble(source,.4,options),anim::pulse(source,{2,.5},options));
        const auto plan=take(PreparedProgram::prepare(recipe));
        check(plan.duration()==seconds(1) && plan.scopes().empty());
        Buffer output(plan.tracks()[0]);bool good=true;
        const auto before=allocations.load();
        for(int direction:{1,-1})for(int i=0;i<=60;++i) {
            const double t=(direction==1?i:60-i)/60.;
            const double swing=t<=.25?4*t:t<=.75?2-4*t:4*t-4;
            const double pulse=t<=.5?2*t:2-2*t;
            const auto pose=output.sample(plan,0,t).geometry.parts[0].local;
            good=good && std::abs(pose.tx-(4+8*swing))<1e-12 && std::abs(pose.ty-4*swing)<1e-12 &&
                std::abs(pose.rotate-(.5+.4*swing))<1e-12 && std::abs(pose.sx-(2+2*pulse))<1e-12 &&
                std::abs(pose.sy-(-3+1.5*pulse))<1e-12;
        }
        const auto after=allocations.load();check(good && before==after);
        const auto scoped=take(PreparedProgram::prepare(composite(recipe).with_effect(fx::blur(0,4))));
        std::array<float,1> values{};take(scoped.sample_scopes(seconds(.5),values));check(values[0]==2);
        const auto staged=take(PreparedProgram::prepare(sequence(anim::move_to(source,{20,30}),anim::shake(source,{8,4},options))));
        Buffer staged_output(staged.tracks()[0]);check(staged_output.sample(staged,0,1.25).geometry.parts[0].local.tx==28);
        check(staged_output.sample(staged,0,2).geometry.parts[0].local.tx==20);
        check(!PreparedProgram::prepare(anim::pulse(source,{0,1})));
        check(!PreparedProgram::prepare(anim::shake(source,{1,1},{.duration=seconds(-1)})));
        check(!PreparedProgram::prepare(anim::wobble(source,std::numeric_limits<double>::infinity())));
    });
    suite.test("timed waypoints reuse translation channels paths and actual preceding poses",[] {
        Fixture f;const auto source=take(f.registry.capture(take(f.registry.create(shape(4)))));
        std::vector<anim::MoveWaypoint> points{{seconds(.25),{8,0},ease::linear,PointPath::bend(.5)},
            {seconds(.75),{8,12}},{seconds(1),{0,0}}};
        const auto path=anim::move_through(source,points,{.part="shape"});points.front().offset={999,999};points.clear();
        const auto plan=take(PreparedProgram::prepare(path));check(plan.duration()==seconds(1) && plan.nodes().size()==4 && plan.scopes().empty());
        Buffer out(plan.tracks()[0]);
        const auto midpoint=out.sample(plan,0,.125).geometry.parts[0].local;
        check(midpoint.tx==8 && midpoint.ty==4,"incoming PointPath changes the actual intermediate trajectory");
        const auto corner=out.sample(plan,0,.25).geometry.parts[0].local;check(corner.tx==12 && corner.ty==0);
        const auto vertical=out.sample(plan,0,.5).geometry.parts[0].local;check(vertical.tx==12 && vertical.ty==6);
        std::array<Transform2D,61> saved{};bool good=true;const auto before=allocations.load();
        for(int i=0;i<=60;++i)saved[std::size_t(i)]=out.sample(plan,0,i/60.).geometry.parts[0].local;
        for(int i=60;i>=0;--i)good=good && saved[std::size_t(i)]==out.sample(plan,0,i/60.).geometry.parts[0].local;
        check(good && allocations.load()==before);check(saved.back().tx==4 && saved.back().ty==0);
        const auto after=take(PreparedProgram::prepare(sequence(anim::move_to(source,{20,30}),path)));
        Buffer after_out(after.tracks()[0]);const auto shifted=after_out.sample(after,0,1.125).geometry.parts[0].local;
        check(shifted.tx==24 && shifted.ty==34,"path offsets anchor to the actual preceding endpoint");
        const auto combined=take(PreparedProgram::prepare(composite(parallel(path,anim::rotate_by(source,1))).with_effect(fx::blur(0,4))));
        Buffer combined_out(combined.tracks()[0]);const auto pose=combined_out.sample(combined,0,.5).geometry.parts[0].local;
        check(pose.tx==12 && pose.ty==6 && pose.rotate==.5);
        std::array<float,1> effects{};take(combined.sample_scopes(seconds(.5),effects));check(effects[0]==2);
        check(!PreparedProgram::prepare(parallel(path,anim::move_by(source,{1,0}))));
        const auto budget=PreparedProgram::prepare(path,{.max_nodes=3});check(!budget && budget.error().code==ProgramError::node_budget);
    });
    suite.test("waypoints reject malformed authoring without publishing a partial path",[] {
        Fixture f;const auto empty=anim::move_through(f.a,{});check(empty.failure() && empty.failure()->code==ProgramError::empty_program);
        for(double at:{0.,-1.,std::numeric_limits<double>::infinity()}) {
            const auto bad=anim::move_through(f.a,{{seconds(at),{1,0}}});check(bad.failure() && bad.failure()->code==ProgramError::invalid_time);
        }
        const auto repeated=anim::move_through(f.a,{{seconds(1),{1,0}},{seconds(1),{2,0}}});check(repeated.failure());
        const auto overflow=anim::move_through(f.a,{{seconds(1),{std::numeric_limits<double>::max(),0}},
            {seconds(2),{-std::numeric_limits<double>::max(),0}}});
        check(overflow.failure() && overflow.failure()->caused_by(MorphError::invalid_property) &&
            overflow.failure()->cause_of(MorphError::invalid_property)->where=="clip 1");
        const auto bad_ease=anim::move_through(f.a,{{seconds(1),{1,0},Ease{static_cast<EaseKind>(255)}}});
        check(bad_ease.failure() && bad_ease.failure()->code==ProgramError::invalid_ease);
        const auto nonfinite=anim::move_through(f.a,{{seconds(1),{std::numeric_limits<double>::quiet_NaN(),0}}});
        check(nonfinite.failure() && nonfinite.failure()->caused_by(MorphError::invalid_property));
        const auto bad_path=anim::move_through(f.a,{{seconds(1),{1,0},ease::linear,{{1e7,0},{}}}});
        check(bad_path.failure() && bad_path.failure()->caused_by(MorphError::invalid_options));
        const auto unknown=PreparedProgram::prepare(anim::move_through(f.a,{{seconds(1),{1,0}}},{.part="missing"}));
        check(!unknown && unknown.error().caused_by(MorphError::unknown_part));
        const auto hold=take(PreparedProgram::prepare(anim::move_through(f.a,{{seconds(.5),{0,0}},{seconds(1),{5,0}}})));
        Buffer out(hold.tracks()[0]);check(out.sample(hold,0,.25).geometry.parts[0].local.tx==0);
        check(out.sample(hold,0,1).geometry.parts[0].local.tx==5);
    });
    suite.test("deferred authoring errors retain typed causes and acquire the actual composition node",[] {
        Fixture f;
        const auto rejected=invalid_program(ProgramFailure{.code=ProgramError::animation,.where={.node=99}}
            .because(MorphFailure{.code=MorphError::invalid_property,.where={.part=7}}));
        const auto copy=rejected;
        check(rejected && rejected.kind()==ProgramKind::invalid && copy.failure()==rejected.failure());
        check(!rejected.binding() && !rejected.inputs() && !rejected.scope() && rejected.children().empty());
        const auto nested=sequence(anim::move_to(f.a,{4,0}),parallel(rejected,anim::rotate_to(f.a,1)));
        const auto prepared=PreparedProgram::prepare(nested);
        check(!prepared && prepared.error().code==ProgramError::animation && prepared.error().where.node==3 &&
            prepared.error().cause_of(MorphError::invalid_property) && prepared.error().cause_of(MorphError::invalid_property)->where=="part 7");
        check(rejected.failure()->where.node==99,"preparing a composition does not mutate its reusable error expression");
        const auto bounded=PreparedProgram::prepare(nested,{.max_nodes=2});
        check(!bounded && bounded.error().code==ProgramError::node_budget);
        const Program empty;check(!empty.failure());
    });
    suite.test("feedback validates arithmetic and representable phase durations before lowering",[] {
        Fixture f;
        constexpr double floor=1.0/std::numeric_limits<double>::max();
        for(const auto& value:{anim::pulse(f.a,{0,1}),anim::pulse(f.a,{floor,1}),
            anim::pulse(f.a,{std::numeric_limits<double>::infinity(),1}),
            anim::shake(f.a,{std::numeric_limits<double>::max(),0}),
            anim::wobble(f.a,std::numeric_limits<double>::max())}) {
            check(value.kind()==ProgramKind::invalid && value.failure() && value.children().empty());
            const auto error=PreparedProgram::prepare(value);
            check(!error && error.error().code==ProgramError::animation &&
                error.error().caused_by(MorphError::invalid_property));
        }
        for(const auto& value:{anim::pulse(f.a,{1,1},{.duration=seconds(std::numeric_limits<double>::denorm_min())}),
            anim::shake(f.a,{1,1},{.duration=seconds(3*std::numeric_limits<double>::denorm_min())}),
            anim::wobble(f.a,.1,{.duration=seconds(-1)})}) {
            check(value.failure() && value.failure()->code==ProgramError::invalid_time);
            const auto error=PreparedProgram::prepare(value);check(!error && error.error().code==ProgramError::invalid_time);
        }
        const auto safe=std::nextafter(floor,std::numeric_limits<double>::infinity());
        check(bool(PreparedProgram::prepare(anim::pulse(f.a,{safe,1}))));
        check(bool(PreparedProgram::prepare(anim::pulse(f.a,{2,2},{.duration=seconds(0)}))));
        check(bool(PreparedProgram::prepare(anim::shake(f.a,{1,1},{.duration=seconds(4*std::numeric_limits<double>::denorm_min())}))));
    });
    suite.test("absolute and relative writes share conflicts and reject invalid or overflowing targets",[] {
        Fixture f;
        for(const auto targets:{PropertyTargets{.rotation=0,.rotation_offset=1},
            PropertyTargets{.scale=Vec2{1,1},.scale_factor=Vec2{2,2}},
            PropertyTargets{.rotation_offset=std::numeric_limits<double>::infinity()},
            PropertyTargets{.scale_factor=Vec2{1,std::numeric_limits<double>::quiet_NaN()}}})
            check(!PreparedProgram::prepare(anim::to(f.a,targets)));
        check(!PreparedProgram::prepare(parallel(anim::rotate_to(f.a,1),anim::rotate_by(f.a,1))));
        check(!PreparedProgram::prepare(parallel(anim::scale_to(f.a,{2,2}),anim::scale_by(f.a,{2,2}))));
        check(!PreparedProgram::prepare(anim::to(f.a,PropertySet{
            {.rotation=1},{.part="shape",.rotation_offset=1}})));
        auto description=shape(0);description.parts[0].local.sx=2;
        const auto source=take(f.registry.capture(take(f.registry.create(description))));
        check(!PreparedProgram::prepare(anim::scale_by(source,{std::numeric_limits<double>::max(),1})));
    });
    suite.test("property selection changes one named part and keeps unrelated hierarchy and geometry",[] {
        Fixture f;auto description=shape(4);description.parts[0].parent="pivot";
        description.parts.push_back({.id="pivot",.local={.tx=7,.ty=9}});
        const auto source=take(f.registry.capture(take(f.registry.create(description))));
        const auto plan=take(PreparedProgram::prepare(anim::to(source,{.position=Vec2{20,30},.scale=Vec2{3,2},.fill=Rgba{0,0,1,1},.part="shape"})));
        Buffer output(plan.tracks()[0]);auto sample=output.sample(plan,0,.5);
        check(sample.geometry.parts.size()==2 && sample.geometry.parts[0].info.id=="pivot");
        check(sample.geometry.parts[0].local.tx==7 && sample.geometry.parts[0].local.ty==9);
        check(sample.geometry.parts[1].local.tx==12 && sample.geometry.parts[1].local.ty==15);
        check(sample.geometry.parts[1].local.sx==2 && sample.geometry.parts[1].local.sy==1.5);
        check(sample.geometry.parts[1].fill->r>0 && sample.geometry.parts[1].fill->b>0);
        const auto root=take(PreparedProgram::prepare(anim::move_to(source,{30,40})));
        Buffer root_output(root.tracks()[0]);const auto root_sample=root_output.sample(root,0,1);
        check(root_sample.geometry.parts[0].local.tx==30 && root_sample.geometry.parts[1].local.tx==4);
        const auto unchanged=take(f.registry.capture(source.object()));
        check(unchanged.revision()==source.revision() && take(unchanged.get(unchanged.root()))->local.tx==7);
    });
    suite.test("morph then properties keeps the morphed shape and properties then morph keeps correspondence",[] {
        Fixture f;
        const auto plan=take(PreparedProgram::prepare(sequence(anim::morph(f.a,f.b),anim::scale_to(f.a,{2,3}),anim::morph(f.a,f.c))));
        Buffer output(plan.tracks()[0]);const auto middle=output.sample(plan,0,1.5);
        check(middle.geometry.parts[0].local.tx==10 && middle.geometry.parts[0].local.sx==1.5 && middle.geometry.parts[0].local.sy==2);
        const auto at_end=output.sample(plan,0,3);
        check(at_end.geometry.parts[0].local.tx==30 && at_end.geometry.parts[0].local.sx==1);
    });
    suite.test("invalid property targets fail at preparation with no registry mutation",[] {
        Fixture f;
        for(const auto& values:std::vector<PropertyTargets>{{},{.position=Vec2{1,2},.offset=Vec2{3,4}},
            {.rotation=std::numeric_limits<double>::quiet_NaN()}, {.scale=Vec2{1,std::numeric_limits<double>::infinity()}},
            {.fill=Rgba{2,0,0,1}}}) {
            const auto plan=PreparedProgram::prepare(anim::to(f.a,values));
            check(!plan && plan.error().caused_by(MorphError::invalid_property));
        }
        const auto missing=PreparedProgram::prepare(anim::to(f.a,{.position=Vec2{1,2},.part="absent"}));
        check(!missing && missing.error().caused_by(MorphError::unknown_part));
        check(take(f.registry.capture(f.ar)).revision()==f.a.revision());
    });
    suite.test("property sampling with process and image effects is allocation free across the whole process",[] {
        Fixture f;
        const auto plan=take(PreparedProgram::prepare(anim::move_to(f.a,{12,8})
            .with_effect(fx::translation_path(PointPath::bend(.2))).with_effect(fx::blur(0,4))));
        Buffer output(plan.tracks()[0]);mlibs::trace::start();output.sample(plan,0,0);
        const auto before=allocations.load();bool good=true;
        for(int i=0;i<=60;++i) {
            const auto sample=plan.sample(0,seconds(i/60.),{output.parts,output.cubics},output.effects);
            good=good && sample.has_value() && output.effects[0]>=0 && output.effects[0]<=4;
        }
        const auto after=allocations.load();mlibs::trace::stop();check(good && before==after);
        check(output.parts[0].local.tx==12 && output.parts[0].local.ty==8);
    });
    suite.test("one animation targets multiple parts and preserves the unselected sibling",[] {
        Fixture f;auto description=shape(4);description.parts[0].parent="group";
        auto sibling=description.parts[0];sibling.id="title";sibling.local.tx=7;description.parts.push_back(sibling);
        sibling.id="badge";sibling.local.tx=11;description.parts.push_back(sibling);
        description.parts.push_back({.id="group"});
        const auto source=take(f.registry.capture(take(f.registry.create(description))));
        const PropertySet targets{{.position=Vec2{24,8},.part="shape"},
            {.rotation=std::numbers::pi,.part="title"}, {.scale=Vec2{2,3},.part="title"}};
        const auto plan=take(PreparedProgram::prepare(anim::to(source,targets)));
        Buffer output(plan.tracks()[0]);
        for(int i=0;i<=60;++i) {
            const auto pose=output.sample(plan,0,i/60.).geometry;
            for(const auto& part:pose.parts) {
                if(part.info.id=="badge")check(part.local.tx==11 && part.local.rotate==0 && part.local.sx==1);
                if(part.info.id=="title")check(part.local.tx==7);
                if(part.info.id=="group")check(part.local==Transform2D{});
            }
        }
        const auto end=output.sample(plan,0,1).geometry;
        for(const auto& part:end.parts) {
            if(part.info.id=="shape")check(part.local.tx==24 && part.local.ty==8);
            if(part.info.id=="title")check(part.local.rotate==std::numbers::pi && part.local.sx==2 && part.local.sy==3);
        }
        const auto reverse=output.sample(plan,0,0).geometry;
        for(const auto& part:reverse.parts)if(part.info.id=="title")check(part.local.rotate==0 && part.local.sx==1);
    });
    suite.test("property sets reject aliasing channel writers instead of depending on declaration order",[] {
        Fixture f;
        const auto collision=PreparedProgram::prepare(anim::to(f.a,PropertySet{
            {.position=Vec2{2,4}}, {.offset=Vec2{1,1},.part="shape"}}));
        check(!collision && collision.error().caused_by(MorphError::property_conflict));
        check(collision.error().cause_of(MorphError::property_conflict)->where.starts_with("part 0"));
        const auto empty=PreparedProgram::prepare(anim::to(f.a,PropertySet{}));
        check(!empty && empty.error().caused_by(MorphError::invalid_property));
        const auto independent=take(PreparedProgram::prepare(anim::to(f.a,PropertySet{
            {.position=Vec2{2,4}}, {.rotation=1,.part="shape"}, {.fill=Rgba{0,1,0,1}}})));
        Buffer output(independent.tracks()[0]);const auto pose=output.sample(independent,0,1).geometry.parts[0];
        check(pose.local.tx==2 && pose.local.rotate==1 && pose.fill->g==1);
    });
    suite.test("parallel channels keep independent timing and retain the authored contribution graph",[] {
        Fixture f;
        const auto plan=take(PreparedProgram::prepare(parallel(
            anim::move_to(f.a,{20,8}).duration(seconds(2)),
            anim::rotate_to(f.a,std::numbers::pi).duration(seconds(1)).easing(ease::in_quad))));
        check(plan.tracks().size()==1 && plan.segments().size()==1 && plan.duration()==seconds(2));
        check(plan.nodes().size()==3 && plan.nodes()[1].contribution==0 && plan.nodes()[2].contribution==1);
        check(plan.nodes()[1].duration==seconds(2) && plan.nodes()[2].duration==seconds(1));
        check(plan.segments()[0].contributions.size()==2 && plan.segments()[0].animation.channels().size()==2);
        Buffer out(plan.tracks()[0]);
        for(int i=0;i<=120;++i) {
            const double t=i/60.;const auto value=out.sample(plan,0,t).geometry.parts[0].local;
            check(std::abs(value.tx-10*t)<1e-12 && std::abs(value.ty-4*t)<1e-12);
            const auto u=std::min(t,1.);check(std::abs(value.rotate-std::numbers::pi*u*u)<1e-12);
        }
        const auto reverse=out.sample(plan,0,.25).geometry.parts[0].local;
        check(reverse.tx==2.5 && reverse.rotate==std::numbers::pi/16);
    });
    suite.test("a serial channel branch chains actual endpoints while another channel remains active",[] {
        Fixture f;
        const auto plan=take(PreparedProgram::prepare(parallel(
            sequence(anim::move_to(f.a,{10,0}).duration(seconds(.5)),anim::move_by(f.a,{5,0}).duration(seconds(.5))),
            anim::rotate_to(f.a,2).duration(seconds(2)))));
        check(plan.segments().size()==1 && plan.segments()[0].contributions.size()==3);
        Buffer out(plan.tracks()[0]);
        check(out.sample(plan,0,.5).geometry.parts[0].local.tx==10);
        const auto mid=out.sample(plan,0,.75).geometry.parts[0].local;
        check(mid.tx==12.5 && mid.rotate==.75);
        const auto late=out.sample(plan,0,1.5).geometry.parts[0].local;
        check(late.tx==15 && late.rotate==1.5);
        const auto restore=out.sample(plan,0,2,true).geometry.parts[0].local;
        check(restore.tx==0 && restore.rotate==0);
    });
    suite.test("property groups compose with morph segments and one shared image scope",[] {
        Fixture f;
        const auto group=composite(parallel(anim::move_by(f.a,{10,4}),anim::rotate_to(f.a,2))).with_effect(fx::blur(0,4));
        const auto plan=take(PreparedProgram::prepare(sequence(anim::morph(f.a,f.b),group,anim::morph(f.a,f.c))));
        check(plan.segments().size()==3 && plan.scopes().size()==1);
        Buffer out(plan.tracks()[0]);const auto middle=out.sample(plan,0,1.5).geometry.parts[0].local;
        check(middle.tx==15 && middle.ty==2 && middle.rotate==1);
        const auto boundary=out.sample(plan,0,2).geometry.parts[0].local;
        check(boundary.tx==20 && boundary.ty==4 && boundary.rotate==2);
        const auto end=out.sample(plan,0,3).geometry.parts[0].local;check(end.tx==30 && end.rotate==0);
        std::array<float,1> fx{};take(plan.sample_scopes(seconds(1.5),fx));check(fx[0]==2);
    });
    suite.test("parallel channel conflict scope and budget failures identify the responsible author node",[] {
        Fixture f;
        const auto conflict=PreparedProgram::prepare(parallel(anim::move_to(f.a,{10,0}),anim::move_by(f.a,{1,0})));
        check(!conflict && conflict.error().code==ProgramError::binding_conflict && conflict.error().where.node==2);
        check(conflict.error().cause_of(MorphError::property_conflict) && conflict.error().cause_of(MorphError::property_conflict)->where.ends_with("channel translation"));
        const auto scope=PreparedProgram::prepare(parallel(anim::move_to(f.a,{10,0}).with_effect(fx::blur(0,4)),anim::rotate_to(f.a,1)));
        check(!scope && scope.error().code==ProgramError::scope_conflict);
        const auto nested=PreparedProgram::prepare(parallel(composite(anim::move_to(f.a,{10,0})).with_effect(fx::blur(0,4)),anim::rotate_to(f.a,1)));
        check(!nested && nested.error().code==ProgramError::scope_conflict);
        const auto reset=PreparedProgram::prepare(parallel(anim::move_to(f.a,{10,0}).source(ProgramSource::snapshot),anim::rotate_to(f.a,1)));
        check(!reset && reset.error().code==ProgramError::binding_conflict);
        const auto budget=PreparedProgram::prepare(parallel(anim::move_to(f.a,{10,0}),anim::rotate_to(f.a,1)),{.max_total_property_channels=1});
        check(!budget && budget.error().code==ProgramError::channel_budget);
    });
    suite.test("independent instantaneous writers combine while ambiguous instantaneous ownership is refused",[] {
        Fixture f;
        const auto plan=take(PreparedProgram::prepare(parallel(anim::move_to(f.a,{10,0}).duration(seconds(0)),
            anim::rotate_to(f.a,2).duration(seconds(0)))));
        Buffer out(plan.tracks()[0]);const auto pose=out.sample(plan,0,0).geometry.parts[0].local;
        check(pose.tx==10 && pose.rotate==2);
        check(out.sample(plan,0,0,true).geometry.parts[0].local.tx==0);
        const auto rejected=PreparedProgram::prepare(parallel(anim::move_to(f.a,{10,0}).duration(seconds(0)),anim::move_by(f.a,{2,0})));
        check(!rejected && rejected.error().code==ProgramError::binding_conflict);
        const auto serial=take(PreparedProgram::prepare(parallel(sequence(anim::move_to(f.a,{10,0}).duration(seconds(0)),
            anim::move_by(f.a,{2,0})),anim::rotate_to(f.a,2))));
        Buffer serial_out(serial.tracks()[0]);
        check(serial_out.sample(serial,0,0).geometry.parts[0].local.tx==10);
        check(serial_out.sample(serial,0,.5).geometry.parts[0].local.tx==11);
    });
    suite.test("channel steps distinguish normal zero-time sampling from exact source restoration",[] {
        Fixture f;
        const auto plan=take(PreparedProgram::prepare(parallel(
            anim::move_to(f.a,{20,0}).easing(Ease::stepped(4,StepPosition::jump_start)),anim::rotate_to(f.a,2))));
        Buffer out(plan.tracks()[0]);
        check(out.sample(plan,0,0).geometry.parts[0].local.tx==5);
        check(out.sample(plan,0,-1).geometry.parts[0].local.tx==0);
        check(out.sample(plan,0,0,true).geometry.parts[0].local.tx==0);
        const auto delayed=take(PreparedProgram::prepare(stagger(seconds(1),anim::move_to(f.b,{50,0}).duration(seconds(0)),
            parallel(anim::move_to(f.a,{20,0}).easing(Ease::stepped(4,StepPosition::jump_start)),anim::rotate_to(f.a,2)))));
        const auto track=delayed.tracks()[0].object==f.ar?0u:1u;Buffer delayed_out(delayed.tracks()[track]);
        check(delayed_out.sample(delayed,track,.5).geometry.parts[0].local.tx==0);
        check(delayed_out.sample(delayed,track,1).geometry.parts[0].local.tx==5);
    });
    suite.test("channel sampling allocates nothing and preflights every writer before touching output",[] {
        Fixture f;const auto plan=take(PreparedProgram::prepare(parallel(
            anim::move_to(f.a,{20,8}).with_effect(fx::translation_path(PointPath::bend(.2))),anim::rotate_to(f.a,2))));
        Buffer out(plan.tracks()[0]);mlibs::trace::start();out.sample(plan,0,0);
        const auto before=allocations.load();bool good=true;
        for(int i=0;i<1000;++i)good=good && bool(plan.sample(0,seconds((i%197)/196.),{out.parts,out.cubics},out.effects));
        const auto after=allocations.load();mlibs::trace::stop();check(good && before==after);
        const auto huge=take(PreparedProgram::prepare(parallel(
            anim::move_to(f.a,{1e308,1e308}).with_effect(fx::translation_path(PointPath::bend(2))),anim::rotate_to(f.a,2))));
        Buffer failed(huge.tracks()[0]);failed.parts[0].local.tx=99;failed.cubics[0].end.x=77;
        const auto invalid=huge.sample(0,seconds(.5),{failed.parts,failed.cubics},failed.effects);
        check(!invalid && failed.parts[0].local.tx==99 && failed.cubics[0].end.x==77);
    });
    suite.test("advanced timelines reset only selected source channels without replacing geometry",[] {
        Fixture f;auto changed=shape(20,true);changed.parts[0].local.rotate=7;changed.parts[0].fill=Rgba{0,0,1,1};
        take(f.registry.update(f.ar,changed));const auto frozen=take(f.registry.capture(f.ar));
        const std::array clips{
            PropertyClip{.targets={{.position=Vec2{10,0}}},.duration=seconds(1)},
            PropertyClip{.targets={{.offset=Vec2{5,0}}},.start=seconds(1),.duration=seconds(1),.source=frozen},
            PropertyClip{.targets={{.rotation=2}},.duration=seconds(2)}};
        const auto timeline=take(PreparedObjectAnimation::property_timeline(f.a,clips));
        const auto plan=take(PreparedProgram::single(timeline,take(PreparedEffects::prepare({})),{.duration=seconds(2)}));
        Buffer out(plan.tracks()[0]);const auto reset=out.sample(plan,0,1).geometry;
        check(reset.parts[0].local.tx==20 && reset.parts[0].local.rotate==1 && reset.parts[0].fill==Rgba{1,0,0,1});
        const auto middle=out.sample(plan,0,1.5).geometry;
        check(middle.parts[0].local.tx==22.5 && middle.parts[0].local.rotate==1.5);
        check(middle.cubics.size()==3,"source channel snapshots do not replace the triangle with their circle geometry");
    });
    suite.test("advanced timeline limits and numeric time failures are explicit",[] {
        Fixture f;
        const std::array clip{PropertyClip{.targets={{.position=Vec2{1,2},.rotation=1,.scale=Vec2{2,2},.fill=Rgba{0,0,1,1}}}}};
        const auto budget=PreparedObjectAnimation::property_timeline(f.a,clip,{.max_channels=3});
        check(!budget && budget.error().code==MorphError::channel_budget && budget.error().where.channel==PropertyChannel::fill);
        auto invalid=clip;invalid[0].start=seconds(1e300);
        check(PreparedObjectAnimation::property_timeline(f.a,invalid).error().code==MorphError::invalid_time);
        invalid=clip;invalid[0].options.shape.point_path=PointPath::bend(.2);
        check(PreparedObjectAnimation::property_timeline(f.a,invalid).error().code==MorphError::unsupported_property_effect);
        invalid=clip;invalid[0].source=Snapshot{};
        check(PreparedObjectAnimation::property_timeline(f.a,invalid).error().code==MorphError::invalid_snapshot);
    });
    suite.test("zero-duration cohorts do not hide an unordered writer behind a serial prefix",[] {
        Fixture f;
        const auto bad=PreparedProgram::prepare(parallel(
            sequence(anim::move_to(f.a,{10,0}).duration(seconds(0)),anim::rotate_to(f.a,2).duration(seconds(0))),
            anim::move_by(f.a,{1,0}).duration(seconds(0))));
        check(!bad && bad.error().code==ProgramError::binding_conflict);
        const auto good=take(PreparedProgram::prepare(sequence(
            parallel(anim::move_to(f.a,{10,0}).duration(seconds(0)),anim::rotate_to(f.a,2).duration(seconds(0))),
            anim::morph(f.a,f.b).duration(seconds(0)),
            parallel(anim::scale_to(f.a,{2,2}).duration(seconds(0)),anim::fill_to(f.a,{0,0,1,1}).duration(seconds(0))))));
        check(good.segments().size()==3);
        Buffer out(good.tracks()[0]);const auto final=out.sample(good,0,0).geometry;
        check(final.parts[0].local.tx==10 && final.parts[0].local.rotate==0 && final.parts[0].local.sx==2);
        check(final.parts[0].fill==Rgba{0,0,1,1} && final.cubics.size()>3);
        const auto source=out.sample(good,0,0,true).geometry;
        check(source.parts[0].local.tx==0 && source.parts[0].local.sx==1 && source.cubics.size()==3);
    });
    suite.test("a delayed authored leaf keeps its path when it owns a group's draw order",[] {
        Fixture f;
        const auto plan=take(PreparedProgram::prepare(parallel(
            sequence(anim::move_by(f.b,{1,0}),anim::move_to(f.a,{10,0})),anim::rotate_to(f.a,2).duration(seconds(2)))));
        const auto track=plan.tracks()[0].object==f.ar?0u:1u;
        const auto& segment=plan.segments()[plan.tracks()[track].first_segment];
        check(segment.node==3 && segment.path=="0/0/1");
        check(segment.contributions.size()==2 && segment.contributions[0].node==4 && segment.contributions[1].node==3);
        Buffer out(plan.tracks()[track]);const auto before=out.sample(plan,track,.5).geometry.parts[0].local;
        check(before.tx==0 && before.rotate==.5);
        const auto during=out.sample(plan,track,1.5).geometry.parts[0].local;check(during.tx==5 && during.rotate==1.5);
    });
    suite.test("wait contributes only time and preserves gap poses and independent channels",[] {
        Fixture f;
        const auto expression=parallel(sequence(anim::move_by(f.a,{10,0}),wait(seconds(.5)),anim::move_by(f.a,{10,0})),
            anim::rotate_to(f.a,2.5).duration(seconds(2.5)));
        const auto plan=take(PreparedProgram::prepare(expression));Buffer out(plan.tracks()[0]);
        check(plan.duration()==seconds(2.5) && plan.nodes().size()==6 && plan.tracks().size()==1);
        const auto& pause=plan.nodes()[3];check(pause.kind==ProgramKind::wait && pause.start==seconds(1) && pause.duration==seconds(.5));
        check(pause.track==invalid_program_index && pause.segment==invalid_program_index && pause.subtree_end==4);
        const auto before=allocations.load();
        for(int i=0;i<=100;++i) {
            const double t=2.5*i/100.;const double x=t<1?10*t:t<1.5?10:10+10*(t-1.5);
            const auto pose=out.sample(plan,0,t).geometry.parts[0].local;
            check_near(pose.tx,x,1e-10);check_near(pose.rotate,t,1e-10);
        }
        check(allocations.load()==before,"pure timeline waits add no allocations to warm sampling");
        const auto delayed=take(PreparedProgram::prepare(delay(seconds(.5),anim::move_by(f.a,{10,0}))));
        Buffer delayed_out(delayed.tracks()[0]);check(delayed.duration()==seconds(1.5));
        check(delayed_out.sample(delayed,0,.25).geometry.parts[0].local.tx==0);
        check(delayed_out.sample(delayed,0,1).geometry.parts[0].local.tx==5);
        check(delayed_out.sample(delayed,0,2).geometry.parts[0].local.tx==10);
    });
    suite.test("wait-only plans and scoped holds retain timing and diagnostics",[] {
        Fixture f;
        const auto timer=take(PreparedProgram::prepare(wait(seconds(2))));
        check(timer.duration()==seconds(2) && timer.nodes().size()==1 && timer.tracks().empty() && timer.segments().empty());
        check(PreparedProgram::prepare(wait(seconds(0))).has_value());
        const auto scoped=take(PreparedProgram::prepare(composite(sequence(anim::move_by(f.a,{10,0}),wait(seconds(1))))
            .with_effect(fx::opacity(1,0))));
        std::array<float,1> values;take(scoped.sample_scopes(seconds(1.5),values));check(values[0]==.25f);
        Buffer out(scoped.tracks()[0]);check(out.sample(scoped,0,1.5).geometry.parts[0].local.tx==10);
        for(double time:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            const auto bad=PreparedProgram::prepare(delay(seconds(time),anim::move_by(f.a,{1,0})));
            check(!bad && bad.error().code==ProgramError::invalid_time && bad.error().where.node==1);
        }
        const auto huge=std::numeric_limits<double>::max();
        check(PreparedProgram::prepare(sequence(wait(seconds(huge)),wait(seconds(huge)))).error().code==ProgramError::invalid_time);
        check(PreparedProgram::prepare(sequence(wait(seconds(huge)),wait(seconds(1)))).error().code==ProgramError::invalid_time);
        check(PreparedProgram::prepare(delay(seconds(1),anim::move_by(f.a,{1,0})),{.max_nodes=2}).error().code==ProgramError::node_budget);
        check(PreparedProgram::prepare(sequence(wait(seconds(0)),Program{})).error().code==ProgramError::empty_program);
    });
    suite.test("finite repeat keeps one authored child and expands bounded inspectable iterations",[] {
        Fixture f;const auto child=sequence(anim::move_by(f.a,{10,0}),wait(seconds(.5)));
        const auto expression=repeat(3,child);
        check(expression.kind()==ProgramKind::repeat && expression.repeat_count()==3 && expression.children().size()==1);
        check(child.kind()==ProgramKind::sequence && child.repeat_count()==0);
        const auto plan=take(PreparedProgram::prepare(expression));
        check(plan.duration()==seconds(4.5) && plan.nodes().size()==10 && plan.tracks().size()==1);
        check(plan.nodes()[0].repeat_count==3 && plan.nodes()[0].subtree_end==10);
        for(std::size_t i=0;i<3;++i) {
            const auto& node=plan.nodes()[1+3*i];check(node.iteration==i && node.parent==0 && node.start==seconds(1.5*i));
        }
        const auto plain=take(PreparedProgram::prepare(sequence(child,child,child)));
        Buffer actual(plan.tracks()[0]),expected(plain.tracks()[0]);
        const auto before=allocations.load();
        for(int i=0;i<=90;++i) {
            const double t=i*.05;const auto frame=actual.sample(plan,0,t);
            check(all_same(frame.geometry,expected.sample(plain,0,t).geometry));
            const double cycle=std::min(2.,std::floor(t/1.5));
            check_near(frame.geometry.parts[0].local.tx,10*(cycle+std::min(1.,t-cycle*1.5)),1e-10);
        }
        check(allocations.load()==before,"repeat introduces no warm sampling allocations");
        check(actual.sample(plan,0,1.25).geometry.parts[0].local.tx==10);
        check(actual.sample(plan,0,4.5).geometry.parts[0].local.tx==30);
    });
    suite.test("nested repeat preserves serial zero-duration order and explicit source semantics",[] {
        Fixture f;
        const auto nested=take(PreparedProgram::prepare(repeat(2,repeat(3,anim::move_by(f.a,{1,0}).duration(seconds(0))))));
        Buffer out(nested.tracks()[0]);check(nested.duration()==seconds(0) && out.sample(nested,0,0).geometry.parts[0].local.tx==6);
        check(nested.nodes().size()==9 && nested.nodes()[1].repeat_count==3 && nested.nodes()[5].iteration==1);
        const auto reset=take(PreparedProgram::prepare(repeat(2,anim::move_by(f.a,{10,0}).source(ProgramSource::snapshot))));
        Buffer reset_out(reset.tracks()[0]);check(reset_out.sample(reset,0,1.5).geometry.parts[0].local.tx==5);
        const auto absolute=take(PreparedProgram::prepare(repeat(2,anim::move_to(f.a,{10,0}))));
        Buffer absolute_out(absolute.tracks()[0]);check(absolute_out.sample(absolute,0,1.5).geometry.parts[0].local.tx==10);
        const auto timer=take(PreparedProgram::prepare(repeat(3,wait(seconds(.25)))));
        check(timer.duration()==seconds(.75) && timer.tracks().empty());
    });
    suite.test("repeat scopes restart inside each iteration and span all iterations outside",[] {
        Fixture f;
        const auto inside=take(PreparedProgram::prepare(repeat(2,composite(anim::move_by(f.a,{10,0})).with_effect(fx::opacity(1,.5)))));
        check(inside.scopes().size()==2 && inside.scopes()[0].start==seconds(0) && inside.scopes()[1].start==seconds(1));
        std::array<float,2> values;take(inside.sample_scopes(seconds(1.5),values));check(values[0]==.5f && values[1]==.75f);
        Buffer out(inside.tracks()[0]);const auto frame=out.sample(inside,0,1.5);
        check_near(frame.geometry.parts[0].local.tx,15,1e-10);check(inside.segments()[frame.segment_index].scope==1);
        const auto outside=take(PreparedProgram::prepare(composite(repeat(2,anim::move_by(f.a,{10,0}))).with_effect(fx::opacity(1,.5))));
        check(outside.scopes().size()==1 && outside.scopes()[0].duration==seconds(2));
        take(outside.sample_scopes(seconds(1.5),values));check(values[0]==.625f);
    });
    suite.test("repeat composes independent channels and rejects overlapping writes",[] {
        Fixture f;
        const auto plan=take(PreparedProgram::prepare(parallel(repeat(3,anim::move_by(f.a,{10,0})),anim::rotate_to(f.a,3).duration(seconds(3)))));
        Buffer out(plan.tracks()[0]);
        for(int i=60;i>=0;--i) {
            const double t=i*.05;const auto frame=out.sample(plan,0,t);check_near(frame.geometry.parts[0].local.tx,10*t,1e-10);
            check_near(frame.geometry.parts[0].local.rotate,t,1e-10);
        }
        const auto multiple=take(PreparedProgram::prepare(repeat(2,parallel(anim::move_by(f.a,{10,0}),anim::move_by(f.b,{-2,0})))));
        check(multiple.tracks().size()==2 && multiple.duration()==seconds(2));
        for(std::size_t i=0;i<multiple.tracks().size();++i) {
            Buffer buffer(multiple.tracks()[i]);const bool first=multiple.tracks()[i].object==f.ar;
            check(buffer.sample(multiple,i,2).geometry.parts[0].local.tx==(first?20:6));
            check(buffer.sample(multiple,i,.5).geometry.parts[0].local.tx==(first?5:9));
        }
        const auto conflict=PreparedProgram::prepare(parallel(repeat(3,anim::move_by(f.a,{10,0})),anim::move_to(f.a,{5,0})));
        check(!conflict && conflict.error().code==ProgramError::binding_conflict);
    });
    suite.test("repeat count and expansion obey the same diagnostic and resource budgets",[] {
        Fixture f;const auto child=anim::move_by(f.a,{1,0});
        const auto zero=PreparedProgram::prepare(sequence(wait(seconds(1)),repeat(0,child)));
        check(!zero && zero.error().code==ProgramError::invalid_repeat && zero.error().where.node==2);
        check(describe(ProgramError::invalid_repeat).name=="hanim.program.invalid-repeat");
        const auto huge=repeat(std::numeric_limits<std::size_t>::max(),wait(seconds(0)));
        check(huge.children().size()==1 && huge.repeat_count()==std::numeric_limits<std::size_t>::max());
        const auto bound=PreparedProgram::prepare(huge,{.max_nodes=4});check(!bound && bound.error().code==ProgramError::node_budget && bound.error().where.node==4);
        check(PreparedProgram::prepare(repeat(3,child),{.max_segments=2}).error().code==ProgramError::segment_budget);
        check(PreparedProgram::prepare(repeat(3,child),{.max_depth=1}).error().code==ProgramError::depth_budget);
        check(PreparedProgram::prepare(repeat(2,composite(child).with_effect(fx::opacity(1,.5))),{.max_scopes=1}).error().code==ProgramError::scope_budget);
        check(PreparedProgram::prepare(repeat(2,Program{})).error().code==ProgramError::empty_program);
        check(PreparedProgram::prepare(repeat(2,wait(seconds(std::numeric_limits<double>::max())))).error().code==ProgramError::invalid_time);
    });
    suite.test("decimal-time serial boundaries remain adjacent without hiding genuine overlap",[] {
        Fixture f;
        const auto child=parallel(sequence(anim::move_by(f.a,{8,0}).duration(seconds(.2)),wait(seconds(.1))),
            anim::wobble(f.a,.03,{.duration=seconds(.3)}),anim::pulse(f.a,{1.04,1.04},{.duration=seconds(.3)}));
        for(double offset:{0.,.1,.3,.137,1.,10.,1000000.}) {
            const auto repeated=PreparedProgram::prepare(delay(seconds(offset),sequence(repeat(3,child),anim::move_by(f.a,{-24,0}).duration(seconds(.3)))));
            check(repeated.has_value(),std::format("nested feedback prepares at offset {}",offset));
            const auto explicit_sequence=PreparedProgram::prepare(delay(seconds(offset),sequence(child,child,child,anim::move_by(f.a,{-24,0}).duration(seconds(.3)))));
            check(explicit_sequence.has_value(),"ordinary sequence gets the same canonical boundary treatment");
            if(repeated && explicit_sequence) {
                Buffer a(repeated->tracks()[0]),b(explicit_sequence->tracks()[0]);
                for(int i=0;i<=60;++i)check(all_same(a.sample(*repeated,0,offset+1.2*i/60.).geometry,b.sample(*explicit_sequence,0,offset+1.2*i/60.).geometry));
            }
        }
        const auto collapsed=PreparedProgram::prepare(parallel(
            delay(seconds(1),anim::rotate_by(f.a,1).duration(seconds(1e16+4))),
            delay(seconds(1e16),anim::move_by(f.a,{1,0}).duration(seconds(2)))));
        check(!collapsed && collapsed.error().code==ProgramError::invalid_time,
            "rebasing must not silently turn a positive interval into an instantaneous write");
        const auto overlap=PreparedProgram::prepare(parallel(anim::move_by(f.a,{1,0}),delay(seconds(std::nextafter(1.,0.)),anim::move_by(f.a,{1,0}))));
        check(!overlap && overlap.error().code==ProgramError::binding_conflict,"even one representable step of real overlap remains an error");
    });
    suite.test("public route follows shared distance samples from the actual stage source",[] {
        Fixture f;MotionPath path;path.move_to({100,100}).line_to({120,100}).line_to({120,160});
        const auto authored=anim::follow_path(f.a,path).duration(seconds(2));path.line_to({999,999});
        const auto plan=take(PreparedProgram::prepare(sequence(anim::move_by(f.a,{5,7}),authored)));
        check(plan.route_segment_count()==2);Buffer out(plan.tracks()[0]);
        const auto quarter=out.sample(plan,0,1.5).geometry.parts[0].local;
        check_near(quarter.tx,25,1e-10);check_near(quarter.ty,7,1e-10);
        const auto middle=out.sample(plan,0,2).geometry.parts[0].local;
        check_near(middle.tx,25,1e-10);check_near(middle.ty,27,1e-10);
        const auto end=out.sample(plan,0,3).geometry.parts[0].local;check(end.tx==25 && end.ty==67);
        check(out.sample(plan,0,1).geometry.parts[0].local.tx==5);
    });
    suite.test("closed routes survive equal endpoints and combine with independent channels and effects",[] {
        Fixture f;MotionPath loop;loop.move_to({0,0}).cubic_to({30,-20},{30,20},{0,0});
        const auto route=anim::follow_path(f.a,loop,{.part="shape"});
        const auto plan=take(PreparedProgram::prepare(composite(parallel(route,anim::rotate_by(f.a,1))).with_effect(fx::opacity(1,.5))));
        Buffer out(plan.tracks()[0]);const auto before=allocations.load();
        for(int i=60;i>=0;--i) {
            const double u=i/60.;const auto frame=out.sample(plan,0,u);const auto point=loop.point(u);
            check_near(frame.geometry.parts[0].local.tx,point.x,1e-10);check_near(frame.geometry.parts[0].local.ty,point.y,1e-10);
            check_near(frame.geometry.parts[0].local.rotate,u,1e-10);
        }
        check(allocations.load()==before,"route sampling allocates no new storage");
        check(out.sample(plan,0,.5).geometry.parts[0].local.tx>20);
        check(out.sample(plan,0,1).geometry.parts[0].local.tx==0);
        std::array<float,1> values;take(plan.sample_scopes(seconds(.5),values));check(values[0]==.75f);
        const auto reused=take(PreparedProgram::prepare(repeat(2,route)));check(reused.route_segment_count()==2);
        Buffer repeated(reused.tracks()[0]);check_near(repeated.sample(reused,0,1.5).geometry.parts[0].local.tx,22.5,1e-8);
    });
    suite.test("routes compose the public point modifier and keep part selection and conflicts",[] {
        Fixture f;auto description=shape(3);description.parts.push_back({.id="child",.parent="shape",.local={.tx=4}});
        const auto source=take(f.registry.capture(take(f.registry.create(description))));
        MotionPath path;path.line_to({20,0});
        const auto plan=take(PreparedProgram::prepare(anim::follow_path(source,path,{.part="child"}).with_effect(fx::translation_path(PointPath::bend(.25)))));
        Buffer out(plan.tracks()[0]);const auto frame=out.sample(plan,0,.5).geometry;
        check(frame.parts[0].local.tx==3);check_near(frame.parts[1].local.tx,14,1e-10);check_near(frame.parts[1].local.ty,5,1e-10);
        const auto vertex=anim::follow_path(f.a,path).with_effect(fx::vertex_path(PointPath::bend(.2)));
        for(const auto& expression:{Program(vertex),parallel(vertex,anim::rotate_by(f.a,.1))}) {
            const auto rejected=PreparedProgram::prepare(expression);
            check(!rejected && rejected.error().caused_by(MorphError::unsupported_property_effect),
                "route-only translation does not silently accept a geometry deformation in either execution path");
        }
        const auto conflict=PreparedProgram::prepare(parallel(anim::follow_path(f.a,path),anim::move_by(f.a,{1,0})));
        check(!conflict && conflict.error().code==ProgramError::binding_conflict);
        const auto duplicate=PreparedProgram::prepare(anim::to(f.a,{.offset=Vec2{1,0},.route=TranslationRoute(path)}));
        check(!duplicate && duplicate.error().caused_by(MorphError::invalid_property));
    });
    suite.test("invalid routes and retained route budgets fail during preparation",[] {
        Fixture f;MotionPath empty,bad,split,route;bad.line_to({std::numeric_limits<double>::quiet_NaN(),0});
        split.line_to({1,0}).move_to({3,0}).line_to({4,0});route.line_to({1,0}).line_to({2,0});
        for(const auto* path:{&empty,&bad,&split})check(!PreparedProgram::prepare(anim::follow_path(f.a,*path)));
        const auto limited=PreparedProgram::prepare(anim::to(f.a,{.route=TranslationRoute(route)},{.max_route_segments=1}));
        check(!limited && limited.error().caused_by(MorphError::route_budget));
        const auto retained=PreparedProgram::prepare(repeat(2,anim::follow_path(f.a,route)),{.max_total_route_segments=3});
        check(!retained && retained.error().code==ProgramError::route_budget);
        check(PreparedProgram::prepare(anim::follow_path(f.a,route),{.max_total_route_segments=2}).has_value());
    });
    return suite.run(argc,argv);
}
