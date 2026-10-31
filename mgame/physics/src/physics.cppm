// mgame.physics — swept collision: when a moving circle first touches a box.
//
//     if (auto hit = mgame::sweep(ball.at, ball.radius, ball.velocity * left, brick)) {
//         ball.at += ball.velocity * (left * hit->t);
//         ball.velocity = mgame::reflect(ball.velocity, hit->normal);
//     }
//
// A circle of radius r moving by d meets a box where its centre's path meets
// the box grown by r: four faces moved out by r, and four quarter circles of
// radius r at the corners. The earliest of those in [0, 1] of the move is the
// hit, with the side's normal (a corner's points from the corner to the
// centre). A circle that already overlaps a box and moves further in meets it
// at once (t = 0), on the side it is nearest, so a game pushed into a box by
// something else does not pass through it.
//
// Only + − × ÷ and √ — which IEEE 754 rounds exactly — on doubles, built
// without contraction or fast math: the same bits everywhere (as hanim's).
export module mgame.physics;

import std;

export namespace mgame {

struct V2 {
    double x = 0, y = 0;
    friend constexpr V2 operator+(V2 a, V2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend constexpr V2 operator-(V2 a, V2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend constexpr V2 operator*(V2 a, double k) noexcept { return {a.x * k, a.y * k}; }
    friend constexpr V2 operator*(double k, V2 a) noexcept { return {a.x * k, a.y * k}; }
    constexpr V2& operator+=(V2 b) noexcept { return x += b.x, y += b.y, *this; }
    constexpr V2 operator-() const noexcept { return {-x, -y}; }
    // Written out: GCC 16 fails on a defaulted friend comparison of an imported type.
    friend constexpr bool operator==(V2 a, V2 b) noexcept { return a.x == b.x && a.y == b.y; }
};
[[nodiscard]] constexpr double dot(V2 a, V2 b) noexcept { return a.x * b.x + a.y * b.y; }
[[nodiscard]] double length(V2 v) noexcept;
[[nodiscard]] V2 normalized(V2 v) noexcept;  // the zero vector stays zero
// v bounced off a surface of normal n (unit): v − 2(v·n)n.
[[nodiscard]] constexpr V2 reflect(V2 v, V2 n) noexcept { return v - n * (2 * dot(v, n)); }

// An axis-aligned box: its least corner and its size.
struct Box {
    double x = 0, y = 0, w = 0, h = 0;
    [[nodiscard]] constexpr double right() const noexcept { return x + w; }
    [[nodiscard]] constexpr double bottom() const noexcept { return y + h; }
    friend constexpr bool operator==(Box a, Box b) noexcept { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }
};

struct Hit {
    double t = 0;  // of the move, in [0, 1]
    V2     normal; // the side met, out of the box (unit)
};

// When a circle at p of radius r moving by d first touches b, if it does within the move.
[[nodiscard]] std::optional<Hit> sweep(V2 p, double r, V2 d, const Box& b) noexcept;
// Whether a circle at p of radius r overlaps b.
[[nodiscard]] bool overlaps(V2 p, double r, const Box& b) noexcept;

}  // namespace mgame
