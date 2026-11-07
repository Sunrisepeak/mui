import std;
import hanim.library;
import mlibs.testing;
import mlibs.trace;

using namespace hanim;
using namespace mlibs::testing;
namespace {
std::atomic<std::size_t> allocations{0};
struct Allocation { void* pointer; std::size_t size; };
std::array<Allocation,1024> recorded{};
std::atomic<bool> recording{false};
std::atomic<std::size_t> recorded_count{0};
std::array<std::atomic<void*>,3> watching{};
std::array<std::atomic<bool>,3> freed{};
void* allocation_containing(const void* address) {
    const auto value=reinterpret_cast<std::uintptr_t>(address);
    for (std::size_t i=0;i<std::min(recorded_count.load(),recorded.size());++i) {
        const auto base=reinterpret_cast<std::uintptr_t>(recorded[i].pointer);
        if (value>=base && value-base<recorded[i].size) return recorded[i].pointer;
    }
    return nullptr;
}
void observe_free(void* pointer) {
    for (std::size_t i=0;i<watching.size();++i) if (pointer && pointer==watching[i].load()) freed[i].store(true);
}
}
namespace {
void record_allocation(void* pointer,std::size_t size) noexcept {
    allocations.fetch_add(1,std::memory_order_relaxed);
    if(pointer && recording.load()) {const auto i=recorded_count.fetch_add(1);if(i<recorded.size())recorded[i]={pointer,size};}
}
void* allocate(std::size_t size) noexcept {
    auto* pointer=std::malloc(size?size:1);record_allocation(pointer,size);return pointer;
}
void* allocate_aligned(std::size_t size,std::align_val_t alignment) noexcept {
    const auto align=static_cast<std::size_t>(alignment);
    if(align<alignof(void*) || (align&(align-1)))return nullptr;
    const auto overhead=align-1+sizeof(void*);
    if(size>std::numeric_limits<std::size_t>::max()-overhead-1)return nullptr;
    auto* base=std::malloc((size?size:1)+overhead);if(!base)return nullptr;
    auto address=reinterpret_cast<std::uintptr_t>(base)+sizeof(void*);
    address=(address+align-1)&~(static_cast<std::uintptr_t>(align)-1);
    auto* pointer=reinterpret_cast<void*>(address);static_cast<void**>(pointer)[-1]=base;
    record_allocation(pointer,size);return pointer;
}
void release(void* pointer) noexcept {observe_free(pointer);std::free(pointer);}
void release_aligned(void* pointer) noexcept {observe_free(pointer);if(pointer)std::free(static_cast<void**>(pointer)[-1]);}
void* require(void* pointer) noexcept {if(!pointer)std::abort();return pointer;}
}
// Pair every allocation family: ASan's nothrow allocation must not be paired
// accidentally with a replacement delete whose allocator is malloc/free.
void* operator new(std::size_t n){return require(allocate(n));}
void* operator new[](std::size_t n){return require(allocate(n));}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{return allocate(n);}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{return allocate(n);}
void* operator new(std::size_t n,std::align_val_t a){return require(allocate_aligned(n,a));}
void* operator new[](std::size_t n,std::align_val_t a){return require(allocate_aligned(n,a));}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{return allocate_aligned(n,a);}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{return allocate_aligned(n,a);}
void operator delete(void* p)noexcept{release(p);}
void operator delete[](void* p)noexcept{release(p);}
void operator delete(void* p,std::size_t)noexcept{release(p);}
void operator delete[](void* p,std::size_t)noexcept{release(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{release(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{release(p);}
void operator delete(void* p,std::align_val_t)noexcept{release_aligned(p);}
void operator delete[](void* p,std::align_val_t)noexcept{release_aligned(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{release_aligned(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{release_aligned(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{release_aligned(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{release_aligned(p);}
namespace {
template<class T,class E> T take(std::expected<T,E> result) { check(bool(result)); if(!result) std::abort(); return std::move(*result); }
Contour triangle(bool closed=true) {
    const std::array segments{ShapeSegment::line({-1,1}),ShapeSegment::line({1,1})};
    return take(Contour::copy({{0,-1},segments,closed}));
}
Contour circle(double radius=1,bool reverse=false) {
    constexpr double k=.5522847498307936;
    std::array<ShapeSegment,4> segments{{
        ShapeSegment::cubic({k,-1},{1,-k},{1,0}),ShapeSegment::cubic({1,k},{k,1},{0,1}),
        ShapeSegment::cubic({-k,1},{-1,k},{-1,0}),ShapeSegment::cubic({-1,-k},{-k,-1},{0,-1})}};
    if(reverse) segments={{ShapeSegment::cubic({-k,-1},{-1,-k},{-1,0}),ShapeSegment::cubic({-1,k},{-k,1},{0,1}),
                          ShapeSegment::cubic({k,1},{1,k},{1,0}),ShapeSegment::cubic({1,-k},{k,-1},{0,-1})}};
    for(auto& s:segments) for(auto* p:{&s.end,&s.control1,&s.control2}) { p->x*=radius; p->y*=radius; }
    return take(Contour::copy({{0,-radius},segments}));
}
ObjectDescription description(Contour geometry,double tx=0,bool child_first=false) {
    PartDescription root; root.id="root"; root.local.tx=tx;
    PartDescription shape; shape.id="shape"; shape.parent="root"; shape.local.tx=tx*2;
    shape.contours={geometry}; shape.fill=Rgba{1,0,0,1};
    ObjectDescription value;
    if(child_first) value.parts={shape,root}; else value.parts={root,shape};
    return value;
}
struct Fixture {
    ObjectRegistry registry=take(ObjectRegistry::make());
    ObjectRef a,b;
    Snapshot from,to;
    Fixture() {
        a=take(registry.create(description(triangle(),0,true)));
        auto target=description(circle(),10); target.parts[1].fill=Rgba{0,0,1,.5f};
        b=take(registry.create(target)); from=take(registry.capture(a)); to=take(registry.capture(b));
    }
    Snapshot add(ObjectDescription data) { return take(registry.capture(take(registry.create(data)))); }
    PreparedObjectAnimation plan() { return take(PreparedObjectAnimation::prepare(from,to)); }
};
struct Buffer {
    std::vector<FramePart> parts;
    std::vector<ShapeCubic> cubics;
    explicit Buffer(const PreparedObjectAnimation& plan):parts(plan.requirements().parts),cubics(plan.requirements().cubics) {}
    FrameBuffer output() { return {parts,cubics}; }
    FrameView sample(const PreparedObjectAnimation& plan,double progress) { return take(plan.sample(progress,output())); }
};
bool near(double a,double b,double tolerance=1e-10) { return std::abs(a-b)<=tolerance; }
bool same(Vec2 a,Vec2 b) { return std::bit_cast<std::uint64_t>(a.x)==std::bit_cast<std::uint64_t>(b.x) && std::bit_cast<std::uint64_t>(a.y)==std::bit_cast<std::uint64_t>(b.y); }
bool same(const ShapeCubic& a,const ShapeCubic& b) { return same(a.start,b.start)&&same(a.control1,b.control1)&&same(a.control2,b.control2)&&same(a.end,b.end); }
bool on_triangle(Vec2 p) {
    constexpr std::array<Vec2,3> vertices{{{0,-1},{-1,1},{1,1}}};
    for(std::size_t i=0;i<3;++i) {
        auto a=vertices[i],b=vertices[(i+1)%3];
        if(near((p.x-a.x)*(b.y-a.y)-(p.y-a.y)*(b.x-a.x),0) && p.x>=std::min(a.x,b.x)-1e-10 && p.x<=std::max(a.x,b.x)+1e-10 && p.y>=std::min(a.y,b.y)-1e-10 && p.y<=std::max(a.y,b.y)+1e-10) return true;
    }
    return false;
}
void check_connected(FrameView frame) {
    for(const auto& range:frame.contours) {
        const auto curves=frame.cubics.subspan(range.first_cubic,range.cubic_count);
        check(!curves.empty());
        for(std::size_t i=0;i<curves.size();++i) check(curves[i].end==curves[(i+1)%curves.size()].start);
    }
}
template<class T> void error_is(const std::expected<T,MorphFailure>& result,MorphError code) {
    check(!result); if(!result) check(result.error().code==code,describe(result.error().code).name);
}
// The fixture's convex cubics are monotone on their endpoint-dominant axis.
// This independent geometric witness permits new de Casteljau cuts without
// demanding the old command count/order or hiding discontinuity with a fade.
double distance_to(Vec2 point,std::span<const ShapeCubic> original) {
    double best=1e100;
    for(const auto& curve:original) {
        const bool x=std::abs(curve.end.x-curve.start.x)>std::abs(curve.end.y-curve.start.y);
        auto axis=[&](Vec2 p){return x?p.x:p.y;};
        const bool increasing=axis(curve.end)>axis(curve.start);
        if(axis(point)<std::min(axis(curve.start),axis(curve.end))-1e-10 || axis(point)>std::max(axis(curve.start),axis(curve.end))+1e-10) continue;
        double low=0,high=1;
        for(int i=0;i<60;++i) { const double mid=(low+high)*.5; if((axis(curve.point(mid))<axis(point))==increasing) low=mid;else high=mid; }
        const auto p=curve.point((low+high)*.5);
        best=std::min(best,dm::sqrt((p.x-point.x)*(p.x-point.x)+(p.y-point.y)*(p.y-point.y)));
    }
    return best;
}
}
int main(int argc,char** argv) {
    Suite suite{"hanim.library.morph"};
    suite.test("allocation instrumentation pairs ordinary array nothrow and aligned families",[] {
        const auto before=allocations.load();
        ::operator delete(::operator new(1));::operator delete[](::operator new[](1));
        ::operator delete(::operator new(1),std::size_t{1});::operator delete[](::operator new[](1),std::size_t{1});
        ::operator delete(::operator new(1,std::nothrow),std::nothrow);::operator delete[](::operator new[](1,std::nothrow),std::nothrow);
        constexpr auto align=std::align_val_t{64};auto* p=::operator new(1,align);
        const bool aligned=reinterpret_cast<std::uintptr_t>(p)%64==0;::operator delete(p,align);
        ::operator delete[](::operator new[](1,align),align);
        ::operator delete(::operator new(1,align),std::size_t{1},align);::operator delete[](::operator new[](1,align),std::size_t{1},align);
        ::operator delete(::operator new(1,align,std::nothrow),align,std::nothrow);::operator delete[](::operator new[](1,align,std::nothrow),align,std::nothrow);
        check(aligned);check_eq(allocations.load()-before,std::size_t{12});
    });
    suite.test("explicit ids match reordered input arrays while source parent-first sibling semantics survive",[] {
        Fixture f; auto plan=f.plan(); Buffer buffer(plan);
        check(plan.requirements().parts==2 && plan.requirements().contours==1 && plan.requirements().cubics>=4);
        check(plan.parts()[0].id=="root" && plan.parts()[1].id=="shape");
        check(plan.parts()[0].parent==invalid_object_slot && plan.parts()[1].parent==0);
        check(plan.parts()[1].from_ref==take(f.from.find("shape")) && plan.parts()[1].to_ref==take(f.to.find("shape")));
        auto first=buffer.sample(plan,0); check(!first.parts[0].fill && first.parts[1].fill==std::optional<Rgba>(Rgba{1,0,0,1}));
        for(const auto& curve:first.cubics) for(double t:{0.,.25,.5,.75,1.}) check(on_triangle(curve.point(t)));
        auto last=buffer.sample(plan,1); check(last.parts[0].local.tx==10 && last.parts[1].local.tx==20);
        check(last.parts[1].fill==std::optional<Rgba>(Rgba{0,0,1,.5f}));
        for(const auto& curve:last.cubics) for(double t:{0.,.25,.5,.75,1.}) { const auto p=curve.point(t);check(near(dm::sqrt(p.x*p.x+p.y*p.y),1,.0004)); }
        check_connected(last);
        auto copy=plan; check(copy.identity()==plan.identity());
        check(last.preparation==plan.identity() && last.source_object==f.a && last.source_revision==f.from.revision());
        check(plan.from().object()==f.a && plan.to().object()==f.b);
    });
    suite.test("midpoint channels and arbitrary repeat seek are deterministic single current geometry",[] {
        Fixture f;auto plan=f.plan();Buffer output(plan);
        auto mid=output.sample(plan,.5);check(mid.parts[0].local.tx==5 && mid.parts[1].local.tx==10);
        check(mid.parts[1].fill && mid.parts[1].fill->a==.75f && *mid.parts[1].fill!=Rgba{1,0,0,1} && *mid.parts[1].fill!=Rgba{0,0,1,.5f});
        auto quarter=output.sample(plan,.25);std::vector<ShapeCubic> reference(quarter.cubics.begin(),quarter.cubics.end());
        for(double t:{1.,.8,0.,.5,.25}) check_connected(output.sample(plan,t));
        for(std::size_t i=0;i<reference.size();++i) check(same(reference[i],output.cubics[i]));
        check(output.sample(plan,-4).progress==0 && output.sample(plan,7).progress==1);
    });
    suite.test("strict capabilities reject part set ordering hierarchy count fill and open-contour changes",[] {
        Fixture f;
        auto missing=description(circle());missing.parts[1].id="different";
        error_is(PreparedObjectAnimation::prepare(f.from,f.add(missing)),MorphError::part_set_mismatch);
        auto extra=description(circle());extra.parts.push_back({.id="other",.parent="root"});
        auto source=description(triangle());source.parts.push_back({.id="other",.parent="root"});
        auto a=f.add(source);std::swap(extra.parts[1],extra.parts[2]);
        error_is(PreparedObjectAnimation::prepare(a,f.add(extra)),MorphError::ordering_mismatch);
        auto parent=source;parent.parts[2].parent="shape";
        error_is(PreparedObjectAnimation::prepare(a,f.add(parent)),MorphError::hierarchy_mismatch);
        ObjectDescription tree;
        tree.parts={{.id="root"},{.id="left",.parent="root"},{.id="right",.parent="root"},
                    {.id="a",.parent="left"},{.id="b",.parent="right"}};
        auto crossed=tree;crossed.parts[3].parent="right";crossed.parts[4].parent="left";
        error_is(PreparedObjectAnimation::prepare(f.add(tree),f.add(crossed)),MorphError::hierarchy_mismatch);
        auto count=description(circle());count.parts[1].contours.push_back(circle(.25));
        error_is(PreparedObjectAnimation::prepare(f.from,f.add(count),{.contours=ContourPairing::strict}),MorphError::contour_count_mismatch);
        auto fill=description(circle());fill.parts[1].fill.reset();
        error_is(PreparedObjectAnimation::prepare(f.from,f.add(fill)),MorphError::missing_fill);
        auto open=description(triangle(false));
        error_is(PreparedObjectAnimation::prepare(f.add(open),f.to),MorphError::open_contour);
    });
    suite.test("budgets and delegated core errors reject preparation without changing any snapshot",[] {
        Fixture f;const auto revision=take(f.registry.revision(f.a));
        error_is(PreparedObjectAnimation::prepare({},f.to),MorphError::invalid_snapshot);
        error_is(PreparedObjectAnimation::prepare(f.from,f.to,{.max_parts=0}),MorphError::invalid_options);
        error_is(PreparedObjectAnimation::prepare(f.from,f.to,{.max_parts=1}),MorphError::part_budget);
        auto double_shape=description(triangle());double_shape.parts[1].contours.push_back(triangle());
        auto capture=f.add(double_shape);
        error_is(PreparedObjectAnimation::prepare(capture,capture,{.max_contours=1}),MorphError::contour_budget);
        error_is(PreparedObjectAnimation::prepare(f.from,f.to,{.max_cubics=1}),MorphError::cubic_budget);
        MorphOptions options;options.shape.max_input_segments=0;
        auto core=PreparedObjectAnimation::prepare(f.from,f.to,options);
        error_is(core,MorphError::shape_failure);if(!core) check(core.error().caused_by(ShapeMorphError::invalid_options));
        const std::array flat{ShapeSegment::line({1,0}),ShapeSegment::line({2,0})};
        auto degenerate=description(take(Contour::copy({{0,0},flat})));
        auto bad=PreparedObjectAnimation::prepare(f.add(degenerate),f.to);
        error_is(bad,MorphError::shape_failure);if(!bad) check(bad.error().caused_by(ShapeMorphError::degenerate_contour));
        check(take(f.registry.revision(f.a))==revision);
    });
    suite.test("all sample errors preserve caller buffers including late overflowing transforms",[] {
        Fixture f;auto plan=f.plan();Buffer buffer(plan);
        for(auto& part:buffer.parts) {part.info.id="sentinel";part.local.tx=123;}
        for(auto& curve:buffer.cubics) curve.start={456,789};
        auto unchanged=[&] {for(const auto& p:buffer.parts) check(p.info.id=="sentinel" && p.local.tx==123);for(const auto& c:buffer.cubics)check(c.start==Vec2{456,789});};
        error_is(PreparedObjectAnimation{}.sample(.5,buffer.output()),MorphError::unprepared);unchanged();
        for(double bad:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}) {error_is(plan.sample(bad,buffer.output()),MorphError::invalid_progress);unchanged();}
        error_is(plan.sample(.5,{std::span(buffer.parts).first(1),buffer.cubics}),MorphError::output_too_small);unchanged();
        error_is(plan.sample(.5,{buffer.parts,std::span(buffer.cubics).first(1)}),MorphError::output_too_small);unchanged();
        auto x=description(triangle()),y=description(circle());x.parts[1].local.tx=-1.7e308;y.parts[1].local.tx=1.7e308;
        auto overflow=take(PreparedObjectAnimation::prepare(f.add(x),f.add(y)));
        error_is(overflow.sample(.5,buffer.output()),MorphError::nonfinite_transform);unchanged();
        // Linear translation is representable, but its path effect is not.
        x.parts[1].local.tx=0;y.parts[1].local.tx=1e308;
        MorphOptions curved;curved.translation_path=PointPath::bend(8);
        auto process_overflow=take(PreparedObjectAnimation::prepare(f.add(x),f.add(y),curved));
        error_is(process_overflow.sample(.5,buffer.output()),MorphError::nonfinite_transform);unchanged();
    });
    suite.test("empty groups remain usable and multiple contours preserve compound winding",[] {
        Fixture f;ObjectDescription empty;empty.parts.push_back({.id="group"});
        auto capture=f.add(empty);auto groups=take(PreparedObjectAnimation::prepare(capture,capture));Buffer no_geometry(groups);
        auto frame=no_geometry.sample(groups,.5);check(frame.cubics.empty() && frame.contours.empty() && !frame.parts[0].fill);
        auto source=description(circle());source.parts[1].contours.push_back(circle(.3,true));
        auto target=source;auto a=f.add(source),b=f.add(target);
        auto valid=take(PreparedObjectAnimation::prepare(a,b));Buffer two(valid);check(two.sample(valid,.5).contours.size()==2);
        target.parts[1].contours[1]=circle(.3,false);
        error_is(PreparedObjectAnimation::prepare(a,f.add(target),{.contours=ContourPairing::strict}),MorphError::unsupported_topology);
        // Aligned, the hole closes and the inner outline grows instead.
        auto aligned=take(PreparedObjectAnimation::prepare(a,f.add(target)));Buffer three(aligned);
        check(three.sample(aligned,.5).contours.size()==3,"a pair, a closing hole and a growing outline");
    });
    suite.test("endpoint resources and metadata outlive registries and the original plan owner",[] {
        PreparedObjectAnimation retained;
        { Fixture f;auto original=f.plan();retained=original;check(f.registry.destroy(f.a).has_value(),"destroy source");check(f.registry.destroy(f.b).has_value(),"destroy target"); }
        Buffer buffer(retained);auto frame=buffer.sample(retained,.75);
        check(frame.parts[1].info.id=="shape" && retained.from() && retained.to());check_connected(frame);
    });
    suite.test("C0 retarget deep owns current pose and releases old preparation and snapshot tables",[] {
        recorded_count=0;recording=true;
        Fixture f;auto old=f.plan();Buffer current(old);auto frame=current.sample(old,.37);
        auto target=f.add(description(triangle(),-8));
        const auto old_geometry=std::vector<ShapeCubic>(frame.cubics.begin(),frame.cubics.end());
        const auto old_tx=frame.parts[1].local.tx;const auto old_fill=frame.parts[1].fill;
        auto* old_payload=allocation_containing(old.identity());
        auto* old_a=allocation_containing(f.from.parts().data());auto* old_b=allocation_containing(f.to.parts().data());
        recording=false;check(old_payload && old_a && old_b);
        watching[0]=old_payload;watching[1]=old_a;watching[2]=old_b;for(auto& flag:freed)flag=false;
        auto next=take(PreparedObjectAnimation::retarget(frame,target));
        check(next.identity()!=old.identity() && !next.from() && next.to().object()==target.object());
        check(next.source_object()==f.a && next.source_revision()==f.from.revision());
        check(next.parts()[1].from_ref==frame.parts[1].info.from_ref && next.parts()[1].to_ref==take(target.find("shape")));
        check(f.registry.destroy(f.a).has_value() && f.registry.destroy(f.b).has_value());f.from={};f.to={};old={};
        for(auto& flag:freed)check(flag.load(),"retarget retains no old plan or endpoint snapshot table");
        for(auto& pointer:watching)pointer=nullptr;
        current.parts.clear();current.parts.shrink_to_fit();current.cubics.clear();current.cubics.shrink_to_fit();
        Buffer output(next);auto zero=output.sample(next,0);check(zero.parts[1].info.id=="shape" && zero.parts[1].local.tx==old_tx && zero.parts[1].fill==old_fill);
        for(const auto& curve:zero.cubics) for(double t:{0.,.25,.5,.75,1.})check(distance_to(curve.point(t),old_geometry)<1e-9,"retarget zero preserves actual Bezier geometry");
        for(double t:{.2,.5,.8})check_connected(output.sample(next,t));
    });
    suite.test("retarget rejects malformed ranges disconnected cubics and changed part contracts",[] {
        Fixture f;auto plan=f.plan();Buffer current(plan);auto frame=current.sample(plan,.5);
        auto target=f.add(description(triangle(),4));
        auto bad=frame;bad.preparation=nullptr;error_is(PreparedObjectAnimation::retarget(bad,target),MorphError::invalid_frame);
        auto info=std::vector<MorphContourInfo>(frame.contours.begin(),frame.contours.end());info[0].cubic_count+=1;bad=frame;bad.contours=info;
        error_is(PreparedObjectAnimation::retarget(bad,target),MorphError::invalid_frame);
        auto curves=current.cubics;curves[0].end.x+=.5;bad=frame;bad.cubics=curves;
        error_is(PreparedObjectAnimation::retarget(bad,target),MorphError::invalid_frame);
        auto missing=description(triangle());missing.parts[1].id="other";
        error_is(PreparedObjectAnimation::retarget(frame,f.add(missing)),MorphError::part_set_mismatch);
        auto old=plan.identity();check(plan.sample(.5,current.output()).has_value() && plan.identity()==old);
    });
    suite.test("warm metadata and caller-buffer sampling add no heap allocation even with active trace",[] {
        Fixture f;auto plan=f.plan();Buffer output(plan);(void)output.sample(plan,.25);
        mlibs::trace::start();(void)output.sample(plan,.25);
        const auto before=allocations.load();bool valid=true;double sum=0;
        for(unsigned i=0;i<1000;++i) {
            const auto frame=plan.sample(static_cast<double>(i%101)/100,output.output());
            valid &= frame.has_value() && plan.parts().size()==2 && plan.contours().size()==1 && plan.identity()==frame->preparation;
            sum+=frame->cubics[0].control1.x+frame->parts[1].local.tx;
        }
        const auto after=allocations.load();auto trace=mlibs::trace::stop();
        check(valid && sum!=0 && !trace.events.empty());check_eq(after,before);
    });
    if constexpr(threads) suite.test("concurrent arbitrary seeks use independent caller buffers",[] {
        Fixture f;auto plan=f.plan();
        auto seek=[&](double u) { Buffer buffer(plan);for(int i=0;i<100;++i) {auto frame=buffer.sample(plan,u);if(frame.parts[1].local.tx!=20*u)return false;}return true;};
        auto a=std::async(std::launch::async,[&]{return seek(.25);});auto b=std::async(std::launch::async,[&]{return seek(.75);});
        check(a.get() && b.get());
    });
    return suite.run(argc,argv);
}
