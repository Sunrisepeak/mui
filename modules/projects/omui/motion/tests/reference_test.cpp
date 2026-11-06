// Layout relationships are data. They neither add painted pieces nor erase
// a selection's internal progress when current allocations change.
import std;
import hanim.core;
import openmui.motion;
import xrender.vector;
import xrender.raster;
import mlibs.testing;

using namespace omui::motion;
using namespace mlibs::testing;
namespace rv=xrender;
namespace ra=xrender::raster;

namespace {
Rect slot(float width,bool right) {const float cell=(width-6)/2;return {right?3+cell:3,3,cell,26};}
Resolver resolver() {return {.keys=[](std::uint32_t n,std::string& key,std::string&){key=std::to_string(n);}};}
rv::DisplayList scene(float width,bool right,bool metadata=true) {
    rv::DisplayList list;list.begin_node(1);list.node_frame(1,{0,0,width,32});
    list.fill_round_rect({0,0,width,32},CornerRadii::all(8),rv::Paint::solid(Color::hex(0x64748b)));
    if(metadata) {
        const std::string left="left",right_key="right",role="selection";
        list.reference_box(1,left,slot(width,false));list.reference_box(1,right_key,slot(width,true));
        const rv::ReferenceWeight basis{right?std::string_view(right_key):std::string_view(left),1};
        list.begin_part({1,role,"",slot(width,right),{0.5f,0.5f},rv::PartKind::box,std::span(&basis,1)});
    }
    list.fill_round_rect(slot(width,right),CornerRadii::all(5),rv::Paint::solid(Color::white()));
    if(metadata)list.end_part();list.end_node();return list;
}
Options options(bool follow=false) {Options c;c.rebase_layout=follow;c.curve.kind=Curve::Kind::tween;c.curve.duration=1;c.curve.ease=hanim::ease::linear;return c;}
bool near(Rect a,Rect b) {return std::abs(a.x-b.x)<0.002f && std::abs(a.y-b.y)<0.002f && std::abs(a.width-b.width)<0.002f && std::abs(a.height-b.height)<0.002f;}
Rect lens(const rv::DisplayList& list) {
    Rect found;std::size_t count=0;Affine transform;std::vector<Affine> stack;
    for(const auto& command:list.commands()) {
        if(const auto* push=std::get_if<rv::PushTransform>(&command)){stack.push_back(transform);transform=transform*push->transform;}
        else if(std::holds_alternative<rv::PopTransform>(command)){check(!stack.empty());if(!stack.empty()){transform=stack.back();stack.pop_back();}}
        else if(const auto* fill=std::get_if<rv::FillRoundRect>(&command);fill && fill->paint.color.r>0.99f && fill->paint.color.g>0.99f && fill->paint.color.b>0.99f){found=transform.map(fill->rect);++count;}
    }
    check(count==1,"one actual white selection carrier is emitted");check(stack.empty());return found;
}
const Part* selection(const Matter& matter) {
    for(const auto& piece:matter.pieces())if(piece.part>=0) {
        const auto& part=matter.parts()[static_cast<std::size_t>(piece.part)];
        if(part.role=="selection")return &part;
    }
    return nullptr;
}
double weight(const Part& part,std::string_view key) {for(const auto& basis:part.lineage)if(basis.key==key)return basis.weight;return 0;}
}

