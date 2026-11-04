// Console-only public consumer: no UI Runtime, window or GUI startup.
import std;
import hanim.engine;
import hanim.render.vector;
import openmui.raster;
import openmui.image;
import openmui.testing;
using namespace hanim;
using namespace omui::testing;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;
template<class T,class E> T take(std::expected<T,E> value) {
    check(value.has_value()); if (!value) std::abort(); return std::move(*value);
}
int main(int argc,char** argv) {
    Suite suite{"hanim.engine.visual"};
    suite.test("without a GUI the public player renders real material/effect pixels and a precise crop", [] {
        auto engine = take(Engine::make());
        constexpr double k=0.5522847498307936;
        const std::array triangle{ShapeSegment::line({-1,1}),ShapeSegment::line({1,1})};
        const std::array circle{ShapeSegment::cubic({k,-1},{1,-k},{1,0}),ShapeSegment::cubic({1,k},{k,1},{0,1}),
            ShapeSegment::cubic({-k,1},{-1,k},{-1,0}),ShapeSegment::cubic({-1,-k},{-k,-1},{0,-1})};
        auto a=take(Contour::copy({{0,-1},triangle})), b=take(Contour::copy({{0,-1},circle}));
        auto describe=[](Contour shape) {return ObjectDescription{{{.id="shape",.local={.tx=64,.ty=64,.sx=40,.sy=40},.contours={shape},.fill=Rgba{1,0,0,1}}}};};
        auto source=take(engine.create_object(describe(a))), target=take(engine.create_object(describe(b)));
        const auto played=take(engine.play(source,anim::morph_to(target),fx::chain({fx::blur(0,1.5f),fx::tone({}, {.r=0.3f,.g=0.8f,.b=1,.amount=0.35f})})));
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
            check(engine.seek_progress(played,progress).has_value());
            const auto frame=take(engine.frame(played)); list.clear();
            check(binding.record(frame.geometry,frame.effect_values,list,{&recording,world,parameters,layers}).has_value());
            const auto report=renderer.render(list,pixels,{}, {.background=geom::Color::hex(0x111827)});
            check(!report.invalid_paints && !report.missing_images && pixels.at(64,64).a>0);
            Hash hash;
            for(int y=0;y<128;++y) {
                hash.bytes(std::as_bytes(std::span(pixels.row(y),128)));
                std::copy_n(pixels.row(y),128,strip.row(y)+index*128);
            }
            hashes[index]=hash.hex();
            const omui::imageio::View full{reinterpret_cast<const std::uint8_t*>(pixels.row(0)),128,128,128*4,omui::imageio::Alpha::premultiplied};
            check(omui::imageio::save_png(output/std::format("frame-{}.png",index),full).has_value());
            if(index==1) {
                const omui::imageio::View crop{reinterpret_cast<const std::uint8_t*>(pixels.row(32)+32),64,64,128*4,omui::imageio::Alpha::premultiplied};
                check(omui::imageio::save_png(output/"region.png",crop).has_value());
                auto decoded=omui::imageio::decode_png(take(omui::imageio::encode_png(crop)));
                check(decoded.has_value() && decoded->width==64 && decoded->height==64);
                // Decode is straight-alpha; compare an opaque interior witness.
                const auto center=pixels.at(64,64);
                const auto offset=(32*64+32)*4;
                check(decoded->rgba[offset]==center.r && decoded->rgba[offset+1]==center.g && decoded->rgba[offset+2]==center.b && decoded->rgba[offset+3]==center.a);
            }
            ++index;
        }
        check(hashes[0]!=hashes[1] && hashes[1]!=hashes[2] && hashes[0]!=hashes[2]);
        const omui::imageio::View sheet{reinterpret_cast<const std::uint8_t*>(strip.row(0)),384,128,384*4,omui::imageio::Alpha::premultiplied};
        check(omui::imageio::save_png(output/"sequence.png",sheet).has_value());
        std::println("headless visual: {} source owner={} slot={} target slot={} viewport=128x128 region=32,32,64,64 backend=CPU times=0,.5,1 hashes={},{},{}",
            output.generic_string(),source.owner,source.slot,target.slot,hashes[0],hashes[1],hashes[2]);
    });
    return suite.run(argc,argv);
}
