// xrender.scene (the foundation plan's §5.1, M6.3): a scene is its ids and its change list; what a view sees is a
// pure function of the two — the same whatever order the scene was made in, the same by the tree as by looking at
// everything, its bytes pinned; a mirror that follows the list is the scene, edit after edit; a steady frame
// allocates nothing; and an instance that is no draw says why.
import std;
import xrender.scene;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace scene = xrender::scene;
namespace math = mlibs::math;
namespace geom = mlibs::geom;

// Allocations counted: a steady frame's extraction makes none.
namespace {
std::atomic<std::uint64_t> allocations{0};
}
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    std::terminate();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

float unit(math::SplitMix64& random) { return static_cast<float>(math::unit24(random.next())); }
// A field of things: `count` boxes scattered through a space 200 across, of 5 meshes and 4 materials.
std::vector<scene::Instance> field(std::uint32_t count, std::uint64_t seed) {
    math::SplitMix64 random{seed};
    std::vector<scene::Instance> made;
    for (std::uint32_t index = 0; index < count; ++index) {
        const math::Vec3 at{unit(random) * 200 - 100, unit(random) * 40 - 20, unit(random) * 200 - 100};
        const float size = 0.5f + unit(random) * 3;
        const math::Quat turn = math::Quat::about(math::normalized(math::Vec3{unit(random) - 0.5f, 1, unit(random) - 0.5f}), unit(random) * 6.28f);
        made.push_back({.id = 1000 + std::uint64_t{index} * 7,
                        .mesh = static_cast<std::uint32_t>(random.next() % 5),
                        .material = static_cast<std::uint32_t>(random.next() % 4),
                        .world = math::Transform{at, turn, {size, size, size}}.matrix(),
                        .bounds = {{-1, -1, -1}, {1, 1, 1}},
                        .layers = index % 11 == 0 ? 2u : 1u,
                        .hidden = index % 17 == 0});
    }
    return made;
}
scene::View looking(math::Vec3 eye, math::Vec3 target, std::uint32_t layers = 1) {
    return {.view = math::look_at(eye, target, {0, 1, 0}), .projection = math::perspective(1.0f, 16.0f / 9.0f, 0.5f, 150.0f), .layers = layers};
}
std::uint64_t bits(const scene::Extracted& seen) {
    mlibs::hash::Fnv1a hash;
    for (const scene::Draw& draw : seen.draws) hash.le(draw.id, 8).le(draw.instance, 4).le(draw.mesh, 4).le(draw.material, 4).f32(draw.depth);
    for (const scene::Batch& batch : seen.batches) hash.le(batch.mesh, 4).le(batch.material, 4).le(batch.first, 4).le(batch.count, 4);
    for (const std::uint32_t light : seen.lights) hash.le(light, 4);
    return hash.value();
}
bool same(const scene::Extracted& one, const scene::Extracted& other) { return bits(one) == bits(other) && one.draws.size() == other.draws.size(); }

}  // namespace

