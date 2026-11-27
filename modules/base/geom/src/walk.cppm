// mlibs.geom.api:walk — a ray stepped through a lattice of cells, one cell after the next in the order it passes
// them (the foundation plan's §5.7, D-184: a lattice is its own acceleration structure — light through a world of
// cells, a line of sight, a bolt's flight). The same in two dimensions and three: a flat lattice is one cell deep.
//
//     geom::walk(ray, {.cells = {width, height, 1}, .cell = 1.0f}, [&](const geom::Passed& at) {
//         return !opaque(at.x, at.y);          // false: stop here
//     });
//
// Cell (x, y, z) is the box from (x, y, z) × cell to (x + 1, y + 1, z + 1) × cell. A ray that starts outside
// enters where it meets the lattice; one that never does visits nothing. Each cell is visited once, with how far
// along the ray it was entered and through which face. Where a ray passes exactly through an edge or a corner it
// steps one axis at a time, the lower axis first: the same cells in the same order on every machine. It allocates
// nothing.
export module mlibs.geom.api:walk;

import std;
import mlibs.math;
import :solid;

export namespace mlibs::geom {

struct Lattice {
    std::array<std::int32_t, 3> cells{1, 1, 1};
    float                       cell = 1.0f;  // a cell's side
};
struct Passed {
    std::int32_t x = 0, y = 0, z = 0;
    float        entered = 0;  // how far along the ray
    int          face = -1;    // the axis it crossed to enter (0 x, 1 y, 2 z); -1: the ray began in it, or entered the lattice here from outside along no single step
};

// `visit(passed)` for each cell the ray passes, no further than `farthest`; it returns whether to go on. How
// many cells were visited.
template <class Visit>
requires std::predicate<Visit&, const Passed&>
std::size_t walk(const Ray& ray, const Lattice& lattice, Visit&& visit, float farthest = std::numeric_limits<float>::max()) {
    if (!(lattice.cell > 0) || lattice.cells[0] <= 0 || lattice.cells[1] <= 0 || lattice.cells[2] <= 0) return 0;
    Box3 whole;
    whole.grow({0, 0, 0});
    whole.grow({static_cast<float>(lattice.cells[0]) * lattice.cell, static_cast<float>(lattice.cells[1]) * lattice.cell, static_cast<float>(lattice.cells[2]) * lattice.cell});
    const std::optional<float> start = meet(ray, whole);
    if (!start || *start > farthest) return 0;
    const std::array<float, 3> from{ray.from.x + ray.along.x * *start, ray.from.y + ray.along.y * *start, ray.from.z + ray.along.z * *start};
    const std::array<float, 3> along{ray.along.x, ray.along.y, ray.along.z};
    std::array<std::int32_t, 3> cell{}, step{};
    std::array<float, 3> next{}, each{};  // the distance at which the ray next crosses a face of each axis, and between two
    for (std::size_t axis = 0; axis < 3; ++axis) {
        cell[axis] = std::clamp(static_cast<std::int32_t>(std::floor(from[axis] / lattice.cell)), std::int32_t{0}, lattice.cells[axis] - 1);
        step[axis] = along[axis] > 0 ? 1 : along[axis] < 0 ? -1 : 0;
        if (step[axis] == 0) {
            next[axis] = each[axis] = std::numeric_limits<float>::infinity();
            continue;
        }
        const float face = static_cast<float>(cell[axis] + (step[axis] > 0 ? 1 : 0)) * lattice.cell;
        next[axis] = *start + (face - from[axis]) / along[axis];
        each[axis] = lattice.cell / std::abs(along[axis]);
    }
    std::size_t visited = 0;
    Passed passed{cell[0], cell[1], cell[2], *start, -1};
    for (;;) {
        ++visited;
        if (!std::invoke(visit, std::as_const(passed))) break;
        const std::size_t axis = next[0] <= next[1] ? (next[0] <= next[2] ? 0 : 2) : (next[1] <= next[2] ? 1 : 2);
        if (step[axis] == 0 || next[axis] > farthest) break;
        cell[axis] += step[axis];
        if (cell[axis] < 0 || cell[axis] >= lattice.cells[axis]) break;
        passed = {cell[0], cell[1], cell[2], next[axis], static_cast<int>(axis)};
        next[axis] += each[axis];
    }
    return visited;
}

}  // namespace mlibs::geom
