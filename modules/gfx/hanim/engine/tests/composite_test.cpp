// Public program/player/renderer consumer, with real CPU pixels and no UI.
import std;
import hanim.engine;
import hanim.render.vector;
import openmui.raster;
import openmui.image;
import openmui.trace;
import openmui.testing;
using namespace hanim;
using namespace hanim::vector_render;
using namespace omui::testing;
namespace render=omui::render;
namespace geom=omui::geom;
namespace raster=omui::raster;
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
template<class T,class E>T take(std::expected<T,E> result) {
    check(bool(result));if(!result)std::abort();
    if constexpr(!std::is_void_v<T>)return std::move(*result);
}
struct Fixture {
    Engine engine=take(Engine::make());
    ObjectRef a=box(16,{1,0,0,.6f}), b=box(28,{0,0,1,.6f}), outside=box(74,{0,1,0,1});
    ObjectRef box(double x,Rgba fill) {
        const std::array edges{ShapeSegment::line({24,0}),ShapeSegment::line({24,24}),ShapeSegment::line({0,24})};
        return take(engine.create_object({{{.id="shape",.local={.tx=x,.ty=16},
            .contours={take(Contour::copy({{},edges,true}))},.fill=fill}}}));
    }
    anim::Morph hold(ObjectRef object) {return anim::morph(object,object);}
};
struct Storage {
    render::DisplayList staging;
    std::vector<geom::Affine> world;
    std::vector<float> parameters;
    std::vector<geom::Rect> layers,bounds;
    std::vector<std::size_t> members,order,stack;
    explicit Storage(ProgramVectorRequirements n):world(n.buffers.world_transforms),parameters(n.buffers.backend_parameters),
        layers(n.buffers.effect_layers),bounds(n.tracks+n.scopes),members(n.scopes),order(n.tracks),stack(n.scope_depth) {}
    ProgramVectorScratch get() {return {{&staging,world,parameters,layers},bounds,members,order,stack};}
};
raster::Surface draw(const render::DisplayList& list) {
    raster::Surface pixels(112,64);render::CpuRenderer cpu;
    const auto result=cpu.render(list,pixels,{});
    check(list.balanced() && !result.effects_skipped && !result.invalid_paints && !result.missing_images);
    return pixels;
}
void leaf(const PreparedProgram& plan,ProgramFrameView frame,std::size_t track,render::DisplayList& output) {
    const auto& view=frame.tracks[track];const auto& segment=plan.segments()[view.segment_index];
    const auto binding=take(PreparedVector::bind(segment.animation,segment.effects));const auto n=binding.requirements();
    render::DisplayList staging;std::vector<geom::Affine> world(n.world_transforms);
    std::vector<float> parameters(n.backend_parameters);std::vector<geom::Rect> layers(n.effect_layers);
    take(binding.record(view.geometry,view.effect_values,output,{&staging,world,parameters,layers}));
}
std::size_t layers(const render::DisplayList& list) {
    return static_cast<std::size_t>(std::ranges::count_if(list.commands(),[](const auto& command){return std::holds_alternative<render::PushLayer>(command);}));
}
}
int main(int argc,char** argv) {
    Suite suite{"hanim.composite"};
    suite.test("one composite blur matches explicit shared layers and differs from per-leaf blur",[] {
        Fixture f;const auto plan=take(f.engine.prepare(parallel(
            composite(parallel(f.hold(f.a),f.hold(f.b))).with_effect(fx::blur(0,4)),f.hold(f.outside))));
        const auto playback=take(f.engine.play(plan));take(f.engine.seek_progress(playback,.5));
        const auto renderer=take(PreparedVectorProgram::bind(plan));Storage storage(renderer.requirements());render::DisplayList output,expected;
        const auto frame=take(f.engine.program_frame(playback)).scene;take(renderer.record(frame,output,storage.get()));
        check(layers(output)==1,"the composite is processed once, not broadcast to two leaves");
        const std::array<float,1> radius{2};
        expected.push_layer(1,render::BlendMode::normal,*render::effects().find("blur"),radius,{16,16,36,24});
        leaf(plan,frame,0,expected);leaf(plan,frame,1,expected);expected.pop_layer();leaf(plan,frame,2,expected);
        const auto pixels=draw(output);check(pixels.hash()==draw(expected).hash());
        take(f.engine.release(playback));
        const auto separate=take(f.engine.prepare(parallel(f.hold(f.a).with_effect(fx::blur(0,4)),
            f.hold(f.b).with_effect(fx::blur(0,4)),f.hold(f.outside))));
        const auto other=take(f.engine.play(separate));take(f.engine.seek_progress(other,.5));
        const auto per_leaf=take(PreparedVectorProgram::bind(separate));Storage other_storage(per_leaf.requirements());render::DisplayList second;
        take(per_leaf.record(take(f.engine.program_frame(other)).scene,second,other_storage.get()));
        check(draw(second).hash()!=pixels.hash(),"overlapping alpha distinguishes group blur from individual blur");
        check(pixels.at(74,16).g==255 && pixels.at(73,16).a==0 && pixels.at(75,17).a==255,"outside object keeps its sharp edges");
        check(pixels.at(14,24).a>0,"group bounds include the filter reach");
        const auto dir=scratch_dir("hanim-composite");
        const omui::imageio::View view{reinterpret_cast<const std::uint8_t*>(pixels.row(0)),112,64,112*4,omui::imageio::Alpha::premultiplied};
        take(omui::imageio::save_png(dir/"group-blur.png",view));
        std::println("composite CPU pixels: {} hash={} layers=1",(dir/"group-blur.png").generic_string(),pixels.hash());
    });
    suite.test("nested scopes preserve leaf-before-inner-before-outer order and expanded bounds",[] {
        Fixture f;const auto plan=take(f.engine.prepare(composite(parallel(
            f.hold(f.a).with_effect(fx::blur(1,1)),composite(f.hold(f.b)).with_effect(fx::blur(2,2))))
            .with_effect(fx::blur(3,3)).with_effect(fx::tone({.r=.2f,.g=.4f,.b=1,.amount=.4f},{.r=.2f,.g=.4f,.b=1,.amount=.4f}))));
        const auto ref=take(f.engine.play(plan));const auto frame=take(f.engine.program_frame(ref)).scene;
        const auto renderer=take(PreparedVectorProgram::bind(plan));Storage storage(renderer.requirements());render::DisplayList output;
        take(renderer.record(frame,output,storage.get()));check(layers(output)==4);
        std::vector<render::EffectRef> opening;
        for(const auto& command:output.commands())if(const auto* layer=std::get_if<render::PushLayer>(&command))opening.push_back(layer->effect);
        check(opening[0].id==render::effects().find("tone")->id && opening[1].id==render::effects().find("blur")->id);
        const auto requirements=renderer.requirements();check(requirements.scopes==2 && requirements.scope_depth==2);
        check(storage.bounds[2].x<16 && storage.bounds[2].right()>52,"outer bounds enclose expanded child and leaf output");
        const auto initial=draw(output).hash();take(f.engine.seek_progress(ref,1));output.clear();
        take(renderer.record(take(f.engine.program_frame(ref)).scene,output,storage.get()));check(draw(output).hash()==initial);
    });
    suite.test("empty composite scopes are exact identities and transform-only groups create no layers",[] {
        Fixture f;const auto plain=take(f.engine.prepare(parallel(f.hold(f.a),f.hold(f.b))));
        const auto wrapped=take(f.engine.prepare(composite(parallel(composite(f.hold(f.a)),composite(f.hold(f.b))))));
        std::array<std::uint64_t,2> hashes{};std::size_t i=0;
        for(const auto& plan:{plain,wrapped}) {
            const auto ref=take(f.engine.play(plan));const auto renderer=take(PreparedVectorProgram::bind(plan));
            Storage storage(renderer.requirements());render::DisplayList output;
            take(renderer.record(take(f.engine.program_frame(ref)).scene,output,storage.get()));check(layers(output)==0);
            hashes[i++]=draw(output).hash();take(f.engine.release(ref));
        }
        check(hashes[0]==hashes[1]);
        const auto transform=take(f.engine.create_object({{{.id="camera"}}}));
        const auto plan=take(f.engine.prepare(composite(f.hold(transform)).with_effect(fx::blur(2,2))));
        const auto ref=take(f.engine.play(plan));const auto renderer=take(PreparedVectorProgram::bind(plan));Storage storage(renderer.requirements());render::DisplayList output;
        take(renderer.record(take(f.engine.program_frame(ref)).scene,output,storage.get()));check(output.commands().empty());
    });
    suite.test("scope membership follows selected segments and author order across seek boundaries",[] {
        Fixture f;const auto plan=take(f.engine.prepare(sequence(
            composite(parallel(f.hold(f.a),f.hold(f.b))).with_effect(fx::blur(1,1)),
            composite(parallel(f.hold(f.b),f.hold(f.a))).with_effect(fx::blur(4,4)))));
        const auto ref=take(f.engine.play(plan));const auto renderer=take(PreparedVectorProgram::bind(plan));Storage storage(renderer.requirements());render::DisplayList output;
        std::array<std::uint64_t,3> hashes{};std::size_t i=0;
        for(double time:{.5,1.,.5}) {
            take(f.engine.seek(ref,seconds(time)));output.clear();const auto frame=take(f.engine.program_frame(ref)).scene;
            take(renderer.record(frame,output,storage.get()));check(layers(output)==1);
            check(storage.order[0]==(time<1?0u:1u),"selected author order, not stable track identity, defines compositing");
            hashes[i++]=draw(output).hash();
        }
        check(hashes[0]!=hashes[1] && hashes[0]==hashes[2]);
    });
    suite.test("program bind diagnoses unsupported scopes and accounts backend expansion",[] {
        Fixture f;
        for(const auto& effect:std::array<EffectSpec,3>{{{"missing",{}},{"blur",{{"typo",0,1}}},{"liquid_glass",{}}}}) {
            const auto plan=take(f.engine.prepare(composite(f.hold(f.a)).with_effect(effect)));
            const auto result=PreparedVectorProgram::bind(plan);check(!result && result.error().scope==0);
        }
        const auto plan=take(f.engine.prepare(composite(f.hold(f.a)).with_effect({"tone",{}})));
        check(PreparedVectorProgram::bind(plan,{.max_prepared_backend_parameters=0}).error().code==VectorError::capacity,
            "default backend parameters count even if no parameter was authored");
        check(PreparedVectorProgram::bind(plan,{.max_prepared_effect_layers=0}).error().code==VectorError::capacity);
    });
    suite.test("invalid frames buffers and scope values preserve the destination",[] {
        Fixture f;const auto plan=take(f.engine.prepare(composite(parallel(f.hold(f.a),f.hold(f.b))).with_effect(fx::blur(1,2))));
        const auto ref=take(f.engine.play(plan));const auto renderer=take(PreparedVectorProgram::bind(plan));Storage storage(renderer.requirements());
        const auto frame=take(f.engine.program_frame(ref)).scene;render::DisplayList output;
        output.fill_rect({0,0,2,2},render::Paint::solid({0,1,0,1}));const auto before=output.hash();
        auto bad=frame;bad.preparation=nullptr;check(renderer.record(bad,output,storage.get()).error().code==VectorError::foreign_frame);
        auto scratch=storage.get();scratch.scope_stack={};check(renderer.record(frame,output,scratch).error().code==VectorError::buffer_too_small);
        scratch=storage.get();scratch.order=scratch.scope_members;check(renderer.record(frame,output,scratch).error().code==VectorError::recording_alias);
        std::array<float,1> invalid{201};bad=frame;bad.scope_effect_values=invalid;
        const auto error=renderer.record(bad,output,storage.get());check(!error && error.error().scope==0 && error.error().code==VectorError::invalid_parameter);
        std::vector<ProgramTrackView> views(frame.tracks.begin(),frame.tracks.end());std::swap(views[0],views[1]);bad=frame;bad.tracks=views;
        check(renderer.record(bad,output,storage.get()).error().code==VectorError::foreign_frame);
        check(output.hash()==before);
        const auto blur=*render::effects().find("blur");const auto original=*render::effects().info(blur);
        check(render::effects().replace(blur,original));
        const auto stale=renderer.record(frame,output,storage.get());
        check(render::effects().replace(blur,original));
        check(!stale && stale.error().code==VectorError::stale_effect && stale.error().scope==0 && output.hash()==before);
    });
    suite.test("prepared scoped playback query and warmed recording allocate nothing",[] {
        Fixture f;const auto plan=take(f.engine.prepare(composite(parallel(f.hold(f.a),f.hold(f.b))).with_effect(fx::blur(0,4))));
        const auto ref=take(f.engine.play(plan));const auto renderer=take(PreparedVectorProgram::bind(plan));Storage storage(renderer.requirements());render::DisplayList output;
        omui::trace::start();
        for(int i=0;i<16;++i) {take(f.engine.seek_progress(ref,.5));output.clear();take(renderer.record(take(f.engine.program_frame(ref)).scene,output,storage.get()));}
        const auto before=allocations.load();bool good=true;
        for(int i=0;i<512;++i) {
            good &= f.engine.seek_progress(ref,static_cast<double>(i%97)/96).has_value();
            const auto frame=f.engine.program_frame(ref);output.clear();
            good &= frame.has_value() && renderer.record(frame->scene,output,storage.get()).has_value();
        }
        const auto after=allocations.load();(void)omui::trace::stop();check(good && before==after);
    });
    return suite.run(argc,argv);
}