static UTest cases{"xrender.scene", [] {
    "a scene is its ids: in id order whatever order it was made in; an edit that cannot be is refused with its reason and changes nothing"_test = [] {
        scene::Scene world;
        const std::vector<scene::Instance> things = field(40, 1);
        for (std::size_t index = things.size(); index-- > 0;) expect(world.add(things[index]).has_value());
        expect(eq(world.instances().size(), std::size_t{40}));
        expect(std::ranges::is_sorted(world.instances(), {}, &scene::Instance::id));
        expect(world.find(1007) != nullptr && world.find(1008) == nullptr);
        const std::size_t before = world.changes().size();
        expect(world.add(things[3]).error() == scene::Errc::duplicate);
        expect(world.add(scene::Instance{.id = 0, .bounds = {{0, 0, 0}, {1, 1, 1}}}).error() == scene::Errc::no_id);
        expect(world.add(scene::Instance{.id = 5}).error() == scene::Errc::empty_bounds);
        scene::Instance broken = things[0];
        broken.id = 6, broken.world.at(3, 0) = std::numeric_limits<float>::quiet_NaN();
        expect(world.add(broken).error() == scene::Errc::not_finite);
        expect(world.move(9, {}).error() == scene::Errc::absent && world.remove(9).error() == scene::Errc::absent);
        expect(world.move(1007, broken.world).error() == scene::Errc::not_finite);
        expect(world.add(scene::Light{.id = 1}).has_value() && world.add(scene::Light{.id = 1}).error() == scene::Errc::duplicate);
        expect(eq(world.changes().size(), before + 1) && eq(world.instances().size(), std::size_t{40})) << "only the light was added";
        expect(!scene::text(scene::Errc::duplicate).empty());
    };
    "what a view sees: sorted by material, mesh, nearest, id, cut into batches; every instance is a draw or says why not; the bytes pinned"_test = [] {
        scene::Scene world;
        for (const scene::Instance& thing : field(600, 2)) expect(fatal(world.add(thing).has_value()));
        expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun}).has_value());
        expect(world.add(scene::Light{.id = 2, .kind = scene::LightKind::point, .at = {0, 0, -20}, .range = 8}).has_value());
        expect(world.add(scene::Light{.id = 3, .kind = scene::LightKind::point, .at = {0, 0, 300}, .range = 8}).has_value());
        expect(world.add(scene::Light{.id = 4, .kind = scene::LightKind::spot, .at = {0, 5, -30}, .range = 20, .hidden = true}).has_value());
        const scene::View view = looking({0, 5, 60}, {0, 0, 0});
        scene::Extracted seen;
        scene::extract(world, view, seen);
        std::println("600 things from (0, 5, 60): {} draws in {} batches, {} lights; hidden {}, another layer {}, outside {}; bits {:016x}", seen.draws.size(), seen.batches.size(), seen.lights.size(),
                     seen.hidden, seen.other_layer, seen.outside, bits(seen));
        expect(gt(seen.draws.size(), std::size_t{50}) && lt(seen.draws.size(), std::size_t{500}));
        expect(eq(seen.looked, 600u) && eq(seen.draws.size() + seen.hidden + seen.other_layer + seen.outside, std::size_t{600}));
        bool ordered = true, batched = true;
        for (std::size_t at = 1; at < seen.draws.size(); ++at) {
            const scene::Draw &one = seen.draws[at - 1], &other = seen.draws[at];
            ordered = ordered && std::tuple(one.material, one.mesh, one.depth, one.id) < std::tuple(other.material, other.mesh, other.depth, other.id);
        }
        std::uint32_t covered = 0;
        for (const scene::Batch& batch : seen.batches) {
            batched = batched && batch.first == covered && batch.count > 0;
            for (std::uint32_t at = batch.first; at < batch.first + batch.count; ++at) batched = batched && seen.draws[at].mesh == batch.mesh && seen.draws[at].material == batch.material;
            covered += batch.count;
        }
        expect(ordered && batched && eq(covered, static_cast<std::uint32_t>(seen.draws.size())));
        expect(le(seen.batches.size(), std::size_t{20})) << "5 meshes of 4 materials";
        // Each instance: a draw, or the reason; and a draw's place is its place in the scene.
        std::uint32_t wrong = 0;
        for (const scene::Instance& thing : world.instances()) {
            const bool drawn = std::ranges::any_of(seen.draws, [&](const scene::Draw& draw) { return draw.id == thing.id && world.instances()[draw.instance].id == thing.id; });
            const scene::Unseen why = scene::why_unseen(world, view, thing.id);
            wrong += drawn != (why == scene::Unseen::seen);
            if (thing.hidden) wrong += why != scene::Unseen::hidden;
        }
        expect(eq(wrong, 0u));
        expect(scene::why_unseen(world, view, 5) == scene::Unseen::absent && scene::text(scene::Unseen::outside) == "outside the view's frustum");
        // The sun and the lamp in view; the lamp behind the camera and the one switched off are not.
        expect(eq(seen.lights.size(), std::size_t{2}) && eq(world.lights()[seen.lights[0]].id, scene::Id{1}) && eq(world.lights()[seen.lights[1]].id, scene::Id{2}));
        // The other layer is another view's.
        scene::Extracted other;
        scene::extract(world, looking({0, 5, 60}, {0, 0, 0}, 2), other);
        expect(gt(other.draws.size(), std::size_t{0}) && std::ranges::all_of(other.draws, [&](const scene::Draw& draw) { return world.find(draw.id)->layers == 2; }));
        expect(eq(bits(seen), std::uint64_t{0xa848713ad337063c})) << std::format("{:016x}", bits(seen));
    };
    "the same scene made in another order, and by the tree instead of every instance: the same draws from 2000 views"_test = [] {
        const std::vector<scene::Instance> things = field(1500, 3);
        scene::Scene world, backwards;
        for (const scene::Instance& thing : things) expect(fatal(world.add(thing).has_value()));
        for (std::size_t index = things.size(); index-- > 0;) expect(fatal(backwards.add(things[index]).has_value()));
        scene::Spatial spatial;
        expect(spatial.follow(world) == scene::Spatial::Followed::rebuilt);
        expect(geom::check(spatial.tree(), spatial.boxes()).has_value());
        math::SplitMix64 random{9};
        scene::Extracted reference, other, by_tree;
        std::uint64_t looked_all = 0, looked_tree = 0, drawn = 0;
        std::uint32_t differing = 0;
        for (int view = 0; view < 2000; ++view) {
            const math::Vec3 eye{unit(random) * 240 - 120, unit(random) * 30 - 10, unit(random) * 240 - 120}, target{unit(random) * 100 - 50, unit(random) * 10 - 5, unit(random) * 100 - 50};
            const scene::View from = looking(eye, target);
            scene::extract(world, from, reference), scene::extract(backwards, from, other), scene::extract(world, spatial, from, by_tree);
            differing += !same(reference, other) || !same(reference, by_tree);
            looked_all += reference.looked, looked_tree += by_tree.looked, drawn += reference.draws.size();
        }
        std::println("1500 things, 2000 views: {} draws a view; looked at {} a view, by the tree {}", drawn / 2000, looked_all / 2000, looked_tree / 2000);
        expect(eq(differing, 0u));
        expect(lt(looked_tree * 3, looked_all * 2)) << "a view that draws four in ten: the tree looks at fewer than two in three";
    };
    "a mirror that follows the change list is the scene: edits tried at random over 300 frames, the tree's draws the reference's at each"_test = [] {
        math::SplitMix64 random{21};
        std::vector<scene::Instance> pool = field(400, 4);
        scene::Scene world;
        scene::Spatial spatial;
        scene::Extracted reference, by_tree;
        std::uint32_t differing = 0, rebuilt = 0, followed = 0, idle = 0;
        std::uint64_t changes = 0;
        for (int frame = 0; frame < 300; ++frame) {
            for (int edit = 0; edit < (frame % 7 == 3 ? 0 : 10); ++edit) {
                scene::Instance& thing = pool[random.next() % pool.size()];
                switch (random.next() % 5) {
                    case 0:
                    case 1: (void)world.add(thing); break;
                    case 2: (void)world.remove(thing.id); break;
                    case 3:
                        thing.world = math::translation({unit(random) * 200 - 100, unit(random) * 40 - 20, unit(random) * 200 - 100});
                        (void)world.move(thing.id, thing.world);
                        break;
                    case 4:
                        thing.hidden = !thing.hidden, thing.bounds.high = {1 + unit(random), 1 + unit(random), 1 + unit(random)};
                        (void)world.set(thing);
                        break;
                }
            }
            changes += world.changes().size();
            // Every thirteenth frame the mirror is not told: it finds out at the next and starts again.
            if (frame % 13 != 12) {
                const scene::Spatial::Followed did = spatial.follow(world);
                rebuilt += did == scene::Spatial::Followed::rebuilt, followed += did == scene::Spatial::Followed::changes, idle += did == scene::Spatial::Followed::nothing;
                bool mirrored = spatial.ids().size() == world.instances().size();
                for (std::size_t index = 0; mirrored && index < spatial.ids().size(); ++index) {
                    const geom::Box3 box = world.world_bounds(spatial.ids()[index]);
                    mirrored = spatial.ids()[index] == world.instances()[index].id && box.low == spatial.boxes()[index].low && box.high == spatial.boxes()[index].high;
                }
                differing += !geom::check(spatial.tree(), spatial.boxes()).has_value();  // refitted or made again: a tree of these boxes
                const scene::View from = looking({unit(random) * 100 - 50, 5, 90}, {0, 0, 0});
                scene::extract(world, from, reference), scene::extract(world, spatial, from, by_tree);
                differing += !mirrored || !same(reference, by_tree);
            }
            world.settle();
        }
        std::println("300 frames, {} changes: the mirror followed {} frames by their changes, began again {} times, had nothing to do {} times", changes, followed, rebuilt, idle);
        expect(eq(differing, 0u));
        expect(eq(world.revision(), std::uint64_t{300}) && eq(world.changes().size(), std::size_t{0}) && eq(world.changed(), changes));
        expect(gt(followed, 200u) && ge(rebuilt, 20u) && gt(idle, 10u));
    };
    "a steady frame allocates nothing: the extraction into what the last frame left, by either path"_test = [] {
        scene::Scene world;
        for (const scene::Instance& thing : field(2000, 5)) expect(fatal(world.add(thing).has_value()));
        scene::Spatial spatial;
        (void)spatial.follow(world);
        scene::Extracted seen, by_tree;
        const scene::View from = looking({0, 5, 80}, {0, 0, 0});
        scene::extract(world, from, seen), scene::extract(world, spatial, from, by_tree);
        const std::uint64_t before = allocations.load();
        for (int frame = 0; frame < 100; ++frame) {
            const scene::View turned = looking({static_cast<float>(frame) * 0.1f, 5, 80}, {0, 0, 0});
            scene::extract(world, turned, seen), scene::extract(world, spatial, turned, by_tree);
            expect(spatial.follow(world) == scene::Spatial::Followed::nothing);
        }
        expect(eq(allocations.load() - before, std::uint64_t{0}));
    };
    "things that only move keep the tree's shape: its boxes are refitted, the draws are the reference's, and it is made again after many"_test = [] {
        scene::Scene world;
        const std::vector<scene::Instance> things = field(800, 6);
        for (const scene::Instance& thing : things) expect(fatal(world.add(thing).has_value()));
        scene::Spatial spatial;
        (void)spatial.follow(world);
        world.settle();
        std::vector<std::uint32_t> order(spatial.tree().order.begin(), spatial.tree().order.end());  // the last frame's
        math::SplitMix64 random{31};
        scene::Extracted reference, by_tree;
        std::uint32_t differing = 0, kept = 0, remade = 0;
        for (int frame = 0; frame < 150; ++frame) {
            for (int moved = 0; moved < 40; ++moved) {
                const scene::Instance& thing = things[random.next() % things.size()];
                expect(world.move(thing.id, math::translation({unit(random) * 200 - 100, unit(random) * 40 - 20, unit(random) * 200 - 100})).has_value());
            }
            expect(spatial.follow(world) == scene::Spatial::Followed::changes);
            const bool same_shape = std::ranges::equal(spatial.tree().order, order);
            kept += same_shape, remade += !same_shape;
            order.assign(spatial.tree().order.begin(), spatial.tree().order.end());
            differing += !geom::check(spatial.tree(), spatial.boxes()).has_value();
            const scene::View from = looking({unit(random) * 100 - 50, 5, 90}, {0, 0, 0});
            scene::extract(world, from, reference), scene::extract(world, spatial, from, by_tree);
            differing += !same(reference, by_tree);
            world.settle();
        }
        std::println("800 things, 40 moved a frame for 150 frames: the tree kept its shape {} frames and was made again {}", kept, remade);
        expect(eq(differing, 0u) && gt(kept, 140u) && ge(remade, 1u) && le(remade, 3u));
    };
}};
