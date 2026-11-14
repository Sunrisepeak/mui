// Console-only public consumer: no UI Runtime, window or GUI startup.
import std;
import hanim.engine;
import hanim.render.vector;
import xrender.raster;
import mlibs.image;
import mlibs.testing;
import mcpplibs.mtest;
using namespace hanim;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace render = xrender;
namespace raster = xrender::raster;
namespace geom = mlibs::geom;
template<class T,class E> T take(std::expected<T,E> value) {
    expect(value.has_value()); if (!value) std::abort(); return std::move(*value);
}
UTest cases{"hanim.engine.visual", [] {
    "without a GUI the public player renders real material/effect pixels and a precise crop"_test = [] {
        auto engine = take(Engine::make());
        constexpr double k=0.5522847498307936;
        const std::array triangle{ShapeSegment::line({-1,1}),ShapeSegment::line({1,1})};
        const std::array circle{ShapeSegment::cubic({k,-1},{1,-k},{1,0}),ShapeSegment::cubic({1,k},{k,1},{0,1}),
            ShapeSegment::cubic({-k,1},{-1,k},{-1,0}),ShapeSegment::cubic({-1,-k},{-k,-1},{0,-1})};
        auto a=take(Contour::copy({{0,-1},triangle})), b=take(Contour::copy({{0,-1},circle}));
        auto describe=[](Contour shape) {return ObjectDescription{{{.id="shape",.local={.tx=64,.ty=64,.sx=40,.sy=40},.contours={shape},.fill=Rgba{1,0,0,1}}}};};
        auto source=take(engine.create_object(describe(a))), target=take(engine.create_object(describe(b)));
        const auto played=take(engine.play(anim::morph(source, target).with_effects(fx::chain({fx::blur(0,1.5f),fx::tone({}, {.r=0.3f,.g=0.8f,.b=1,.amount=0.35f})}))));
        const auto first=take(engine.frame(played));
        std::array<render::GradientStop,2> stops{{{0,{0.1f,0.8f,1,1}},{1,{0.8f,0.2f,0.9f,1}}}};
        render::Paint gradient;gradient.kind=render::Paint::Kind::gradient;gradient.gradient.start={-1,-1};gradient.gradient.end={1,1};gradient.gradient.stops=stops;
        const std::array materials{vector_render::MaterialBinding{"shape",render::Paint::solid({1,.55f,.1f,1}),gradient}};
        auto binding=take(vector_render::PreparedVector::bind(*first.animation,*first.effects,materials));
        const auto count=binding.requirements();
        std::vector<geom::Affine> world(count.world_transforms);
        std::vector<float> parameters(count.backend_parameters);
        std::vector<geom::Rect> layers(count.effect_layers);
        render::DisplayList recording,list;render::CpuRenderer renderer;
        raster::Surface pixels(128,128), strip(384,128);
        const auto output=scratch_dir("hanim-engine-visual");
        std::array<std::string,3> hashes;
        int index=0;
        for (double progress:{0.,.5,1.}) {
            expect(engine.seek_progress(played,progress).has_value());
            const auto frame=take(engine.frame(played)); list.clear();
            expect(binding.record(frame.geometry,frame.effect_values,list,{&recording,world,parameters,layers}).has_value());
            const auto report=renderer.render(list,pixels,{}, {.background=geom::Color::hex(0x111827)});
            expect(!report.invalid_paints && !report.missing_images && pixels.at(64,64).a>0);
            Hash hash;
            for(int y=0;y<128;++y) {
                hash.bytes(std::as_bytes(std::span(pixels.row(y),128)));
                std::copy_n(pixels.row(y),128,strip.row(y)+index*128);
            }
            hashes[index]=hash.hex();
            const mlibs::image::View full{reinterpret_cast<const std::uint8_t*>(pixels.row(0)),128,128,128*4,mlibs::image::Alpha::premultiplied};
            expect(mlibs::image::save_png(output/std::format("frame-{}.png",index),full).has_value());
            if(index==1) {
                const mlibs::image::View crop{reinterpret_cast<const std::uint8_t*>(pixels.row(32)+32),64,64,128*4,mlibs::image::Alpha::premultiplied};
                expect(mlibs::image::save_png(output/"region.png",crop).has_value());
                auto decoded=mlibs::image::decode_png(take(mlibs::image::encode_png(crop)));
                expect(decoded.has_value() && decoded->width==64 && decoded->height==64);
                // Decode is straight-alpha; compare an opaque interior witness.
                const auto center=pixels.at(64,64);
                const auto offset=(32*64+32)*4;
                expect(decoded->rgba[offset]==center.r && decoded->rgba[offset+1]==center.g && decoded->rgba[offset+2]==center.b && decoded->rgba[offset+3]==center.a);
            }
            ++index;
        }
        expect(hashes[0]!=hashes[1] && hashes[1]!=hashes[2] && hashes[0]!=hashes[2]);
        const mlibs::image::View sheet{reinterpret_cast<const std::uint8_t*>(strip.row(0)),384,128,384*4,mlibs::image::Alpha::premultiplied};
        expect(mlibs::image::save_png(output/"sequence.png",sheet).has_value());
        std::println("headless visual: {} source owner={} slot={} target slot={} viewport=128x128 region=32,32,64,64 backend=CPU times=0,.5,1 hashes={},{},{}",
            output.generic_string(),source.owner,source.slot,target.slot,hashes[0],hashes[1],hashes[2]);
    };
    "a nested multitrack program renders every current phase through prebound native adapters"_test = [] {
        auto engine=take(Engine::make());
        constexpr double k=.5522847498307936;
        const std::array triangle{ShapeSegment::line({-1,1}),ShapeSegment::line({1,1})};
        const std::array circle{ShapeSegment::cubic({k,-1},{1,-k},{1,0}),ShapeSegment::cubic({1,k},{k,1},{0,1}),
            ShapeSegment::cubic({-k,1},{-1,k},{-1,0}),ShapeSegment::cubic({-1,-k},{-k,-1},{0,-1})};
        const auto tri=take(Contour::copy({{0,-1},triangle})), round=take(Contour::copy({{0,-1},circle}));
        auto object=[&](Contour shape,double x,Rgba color) {
            return take(engine.create_object({{{.id="shape",.local={.tx=x,.ty=64,.sx=24,.sy=24},.contours={shape},.fill=color}}}));
        };
        const auto a=object(tri,40,{1,.3f,.1f,1}),b=object(round,64,{.2f,.6f,1,1}),c=object(tri,90,{.7f,.3f,.9f,1});
        const auto d=object(tri,164,{.1f,.8f,.5f,1}),e=object(round,194,{1,.8f,.2f,1});
        const auto source=take(engine.capture_object(a));
        const auto plan=take(PreparedProgram::prepare(parallel({
            sequence({bind(source,take(engine.capture_object(b)),{fx::blur(0,1)}),
                      bind(source,take(engine.capture_object(c)),{fx::blur(1,0)})}),
            bind(take(engine.capture_object(d)),take(engine.capture_object(e)),{}, {.duration=seconds(2)})})));
        const auto playback=take(engine.play(plan));
        struct Binding {
            vector_render::PreparedVector plan;
            std::vector<geom::Affine> world;
            std::vector<float> parameters;
            std::vector<geom::Rect> layers;
        };
        std::vector<Binding> bindings;
        for(const auto& segment:plan.segments()) {
            Binding bound;bound.plan=take(vector_render::PreparedVector::bind(segment.animation,segment.effects));
            const auto count=bound.plan.requirements();bound.world.resize(count.world_transforms);
            bound.parameters.resize(count.backend_parameters);bound.layers.resize(count.effect_layers);bindings.push_back(std::move(bound));
        }
        render::DisplayList list,recording;render::CpuRenderer renderer;
        raster::Surface pixels(240,128),strip(1200,128);
        auto draw=[&](double time) {
            expect(engine.seek(playback,seconds(time)).has_value());list.clear();
            for(std::size_t track=0;track<plan.tracks().size();++track) {
                const auto frame=take(engine.frame(playback,track));auto& binding=bindings[frame.segment_index];
                expect(binding.plan.record(frame.geometry,frame.effect_values,list,{&recording,binding.world,binding.parameters,binding.layers}).has_value());
            }
            const auto result=renderer.render(list,pixels,{}, {.background=geom::Color::hex(0x111827)});
            expect(!result.invalid_paints && !result.missing_images && !result.effects_skipped);
            Hash hash;for(int y=0;y<128;++y)hash.bytes(std::as_bytes(std::span(pixels.row(y),240)));
            return hash.hex();
        };
        std::array<std::string,5> hashes;
        for(int i=0;i<5;++i) {
            hashes[i]=draw(i*.5);
            for(int y=0;y<128;++y)std::copy_n(pixels.row(y),240,strip.row(y)+240*i);
        }
        for(int i=0;i<5;++i)for(int j=i+1;j<5;++j)expect(hashes[i]!=hashes[j]);
        for(int i:{3,0,4,1,2})expect(draw(i*.5)==hashes[i]) << "unordered seek must reproduce actual composite pixels";
        const auto output=scratch_dir("hanim-program-visual");
        const mlibs::image::View sheet{reinterpret_cast<const std::uint8_t*>(strip.row(0)),1200,128,1200*4,mlibs::image::Alpha::premultiplied};
        expect(mlibs::image::save_png(output/"sequence.png",sheet).has_value());
        std::println("headless program: {} tracks=2 segments=3 times=0,.5,1,1.5,2 hashes={},{},{},{},{}",
            output.generic_string(),hashes[0],hashes[1],hashes[2],hashes[3],hashes[4]);
    };
}};
