// mlibs.geom's tree of boxes (the foundation plan's §5.7, D-184, step a): what it must be — every thing in one
// leaf, each node holding what is under it — and what it must answer: the same as asking every thing, for boxes
// and for rays, and the same tree for the same things, its bits pinned.
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

float unit(math::SplitMix64& random) { return static_cast<float>(math::unit24(random.next())); }
// Boxes scattered through a room of 100: small, some long, a few of no size, some the same as another.
std::vector<geom::Box3> scattered(std::size_t count, std::uint64_t seed) {
    math::SplitMix64 random{seed};
    std::vector<geom::Box3> boxes;
    for (std::size_t index = 0; index < count; ++index) {
        const Vec3 at{unit(random) * 100.0f, unit(random) * 100.0f, unit(random) * 100.0f};
        const Vec3 size = index % 17 == 0 ? Vec3{0, 0, 0} : Vec3{unit(random) * 3.0f, unit(random) * (index % 5 == 0 ? 20.0f : 3.0f), unit(random) * 3.0f};
        geom::Box3 box;
        box.grow(at), box.grow(at + size);
        boxes.push_back(index % 23 == 22 ? boxes[index - 1] : box);
    }
    return boxes;
}
std::uint64_t bits(const geom::Bvh& tree) {
    mlibs::hash::Fnv1a hash;
    for (const geom::Bvh::Node& node : tree.nodes)
        hash.f32(node.bounds.low.x).f32(node.bounds.low.y).f32(node.bounds.low.z).f32(node.bounds.high.x).f32(node.bounds.high.y).f32(node.bounds.high.z).le(node.first, 4).le(node.count, 4);
    for (const std::uint32_t item : tree.order) hash.le(item, 4);
    return hash.value();
}

}  // namespace

