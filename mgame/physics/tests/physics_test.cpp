// Swept collision: a face met at the exact time with its side; a corner met
// on its curve, the normal from the corner; a fast circle that would skip a
// thin box in a sampled step still meets it; a circle already inside going
// in is met at once, going out is let go; misses; reflections; a ball
// bounced around a box of bricks for ten thousand moves gives the same bits
// everywhere.
import std;
import mgame.physics;
import openmui.testing;

using namespace omui::testing;
using mgame::Box;
using mgame::V2;

int main(int argc, char** argv) {
    Suite s{"mgame.physics"};

    s.test("a face: the exact time and the side; a fast circle through a thin box still meets it", [] {
        const Box b{100, 0, 50, 100};
        auto hit = mgame::sweep({0, 50}, 10, {200, 0}, b);
        check(hit && hit->t == 0.45 && hit->normal == V2{-1, 0}, hit ? std::format("t {}", hit->t) : std::string("none"));
        auto down = mgame::sweep({125, -100}, 5, {0, 200}, b);
        check(down && down->t == 0.475 && down->normal == V2{0, -1});
        // A box 2 wide, a circle moving 1000 in one step: sampled at its ends it would miss.
        auto thin = mgame::sweep({0, 50}, 3, {1000, 0}, Box{500, 0, 2, 100});
        check(thin && thin->t == 0.497, "met mid-step");
        check(!mgame::sweep({0, 200}, 10, {200, 0}, b), "beside it: missed");
        check(!mgame::sweep({0, 50}, 10, {50, 0}, b), "short of it: missed");
    });

    s.test("a corner: met on its curve, the normal from the corner to the centre", [] {
        const Box b{100, 100, 50, 50};
        // Diagonally at the top-left corner.
        auto hit = mgame::sweep({50, 50}, 10, {50, 50}, b);
        check(hit.has_value(), "met");
        if (!hit) return;
        const V2 at = V2{50, 50} + V2{50, 50} * hit->t;
        check(std::abs(mgame::length(at - V2{100, 100}) - 10) < 1e-9, "on the corner's circle");
        check(std::abs(hit->normal.x + std::sqrt(0.5)) < 1e-12 && std::abs(hit->normal.y + std::sqrt(0.5)) < 1e-12, "out of the corner, diagonally");
        // Grazing past the corner's circle: missed.
        check(!mgame::sweep({50, 80}, 10, {100, -100}, Box{100, 100, 50, 50}), "past the corner");
    });

    s.test("a graze along a face, or past a corner's tangent, is no hit; a corner met from a hair away keeps its precision", [] {
        const Box b{100, 100, 50, 50};
        check(!mgame::sweep({50, 90}, 10, {200, 0}, b), "sliding along the top face, touching it: no bounce");
        // Very near the corner, moving straight at it: the time is tiny and exact, not lost to cancellation.
        const double gap = 1e-9;
        const V2 start = V2{100, 100} - V2{std::sqrt(0.5), std::sqrt(0.5)} * (10 + gap);
        auto hit = mgame::sweep(start, 10, {1, 1}, b);
        check(hit && hit->t > 0 && hit->t < 2e-9, hit ? std::format("t {}", hit->t) : std::string("none"));
    });

    s.test("already inside: going in, met at once on the nearest side; going out, let go", [] {
        const Box b{0, 0, 100, 100};
        auto in = mgame::sweep({105, 50}, 10, {-5, 0}, b);
        check(in && in->t == 0 && in->normal == V2{1, 0});
        check(!mgame::sweep({105, 50}, 10, {5, 0}, b), "moving out");
        auto deep = mgame::sweep({90, 50}, 5, {-1, 0}, b);
        check(deep && deep->t == 0 && deep->normal == V2{1, 0}, "the centre inside: out by the nearest face");
    });

    s.test("a ball in a walled room of bricks, ten thousand moves: never inside a wall, the same bits everywhere", [] {
        const std::vector<Box> walls{{-20, 0, 20, 720}, {1280, 0, 20, 720}, {0, -20, 1280, 20}, {0, 720, 1280, 20}};
        std::vector<Box> boxes = walls;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 10; ++col) boxes.push_back({100.0 + col * 110, 80.0 + row * 40, 96, 28});
        V2 at{640, 600}, v{7.3, -5.9};
        const double r = 8;
        std::uint64_t h = 0xcbf29ce484222325ull;
        int bounces = 0;
        for (int step = 0; step < 10000; ++step) {
            double left = 1;
            for (int k = 0; k < 8 && left > 0; ++k) {
                std::optional<mgame::Hit> first;
                for (const Box& b : boxes)
                    if (auto hit = mgame::sweep(at, r, v * left, b); hit && (!first || hit->t < first->t)) first = hit;
                if (!first) {
                    at += v * left;
                    left = 0;
                    break;
                }
                at += v * (left * first->t);
                v = mgame::reflect(v, first->normal);
                left *= 1 - first->t;
                ++bounces;
            }
            for (const Box& w : walls) check(!mgame::overlaps(at, r - 1e-6, w), "never inside a wall");
            for (const double c : {at.x, at.y}) {
                const auto bits = std::bit_cast<std::uint64_t>(c);
                for (int b = 0; b < 8; ++b) h = (h ^ ((bits >> (8 * b)) & 0xff)) * 0x100000001b3ull;
            }
        }
        std::println("    room {:016x} ({} bounces)", h, bounces);
        check(bounces > 100, std::format("{} bounces", bounces));
        check_eq(h, std::uint64_t{0xa2bdb8858d7dc738});  // GCC, LLVM and wasm
    });

    return s.run(argc, argv);
}
