import std;
import xrender.vector;
import mlibs.geom;
import xrender.raster;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace render=xrender;
namespace raster=xrender::raster;
namespace geom=mlibs::geom;
namespace {
constexpr int width=180,height=140;
std::vector<float> blur(float radius) {
    return render::effects().params(*render::effects().find("blur"),std::array{std::pair<std::string_view,float>{"radius",radius}});
}
void scope(render::DisplayList& list,float radius,bool content) {
    const auto values=blur(radius);
    list.push_layer(1,render::BlendMode::normal,*render::effects().find("blur"),values,{0,0,width,height},{},
        content?render::LayerBoundsMode::content:render::LayerBoundsMode::declared);
}
render::DisplayList scene(bool content,float shift=0) {
    render::DisplayList list;list.begin_node(1);
    // Content mode does not need a manually supplied allocation rectangle.
    auto outer=blur(3);
    list.push_layer(1,render::BlendMode::normal,*render::effects().find("blur"),outer,
        content?geom::Rect{}:geom::Rect{0,0,width,height},{},content?render::LayerBoundsMode::content:render::LayerBoundsMode::declared);
    list.push_transform(geom::Affine::translate(30+shift,25)*geom::Affine::scale(1.5f,1.5f));
    list.begin_node(2);
    auto inner=blur(2);
    list.push_layer(1,render::BlendMode::normal,*render::effects().find("blur"),inner,
        content?geom::Rect{}:geom::Rect{-30,-25,width,height},{},content?render::LayerBoundsMode::content:render::LayerBoundsMode::declared);
    list.fill_rect({4,5,19,13},geom::Color::hex(0x3288dd));
    list.fill_rect({50,26,7,8},geom::Color::hex(0xf0a432));
    list.box_shadow({38,10,10,8},geom::CornerRadii::all(2),{2,1},3,1,geom::Color::hex(0x997744),false);
    list.pop_layer();list.end_node();list.pop_transform();list.pop_layer();list.end_node();return list;
}
raster::Surface draw(const render::DisplayList& list,float scale=1) {
    raster::Surface pixels(static_cast<int>(width*scale),static_cast<int>(height*scale));render::CpuRenderer cpu;
    auto report=cpu.render(list,pixels,{}, {.scale=scale});expect(report.effects_skipped==0);return pixels;
}
}
UTest cases{"render.layer-content", [] {
    "nested filter sizes are output logical pixels independent of object transform"_test = [] {
        for(float dpi:{1.f,2.f}) {
            render::DisplayList scaled,flattened;
            scaled.push_transform(geom::Affine::translate(20,20)*geom::Affine::scale(2,2));
            for(float radius:{2.f,3.f}) {
                auto values=blur(radius);
                scaled.push_layer(1,render::BlendMode::normal,*render::effects().find("blur"),values,{0,0,30,30});
                flattened.push_layer(1,render::BlendMode::normal,*render::effects().find("blur"),values,{20,20,60,60});
            }
            scaled.fill_rect({5,5,10,10},geom::Color::white());flattened.fill_rect({30,30,20,20},geom::Color::white());
            for(int i=0;i<2;++i){scaled.pop_layer();flattened.pop_layer();}scaled.pop_transform();
            expect(draw(scaled,dpi).hash()==draw(flattened,dpi).hash()) << "recursive offscreen capture does not multiply the filter DPI by object scale";
        }
    };
    "content bounds retain displaced children shadows and nested blur at each DPI"_test = [] {
        for(float dpi:{1.f,2.f}) {
            auto automatic=scene(true),reference=scene(false);
            render::LayerBoundsMap measured;expect(measured.measure(automatic,geom::Affine::scale(dpi,dpi),dpi));
            expect(measured.entries().size()==2);
            const auto* outer=measured.at(1);expect(outer && outer->area.width>80*dpi && outer->area.width<150*dpi);
            expect(draw(automatic,dpi).hash()==draw(reference,dpi).hash()) << "automatic capture matches a generously bounded real render";
        }
    };
    "identity content scopes preserve ordinary group clipping and recorded copies"_test = [] {
        render::DisplayList plain,identity;
        for(auto* list:{&plain,&identity}) {
            list->push_clip({20,20,80,60},geom::CornerRadii::all(11));
            if(list==&identity) {auto values=blur(0);list->push_layer(1,render::BlendMode::normal,*render::effects().find("blur"),values,{}, {},render::LayerBoundsMode::content);}
            else list->push_layer(1);
            list->fill_rect({10,10,110,90},geom::Color::hex(0x7744dd));
            list->pop_layer();list->pop_clip();
        }
        expect(draw(plain).hash()==draw(identity).hash());
        render::DisplayList copy;copy.append(identity);expect(copy.hash()==identity.hash());expect(draw(copy).hash()==draw(identity).hash());
    };
    "incremental damage covers the old and new generated output of moving content scopes"_test = [] {
        render::DamageTracker damage;render::CpuRenderer cpu;raster::Surface retained(width,height);
        for(float shift:{0.f,9.f,-11.f,3.f}) {
            auto list=scene(true,shift);const auto rows=damage.update(list,width,height,1);
            auto report=cpu.render(list,retained,{}, {.rows=rows});expect(report.effects_skipped==0);
            expect(retained.hash()==draw(list).hash()) << "partial redraw equals a full output including every halo";
        }
    };
    "content measurement rejects unbalanced scopes and publishes no valid lookup"_test = [] {
        render::DisplayList list;scope(list,2,true);list.fill_rect({10,10,5,5},geom::Color::white());
        render::LayerBoundsMap measured;expect(!measured.measure(list,{},1));expect(measured.at(0)==nullptr);
        list.pop_layer();expect(measured.measure(list,{},1));expect(measured.at(0)!=nullptr);
    };
}};
