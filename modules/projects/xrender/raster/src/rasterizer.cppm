// openmui.raster:rasterizer — paths to anti-aliased coverage, exactly.
//
// Curves are flattened in floating point (operation order fixed, no fused
// multiply-add), endpoints are rounded to 1/256 pixel, and from there on the
// arithmetic is integer: each edge adds its cover and area to the cells it
// crosses (the scheme of FreeType's ftgrays), and a sweep per row turns the
// accumulated winding into coverage under the non-zero or even-odd rule. The
// same path gives the same bytes on every platform (D-43, spec R-2).
export module xrender.raster.api:rasterizer;

import std;
import mlibs.geom;
import :surface;

export namespace xrender::raster {

// Source-compatible names for the shared geometry contract. No enum or
// operand-array conversion is needed at the renderer/raster boundary.
using Verb = mlibs::geom::PathVerb;
using FillRule = mlibs::geom::FillRule;
using PathData = mlibs::geom::PathView;

// A path built by hand (tests, glyph outlines).
class PathBuilder {
public:
    PathBuilder& move_to(Point p);
    PathBuilder& line_to(Point p);
    PathBuilder& quad_to(Point c, Point p);
    PathBuilder& cubic_to(Point c1, Point c2, Point p);
    PathBuilder& close();
    PathBuilder& rect(const Rect& r);
    [[nodiscard]] inline PathData data() const noexcept { return {verbs_, points_}; }
    [[nodiscard]] inline bool empty() const noexcept { return verbs_.empty(); }
    void clear() noexcept {
        verbs_.clear();
        points_.clear();
    }

private:
    std::vector<Verb>  verbs_;
    std::vector<Point> points_;
};

using Polygons = mlibs::geom::PathPolylines;

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
    [[nodiscard]] inline bool empty() const noexcept { return runs.empty(); }
};

// Coverage of polygons inside `clip` (device pixels).
//
// The cells an edge crosses add into a row's accumulators, their columns
// marked in the row's bits, and a sweep per row visits the marked columns
// alone and turns them into coverage: at a cell, the winding so far and the
// cell's area; between two cells, the winding alone, so the run between them
// is one value. Both outputs give the same bytes; the cost follows the
// outline (a word of bits skips 64 columns of a shape's inside).
class Rasterizer {
public:
    void rasterize(const Polygons& polygons, FillRule rule, PixelRect clip, Coverage& out);
    void rasterize(const Polygons& polygons, FillRule rule, PixelRect clip, Spans& out);
    // The same runs as Spans holds, given one by one to emit(row, column,
    // count, value) in device pixels (value 1…255; runs of 0 left out) —
    // for a caller that composites a run as it comes rather than keep them.
    // Adjacent runs of one value may come apart: what each pixel gets is the same.
    template <class Emit>
    void rasterize_runs(const Polygons& polygons, FillRule rule, PixelRect clip, Emit&& emit) {
        if (!prepare(polygons, clip)) return;
        sweep_bands(polygons, rule, [&](std::int32_t y, std::int32_t x, std::int32_t count, std::uint8_t v) {
            if (v != 0) emit(y + min_y_, x + min_x_, count, v);
        });
    }

