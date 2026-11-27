// openmui.raster:stroke — the outline of a stroked path.
//
// A stroke becomes one loop per open contour (the left side, the end cap,
// the right side back, the start cap) and two per closed one (the left side,
// the right side back): a band that winds the same way all along, so filling
// under the non-zero rule covers it once where it crosses itself, and the
// inside of a closed contour not at all. Joins go on the outer side of a
// turn; the inner side takes the point where its offsets meet, or the vertex
// between them where the segments are too short (Skia's pivot).
export module mrender.raster.api:stroke;

import std;
import mlibs.geom;
import :rasterizer;

export namespace mrender::raster {

enum class Cap : std::uint8_t { butt, round, square };
enum class Join : std::uint8_t { miter, round, bevel };

struct Stroke {
    float                  width = 1;
    Cap                    cap = Cap::butt;
    Join                   join = Join::miter;
    float                  miter_limit = 4;
    // on, off, on, off … in path units; odd lists repeat twice per cycle.
    // Empty/all-zero or negative/nonfinite patterns use a solid stroke.
    std::span<const float> dashes;
    float                  dash_offset = 0; // nonfinite: zero; finite offsets wrap through the complete cycle
    // The part of each contour drawn, as fractions of its length: from
    // trim_start to trim_end (clamped to 0…1, swapped when reversed), both
    // moved on by trim_offset, wrapping round a closed contour and splitting
    // an open one. Trimming comes before dashing; the defaults draw it all.
    float                  trim_start = 0;
    float                  trim_end = 1;
    float                  trim_offset = 0;
};

// What making a stroke's outline works in — the path flattened, its lines
// trimmed and dashed, the loops before the transform, each side's offsets —
// kept by a caller that strokes again (a Canvas has one), so a stroke drawn
// again allocates nothing.
struct StrokeScratch {
    Polygons flat, lines, cut, local;
    std::vector<Point> poly, normals;
    std::array<std::vector<double>, 2> angles;
    std::vector<float> lengths;
};

// Appends the stroke outline of `path` (transformed by `m`) to `out`. The
// width is in path units: a scaled transform scales it. Without `scratch`:
// one of its own for this call.
void stroke_outline(PathData path, const Affine& m, const Stroke& stroke, Polygons& out);
void stroke_outline(PathData path, const Affine& m, const Stroke& stroke, Polygons& out, StrokeScratch& scratch);

// The coverage of a stroked path, as Canvas::stroke_path composites it (the
// outline filled nonzero), cut to `clip`.
void stroke_coverage(PathData path, const Stroke& stroke, const Affine& m, PixelRect clip, Coverage& out);

}  // namespace mrender::raster