int main(int argc,char** argv) {
    Suite suite{"motion.references"};
    suite.test("reference metadata preserves native pixels matching statistics bounds and stagger",[] {
        auto a=scene(320,false,false),b=scene(320,true,false),ma=scene(320,false),mb=scene(320,true);
        check(a.hash()==ma.hash() && b.hash()==mb.hash(),"metadata is outside visual hashes");
        auto config=options();config.stagger=0.07;config.order=Order::document;config.stretch=0.16f;
        Morph plain(Matter::extract(a,resolver()),Matter::extract(b,resolver()),config,0),meta(Matter::extract(ma,resolver()),Matter::extract(mb,resolver()),config,0);
        check(plain.end()==meta.end() && plain.stats().matched==meta.stats().matched && plain.stats().from_only==meta.stats().from_only && plain.stats().to_only==meta.stats().to_only);
        check(plain.to().pieces().size()==meta.to().pieces().size() && plain.to().bounds()==meta.to().bounds(),"no extra part drawable or visual centre");
        for(const double t:{0.0,0.03,0.07,0.31,0.58,1.2}) {
            rv::DisplayList p,m;plain.draw(t,p);meta.draw(t,m);check(p.balanced() && m.balanced());
            ra::Surface ps(340,50),ms(340,50);rv::CpuRenderer renderer;
            (void)renderer.render(p,ps,{},{});(void)renderer.render(m,ms,{},{});
            check(std::ranges::equal(ps.pixels(),ms.pixels()),"native output at the same time is pixel-identical with metadata enabled");
        }
    });
    suite.test("an unchanged selected reference follows new allocation without phantom stretch",[] {
        auto a=scene(320,true),b=scene(1120,true);auto config=options(true);config.stretch=0.16f;
        Morph morph(Matter::extract(a,resolver()),Matter::extract(b,resolver()),config,0);
        for(const double t:{0.0,0.04,0.2,0.5,1.0}) {
            rv::DisplayList out;morph.draw(t,out);check(out.balanced());
            check(near(lens(out),slot(1120,true)),"state is unchanged: layout motion cannot send its lens through other options");
        }
    });
    suite.test("interrupted state reference lineage survives repeated responsive allocations",[] {
        auto a=scene(320,false),b=scene(320,true);Morph first(Matter::extract(a,resolver()),Matter::extract(b,resolver()),options(),0);
        auto snapshot=first.snapshot(0.35);const auto* part=selection(snapshot);check(part!=nullptr);if(!part)return;
        check(part->lineage.size()==2 && std::abs(weight(*part,"left")-0.65)<1e-9 && std::abs(weight(*part,"right")-0.35)<1e-9,"snapshot captures actual state progress, not merely the final option name");
        auto wide=scene(1120,true);Morph second(std::move(snapshot),Matter::extract(wide,resolver()),options(true),1);
        rv::DisplayList start;second.draw(1,start);auto expected=slot(1120,false);expected.x+=0.35f*(slot(1120,true).x-expected.x);
        check(near(lens(start),expected),"remaining state motion is retained in current slot coordinates");
        auto interrupted=second.snapshot(1.4);part=selection(interrupted);check(part!=nullptr);if(!part)return;
        check(part->lineage.size()==2 && std::abs(weight(*part,"left")-0.39)<1e-9 && std::abs(weight(*part,"right")-0.61)<1e-9,"retarget combines lineage without duplicate-reference growth");
        auto medium=scene(640,true);Morph third(std::move(interrupted),Matter::extract(medium,resolver()),options(true),2);
        rv::DisplayList again;third.draw(2,again);expected=slot(640,false);expected.x+=0.61f*(slot(640,true).x-expected.x);
        check(near(lens(again),expected),"a second resize cannot collapse unfinished state progress to its endpoint");
    });
    suite.test("arena copies retain reference names lineage and existing pixel ownership",[] {
        auto original=scene(320,true);rv::DisplayList copy;copy.append(original);original.clear();
        original.reference_box(1,"overwrite-source-storage",{1,2,3,4});
        const auto matter=Matter::extract(copy,resolver());const auto* part=selection(matter);
        check(copy.balanced() && part && part->role=="selection" && part->lineage.size()==1 && part->lineage.front().key=="right");
        check(matter.references().size()==2 && matter.pieces().size()==2,"reference lifetime never depends on temporary strings or another list's arena");
        ra::Surface pixels(340,50);rv::CpuRenderer renderer;rv::CpuOptions config;config.object_ids=true;
        (void)renderer.render(copy,pixels,{},config);const auto at=slot(320,true).center();
        check(renderer.object_ids()[static_cast<std::size_t>(at.y)*340+static_cast<std::size_t>(at.x)]==1,"paint parts never invent or erase node pixel ownership");
    });
    suite.test("sampled allocation lineage survives interruption separately from visual state",[] {
        auto placed=[](float x,float state) {
            rv::DisplayList list;list.begin_node(1);list.push_transform(Affine::translate(x,20)*Affine::translate(0,state));
            list.node_frame(1,{0,0,80,32},1,{x,20,80,32},{400,200},Affine::translate(0,state));
            list.fill_rect({0,0,80,32},rv::Paint::solid(Color::white()));list.pop_transform();list.end_node();return list;
        };
        const auto a=placed(20,2),b=placed(120,10),c=placed(200,14);
        Morph first(Matter::extract(a,resolver()),Matter::extract(b,resolver()),options(),0);
        auto source=first.snapshot(.25);check(source.frames().size()==1);
        if(source.frames().empty())return;
        check(source.frames()[0].sampled_placement_origin==Point{45,20});
        check(source.frames()[0].transform.apply(Point{})==Point{45,24},"visual state is additional to sampled allocation");
        Morph second(std::move(source),Matter::extract(c,resolver()),options(),1);
        auto halfway=second.snapshot(1.5);check(halfway.frames().size()==1);
        if(halfway.frames().empty())return;
        check(halfway.frames()[0].sampled_placement_origin==Point{122.5f,20},"interruption retains actual origin, not the old target");
        check(halfway.frames()[0].transform.apply(Point{})==Point{122.5f,29});
        auto target=Matter::extract(c,resolver());halfway.follow_layout(target);
        check(halfway.frames()[0].sampled_placement_origin==Point{200,20});
        check(halfway.frames()[0].transform.apply(Point{})==Point{200,29},"live allocation replacement keeps unfinished visual feedback");
    });
    return suite.run(argc,argv);
}
