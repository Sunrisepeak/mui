// A ray stepped through a lattice (mlibs.geom.api:walk; the foundation plan's §5.7, D-184): the cells it passes,
// each once and in order, the same flat and deep, checked against points taken along the ray.
import std;
import mlibs.geom;
import mlibs.math;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace geom = mlibs::geom;
namespace math = mlibs::math;
using math::Vec3;

namespace {
using Cell = std::array<std::int32_t, 3>;
std::vector<geom::Passed> passed(const geom::Ray& ray, const geom::Lattice& lattice, float farthest = std::numeric_limits<float>::max()) {
    std::vector<geom::Passed> out;
    geom::walk(ray, lattice, [&](const geom::Passed& at) { return out.push_back(at), true; }, farthest);
    return out;
}
}  // namespace

static UTest cases{"geom.walk", [] {
    "along a row, across a flat lattice, from outside, and not at all"_test = [] {
        const geom::Lattice flat{.cells = {8, 6, 1}, .cell = 1.0f};
        const auto row = passed({{0.5f, 2.5f, 0.5f}, {1, 0, 0}}, flat);
        expect(fatal(eq(row.size(), std::size_t{8})));
        for (std::size_t index = 0; index < row.size(); ++index)
            expect(row[index].x == static_cast<std::int32_t>(index) && row[index].y == 2 && row[index].z == 0 && row[index].face == (index ? 0 : -1) &&
                   row[index].entered == (index ? static_cast<float>(index) - 0.5f : 0.0f));
        // A diagonal that is not through corners: one cell for each face crossed, and one.
        const geom::Ray slanted{{0.25f, 0.5f, 0.5f}, math::normalized(Vec3{7, 4, 0})};
        const auto slope = passed(slanted, flat);
        expect(eq(slope.size(), std::size_t{7 + 4 + 1})) << slope.size();
        expect(slope.back().x == 7 && slope.back().y == 4);
        for (std::size_t index = 1; index < slope.size(); ++index) {
            const int moved = std::abs(slope[index].x - slope[index - 1].x) + std::abs(slope[index].y - slope[index - 1].y);
            expect(moved == 1 && slope[index].entered >= slope[index - 1].entered) << "a face at a time, further each time";
        }
        // From outside it enters where it meets the lattice; beside it, nothing; backwards, leftward.
        const auto entering = passed({{-3.0f, 1.5f, 0.5f}, {1, 0, 0}}, flat);
        expect(eq(entering.size(), std::size_t{8}) && entering[0].x == 0 && entering[0].entered == 3.0f);
        expect(passed({{-3.0f, 9.5f, 0.5f}, {1, 0, 0}}, flat).empty());
        const auto back = passed({{7.5f, 0.5f, 0.5f}, {-1, 0, 0}}, flat);
        expect(eq(back.size(), std::size_t{8}) && back.back().x == 0);
        // No further than asked; and it stops when told.
        expect(eq(passed({{0.5f, 2.5f, 0.5f}, {1, 0, 0}}, flat, 3.0f).size(), std::size_t{4}));
        std::size_t seen = 0;
        expect(eq(geom::walk({{0.5f, 2.5f, 0.5f}, {1, 0, 0}}, flat, [&](const geom::Passed& at) { return ++seen, at.x < 2; }), std::size_t{3}) && eq(seen, std::size_t{3}));
        // Through a corner exactly: one axis at a time, x first.
        const auto corner = passed({{0.5f, 0.5f, 0.5f}, math::normalized(Vec3{1, 1, 0})}, flat);
        expect(corner.size() >= 3 && corner[1].x == 1 && corner[1].y == 0 && corner[2].x == 1 && corner[2].y == 1);
    };
    "through a deep lattice: every cell a point of the ray is in was passed, in order; its bits pinned"_test = [] {
        const geom::Lattice deep{.cells = {32, 24, 16}, .cell = 0.5f};
        math::SplitMix64 random{9};
        const auto unit = [&] { return static_cast<float>(math::unit24(random.next())); };
        mlibs::hash::Fnv1a hash;
        std::size_t cells = 0;
        for (int query = 0; query < 200; ++query) {
            const Vec3 from{unit() * 16.0f, unit() * 12.0f, unit() * 8.0f};
            const geom::Ray ray{from, math::normalized(Vec3{unit() - 0.5f, unit() - 0.5f, unit() - 0.5f})};
            const auto walked = passed(ray, deep);
            expect(fatal(!walked.empty()));
            std::set<Cell> in_walk;
            for (const geom::Passed& at : walked) {
                expect(in_walk.insert({at.x, at.y, at.z}).second) << "a cell twice";
                hash.le(static_cast<std::uint32_t>(at.x), 4).le(static_cast<std::uint32_t>(at.y), 4).le(static_cast<std::uint32_t>(at.z), 4).f32(at.entered);
            }
            // Points along the ray, well inside their cells: each cell is one the walk passed.
            for (float distance = 0; distance < 40.0f; distance += 0.013f) {
                const Vec3 point = ray.from + ray.along * distance;
                const Vec3 scaled = point * (1.0f / deep.cell);
                const Cell cell{static_cast<std::int32_t>(std::floor(scaled.x)), static_cast<std::int32_t>(std::floor(scaled.y)), static_cast<std::int32_t>(std::floor(scaled.z))};
                if (cell[0] < 0 || cell[1] < 0 || cell[2] < 0 || cell[0] >= 32 || cell[1] >= 24 || cell[2] >= 16) break;
                const auto inside = [](float value) { return value - std::floor(value) > 0.01f && value - std::floor(value) < 0.99f; };
                if (inside(scaled.x) && inside(scaled.y) && inside(scaled.z)) expect(in_walk.contains(cell)) << std::format("ray {}: ({}, {}, {}) at {}", query, cell[0], cell[1], cell[2], distance);
            }
            cells += walked.size();
        }
        std::println("200 rays through 32 × 24 × 16: {} cells passed, bits {:016x}", cells, hash.value());
        expect(eq(hash.value(), std::uint64_t{0x92847746d880f302})) << std::format("{:016x}", hash.value());
    };
}};
