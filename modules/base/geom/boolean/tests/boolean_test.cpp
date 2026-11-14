// mlibs.geom.boolean: two solids joined, one cut from the other, what they share — a solid again, of the volume
// it should have; the attributes both carry ride along and an edge the cut made is sharp; what is not a solid is
// refused with which and why; and the same two meshes give the same bytes everywhere (pinned).
import std;
import mlibs.geom.boolean;
import mlibs.math;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace geom = mlibs::geom;
namespace math = mlibs::math;

namespace {

geom::Mesh moved(geom::Mesh mesh, math::Vec3 by) {
    for (math::Vec3& at : mesh.positions) at = at + by;
    return mesh;
}
geom::Mesh turned(geom::Mesh mesh, math::Vec3 axis, float angle) {
    const math::Quat turn = math::Quat::about(math::normalized(axis), angle);
    for (math::Vec3& at : mesh.positions) at = math::rotate(turn, at);
    for (math::Vec3& normal : mesh.normals) normal = math::rotate(turn, normal);
    for (math::Vec4& tangent : mesh.tangents) {
        const math::Vec3 along = math::rotate(turn, {tangent.x, tangent.y, tangent.z});
        tangent = {along.x, along.y, along.z, tangent.w};
    }
    return mesh;
}
bool about(double one, double other, double within) { return std::abs(one - other) <= within; }
std::uint64_t bits(const geom::Mesh& mesh) {
    mlibs::hash::Fnv1a hash;
    for (const math::Vec3 at : mesh.positions) hash.f32(at.x).f32(at.y).f32(at.z);
    for (const math::Vec3 normal : mesh.normals) hash.f32(normal.x).f32(normal.y).f32(normal.z);
    for (const math::Vec2 uv : mesh.uvs) hash.f32(uv.x).f32(uv.y);
    for (const std::uint32_t index : mesh.indices) hash.le(index, 4);
    return hash.value();
}

}  // namespace

