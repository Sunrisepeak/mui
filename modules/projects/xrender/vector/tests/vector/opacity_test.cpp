import std;
import mlibs.geom;
import xrender.raster;
import xrender.vector;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace render=xrender;
namespace geom=mlibs::geom;
namespace raster=xrender::raster;
namespace {
render::DisplayList scene(float amount,bool filter=true,bool individual=false,bool decorated=false) {
    render::DisplayList out;
    if(decorated)out.fill_rect({0,0,100,80},geom::Color::hex(0x183047));
    out.push_clip({5.25f,6.5f,83,64});
    const auto ref=*render::effects().find("opacity");
    const std::array values{std::pair<std::string_view,float>{"amount",amount}};
    const auto params=render::effects().params(ref,values);
    out.push_layer(decorated?.75f:1.f,decorated?render::BlendMode::multiply:render::BlendMode::normal,
        filter?ref:render::EffectRef{},filter?std::span<const float>(params):std::span<const float>{},{8,8,72,56});
    const float alpha=individual?amount:1.f;
    out.fill_rect({8,8,48,48},geom::Color{1,0,0,alpha});
    out.fill_rect({32,16,48,48},geom::Color{0,0,1,alpha});
    out.pop_layer();out.pop_clip();return out;
}
raster::Surface draw(const render::DisplayList& list,float dpi=1) {
    render::CpuRenderer renderer;raster::Surface pixels(int(100*dpi),int(80*dpi));
    const auto report=renderer.render(list,pixels,{}, {.scale=dpi});
    expect(report.effects_skipped==0 && report.invalid_paints==0 && list.balanced());return pixels;
}
}
static UTest cases{"render.opacity", [] {
    "group opacity preserves overlap semantics and premultiplied endpoints"_test = [] {
        const auto full=draw(scene(1)),half=draw(scene(.5)),hidden=draw(scene(0));
        expect(full.hash()==draw(scene(1,false)).hash());
        expect(half.at(20,20).a==128 && half.at(40,20).a==128) << "the composed layer fades once even where children overlap";
        expect(draw(scene(.5,false,true)).at(40,20).a>half.at(40,20).a) << "changing individual paint alpha is a different operation";
        expect(std::ranges::all_of(hidden.pixels(),[](auto p){return p.r==0 && p.g==0 && p.b==0 && p.a==0;}));
        expect(std::ranges::all_of(half.pixels(),[](auto p){return p.r<=p.a && p.g<=p.a && p.b<=p.a;}));
    };
    "identity retains outer opacity blend clipping and fractional DPI"_test = [] {
        for(float dpi:{1.f,1.25f,2.f}) {
            expect(draw(scene(1,true,false,true),dpi).hash()==draw(scene(1,false,false,true),dpi).hash());
            expect(draw(scene(.5,true,false,true),dpi).hash()!=draw(scene(1,true,false,true),dpi).hash());
        }
    };
    "all opacity samples replay without changing capture bounds or source geometry"_test = [] {
        const auto info=render::effects().info(*render::effects().find("opacity"));
        expect(info && info->row_local && info->preserves_hit_geometry && info->gpu.fragment);
        std::array<std::uint64_t,61> hashes{};int previous=-1;
        for(int i=0;i<=60;++i) {
            const auto pixels=draw(scene(i/60.f));hashes[std::size_t(i)]=pixels.hash();
            const auto alpha=pixels.at(40,20).a;expect(alpha>=previous);previous=alpha;
            expect(pixels.at(2,2).a==0 && pixels.at(90,72).a==0);
        }
        for(int i=60;i>=0;--i)expect(draw(scene(i/60.f)).hash()==hashes[std::size_t(i)]);
    };
}};
