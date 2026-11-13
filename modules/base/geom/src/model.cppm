// mlibs.geom.api:model — shapes made from an outline: turned about an axis, pushed through a depth; and a polygon
// cut into triangles (the foundation plan's §5.1: modelling by code, so a thing can be made with no file).
//
//     const std::array<Vec2, 5> vase{{{0, 2}, {0.5f, 2}, {0.3f, 1}, {0.8f, 0}, {0, 0}}};      // radius, height: from the top down
//     const geom::Mesh turned = *geom::revolved(vase, 32);
//     const std::array<Vec2, 6> ell{{{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 3}, {0, 3}}};
//     const geom::Mesh pushed = *geom::extruded(ell, 0.5f);
//
// Each gives positions, normals, tangents and uvs under `mesh`'s conventions (fronts counter-clockwise, +Y up), the
// same bits everywhere, and refuses with the reason what cannot be a shape.
export module mlibs.geom.api:model;

import std;
import mlibs.math;
import :mesh;

export namespace mlibs::geom {

// A polygon — its corners in order round, either way, not crossing itself, no three in a row on one line — as
// triangles: three corners' numbers each, counter-clockwise where +Y is up. `corners − 2` of them.
[[nodiscard]] std::expected<std::vector<std::uint32_t>, std::string> triangulated(std::span<const Vec2> polygon);

// A profile turned about the Y axis: its points are (radius, height), from the top down, radius never below 0.
// Smooth around; along the profile smooth too, but a corner sharper than 60° stays a corner. It is closed where
// the profile begins and ends on the axis (radius 0). `sectors` around: 3 at least.
[[nodiscard]] std::expected<Mesh, std::string> revolved(std::span<const Vec2> profile, std::uint32_t sectors = 32);

// An outline in the x–y plane pushed through `depth` along Z (half each way): two flat faces and a wall. Closed.
// With a `bevel` its edges are cut at 45°: the faces are the outline drawn in by so much, the wall stops so much
// short of each, and a flat piece joins them — the edge that catches the light. A bevel the outline has no room
// for, or a corner too sharp for one, is refused with its reason.
[[nodiscard]] std::expected<Mesh, std::string> extruded(std::span<const Vec2> outline, float depth, float bevel = 0);

// The space a closed mesh holds (by its triangles: the sum of the tetrahedra they make with the origin). Of one
// that is not closed, or faces inward: not a volume.
[[nodiscard]] double volume(const Mesh& mesh) noexcept;

}  // namespace mlibs::geom
