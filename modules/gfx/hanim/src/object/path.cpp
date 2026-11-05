module hanim.object;
import std;
import mlibs.log;
import mlibs.trace;

namespace hanim {
std::expected<ImportedPath,PathImportFailure> import_path(mlibs::geom::PathView path,PathImportLimits limits) {
    mlibs::trace::Span span("path.import","hanim.object");
    constexpr mlibs::log::Log diagnostic{"hanim.object"};
    auto refuse=[&](ObjectError code,std::optional<mlibs::geom::PathFailure> cause={},
                    std::size_t contour=std::numeric_limits<std::size_t>::max()) -> std::expected<ImportedPath,PathImportFailure> {
        diagnostic.debug("path import refused: {}",object_error_name(code));
        return std::unexpected(PathImportFailure{code,cause,contour});
    };
    if(!limits.max_contours || limits.max_contours>65536 || !limits.max_segments || limits.max_segments>(1u<<20))
        return refuse(ObjectError::invalid_limits);
    auto reader=mlibs::geom::PathReader::make(path,limits.source);
    if(!reader)return refuse(ObjectError::invalid_path,reader.error());
    const auto info=reader->info();
    if(info.contours>limits.max_contours || info.segments>limits.max_segments)return refuse(ObjectError::capacity);
    ImportedPath result;result.contours.reserve(info.contours);
    std::vector<ShapeSegment> segments;segments.reserve(std::min(info.segments,std::size_t{4096}));
    Vec2 start;bool closed=false;ImportedContour current;
    auto point=[](mlibs::geom::Point p){return Vec2{p.x,p.y};};
    auto finish=[&]() -> std::expected<void,ObjectError> {
        if(current.end_command==current.first_command)return {};
        auto contour=Contour::copy({start,segments,closed},65536);
        if(!contour)return std::unexpected(contour.error());
        current.geometry=std::move(*contour);result.contours.push_back(std::move(current));
        current={};segments.clear();return {};
    };
    while(const auto command=reader->next()) {
        if(command->begins_contour) {
            if(auto completed=finish();!completed)return refuse(completed.error(),{},command->contour-1);
            start=point(command->contour_start);closed=false;
            current.first_command=command->index;current.end_command=command->index;
            current.first_point=command->point_index;
        }
        current.end_command=command->index+1;
        current.point_count=command->point_index+command->operands.size()-current.first_point;
        switch(command->verb) {
            case mlibs::geom::PathVerb::move:break;
            case mlibs::geom::PathVerb::line:segments.push_back(ShapeSegment::line(point(command->end)));break;
            case mlibs::geom::PathVerb::quad:
                segments.push_back(ShapeSegment::quadratic(point(command->operands[0]),point(command->end)));break;
            case mlibs::geom::PathVerb::cubic:
                segments.push_back(ShapeSegment::cubic(point(command->operands[0]),point(command->operands[1]),point(command->end)));break;
            case mlibs::geom::PathVerb::close:closed=true;break;
        }
        if(command->verb!=mlibs::geom::PathVerb::move && command->verb!=mlibs::geom::PathVerb::close)
            current.segment_commands.push_back(command->index);
    }
    if(auto completed=finish();!completed)return refuse(completed.error(),{},result.contours.size());
    mlibs::trace::count("path.contours",static_cast<double>(result.contours.size()),"hanim.object");
    mlibs::trace::count("path.segments",static_cast<double>(info.segments),"hanim.object");
    return result;
}
} // namespace hanim
