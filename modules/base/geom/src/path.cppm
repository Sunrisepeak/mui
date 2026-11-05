// Neutral 2D paths shared by rendering, outlines and animation adapters.
export module mlibs.geom.api:path;
import std;
import :primitives;

export namespace mlibs::geom {

enum class PathVerb : std::uint8_t { move, line, quad, cubic, close };
enum class FillRule : std::uint8_t { nonzero, even_odd };
struct PathView {
    std::span<const PathVerb> verbs;
    std::span<const Point> points;
};
struct MutablePathView {
    std::span<PathVerb> verbs;
    std::span<Point> points;
};
struct PathLimits {
    std::size_t max_verbs = 1u << 20, max_points = 3u << 20, max_contours = 1u << 16;
};
enum class PathError : std::uint8_t {
    verb_budget, point_budget, contour_budget, invalid_verb, missing_move,
    missing_points, trailing_points, nonfinite_coordinate
};
struct PathFailure {
    PathError code;
    std::size_t verb = 0, point = 0;
};
struct PathInfo {
    std::size_t contours = 0, segments = 0, closes = 0;
};
[[nodiscard]] constexpr std::string_view path_error_name(PathError code) noexcept {
    switch(code) {
        case PathError::verb_budget:return "verb-budget";
        case PathError::point_budget:return "point-budget";
        case PathError::contour_budget:return "contour-budget";
        case PathError::invalid_verb:return "invalid-verb";
        case PathError::missing_move:return "missing-move";
        case PathError::missing_points:return "missing-points";
        case PathError::trailing_points:return "trailing-points";
        case PathError::nonfinite_coordinate:return "nonfinite-coordinate";
    }return "unknown";
}
// Empty is valid. After close, a drawing command begins a new contour at the
// preceding contour's start, matching the renderer's existing path semantics.
// This validates encoding and finite coordinates, not simplicity or topology.
[[nodiscard]] constexpr std::expected<PathInfo,PathFailure>
inspect_path(PathView path,PathLimits limits = {}) noexcept {
    if(path.verbs.size()>limits.max_verbs)return std::unexpected(PathFailure{PathError::verb_budget});
    if(path.points.size()>limits.max_points)return std::unexpected(PathFailure{PathError::point_budget});
    PathInfo result;std::size_t point=0;bool moved=false,open=false;
    for(std::size_t i=0;i<path.verbs.size();++i) {
        const auto verb=path.verbs[i];std::size_t count=0;
        switch(verb) {
            case PathVerb::move:count=1;break;
            case PathVerb::line:count=1;break;
            case PathVerb::quad:count=2;break;
            case PathVerb::cubic:count=3;break;
            case PathVerb::close:break;
            default:return std::unexpected(PathFailure{PathError::invalid_verb,i,point});
        }
        if(verb==PathVerb::move)moved=true;
        if(!moved)return std::unexpected(PathFailure{PathError::missing_move,i,point});
        if(count>path.points.size()-point)return std::unexpected(PathFailure{PathError::missing_points,i,point});
        for(std::size_t j=0;j<count;++j) {
            const auto p=path.points[point+j];
            if(!std::isfinite(p.x) || !std::isfinite(p.y))
                return std::unexpected(PathFailure{PathError::nonfinite_coordinate,i,point+j});
        }
        if(verb==PathVerb::move || (!open && verb!=PathVerb::close)) {
            if(result.contours>=limits.max_contours)
                return std::unexpected(PathFailure{PathError::contour_budget,i,point});
            ++result.contours;open=true;
        }
        if(verb==PathVerb::close){++result.closes;open=false;}
        else if(verb!=PathVerb::move)++result.segments;
        point+=count;
    }
    if(point!=path.points.size())return std::unexpected(PathFailure{PathError::trailing_points,path.verbs.size(),point});
    return result;
}

// Command and operand indices refer to the captured source path, not to a
// display label. Adapters attach their own object identity/version to them.
struct PathCommand {
    PathVerb verb;
    std::size_t index=0, point_index=0, contour=0;
    std::span<const Point> operands;
    Point from{}, end{}, contour_start{};
    bool begins_contour=false;
};
// A validated borrowed reader: keep the source arrays alive AND unchanged.
// make() preflights the complete encoding before any command is exposed.
// next() allocates nothing and preserves explicit and implicit contour starts.
class PathReader {
public:
    [[nodiscard]] static constexpr std::expected<PathReader,PathFailure>
    make(PathView path,PathLimits limits = {}) noexcept {
        const auto info=inspect_path(path,limits);
        if(!info)return std::unexpected(info.error());
        return PathReader(path,*info);
    }
    [[nodiscard]] constexpr PathInfo info() const noexcept {return info_;}
    [[nodiscard]] constexpr std::optional<PathCommand> next() noexcept {
        if(verb_==path_.verbs.size())return std::nullopt;
        const auto verb=path_.verbs[verb_];
        const std::size_t count=verb==PathVerb::close?0:verb==PathVerb::quad?2:verb==PathVerb::cubic?3:1;
        const auto operands=path_.points.subspan(point_,count);
        const bool begins=verb==PathVerb::move || (!open_ && verb!=PathVerb::close);
        if(begins) {
            if(started_)++contour_;started_=true;open_=true;
            start_=verb==PathVerb::move?operands[0]:current_;
        }
        const Point from=verb==PathVerb::move?operands[0]:current_;
        const Point end=verb==PathVerb::close?start_:operands.back();
        PathCommand command{verb,verb_++,point_,contour_,operands,from,end,start_,begins};
        point_+=count;current_=end;
        if(verb==PathVerb::close)open_=false;
        return command;
    }
private:
    constexpr PathReader(PathView path,PathInfo info):path_(path),info_(info){}
    PathView path_;
    PathInfo info_;
    std::size_t verb_=0,point_=0,contour_=0;
    Point current_{},start_{};
    bool started_=false,open_=false;
};
// Flattened path contours in the requested coordinate space.
struct PathPolylines {
    std::vector<Point>       points;
    std::vector<std::size_t> starts;  // index of each polygon's first point
    std::vector<bool>        closed;  // the path closed it explicitly (strokes join its ends)

    [[nodiscard]] std::size_t count() const noexcept { return starts.size(); }
    [[nodiscard]] std::span<const Point> polygon(std::size_t i) const noexcept {
        std::size_t b = starts[i], e = i + 1 < starts.size() ? starts[i + 1] : points.size();
        return {points.data() + b, e - b};
    }
    void begin() {
        starts.push_back(points.size());
        closed.push_back(false);
    }
    void clear() noexcept {
        points.clear();
        starts.clear();
        closed.clear();
    }
};

// Low-level append operation used by the rasterizer. The source must have a
// valid encoding (inspect_path provides validation), transform/coordinates
// must be finite and tolerance must be finite and positive. It uses the
// existing deterministic formula, capped at 256 chords per curve. The cap is
// an approximation policy, not a guaranteed tolerance for arbitrary scales.
// This first shared conversion retains its existing output/order; adaptive
// error certificates and per-generated-vertex provenance remain separate work.
void flatten_path(PathView path,const Affine& transform,PathPolylines& output,float tolerance=1.0f/32.0f);

} // namespace mlibs::geom