static UTest cases{"geom.bvh", [] {
    "a tree made a part at a time is the tree made at once, node for node, however the parts are cut"_test = [] {
        const auto boxes = scattered(1000, 7);
        const geom::Bvh whole = geom::bvh_of(boxes);
        for (const std::size_t things : {std::size_t{0}, std::size_t{1}, std::size_t{100}, std::size_t{1000}, std::size_t{5000}}) {
            geom::BvhBuild build(boxes);
            expect(build.begun() && !build.done());
            std::uint32_t steps = 1;
            while (!build.step(things)) ++steps;
            expect(build.done());
            const geom::Bvh made = build.take();
            expect(!build.begun()) << "taken: as before it began";
            expect(eq(bits(made), bits(whole)) && eq(made.depth, whole.depth) && eq(made.nodes.size(), whole.nodes.size())) << std::format("{} things a step", things);
            // A step makes a node at the least; the root alone looks at every thing.
            if (things == 0) expect(eq(steps, static_cast<std::uint32_t>(whole.nodes.size()))) << "a node a step";
            if (things == 1000) expect(gt(steps, 5u) && lt(steps, 20u)) << std::format("{} steps", steps);
            if (things == 5000) expect(eq(steps, 2u)) << std::format("{} steps", steps);
        }
        // Of nothing: done as it begins, a tree of no nodes.
        geom::BvhBuild none{std::span<const geom::Box3>{}};
        expect(none.begun() && none.done() && none.step(10) && none.take().nodes.empty());
        expect(!geom::BvhBuild{}.begun());
        // Begun again in the room of a tree no longer needed: the same tree; one given up is not begun.
        geom::BvhBuild again(boxes);
        static_cast<void>(again.step(300));
        again.drop();
        expect(!again.begun());
        again.begin(boxes, {}, geom::bvh_of(scattered(1000, 3)));
        while (!again.step(700)) {}
        expect(eq(bits(again.take()), bits(whole)));
    };
    "a tree over boxes: every thing in one leaf, each node holding what is under it; of nothing, nothing; the same things, the same tree"_test = [] {
        const auto boxes = scattered(1000, 7);
        const geom::Bvh tree = geom::bvh_of(boxes);
        expect(geom::check(tree, boxes).has_value()) << geom::check(tree, boxes).error_or("");
        expect(le(tree.depth, 10u) && gt(tree.nodes.size(), 250u)) << std::format("depth {}, {} nodes", tree.depth, tree.nodes.size());
        expect(geom::bvh_of(std::span<const geom::Box3>{}).nodes.empty() && geom::check(geom::bvh_of(std::span<const geom::Box3>{}), {}).has_value());
        const geom::Bvh one = geom::bvh_of(std::span<const geom::Box3>{boxes.data(), 1});
        expect(eq(one.nodes.size(), std::size_t{1}) && eq(one.nodes[0].count, 1u) && eq(one.depth, 1u));
        const geom::Bvh fine = geom::bvh_of(boxes, {.leaf = 1});
        expect(geom::check(fine, boxes, {.leaf = 1}).has_value() && eq(fine.nodes.size(), boxes.size() * 2 - 1)) << "a thing a leaf: 2n - 1 nodes";
        // Things all in one place still part: by their numbers.
        const std::vector<geom::Box3> same(64, boxes[3]);
        expect(geom::check(geom::bvh_of(same), same).has_value() && le(geom::bvh_of(same).depth, 5u));
        std::println("a tree over 1000 boxes: {} nodes, depth {}, bits {:016x}", tree.nodes.size(), tree.depth, bits(tree));
        expect(eq(bits(tree), bits(geom::bvh_of(boxes)))) << "built twice";
        expect(eq(bits(tree), std::uint64_t{0x686f4b01e4d74ff5})) << std::format("{:016x}", bits(tree));
        // A tree that is wrong is said to be.
        geom::Bvh broken = tree;
        broken.order[5] = broken.order[6];
        expect(!geom::check(broken, boxes).has_value());
        broken = tree, broken.nodes[1].bounds.high.x -= 50.0f;
        expect(!geom::check(broken, boxes).has_value());
    };
    "what is within a region: the same things as asking every box, each once"_test = [] {
        const auto boxes = scattered(1000, 11);
        const geom::Bvh tree = geom::bvh_of(boxes);
        math::SplitMix64 random{3};
        std::size_t asked = 0, found_all = 0, visited_all = 0;
        for (int query = 0; query < 200; ++query) {
            geom::Box3 region;
            const Vec3 at{unit(random) * 100.0f, unit(random) * 100.0f, unit(random) * 100.0f};
            region.grow(at), region.grow(at + Vec3{unit(random) * 20.0f, unit(random) * 20.0f, unit(random) * 20.0f});
            std::vector<std::uint32_t> by_tree, by_all;
            std::size_t visited = 0;
            geom::each_within(tree, region, [&](std::uint32_t item) {
                ++visited;
                if (geom::overlap(boxes[item], region)) by_tree.push_back(item);
            });
            for (std::uint32_t item = 0; item < boxes.size(); ++item)
                if (geom::overlap(boxes[item], region)) by_all.push_back(item);
            std::ranges::sort(by_tree);
            expect(by_tree == by_all) << std::format("query {}: {} by the tree, {} by all", query, by_tree.size(), by_all.size());
            ++asked, found_all += by_all.size(), visited_all += visited;
        }
        std::println("{} regions: {} things within, {} looked at of {} (asking all)", asked, found_all, visited_all, asked * boxes.size());
        expect(lt(visited_all, asked * boxes.size() / 8)) << "the tree spares most of the asking";
    };
    "what a ray meets first: the same thing and distance as asking every box; of two as near, the lower number"_test = [] {
        const auto boxes = scattered(1000, 19);
        const geom::Bvh tree = geom::bvh_of(boxes);
        math::SplitMix64 random{5};
        std::size_t hits = 0, asked_all = 0;
        for (int query = 0; query < 300; ++query) {
            const Vec3 from{unit(random) * 100.0f, unit(random) * 100.0f, unit(random) * 100.0f};
            const geom::Ray ray{from, math::normalized(Vec3{unit(random) - 0.5f, unit(random) - 0.5f, unit(random) - 0.5f})};
            std::size_t asked = 0;
            const auto by_tree = geom::nearest(tree, ray, [&](std::uint32_t item) {
                ++asked;
                return geom::meet(ray, boxes[item]);
            });
            std::optional<geom::RayHit> by_all;
            for (std::uint32_t item = 0; item < boxes.size(); ++item)
                if (const auto distance = geom::meet(ray, boxes[item]); distance && (!by_all || *distance < by_all->distance)) by_all = geom::RayHit{item, *distance};
            expect(by_tree.has_value() == by_all.has_value() && (!by_all || (by_tree->item == by_all->item && by_tree->distance == by_all->distance)))
                << std::format("ray {}: {} at {} by the tree, {} at {} by all", query, by_tree ? by_tree->item : 0, by_tree ? by_tree->distance : -1, by_all ? by_all->item : 0, by_all ? by_all->distance : -1);
            hits += by_all.has_value(), asked_all += asked;
        }
        std::println("300 rays: {} met something; {} boxes asked of {} (asking all)", hits, asked_all, 300 * boxes.size());
        expect(gt(hits, 30u) && lt(asked_all, 300 * boxes.size() / 8));
        // No further than asked.
        const Vec3 target = boxes[1].centre();
        const geom::Ray along{{-10, target.y, target.z}, {1, 0, 0}};
        const auto far = geom::nearest(tree, along, [&](std::uint32_t item) { return geom::meet(along, boxes[item]); });
        expect(fatal(far.has_value()));
        expect(!geom::nearest(tree, along, [&](std::uint32_t item) { return geom::meet(along, boxes[item]); }, far->distance * 0.5f).has_value());
    };
    "a ray and a mesh: a ball of radius 2 is met at its skin from outside and from within; beside it, nothing"_test = [] {
        const geom::Mesh ball = geom::sphere(2.0f, 24, 48);
        const geom::Bvh tree = geom::bvh_of(ball);
        std::vector<geom::Box3> boxes(ball.triangles());
        for (std::size_t triangle = 0; triangle < boxes.size(); ++triangle)
            for (int corner = 0; corner < 3; ++corner) boxes[triangle].grow(ball.positions[ball.indices[triangle * 3 + static_cast<std::size_t>(corner)]]);
        expect(geom::check(tree, boxes).has_value());
        const auto outside = geom::meet({{0.3f, 0.2f, 10}, {0, 0, -1}}, ball, tree);
        expect(fatal(outside.has_value()));
        expect(lt(std::abs(outside->distance - (10.0f - std::sqrt(4.0f - 0.13f))), 0.02f)) << outside->distance;
        const auto within = geom::meet({{0, 0, 0}, math::normalized(Vec3{1, 1, 1})}, ball, tree);
        expect(fatal(within.has_value()));
        expect(lt(std::abs(within->distance - 2.0f), 0.02f)) << within->distance;
        expect(!geom::meet({{2.5f, 0, 10}, {0, 0, -1}}, ball, tree).has_value());
        // The same as asking every triangle.
        const geom::Ray ray{{0.3f, 0.2f, 10}, {0, 0, -1}};
        std::optional<float> by_all;
        for (std::size_t triangle = 0; triangle < ball.triangles(); ++triangle)
            if (const auto distance = geom::meet(ray, ball.positions[ball.indices[triangle * 3]], ball.positions[ball.indices[triangle * 3 + 1]], ball.positions[ball.indices[triangle * 3 + 2]]);
                distance && (!by_all || *distance < *by_all))
                by_all = distance;
        expect(by_all == std::optional<float>(outside->distance));
    };
}};
