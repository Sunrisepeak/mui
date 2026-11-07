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
    suite.test("SVG path data: every command, absolute and relative, numbers as SVG writes them", [] {
        using namespace mlibs::geom;
        const auto square = parse_path_data("M10 10 H50 V50 H10 Z");
        check(square && square->verbs.size() == 5 && square->points.back() == Point{10, 50}, "a square of H and V");
        const auto relative = parse_path_data("m10 10 l40 0 l0 40 l-40 0 z");
        check(relative && relative->points == square->points, "relative is the same square");
        const auto repeated = parse_path_data("M0 0 10 0 10 10");  // pairs after a moveto are linetos
        check(repeated && repeated->verbs == std::vector<PathVerb>{PathVerb::move, PathVerb::line, PathVerb::line}, "implicit linetos");
        const auto numbers = parse_path_data("M.5.5-.5-.5L1e1,2E0");
        check(numbers && numbers->points.size() == 3 && numbers->points[1] == Point{-.5f, -.5f} && numbers->points[2] == Point{10, 2}, "packed numbers");
        const auto smooth = parse_path_data("M0 0 C10 0 20 10 20 20 S30 40 40 40");
        check(smooth && smooth->points[4] == Point{20, 30}, "S reflects the last control point about the current one");
        const auto quadratic = parse_path_data("M0 0 Q10 10 20 0 T40 0");
        check(quadratic && quadratic->verbs.back() == PathVerb::quad && quadratic->points[3] == Point{30, -10}, "T reflects the quadratic's control");
        // A circle of radius 10 about (20, 20) as two half arcs: every point of every cubic on it, within a curve's error.
        const auto circle = parse_path_data("M10 20 A10 10 0 1 1 30 20 A10 10 0 1 1 10 20 Z");
        check(circle.has_value(), circle ? "" : circle.error().message);
        if (circle) {
            check(std::ranges::count(circle->verbs, PathVerb::cubic) == 4, "a quarter turn a cubic");
            double worst = 0;
            for (std::size_t k = 1; k + 1 < circle->points.size(); k += 3) {
                const auto p = circle->points[k + 2];
                worst = std::max(worst, std::abs(std::hypot(p.x - 20.0, p.y - 20.0) - 10));
            }
            check(worst < 1e-4, std::format("the arcs' ends on the circle: {}", worst));
            check(circle->points[6] == Point{30, 20}, "the first half ends exactly where it said");
        }
        const auto flags = parse_path_data("M0 0a5 5 0 1010 0");
        check(flags && flags->points.back() == Point{10, 0}, "flags without separators");
        const auto line = parse_path_data("M0 0 A0 5 0 0 1 10 0");
        check(line && line->verbs.back() == PathVerb::line, "a degenerate arc is a line");
        for (const auto& [bad, at] : std::array{std::pair{std::string_view("L10 10"), std::size_t{1}}, std::pair{std::string_view("M0 0 X5"), std::size_t{6}},
                                                std::pair{std::string_view("M0 0 C1 2 3"), std::size_t{11}}, std::pair{std::string_view(""), std::size_t{0}}}) {
            const auto refused = parse_path_data(bad);
            check(!refused && refused.error().offset == at, std::format("`{}` refused at {}: {}", bad, at, refused ? "accepted" : std::format("{} ({})", refused.error().message, refused.error().offset)));
        }
    });
    return suite.run(argc,argv);
}
