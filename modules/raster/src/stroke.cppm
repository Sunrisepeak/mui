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
    std::span<const float> dashes;  // on, off, on, off … in device-independent units
    float                  dash_offset = 0;
};

// Appends the stroke outline of `path` (transformed by `m`) to `out`. The
// width is in path units: a scaled transform scales it.
void stroke_outline(PathData path, const Affine& m, const Stroke& stroke, Polygons& out);

}  // namespace omui::raster
