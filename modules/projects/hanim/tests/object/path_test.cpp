import std;
import hanim;
import mlibs.testing;
using namespace hanim;
using namespace mlibs::testing;
namespace geom=mlibs::geom;

namespace {
template<class T,class E>T take(std::expected<T,E> value) {
    check(value.has_value());if(!value)std::abort();
    if constexpr(!std::is_void_v<T>)return std::move(*value);
}
}
int main(int argc,char** argv) {
    Suite suite{"hanim.object.path"};
    suite.test("path import owns precise controls and maps every segment to its source command",[] {
        ImportedPath imported;
        {
            const std::array verbs{geom::PathVerb::move,geom::PathVerb::quad,geom::PathVerb::cubic,geom::PathVerb::close,
                geom::PathVerb::move,geom::PathVerb::line};
            const std::array points{geom::Point{1,2},geom::Point{3,4},geom::Point{5,6},geom::Point{7,8},
                geom::Point{9,10},geom::Point{11,12},geom::Point{13,14},geom::Point{15,16}};
            imported=take(import_path({verbs,points}));
        }
        check(imported.contours.size()==2);
        const auto& first=imported.contours[0];const auto a=take(first.geometry.view());
        check(a.start==Vec2{1,2} && a.closed && a.segments.size()==2);
        check(a.segments[0].verb==ShapeVerb::quadratic && a.segments[0].control1==Vec2{3,4} && a.segments[0].end==Vec2{5,6});
        check(a.segments[1].control2==Vec2{9,10} && a.segments[1].end==Vec2{11,12});
        check(first.first_command==0 && first.end_command==4 && first.first_point==0 && first.point_count==6);
        check(first.segment_commands==std::vector<std::size_t>{1,2});
        const auto& second=imported.contours[1];const auto b=take(second.geometry.view());
        check(!b.closed && b.start==Vec2{13,14} && b.segments[0].end==Vec2{15,16});
        check(second.first_command==4 && second.first_point==6 && second.point_count==2);
        check(second.segment_commands==std::vector<std::size_t>{5});
    });
    suite.test("implicit contours and empty moved contours preserve the original geometry semantics",[] {
        const std::array verbs{geom::PathVerb::move,geom::PathVerb::line,geom::PathVerb::close,
            geom::PathVerb::line,geom::PathVerb::move};
        const std::array points{geom::Point{2,3},geom::Point{5,6},geom::Point{8,9},geom::Point{10,11}};
        const auto imported=take(import_path({verbs,points}));check(imported.contours.size()==3);
        const auto a=take(imported.contours[1].geometry.view());
        check(a.start==Vec2{2,3} && !a.closed && a.segments[0].end==Vec2{8,9});
        check(imported.contours[1].segment_commands==std::vector<std::size_t>{3});
        const auto empty=take(imported.contours[2].geometry.view());check(empty.start==Vec2{10,11} && empty.segments.empty());
        check(take(import_path({})).contours.empty());
    });
    suite.test("source validation and import budgets retain structured diagnostics",[] {
        const std::array verbs{geom::PathVerb::move,geom::PathVerb::line,geom::PathVerb::line,geom::PathVerb::close};
        const std::array points{geom::Point{0,0},geom::Point{3,0},geom::Point{0,3}};
        const auto capacity=import_path({verbs,points},{.max_segments=1});check(!capacity && capacity.error().code==ObjectError::capacity);
        const auto invalid=import_path({verbs,std::span(points).first(2)});
        check(!invalid && invalid.error().code==ObjectError::invalid_path && invalid.error().caused_by(geom::PathError::missing_points));
        check(invalid.error().cause_of(geom::PathError::missing_points)->where=="verb 2, point 2");
        const auto limit=import_path({verbs,points},{.max_contours=0});check(!limit && limit.error().code==ObjectError::invalid_limits);
    });
    suite.test("imported paths feed the public object and morph plan with identical process geometry",[] {
        const std::array verbs{geom::PathVerb::move,geom::PathVerb::line,geom::PathVerb::line,geom::PathVerb::close};
        const std::array a{geom::Point{0,0},geom::Point{12,0},geom::Point{0,12}};
        const std::array b{geom::Point{0,0},geom::Point{24,0},geom::Point{0,24}};
        const auto from=take(import_path({verbs,a})),to=take(import_path({verbs,b}));
        auto engine=take(Engine::make());
        auto source=take(engine.create_object({{{.id="shape",.contours={from.contours[0].geometry},.fill=Rgba{1,0,0,1}}}}));
        auto target=take(engine.create_object({{{.id="shape",.contours={to.contours[0].geometry},.fill=Rgba{1,0,0,1}}}}));
        auto playback=take(engine.play(anim::morph(source,target)));
        const auto direct=take(PreparedShapeMorph::prepare(take(from.contours[0].geometry.view()),take(to.contours[0].geometry.view())));
        std::vector<ShapeCubic> reference(direct.segment_count());
        for(int i=0;i<=60;++i) {
            take(engine.seek_progress(playback,i/60.));const auto pose=take(engine.frame(playback));
            take(direct.sample(i/60.,reference));check(pose.geometry.cubics.size()==reference.size());
            for(std::size_t j=0;j<reference.size();++j) {
                const auto& actual=pose.geometry.cubics[j];const auto& expected=reference[j];
                check(actual.start==expected.start && actual.control1==expected.control1 && actual.control2==expected.control2 && actual.end==expected.end);
            }
        }
        take(engine.release(playback));
    });
    suite.test("paths become an object's parts, each keeping its id, place, fill and stroke",[] {
        // A glyph's outline as mlibs.text gives it, in ems: a square with a hole, and a space.
        const std::array verbs{geom::PathVerb::move,geom::PathVerb::line,geom::PathVerb::line,geom::PathVerb::line,geom::PathVerb::close,
            geom::PathVerb::move,geom::PathVerb::line,geom::PathVerb::line,geom::PathVerb::close};
        const std::array points{geom::Point{0,0},geom::Point{1,0},geom::Point{1,1},geom::Point{0,1},
            geom::Point{.25f,.25f},geom::Point{.75f,.25f},geom::Point{.5f,.75f}};
        const std::array parts{
            PathPart{.id="glyph-0",.local={.tx=4,.sx=40,.sy=40},.path={verbs,points},.fill=Rgba{1,1,1,1},.stroke=PartStroke{{1,0,0,1},.05}},
            PathPart{.id="glyph-1",.local={.tx=44}},
        };
        const auto object=take(object_from_paths("word",parts));
        check(object.parts.size()==3 && object.parts[0].id=="word" && object.parts[0].contours.empty());
        const auto& glyph=object.parts[1];
        check(glyph.id=="glyph-0" && glyph.parent=="word" && glyph.local.sx==40 && glyph.contours.size()==2);
        check(glyph.fill && glyph.stroke && glyph.stroke->width==.05 && glyph.stroke->color.r==1);
        check(object.parts[2].contours.empty() && !object.parts[2].fill,"a space is a part without contours");
        auto registry=take(ObjectRegistry::make());const auto made=take(registry.create(object));
        const auto snapshot=take(registry.capture(made));
        check(snapshot.capability_version("hanim.stroke")==1,"a stroked part declares the stroke capability");
        check(take(snapshot.get(take(snapshot.find("glyph-0"))))->stroke->width==.05);
        auto outlined=object;outlined.parts[1].fill.reset();
        check(registry.update(made,outlined).has_value(),"a part may be drawn by its stroke alone");
        auto unseen=object;unseen.parts[1].stroke->width=-1;
        check(!registry.update(made,unseen),"a negative stroke width is refused");
    });
    return suite.run(argc,argv);
}
