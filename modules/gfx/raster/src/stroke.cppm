// openmui.raster:stroke — the outline of a stroked path.
//
// A stroke becomes polygons — one per segment, join and cap — each oriented
// positively, so filling them under the non-zero rule gives their union:
// overlaps never double the coverage, and no seam appears between pieces.
export module openmui.raster:stroke;

import std;
import openmui.geom;
import :rasterizer;

export namespace omui::raster {

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
};

// Appends the stroke outline of `path` (transformed by `m`) to `out`. The
// width is in path units: a scaled transform scales it.
void stroke_outline(PathData path, const Affine& m, const Stroke& stroke, Polygons& out);

// The coverage of a stroked path, as Canvas::stroke_path composites it (the
// outline filled nonzero), cut to `clip`.
void stroke_coverage(PathData path, const Stroke& stroke, const Affine& m, PixelRect clip, Coverage& out);

}  // namespace omui::raster
