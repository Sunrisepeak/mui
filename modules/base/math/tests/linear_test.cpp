// mlibs.math's linear algebra (the foundation plan's M6.3): the conventions it states held by cases — right-handed,
// a column vector on the right, depth 0 … 1, a camera along −Z — the algebra's identities, and the bits of what
// is built from an angle pinned (the same on every platform).
import std;
import mlibs.math;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace mlibs::math;

namespace {
constexpr float quarter = 1.57079632679489661923f;
bool near_to(float one, float other, float within = 1e-5f) { return std::abs(one - other) <= within; }
bool near_to(Vec3 one, Vec3 other, float within = 1e-5f) { return near_to(one.x, other.x, within) && near_to(one.y, other.y, within) && near_to(one.z, other.z, within); }
bool near_to(const Mat4& one, const Mat4& other, float within = 1e-5f) {
    for (std::size_t index = 0; index < 16; ++index)
        if (!near_to(one.values[index], other.values[index], within)) return false;
    return true;
}
std::uint64_t bits(const Mat4& matrix) {
    mlibs::hash::Fnv1a hash;
    for (const float value : matrix.values) hash.f32(value);
    return hash.value();
}
}  // namespace

UTest cases{"math.linear", [] {
    "vectors: right-handed — x cross y is z; a length, a direction of length 1, and no direction invented for zero"_test = [] {
        constexpr Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
        static_assert(cross(x, y) == z && cross(y, z) == x && cross(z, x) == y);
        static_assert(dot(Vec3{1, 2, 3}, Vec3{4, -5, 6}) == 12.0f);
        static_assert(Vec3{1, 2, 3} + Vec3{1, 1, 1} * 2.0f - Vec3{0, 0, 1} == Vec3{3, 4, 4});
        static_assert(scaled({1, 2, 3}, {2, 3, 4}) == Vec3{2, 6, 12} && mix(Vec3{0, 0, 0}, Vec3{2, 4, 8}, 0.25f) == Vec3{0.5f, 1, 2});
        expect(eq(length(Vec3{3, 4, 12}), 13.0f));
        expect(near_to(length(normalized(Vec3{3, -4, 12})), 1.0f) && normalized(Vec3{}) == Vec3{});
        static_assert(Vec4{Vec3{1, 2, 3}, 1}.xyz() == Vec3{1, 2, 3});
    };

    "a rotation: a quarter turn about z takes x to y; a product is the second, then the first; its conjugate undoes it"_test = [] {
        const Quat about_z = Quat::about({0, 0, 2}, quarter), about_x = Quat::about({1, 0, 0}, quarter);
        expect(near_to(rotate(about_z, {1, 0, 0}), {0, 1, 0}));
        expect(near_to(rotate(about_x, {0, 1, 0}), {0, 0, 1}));
        expect(near_to(rotate(about_z * about_x, {0, 1, 0}), rotate(about_z, rotate(about_x, {0, 1, 0})))) << "about x first, then about z";
        expect(near_to(rotate(conjugate(about_z) * about_z, {3, -2, 5}), {3, -2, 5}));
        expect(Quat::about({}, 1.0f) == Quat{}) << "no axis: no rotation";
        expect(near_to(dot(about_z, about_z), 1.0f));
        // Half-way between none and a quarter turn is an eighth; by the shorter arc whichever sign the end has.
        const Quat eighth = Quat::about({0, 0, 1}, quarter * 0.5f);
        const Quat half = mix(Quat{}, about_z, 0.5f), other = mix(Quat{}, Quat{-about_z.x, -about_z.y, -about_z.z, -about_z.w}, 0.5f);
        expect(near_to(rotate(half, {1, 0, 0}), rotate(eighth, {1, 0, 0})) && near_to(rotate(other, {1, 0, 0}), rotate(eighth, {1, 0, 0})));
    };

    "a matrix: columns, a column vector on the right; a product is the second, then the first; the rotation's matrix turns as the rotation does"_test = [] {
        constexpr Mat4 move = translation({1, 2, 3}), grow = scaling({2, 2, 2});
        static_assert(Mat4{} * move == move && move * Mat4{} == move);
        static_assert(move.at(3, 0) == 1 && move.at(3, 1) == 2 && move.at(3, 2) == 3, "the translation is the fourth column");
        static_assert(point(move * grow, {1, 1, 1}) == Vec3{3, 4, 5});
        static_assert(point(grow * move, {1, 1, 1}) == Vec3{4, 6, 8});
        static_assert(direction(move, {1, 0, 0}) == Vec3{1, 0, 0});
        static_assert(transposed(transposed(move)) == move && transposed(move).at(0, 3) == 1);
        const Quat turn = Quat::about({1, 2, -1}, 0.7f);
        for (const Vec3 probe : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0.3f, -2, 5}}) expect(near_to(direction(rotation(turn), probe), rotate(turn, probe)));
        const Transform placed{.translation = {4, 0, -1}, .rotation = turn, .scale = {1, 2, 3}};
        expect(near_to(point(placed.matrix(), {1, 1, 1}), placed.point({1, 1, 1}))) << "scaled, then turned, then moved";
    };

    "an inverse: of a placement, of a projection; none for a matrix that has none"_test = [] {
        const Mat4 placed = Transform{.translation = {4, 0, -1}, .rotation = Quat::about({1, 2, -1}, 0.7f), .scale = {1, 2, 3}}.matrix();
        const auto back = inverse(placed);
        expect(fatal(back.has_value()));
        expect(near_to(*back * placed, Mat4{}) && near_to(placed * *back, Mat4{}));
        const Mat4 lens = perspective(1.0f, 1.5f, 0.1f, 50.0f);
        const auto unlens = inverse(lens);
        expect(fatal(unlens.has_value()));
        expect(near_to(point(*unlens, point(lens, {0.3f, -0.2f, -7})), {0.3f, -0.2f, -7}, 1e-3f));
        static_assert(!inverse(scaling({1, 0, 1})).has_value());
        static_assert(inverse(translation({1, 2, 3})) == translation({-1, -2, -3}));
    };

    "a camera looks along −Z: what it looks at is straight ahead, what is to its right has x above 0, up is up"_test = [] {
        const Mat4 view = look_at({0, 2, 5}, {0, 2, 0}, {0, 1, 0});
        expect(near_to(point(view, {0, 2, 0}), {0, 0, -5}));
        expect(near_to(point(view, {1, 2, 0}), {1, 0, -5}) && near_to(point(view, {0, 3, 0}), {0, 1, -5}));
        const Mat4 turned = look_at({3, 0, 0}, {0, 0, 0}, {0, 1, 0});  // looking along −x
        expect(near_to(point(turned, {0, 0, 0}), {0, 0, -3}) && near_to(point(turned, {0, 0, -1}), {1, 0, -3})) << "−z of the world is to its right";
        expect(look_at({1, 1, 1}, {1, 1, 1}, {0, 1, 0}) == Mat4{} && look_at({0, 0, 0}, {0, 5, 0}, {0, 1, 0}) == Mat4{}) << "no line of sight, or up along it: no view";
    };

    "depth is 0 … 1: the near plane at 0 and the far at 1; reversed, near at 1 and what is far away toward 0; y up, x by the aspect"_test = [] {
        const Mat4 lens = perspective(quarter, 2.0f, 0.5f, 100.0f);
        expect(near_to(point(lens, {0, 0, -0.5f}).z, 0.0f) && near_to(point(lens, {0, 0, -100}).z, 1.0f, 1e-4f));
        // A quarter turn of view top to bottom: at distance 1 the top edge is at height tan(π/8).
        const float edge = std::tan(quarter * 0.5f);
        expect(near_to(point(lens, {0, edge, -1}).y, 1.0f) && near_to(point(lens, {2 * edge, 0, -1}).x, 1.0f)) << "the frame's edges are at ±1";
        const float nearer = point(lens, {0, 0, -1}).z, further = point(lens, {0, 0, -10}).z;
        expect(nearer < further);
        const Mat4 reversed = perspective_reversed(quarter, 2.0f, 0.5f);
        expect(near_to(point(reversed, {0, 0, -0.5f}).z, 1.0f) && near_to(point(reversed, {0, 0, -1e6f}).z, 0.0f, 1e-5f));
        expect(point(reversed, {0, 0, -1}).z > point(reversed, {0, 0, -10}).z);
        expect(near_to(point(reversed, {0, edge, -1}).y, 1.0f));
        constexpr Mat4 flat = orthographic(-2, 6, -1, 3, 1, 11);
        static_assert(point(flat, {-2, -1, -1}) == Vec3{-1, -1, 0} && point(flat, {6, 3, -11}) == Vec3{1, 1, 1});
    };

    "what is built from an angle is the same bits everywhere: a rotation, a view, the two perspectives"_test = [] {
        const Mat4 turn = rotation(Quat::about({1, 2, 3}, 0.9f));
        const Mat4 clip = perspective(1.1f, 16.0f / 9.0f, 0.1f, 300.0f) * look_at({3, 4, 5}, {0, 1, 0}, {0, 1, 0}) * turn;
        const Mat4 reversed = perspective_reversed(0.8f, 4.0f / 3.0f, 0.05f);
        std::println("    linear {:016x} {:016x} {:016x}", bits(turn), bits(clip), bits(reversed));
        expect(eq(bits(turn), std::uint64_t{0xe4190449ec0b86bb}));
        expect(eq(bits(clip), std::uint64_t{0x4990148cfa6edf2e}));
        expect(eq(bits(reversed), std::uint64_t{0x6ce7b24fb31ff330}));
    };
}};