    // The same coverage, a row's short pieces gathered: emit(row, column,
    // count, value, values) — `values` null for a run of `value` (long runs:
    // a shape's inside), else the coverage of each of `count` pixels (the
    // edge cells and the short runs between them, together), for a caller
    // that composites a stretch of varying coverage at once.
    template <class Emit>
    void rasterize_rows(const Polygons& polygons, FillRule rule, PixelRect clip, Emit&& emit) {
        if (!prepare(polygons, clip)) return;
        const std::int32_t step = band_rows();
        if (row_pieces_.size() < static_cast<std::size_t>(width_)) row_pieces_.resize(static_cast<std::size_t>(width_));
        for (std::int32_t first = 0; first < box_height_; first += step) {
            band(polygons, first, std::min(step, box_height_ - first));
            sweep_rows(rule, emit);
        }
    }

private:
    // sweep's coverage, a row at a time: a piece of varying coverage is
    // written in place (row_pieces_, a byte a column) and given whole; a run
    // of one value at least long_run pixels (a shape's inside) as a run.
    template <class Emit>
    void sweep_rows(FillRule rule, Emit&& emit) {
        constexpr std::int32_t long_run = 16;
        auto value = [rule](std::int64_t v) -> std::uint8_t {
            if (v < 0) v = -v;
            std::int64_t c = v >> 9;
            if (rule == FillRule::even_odd) {
                c &= 511;
                if (c > 256) c = 512 - c;
            }
            return static_cast<std::uint8_t>(c > 255 ? 255 : c);
        };
        std::uint8_t* piece = row_pieces_.data();
        for (std::int32_t y = 0; y < height_; ++y) {
            Cell* row = cells_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
            std::uint64_t* marks = marked_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(words_);
            const std::int32_t device_row = y + min_y_;
            std::int64_t acc = 0;
            std::int32_t x = 0;            // the next pixel not yet given
            std::int32_t piece_from = -1, piece_end = 0;  // the piece being gathered (from −1: none)
            const auto flush = [&] {
                if (piece_from >= 0) emit(device_row, piece_from + min_x_, piece_end - piece_from, std::uint8_t{0}, piece + piece_from);
                piece_from = -1;
            };
            const auto gather = [&](std::int32_t from, std::int32_t to) {
                if (piece_from < 0) piece_from = from;
                piece_end = to;
            };
            // The pixels from x to `to`, all of one value.
            const auto between = [&](std::int32_t to, std::uint8_t v) {
                const std::int32_t count = to - x;
                if (count <= 0) return;
                if (v == 0) {
                    flush();
                } else if (count >= long_run) {
                    flush();
                    emit(device_row, x + min_x_, count, v, static_cast<const std::uint8_t*>(nullptr));
                } else {
                    std::memset(piece + x, v, static_cast<std::size_t>(count));
                    gather(x, to);
                }
            };
            for (std::int32_t word = 0; word < words_; ++word) {
                std::uint64_t bits = std::exchange(marks[word], 0);
                while (bits != 0) {
                    const std::int32_t column = word * 64 + std::countr_zero(bits);
                    bits &= bits - 1;
                    const Cell c = std::exchange(row[column], Cell{});
                    between(column, value(acc << 9));
                    acc += c.cover;
                    const std::uint8_t v = value((acc << 9) - c.area);
                    if (v == 0) {
                        flush();
                    } else {
                        piece[column] = v;
                        gather(column, column + 1);
                    }
                    x = column + 1;
                }
            }
            between(width_, value(acc << 9));
            flush();
        }
    }

private:
    std::vector<std::uint8_t> row_pieces_;  // rasterize_rows's gathered piece
    struct Cell {
        std::int32_t cover = 0, area = 0;
    };
    // The box the polygons cover inside the clip; false: nothing to cover.
    bool prepare(const Polygons& polygons, PixelRect clip);
    // The box a band of rows at a time — as many as 2^20 cells hold — each
    // band's cells made (band) and swept: a band renders as the whole would
    // (an edge's rows outside it are skipped, not cut).
    [[nodiscard]] std::int32_t band_rows() const;
    void band(const Polygons& polygons, std::int32_t first_row, std::int32_t rows);
    template <class Emit>
    void sweep_bands(const Polygons& polygons, FillRule rule, Emit&& emit) {
        const std::int32_t step = band_rows();
        for (std::int32_t first = 0; first < box_height_; first += step) {
            band(polygons, first, std::min(step, box_height_ - first));
            sweep(rule, emit);
        }
    }
    // emit(row in the band, column, count, value) for every pixel of the band (min_y_, height_).
    template <class Emit>
    void sweep(FillRule rule, Emit&& emit);
    void line(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2);
    void scanline(std::int32_t ey, std::int32_t x1, std::int32_t fy1, std::int32_t x2, std::int32_t fy2);
    void cell(std::int32_t ex, std::int32_t ey, std::int32_t cover, std::int32_t area);

    std::vector<Cell> cells_;              // row by row, width_ a row; zero but between prepare and sweep
    std::vector<std::uint64_t> marked_;    // a bit a column with a cell, words_ a row; zero likewise
    std::int32_t      words_ = 0;                        // marked_'s words a row
    std::int32_t      box_y_ = 0, box_height_ = 0;          // the whole box's rows; min_y_/height_ the band's
    std::int32_t      min_x_ = 0, min_y_ = 0, width_ = 0, height_ = 0;
};

// emit(row, column, count, value) for every pixel of the bounds, in runs: a
// cell's pixel alone, and the pixels between cells together.
template <class Emit>
void Rasterizer::sweep(FillRule rule, Emit&& emit) {
    auto value = [rule](std::int64_t v) -> std::uint8_t {
        if (v < 0) v = -v;
        std::int64_t c = v >> 9;
        if (rule == FillRule::even_odd) {
            c &= 511;
            if (c > 256) c = 512 - c;
        }
        return static_cast<std::uint8_t>(c > 255 ? 255 : c);
    };
    for (std::int32_t y = 0; y < height_; ++y) {
        Cell* row = cells_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
        std::uint64_t* marks = marked_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(words_);
        std::int64_t acc = 0;
        std::int32_t x = 0;  // the next pixel not emitted
        for (std::int32_t word = 0; word < words_; ++word) {
            std::uint64_t bits = std::exchange(marks[word], 0);
            while (bits != 0) {
                const std::int32_t column = word * 64 + std::countr_zero(bits);
                bits &= bits - 1;
                const Cell c = std::exchange(row[column], Cell{});
                if (column > x) emit(y, x, column - x, value(acc << 9));
                acc += c.cover;
                emit(y, column, 1, value((acc << 9) - c.area));
                x = column + 1;
            }
        }
        if (x < width_) emit(y, x, width_ - x, value(acc << 9));
    }
}

// Convenience: flatten and rasterise in one step.
void fill_coverage(PathData path, const Affine& m, FillRule rule, PixelRect clip, Coverage& out);

}  // namespace xrender::raster