static UTest cases{"mlibs.geom.boolean", [] {
    "two boxes: joined, cut and shared are solids of the volumes they should be"_test = [] {
        const geom::Mesh one = geom::box({1, 1, 1}), other = moved(one, {1, 0, 0});
        expect(geom::closed(one).has_value() && about(geom::volume(one), 8, 1e-6));
        const auto joined = geom::united(one, other), cut = geom::without(one, other), shared = geom::common(one, other);
        expect(fatal(joined.has_value() && cut.has_value() && shared.has_value())) << (joined ? "" : joined.error());
        expect(about(geom::volume(*joined), 12, 1e-5) && about(geom::volume(*cut), 4, 1e-5) && about(geom::volume(*shared), 4, 1e-5));
        for (const geom::Mesh* mesh : {&*joined, &*cut, &*shared}) expect(geom::check(*mesh).has_value() && geom::closed(*mesh).has_value());
        // A result is combined again: (one − other) ∪ (one ∩ other) is one.
        const auto again = geom::united(*cut, *shared);
        expect(fatal(again.has_value()));
        expect(about(geom::volume(*again), 8, 1e-5));
        // Nothing left, nothing shared: a mesh of no triangles; and joining with nothing is the thing itself.
        expect(geom::without(one, one)->indices.empty() && geom::common(one, moved(one, {5, 0, 0}))->indices.empty());
        const auto alone = geom::united(one, geom::Mesh{});
        expect(fatal(alone.has_value()));
        expect(about(geom::volume(*alone), 8, 1e-6) && eq(alone->normals.size(), alone->positions.size()));
    };

    "a hole bored through a block: the volume is the block's less the bore's, and the cut's edge is sharp"_test = [] {
        const geom::Mesh block = geom::box({1, 1, 1}), bore = geom::cylinder(0.5f, 4, 48);
        const auto drilled = geom::without(block, bore);
        expect(fatal(drilled.has_value())) << (drilled ? "" : drilled.error());
        // A cylinder of 48 flat sides: its base is ½ · 48 · r² · sin(2π / 48).
        const double base = 0.5 * 48 * 0.25 * std::sin(2 * std::numbers::pi / 48);
        expect(about(geom::volume(*drilled), 8 - base * 2, 1e-4)) << geom::volume(*drilled);
        expect(geom::closed(*drilled).has_value());
        // Normals, tangents and texture coordinates came along, each of length 1; on the block's flat faces a
        // normal is the face's own right up to the hole's rim (the rim has corners of its own for the bore's wall).
        expect(fatal(eq(drilled->normals.size(), drilled->positions.size()) && eq(drilled->uvs.size(), drilled->positions.size()) && eq(drilled->tangents.size(), drilled->positions.size())));
        std::size_t on_top = 0, rim_wall = 0;
        for (std::size_t corner = 0; corner < drilled->positions.size(); ++corner) {
            const math::Vec3 at = drilled->positions[corner], normal = drilled->normals[corner];
            expect(about(math::length(normal), 1, 1e-5));
            if (at.y != 1) continue;
            if (normal.y > 0.999f) ++on_top;
            else {
                expect(std::abs(normal.y) < 1e-4f) << "a corner of the bore's wall at the rim looks sideways";
                ++rim_wall;
            }
        }
        expect(ge(on_top, std::size_t{4 + 48}) && ge(rim_wall, std::size_t{48}));
    };

    "what is not a solid is refused: which of the two, and what is wrong"_test = [] {
        const geom::Mesh block = geom::box({1, 1, 1});
        const auto open = geom::united(block, geom::sheet(2, 2));
        expect(!open.has_value() && open.error().starts_with("the second is not a solid") && open.error().contains("not closed")) << (open ? "" : open.error());
        geom::Mesh broken = block;
        broken.indices[5] = 99;
        const auto wild = geom::without(broken, block);
        expect(!wild.has_value() && wild.error().starts_with("the first is not a mesh")) << (wild ? "" : wild.error());
        expect(!geom::closed(geom::sheet(1, 1)).has_value() && !geom::closed(geom::Mesh{}).has_value());
        // A block with a face's two triangles turned inside out is not one either.
        geom::Mesh inside_out = block;
        std::swap(inside_out.indices[1], inside_out.indices[2]), std::swap(inside_out.indices[4], inside_out.indices[5]);
        expect(!geom::closed(inside_out).has_value());
        // An attribute only one of them has is left out; the places are still combined.
        geom::Mesh bare = moved(block, {1, 0, 0});
        bare.uvs.clear();
        const auto joined = geom::united(block, bare);
        expect(fatal(joined.has_value()));
        expect(joined->uvs.empty() && eq(joined->normals.size(), joined->positions.size()) && about(geom::volume(*joined), 12, 1e-5));
    };

    "the same two meshes, the same bytes: here twice, and on every machine (pinned)"_test = [] {
        // Curved against flat and curved against curved, at angles that are no multiple of a quarter turn.
        const auto made = [] {
            const geom::Mesh ball = geom::sphere(1, 16, 32), block = turned(geom::box({0.5f, 0.5f, 0.5f}), {1, 2, 3}, 0.4f), rod = moved(turned(geom::cylinder(0.25f, 2.5f, 24), {1, 0, 0.3f}, 0.6f), {0.1f, 0.2f, -0.3f});
            return geom::united(*geom::without(ball, block), rod);
        };
        const auto first = made(), second = made();
        expect(fatal(first.has_value() && second.has_value())) << (first ? "" : first.error());
        expect(eq(bits(*first), bits(*second)) && geom::closed(*first).has_value());
        bool in_order = true;
        for (std::size_t at = 3; at + 2 < first->indices.size(); at += 3) {
            const std::span<const std::uint32_t> before(first->indices.data() + at - 3, 3), here(first->indices.data() + at, 3);
            in_order = in_order && !std::ranges::lexicographical_compare(here, before) && here[0] <= here[1] && here[0] <= here[2];
        }
        expect(in_order) << "the triangles are in their own order, each from its least corner";
        expect(eq(first->triangles(), std::size_t{1524}));
        expect(eq(bits(*first), std::uint64_t{13990725960752970763ull})) << std::format("{:#x}", bits(*first));
    };
}};
