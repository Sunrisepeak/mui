// openmui.raster:rasterizer — paths to anti-aliased coverage, exactly.
//
// Curves are flattened in floating point (operation order fixed, no fused
// multiply-add), endpoints are rounded to 1/256 pixel, and from there on the
// arithmetic is integer: each edge adds its cover and area to the cells it
// crosses (the scheme of FreeType's ftgrays), and a sweep per row turns the
// accumulated winding into coverage under the non-zero or even-odd rule. The
// same path gives the same bytes on every platform (D-43, spec R-2).
export module openmui.raster:rasterizer;

import std;
import openmui.geom;
import :surface;

export namespace omui::raster {

// The same values as openmui.render's PathVerb, so a display list's paths are
// rasterised without conversion.
enum class Verb : std::uint8_t { move, line, quad, cubic, close };
enum class FillRule : std::uint8_t { nonzero, even_odd };

struct PathData {
    std::span<const Verb>  verbs;
    std::span<const Point> points;
};

// A path built by hand (tests, glyph outlines).
class PathBuilder {
public:
    PathBuilder& move_to(Point p);
    PathBuilder& line_to(Point p);
    PathBuilder& quad_to(Point c, Point p);
    PathBuilder& cubic_to(Point c1, Point c2, Point p);
    PathBuilder& close();
    PathBuilder& rect(const Rect& r);
    [[nodiscard]] PathData data() const noexcept { return {verbs_, points_}; }
    [[nodiscard]] bool empty() const noexcept { return verbs_.empty(); }
    void clear() noexcept {
        verbs_.clear();
        points_.clear();
    }

private:
    std::vector<Verb>  verbs_;
    std::vector<Point> points_;
};

// Flattened outline: closed polygons in device space.
struct Polygons {
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

// Curves become line segments within `tolerance` device pixels. The default,
// 1/32 pixel, keeps the area lost to chords below 0.1% at UI sizes, so curved
// edges anti-alias like the exact curve.
void flatten(PathData path, const Affine& m, Polygons& out, float tolerance = 1.0f / 32.0f);

// Runs of equal coverage on one row: what a shape covers, without the pixels
// it does not. A fill composites a run of one value as one colour.
struct Span {
    std::int32_t y = 0;
    std::int32_t x = 0;
    std::int32_t length = 0;
    std::uint8_t value = 0;  // 1…255 (runs of 0 are left out)
};

struct Spans {
    PixelRect         bounds;  // the rectangle the runs lie in
    std::vector<Span> runs;    // by row, then by column
    [[nodiscard]] bool empty() const noexcept { return runs.empty(); }
};

// Coverage of polygons inside `clip` (device pixels).
//
// The cells an edge crosses are kept sparse — only those it touches, sorted
// by row and column — and a sweep per row turns them into coverage: at a
// cell, the winding so far and the cell's area; between two cells, the
// winding alone, so the run between them is one value. The arithmetic per
// pixel is the dense sweep's, so both outputs give the same bytes; the cost
// follows the outline, not the area.
class Rasterizer {
public:
    void rasterize(const Polygons& polygons, FillRule rule, PixelRect clip, Coverage& out);
    void rasterize(const Polygons& polygons, FillRule rule, PixelRect clip, Spans& out);

private:
    struct Cell {
        std::int32_t y, x, cover, area;
    };
    // Bounds and cells; false: nothing to cover.
    bool prepare(const Polygons& polygons, PixelRect clip);
    template <class Emit>
    void sweep(FillRule rule, Emit&& emit) const;
    void line(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2);
    void scanline(std::int32_t ey, std::int32_t x1, std::int32_t fy1, std::int32_t x2, std::int32_t fy2);
    void cell(std::int32_t ex, std::int32_t ey, std::int32_t cover, std::int32_t area);

    std::vector<Cell> cells_;
    std::int32_t      min_x_ = 0, min_y_ = 0, width_ = 0, height_ = 0;
};

// Convenience: flatten and rasterise in one step.
void fill_coverage(PathData path, const Affine& m, FillRule rule, PixelRect clip, Coverage& out);

}  // namespace omui::raster
