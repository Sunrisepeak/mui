import std;
import mlibs.geom;
import mlibs.testing;
using namespace mlibs::geom;
using namespace mlibs::testing;

int main(int argc,char** argv) {
    Suite suite{"geom.path"};
    suite.test("one neutral reader preserves curve controls and source command indices",[] {
        constexpr std::array verbs{PathVerb::move,PathVerb::line,PathVerb::quad,PathVerb::cubic,PathVerb::close};
        constexpr std::array points{Point{1,2},Point{3,4},Point{5,6},Point{7,8},Point{9,10},Point{11,12},Point{13,14}};
        constexpr auto valid=inspect_path({verbs,points});
        static_assert(valid && valid->contours==1 && valid->segments==3 && valid->closes==1);
        auto reader=PathReader::make({verbs,points});check(reader.has_value());
        const auto move=reader->next(),line=reader->next(),quad=reader->next(),cubic=reader->next(),close=reader->next();
        check(move->begins_contour && move->from==points[0]);
        check(line->from==points[0] && line->end==points[1] && line->point_index==1);
        check(quad->index==2 && quad->point_index==2 && quad->operands.size()==2 && quad->operands[0]==points[2]);
        check(cubic->from==points[3] && cubic->end==points[6] && cubic->operands.size()==3);
        check(close->end==points[0] && close->point_index==7 && close->operands.empty());
        check(!reader->next() && !reader->next());
    });
    suite.test("close continuation and repeated moves retain explicit contour boundaries",[] {
        const std::array verbs{PathVerb::move,PathVerb::line,PathVerb::close,PathVerb::close,
            PathVerb::line,PathVerb::move,PathVerb::move,PathVerb::quad};
        const std::array points{Point{2,3},Point{4,5},Point{6,7},Point{8,9},Point{10,11},Point{12,13},Point{14,15}};
        auto reader=PathReader::make({verbs,points});check(reader && reader->info().contours==4);
        for(int i=0;i<4;++i)(void)reader->next();
        const auto continuation=reader->next();
        check(continuation->begins_contour && continuation->contour==1 && continuation->from==points[0]);
        check(continuation->end==points[2] && continuation->contour_start==points[0]);
        check(reader->next()->contour==2);check(reader->next()->contour==3);
        check(reader->next()->end==points[6]);
    });
    suite.test("malformed encodings and budgets fail before a reader is published",[] {
        const std::array points{Point{0,0},Point{1,1}};
        auto refused=[&](std::span<const PathVerb> verbs,std::span<const Point> values,PathError expected) {
            const auto reader=PathReader::make({verbs,values});check(!reader && reader.error().code==expected);
        };
        const std::array missing_move{PathVerb::line};refused(missing_move,std::span(points).first(1),PathError::missing_move);
        const std::array missing{PathVerb::move,PathVerb::cubic};refused(missing,points,PathError::missing_points);
        const std::array trailing{PathVerb::move};refused(trailing,points,PathError::trailing_points);
        const std::array unknown{PathVerb::move,static_cast<PathVerb>(255)};refused(unknown,points,PathError::invalid_verb);
        const std::array invalid{Point{std::numeric_limits<float>::infinity(),0}};refused(trailing,invalid,PathError::nonfinite_coordinate);
        const std::array valid{PathVerb::move,PathVerb::line};
        const auto verbs=PathReader::make({valid,points},{.max_verbs=1});check(!verbs && verbs.error().code==PathError::verb_budget);
        const auto operands=PathReader::make({valid,points},{.max_points=1});check(!operands && operands.error().code==PathError::point_budget);
        const auto contours=PathReader::make({valid,points},{.max_contours=0});check(!contours && contours.error().code==PathError::contour_budget);
        check(PathReader::make({})->info().contours==0);
    });
    return suite.run(argc,argv);
}
