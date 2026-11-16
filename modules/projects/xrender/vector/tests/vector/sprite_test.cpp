// xrender.sprite: a region skinned over a grid — where its corners go on a bone, how a bend takes them over,
// and that the same bones give the same corners.
import std;
import mcpplibs.mtest;
import mlibs.geom;
import mlibs.hash;
import xrender.sprite;

using namespace mcpplibs::mtest;
namespace sprite = xrender::sprite;
using sprite::Point;

namespace {

// A sleeve drawn level, 80 pixels long and 20 wide: its shoulder at (0, 10), its wrist at (80, 10).
const sprite::Region sleeve{.source = {0, 0, 80, 20}, .anchor = {0, 10}, .tip = {80, 10}};
bool near(Point one, Point other, float within = 1e-4f) { return std::abs(one.x - other.x) <= within && std::abs(one.y - other.y) <= within; }

}  // namespace

UTest cases{"xrender.sprite", [] {
    "a region on its bone: a grid of corners a row at a time, the anchor on the joint, the tip the bone's length along it, two triangles a cell"_test = [] {
        sprite::Mesh mesh;
        const sprite::Bone bone{{100, 50}, 0};
        sprite::skinned(sleeve, {.columns = 4, .rows = 1, .length = 40}, {bone, bone}, {}, mesh);
        expect(eq(mesh.at.size(), std::size_t{10}) && eq(mesh.uv.size(), std::size_t{10}) && eq(mesh.index.size(), std::size_t{24}));
        // Fitted: 80 pixels of art are the bone's 40 units; the picture's y runs down, the bone's up.
        expect(near(mesh.at[0], {100, 55}) && near(mesh.at[4], {140, 55}) && near(mesh.at[5], {100, 45}) && near(mesh.at[9], {140, 45}));
        expect(mesh.uv[0] == Point{0, 0} && mesh.uv[4] == Point{80, 0} && mesh.uv[9] == Point{80, 20});
        expect(eq(mesh.index[0], std::uint16_t{0}) && eq(mesh.index[1], std::uint16_t{1}) && eq(mesh.index[2], std::uint16_t{6}) && eq(mesh.index[5], std::uint16_t{5}));
        for (const std::uint16_t corner : mesh.index) expect(lt(corner, std::uint16_t{10}));

        // The bone turned a quarter: along it is up.
        const sprite::Bone up{{100, 50}, 1.5707963267948966};
        sprite::skinned(sleeve, {.columns = 4, .rows = 1, .length = 40}, {bone, up}, {}, mesh);
        expect(near(mesh.at[4], {95, 90}) && near(mesh.at[9], {105, 90})) << mesh.at[4].x << " " << mesh.at[4].y;
        // A scale of its own in place of the fit; mirrored, the art's two sides change places; facing the other way too.
        sprite::skinned(sleeve, {.columns = 1, .rows = 1, .scale = 2, .length = 40}, {bone, bone}, {}, mesh);
        expect(near(mesh.at[1], {260, 70}));
        sprite::skinned(sleeve, {.columns = 1, .rows = 1, .mirror = true, .length = 40}, {bone, bone}, {}, mesh);
        expect(near(mesh.at[0], {100, 45}) && near(mesh.at[2], {100, 55}));
        sprite::skinned(sleeve, {.columns = 1, .rows = 1, .facing = -1, .length = 40}, {bone, bone}, {}, mesh);
        expect(near(mesh.at[0], {100, 45}));
        // A region with no line has the bone's own turn and one unit a pixel.
        sprite::skinned({.source = {0, 0, 10, 10}, .anchor = {0, 0}, .tip = {0, 0}}, {.length = 40}, {bone, bone}, {}, mesh);
        expect(near(mesh.at[1], {110, 50}) && near(mesh.at[2], {100, 40}));
    };

    "a bend: corners before the elbow stay the upper arm's, corners past it are the forearm's, and between they go over — the sleeve stays whole"_test = [] {
        // Drawn straight: the upper arm from (0, 0), the forearm from (40, 0). Now the forearm is turned up a quarter.
        const sprite::Bone upper{{0, 0}, 0}, fore_drawn{{40, 0}, 0}, fore_now{{40, 0}, 1.5707963267948966};
        const std::array<sprite::Posed, 1> bends{sprite::Posed{fore_drawn, fore_now}};
        sprite::Mesh straight, bent;
        const sprite::Grid grid{.columns = 8, .rows = 2, .blend = 16, .length = 80};
        sprite::skinned(sleeve, grid, {upper, upper}, {}, straight);
        sprite::skinned(sleeve, grid, {upper, upper}, bends, bent);
        expect(eq(bent.at.size(), straight.at.size()) && bent.uv == straight.uv && bent.index == straight.index);
        // The middle row lies on the bones: the shoulder where it was, the elbow's column half-way over, the wrist
        // straight above the elbow at the forearm's length.
        const std::size_t middle = 9;
        expect(near(bent.at[middle], {0, 0}) && near(bent.at[middle + 2], straight.at[middle + 2])) << "before the blend: the upper arm's";
        expect(near(bent.at[middle + 8], {40, 40})) << bent.at[middle + 8].x << " " << bent.at[middle + 8].y;
        expect(near(bent.at[middle + 6], {40, 20})) << "past the blend: the forearm's";
        expect(near(bent.at[middle + 4], {40, 0})) << "the joint itself does not move";
        // Along each row the corners go on in order: no corner behind the one before it along the arm's way.
        for (std::size_t column = 0; column < 8; ++column) {
            const Point here = bent.at[middle + column], next = bent.at[middle + column + 1];
            expect(ge(next.x - here.x + next.y - here.y, -1e-4f));
        }
        // With the forearm where it was drawn the bend changes nothing.
        const std::array<sprite::Posed, 1> unbent{sprite::Posed{fore_drawn, fore_drawn}};
        sprite::Mesh same;
        sprite::skinned(sleeve, grid, {upper, upper}, unbent, same);
        for (std::size_t corner = 0; corner < same.at.size(); ++corner) expect(near(same.at[corner], straight.at[corner]));
    };

    "the same bones, the same corners, bit for bit; a mesh grown once is not grown again; a cel's four placed corners are one cell"_test = [] {
        const sprite::Bone upper{{3.25f, -7.5f}, 0.37}, fore_drawn{{40, 2}, -0.2}, fore_now{{38, 5}, 1.1};
        const std::array<sprite::Posed, 1> bends{sprite::Posed{fore_drawn, fore_now}};
        const sprite::Grid grid{.columns = 6, .rows = 3, .blend = 12, .facing = -1, .length = 64};
        sprite::Mesh one, other;
        sprite::skinned(sleeve, grid, {upper, {upper.joint, 0.9}}, bends, one);
        const Point* held = one.at.data();
        sprite::skinned(sleeve, grid, {upper, {upper.joint, 0.9}}, bends, other);
        expect(one.at == other.at && one.uv == other.uv && one.index == other.index);
        sprite::skinned(sleeve, grid, {upper, {upper.joint, 0.9}}, bends, one);
        expect(one.at.data() == held) << "nothing allocated the second time";
        mlibs::hash::Fnv1a hash;
        for (const Point corner : one.at) hash.f32(corner.x).f32(corner.y);
        const std::uint64_t bits = hash.value();
        std::println("a sleeve of 6 × 3 cells bent at the elbow, facing left: its corners' bits {:#018x}", bits);

        expect(eq(bits, std::uint64_t{0xfeeba9b7df6eeb44ull})) << "pinned: the corners gongfu's fighters are drawn from";

        const std::array<Point, 4> placed{Point{1, 2}, {3, 2}, {3, 5}, {1, 5}};
        sprite::cornered({10, 20, 30, 40}, placed, one);
        expect(eq(one.at.size(), std::size_t{4}) && one.at[2] == Point{3, 5} && one.uv[0] == Point{10, 20} && one.uv[2] == Point{40, 60} && eq(one.index.size(), std::size_t{6}));
    };
}};
