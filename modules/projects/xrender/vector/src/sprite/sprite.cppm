// xrender.sprite — pictures on bones in two dimensions.
//
//     xrender::sprite::Mesh mesh;
//     xrender::sprite::skinned({.source = sleeve, .anchor = shoulder, .tip = wrist}, {.columns = 6, .rows = 2, .length = 80},
//                              {upper_rest, upper_now}, bends /* the forearm: where it was drawn, where it is */, mesh);
//     list.draw_mesh(picture, mesh.at, mesh.uv, mesh.index, ...);       // xrender.vector
//
// A function of what it is given: the same bones, the same corners, bit for bit (mlibs.math's deterministic
// sines; no fast-math). It keeps nothing and allocates nothing once `Mesh` has grown to its size.
export module xrender.sprite;

import std;
export import mlibs.geom;

export namespace xrender::sprite {

using mlibs::geom::Point;
using mlibs::geom::Rect;

// Where a bone is: its joint, and its turn (radians, counter-clockwise from +x, y up).
struct Bone {
    Point  joint;
    double angle = 0;
};
// A bone as the picture was drawn for it and as it is now.
struct Posed {
    Bone rest, now;
};

// A picture's region (pixels, y down) and its line on the bone: `anchor` sits on the bone's joint, `tip` lies
// along it — together they give the art's turn and, fitted, its size.
struct Region {
    Rect  source;
    Point anchor, tip;
};

struct Grid {
    std::int32_t columns = 1, rows = 1;  // cells across the region and down it
    // A corner goes over to a bend bone across this many pixels of the art about that bone's joint.
    float  blend = 16;
    bool   mirror = false;  // the art reflected across its anchor-to-tip line
    double facing = 1;      // −1: the whole figure faces the other way
    // Units a pixel of the art. 0: fitted — anchor to tip is `length`, the bone's.
    double scale = 0;
    double length = 0;
};

// Corners (in the bones' space), where each is in the picture (pixels), and triangles: what a textured mesh
// is drawn from.
struct Mesh {
    std::vector<Point>         at, uv;
    std::vector<std::uint16_t> index;
    void clear() noexcept { at.clear(), uv.clear(), index.clear(); }
};

// The region as (columns + 1) × (rows + 1) corners on `own.now`, a row at a time, and two triangles a cell.
// With `bends` (the next bones along: an elbow, a knee, a hem), each corner is first placed in the rest pose
// and then taken over by each bend bone in turn — wholly past its joint, not at all before it, across `blend`
// between. How far toward a bend is measured along the part's own bone (where the bend's joint lies on it),
// so a rest pose that is itself bent does not split the part across its width. `out` is replaced.
void skinned(const Region& region, const Grid& grid, const Posed& own, std::span<const Posed> bends, Mesh& out);

// The region as one cell whose corners the caller has placed (a whole figure's cel moved as one): `placed` are
// the region's corners, clockwise from its top left. `out` is replaced.
void cornered(const Rect& source, std::span<const Point, 4> placed, Mesh& out);

}  // namespace xrender::sprite
