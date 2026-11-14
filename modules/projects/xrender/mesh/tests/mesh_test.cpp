// xrender.mesh, the CPU reference (the foundation plan's §5.1–§5.3, M6.3): a pixel says who drew it, down to the
// triangle; what is nearer hides what is farther, and what reached the frame is told instance by instance;
// triangles that share an edge share no pixel and leave none between them; a floor that runs under and behind the
// camera is cut at the view and drawn; leaving clusters out changes no pixel; a thing that moved says by how many
// pixels; either depth convention gives the same picture; and the picture's bits are pinned.
import std;
import xrender.mesh;
import mlibs.hash;
import mlibs.image;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace mesh = xrender::mesh;
namespace scene = xrender::scene;
namespace math = mlibs::math;
namespace geom = mlibs::geom;

namespace {

// The meshes and materials of every scene here: a box, a floor, a sphere, a cylinder, a sheet of 16 × 16.
struct Library {
    std::vector<geom::ClusteredMesh> made;
    std::vector<const geom::ClusteredMesh*> meshes;
    std::vector<geom::Bvh> grown;
    std::vector<const geom::Bvh*> trees;
    std::vector<mesh::Texture> pictures;
    std::vector<const mesh::Texture*> textures;
    std::vector<mesh::Material> materials{{{0.8f, 0.2f, 0.15f}, {}}, {{0.5f, 0.5f, 0.5f}, {}}, {{0.15f, 0.4f, 0.8f}, {}}, {{0.9f, 0.75f, 0.2f}, {}}, {{0.1f, 0.1f, 0.1f}, {0.9f, 0.5f, 0.1f}},
                                          {{0.15f, 0.4f, 0.8f}, {}, 0.25f, 0}, {{1.0f, 0.77f, 0.34f}, {}, 0.3f, 1}, {{0.15f, 0.4f, 0.8f}, {}, 0.6f, 0},
                                          // The harbour's: a wet quay, a warm lamp's globe and a cold one's (brighter than the display shows), iron.
                                          {{0.05f, 0.055f, 0.065f}, {}, 0.28f, 0}, {{0.1f, 0.1f, 0.1f}, {9.0f, 5.0f, 1.6f}}, {{0.1f, 0.1f, 0.1f}, {3.0f, 5.0f, 8.0f}}, {{0.25f, 0.25f, 0.27f}, {}, 0.45f, 1}};
    Library() {
        made.push_back(*geom::clustered(geom::box({1, 1, 1})));
        made.push_back(*geom::clustered(geom::sheet(200, 200, 4, 4)));
        made.push_back(*geom::clustered(geom::sphere(1, 32, 64), 32));
        made.push_back(*geom::clustered(geom::cylinder(0.7f, 2, 48), 32));
        made.push_back(*geom::clustered(geom::sheet(8, 8, 16, 16)));
        // A floor whose texture coordinates repeat every four units: for a picture of tiles.
        geom::Mesh tiled = geom::sheet(200, 200, 4, 4);
        for (math::Vec2& uv : tiled.uvs) uv = {uv.x * 50, uv.y * 50};
        made.push_back(*geom::clustered(tiled));
        // Tiles: 64 × 64, eight of them a side, light and dark.
        std::vector<std::uint8_t> tiles(64 * 64 * 4, 255);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const std::uint8_t level = ((x / 8) + (y / 8)) % 2 ? 235 : 70;
                tiles[static_cast<std::size_t>(y * 64 + x) * 4] = level, tiles[static_cast<std::size_t>(y * 64 + x) * 4 + 1] = level, tiles[static_cast<std::size_t>(y * 64 + x) * 4 + 2] = level;
            }
        pictures.push_back(*mesh::Texture::made(tiles, 64, 64));
        textures.push_back(&pictures.front());
        materials.push_back({{1, 1, 1}, {}, 1, 0, 0});  // the tiles' own colours
        materials.push_back({{1, 1, 1}, {}, 0.05f, 1});  // a mirror: white metal, all but smooth
        for (const geom::ClusteredMesh& one : made) meshes.push_back(&one), grown.push_back(geom::bvh_of(one.mesh));
        for (const geom::Bvh& tree : grown) trees.push_back(&tree);
    }
    // Without trees nothing casts a shadow; with them a light is hidden where something is between.
    [[nodiscard]] mesh::Assets assets(bool shadows = false) const { return {meshes, materials, shadows ? std::span<const geom::Bvh* const>(trees) : std::span<const geom::Bvh* const>(), textures}; }
    [[nodiscard]] geom::Box3 bounds(std::uint32_t which) const { return geom::bounds(made[which].mesh); }
};
constexpr std::uint32_t box = 0, floor_mesh = 1, ball = 2, post = 3, sheet = 4, tiled_floor = 5;
constexpr std::uint32_t matt_red = 0, matt_grey = 1, matt_blue = 2, polished = 5, gold = 6, satin = 7, wet = 8, warm_glow = 9, cold_glow = 10, iron = 11, tiles = 12, mirror = 13;  // materials
const Library& library() {
    static const Library made;
    return made;
}
void put(scene::Scene& world, scene::Id id, std::uint32_t which, std::uint32_t material, const math::Mat4& where) {
    expect(fatal(world.add({.id = id, .mesh = which, .material = material, .world = where, .bounds = library().bounds(which)}).has_value()));
}
struct Camera {
    math::Vec3 eye{0, 0, 5}, target{0, 0, 0}, up{0, 1, 0};
    float aspect = 1;
    bool reversed = false;
    [[nodiscard]] scene::View view() const {
        return {.view = math::look_at(eye, target, up), .projection = reversed ? math::perspective_reversed(1.0f, aspect, 0.5f) : math::perspective(1.0f, aspect, 0.5f, 100.0f)};
    }
};
struct Frame {
    mesh::Targets targets;
    mesh::Stats stats;
};
Frame drawn(const scene::Scene& world, const Camera& camera, int width, int height, mesh::Pass& pass, mesh::Options options = {}, bool shadows = false) {
    Frame frame;
    frame.targets.resize(width, height);
    scene::Extracted seen;
    scene::extract(world, camera.view(), seen);
    options.reversed_depth = camera.reversed;
    pass.draw(world, seen, camera.view(), library().assets(shadows), frame.targets, options);
    frame.stats = pass.last();
    return frame;
}
Frame drawn(const scene::Scene& world, const Camera& camera, int width, int height, mesh::Options options = {}, bool shadows = false) {
    mesh::Pass pass;
    return drawn(world, camera, width, height, pass, options, shadows);
}
std::uint64_t bits(const mesh::Targets& targets) {
    mlibs::hash::Fnv1a hash;
    for (const std::uint8_t byte : targets.colour) hash.le(byte, 1);
    for (const mesh::Pixel& id : targets.ids) hash.le(id.draw, 4).le(id.cluster, 4).le(id.triangle, 4);
    for (const float depth : targets.depth) hash.f32(depth);
    return hash.value();
}
std::filesystem::path keep(const mesh::Targets& targets, std::string_view name) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "openmui-tests" / "xrender-mesh";
    std::filesystem::create_directories(directory);
    const auto file = directory / std::format("{}.png", name);
    const mlibs::image::View view{targets.colour.data(), targets.width, targets.height, targets.width * 4, mlibs::image::Alpha::straight};
    expect(mlibs::image::save_png(file, view).has_value()) << file.generic_string();
    return file;
}
// A yard: a floor, a red box, a blue ball, a yellow post, a lamp that glows; a sun and the lamp's light.
scene::Scene yard() {
    scene::Scene world;
    put(world, 1, floor_mesh, 1, math::translation({0, -1, 0}));
    put(world, 10, box, 0, math::Transform{{-2.2f, 0, 0}, math::Quat::about({0, 1, 0}, 0.6f), {1, 1, 1}}.matrix());
    put(world, 11, ball, 2, math::translation({0.4f, 0, 0.8f}));
    put(world, 12, post, 3, math::translation({2.6f, 0, -0.5f}));
    // The lamp's globe: the light is inside it, so it is not in its own light's way.
    expect(world.add({.id = 13, .mesh = ball, .material = 4, .world = math::Transform{{1.2f, 1.6f, -1.5f}, {}, {0.25f, 0.25f, 0.25f}}.matrix(), .bounds = library().bounds(ball), .casts = false}).has_value());
    expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .colour = {1, 0.96f, 0.9f}, .intensity = 0.9f, .toward = {-0.4f, -1, -0.5f}}).has_value());
    expect(world.add(scene::Light{.id = 2, .kind = scene::LightKind::point, .colour = {1, 0.6f, 0.2f}, .intensity = 2.5f, .at = {1.2f, 1.6f, -1.5f}, .range = 6}).has_value());
    return world;
}
// A harbour at night after rain: a wet quay, containers, bollards, three lamps
// on posts (two warm, one cold) and a spot from a crane, a faint moon; fog low over it all.
scene::Scene harbour() {
    scene::Scene world;
    put(world, 1, floor_mesh, wet, math::translation({0, -1, 0}));
    const auto scaled = [](math::Vec3 at, math::Vec3 size, float turned = 0) { return math::Transform{at, math::Quat::about({0, 1, 0}, turned), size}.matrix(); };
    put(world, 10, box, matt_red, scaled({-3.6f, -0.2f, -2.2f}, {1.7f, 0.8f, 0.8f}, 0.25f));
    put(world, 11, box, matt_blue, scaled({-3.4f, 1.4f, -2.3f}, {1.7f, 0.8f, 0.8f}, 0.1f));
    put(world, 12, box, matt_grey, scaled({4.2f, -0.2f, -4.0f}, {1.7f, 0.8f, 0.8f}, -0.5f));
    put(world, 13, ball, polished, scaled({1.0f, -0.55f, 1.6f}, {0.45f, 0.45f, 0.45f}));
    std::uint32_t next = 20;
    for (const float x : {-4.5f, -1.5f, 1.5f, 4.5f}) put(world, next++, post, iron, scaled({x, -0.72f, 3.2f}, {0.32f, 0.28f, 0.32f}));
    struct Lamp {
        math::Vec3 foot;
        bool warm;
    };
    std::uint32_t light = 1;
    for (const Lamp lamp : {Lamp{{-1.4f, -1, -0.8f}, true}, Lamp{{2.4f, -1, 0.6f}, false}, Lamp{{0.6f, -1, -6.5f}, true}}) {
        const math::Vec3 top = lamp.foot + math::Vec3{0, 4.15f, 0};
        put(world, next++, post, iron, scaled(lamp.foot + math::Vec3{0, 2, 0}, {0.1f, 2, 0.1f}));
        // The globe: the light is inside it, so it is not in its own light's way.
        expect(world.add({.id = next++, .mesh = ball, .material = lamp.warm ? warm_glow : cold_glow, .world = scaled(top, {0.2f, 0.2f, 0.2f}), .bounds = library().bounds(ball), .casts = false}).has_value());
        expect(world.add(scene::Light{.id = light++, .kind = scene::LightKind::point, .colour = lamp.warm ? math::Vec3{1, 0.62f, 0.25f} : math::Vec3{0.55f, 0.75f, 1}, .intensity = 3.2f, .at = top, .range = 10}).has_value());
    }
    expect(world.add(scene::Light{.id = light++, .kind = scene::LightKind::spot, .colour = {1, 0.97f, 0.9f}, .intensity = 5, .at = {5.5f, 6, -1}, .toward = math::normalized(math::Vec3{-0.45f, -1, -0.15f}), .range = 14, .cone = 0.3f}).has_value());
    expect(world.add(scene::Light{.id = light++, .kind = scene::LightKind::sun, .colour = {0.6f, 0.7f, 1}, .intensity = 0.05f, .toward = {0.3f, -1, 0.4f}}).has_value());
    return world;
}
const Camera harbour_camera{.eye = {-0.6f, 2.3f, 10.5f}, .target = {0.2f, 0.2f, 0}, .aspect = 16.0f / 9.0f};
const mesh::Options harbour_night{.background = {0.004f, 0.005f, 0.01f}, .ambient = {0.006f, 0.007f, 0.012f}, .fog = {.colour = {0.02f, 0.026f, 0.04f}, .density = 0.045f, .base = -1, .falloff = 0.35f},
                                  .environment = {.on = true, .above = {0.01f, 0.014f, 0.03f}, .level = {0.035f, 0.04f, 0.06f}, .below = {0.004f, 0.004f, 0.005f}}};
const Camera yard_camera{.eye = {0.5f, 2.2f, 7.5f}, .target = {0.2f, 0, 0}, .aspect = 16.0f / 9.0f};

}  // namespace

static UTest cases{"xrender.mesh", [] {
    "a pixel says who drew it: the instance, the triangle facing the camera, its normal and its depth; where nothing is, nothing"_test = [] {
        scene::Scene world;
        put(world, 42, box, 0, {});
        const Camera camera;
        const Frame frame = drawn(world, camera, 201, 201);
        const auto middle = mesh::at(frame.targets, 100, 100);
        expect(fatal(middle.has_value()));
        // The face at z = 1 is 4 in front of the eye: its depth is the projection's of that.
        const math::Vec4 there = camera.view().projection * math::Vec4{0, 0, -4, 1};
        expect(eq(middle->instance, scene::Id{42}) && eq(middle->material, 0u) && lt(std::abs(middle->depth - there.z / there.w), 1e-5f)) << middle->depth;
        expect(lt(std::abs(middle->normal.x), 1e-5f) && lt(std::abs(middle->normal.y), 1e-5f) && gt(middle->normal.z, 0.9999f));
        // That triangle of the mesh: it faces +z and the box's middle is on it.
        const geom::Mesh& made = library().made[box].mesh;
        const math::Vec3 a = made.positions[made.indices[middle->triangle * 3]], b = made.positions[made.indices[middle->triangle * 3 + 1]], c = made.positions[made.indices[middle->triangle * 3 + 2]];
        expect(eq(a.z, 1.0f) && eq(b.z, 1.0f) && eq(c.z, 1.0f) && gt(math::cross(b - a, c - a).z, 0.0f));
        expect(!mesh::at(frame.targets, 0, 0).has_value() && !mesh::at(frame.targets, 500, 3).has_value());
        // Of the box's 12 triangles two face the camera; the rest are seen from behind (the sides are edge on to
        // nothing at this distance: they face away).
        expect(eq(frame.stats.triangles, std::uint64_t{12}) && eq(frame.stats.filled, std::uint64_t{2}) && eq(frame.stats.facing_away, std::uint64_t{10}));
        std::vector<mesh::Shown> shown;
        mesh::shown(frame.targets, shown);
        expect(fatal(eq(shown.size(), std::size_t{1})));
        // A face 2 across at 4 away under an angle of view of 1 radian: 2 / (2 · 4 · tan 0.5) of the frame's height.
        const double side = 201.0 * 2.0 / (2.0 * 4.0 * math::det::sin(0.5) / math::det::cos(0.5));
        std::println("a box 4 away: {} pixels, {}..{} × {}..{}; the face should be {:.1f} across", shown[0].pixels, shown[0].x0, shown[0].x1, shown[0].y0, shown[0].y1, side);
        expect(lt(std::abs((shown[0].x1 - shown[0].x0) - side), 1.01) && lt(std::abs((shown[0].y1 - shown[0].y0) - side), 1.01));
        expect(lt(std::abs(static_cast<double>(shown[0].pixels) - side * side), side * 2));
    };
    "what is nearer hides what is farther: the pixel is the nearer one's, and a thing wholly behind another was a draw and reached no pixel"_test = [] {
        scene::Scene world;
        put(world, 1, box, 1, math::scaling({2, 2, 0.2f}));                              // a wall
        put(world, 2, box, 0, math::Transform{{0, 0, 2}, {}, {0.3f, 0.3f, 0.3f}}.matrix());   // a small box in front of it
        put(world, 3, ball, 2, math::Transform{{0.5f, 0.5f, -3}, {}, {0.5f, 0.5f, 0.5f}}.matrix());  // a ball behind it
        const Frame frame = drawn(world, {}, 200, 200);
        expect(eq(mesh::at(frame.targets, 100, 100)->instance, scene::Id{2}) && eq(mesh::at(frame.targets, 40, 100)->instance, scene::Id{1}));
        std::vector<mesh::Shown> shown;
        mesh::shown(frame.targets, shown);
        expect(fatal(frame.targets.draws.size() == 3 && shown.size() == 2)) << "three draws, two that reached the frame";
        expect(eq(shown[0].instance, scene::Id{1}) && eq(shown[1].instance, scene::Id{2}) && gt(shown[0].pixels, shown[1].pixels));
        expect(gt(frame.stats.hidden, std::uint64_t{0}));
    };
    "triangles that share an edge share no pixel and leave none between them: a sheet of 512 turned any way is each pixel once, and seen square on is whole"_test = [] {
        math::SplitMix64 random{3};
        std::uint32_t differing = 0;
        std::uint64_t covered = 0;
        for (int turn = 0; turn < 24; ++turn) {
            const auto unit = [&] { return static_cast<float>(math::unit24(random.next())); };
            // Each by name, one after the other: the same 24 turns whichever compiler built it.
            const math::Vec3 at{unit() - 0.5f, unit() - 0.5f, unit() - 0.5f};
            const float angle = unit() * 6.28f;
            const math::Vec3 axis = math::normalized(math::Vec3{unit() - 0.5f, unit() + 0.2f, unit() - 0.5f});
            const math::Transform where{at, math::Quat::about(axis, angle), {1, 1, 1}};
            const Camera camera{.eye = {0, 9, 0.5f}, .target = {0, 0, 0}};
            scene::Scene fine;
            put(fine, 1, sheet, 0, where.matrix());
            const Frame many = drawn(fine, camera, 160, 160);
            // Each pixel once: none was written twice, whichever way the sheet faces.
            differing += many.stats.hidden != 0;
            std::uint64_t with_id = 0;
            for (const mesh::Pixel& id : many.targets.ids) with_id += id.draw != mesh::none;
            differing += with_id != many.stats.written;
            covered += with_id;
        }
        std::println("a sheet of 512 triangles turned 24 ways: {} pixels covered, each written once", covered);
        expect(eq(differing, 0u) && gt(covered, std::uint64_t{20000}));
        // And no pixel between them is left: seen square on, the sheet's inside is whole.
        scene::Scene square;
        put(square, 1, sheet, 0, {});
        const Frame flat = drawn(square, {.eye = {0, 9, 0}, .target = {0, 0, 0}, .up = {0, 0, -1}}, 160, 160);
        std::vector<mesh::Shown> shown;
        mesh::shown(flat.targets, shown);
        expect(fatal(eq(shown.size(), std::size_t{1})));
        expect(eq(shown[0].pixels, static_cast<std::uint32_t>((shown[0].x1 - shown[0].x0) * (shown[0].y1 - shown[0].y0)))) << "its box on the screen is full";
        std::set<std::uint32_t> triangles;
        for (const mesh::Pixel& id : flat.targets.ids)
            if (id.draw != mesh::none) triangles.insert(id.triangle);
        expect(eq(triangles.size(), std::size_t{512})) << "and every triangle of it has pixels of its own";
    };
    "a floor that runs under and behind the camera is cut at the view and drawn: below the horizon floor, above it nothing"_test = [] {
        scene::Scene world;
        put(world, 7, floor_mesh, 1, math::translation({0, -1, 0}));
        const Frame frame = drawn(world, {.eye = {0, 0, 0}, .target = {0, 0, -1}}, 160, 120);
        std::println("a floor under the camera: {} triangles, {} outside, {} cut, {} filled, {} facing away; {} pixels written", frame.stats.triangles, frame.stats.outside, frame.stats.clipped,
                     frame.stats.filled, frame.stats.facing_away, frame.stats.written);
        expect(gt(frame.stats.clipped, std::uint64_t{0}));
        std::uint32_t wrong = 0;
        for (int x = 0; x < 160; ++x) {
            for (int y = 0; y < 58; ++y) wrong += mesh::at(frame.targets, x, y).has_value();
            for (int y = 62; y < 120; ++y) wrong += !mesh::at(frame.targets, x, y).has_value();
        }
        expect(eq(wrong, 0u));
        // Nearer the bottom of the frame is nearer the camera.
        expect(lt(mesh::at(frame.targets, 80, 119)->depth, mesh::at(frame.targets, 80, 70)->depth));
        expect(gt(mesh::at(frame.targets, 80, 100)->normal.y, 0.9999f));
    };
    "leaving out clusters that are outside the view or face away changes no pixel"_test = [] {
        const scene::Scene world = yard();
        const Frame with = drawn(world, yard_camera, 320, 180), without = drawn(world, yard_camera, 320, 180, {.cull_clusters = false});
        std::println("the yard: {} clusters, {} outside the view and {} facing away left out; {} of {} triangles filled", with.stats.clusters, with.stats.clusters_outside, with.stats.clusters_away,
                     with.stats.filled, with.stats.triangles);
        expect(eq(bits(with.targets), bits(without.targets)));
        expect(gt(with.stats.clusters_away, 20u) && lt(with.stats.triangles, without.stats.triangles));
        expect(eq(without.stats.clusters_away + without.stats.clusters_outside, 0u));
        // Close to the ball, most of the yard is outside the view: the same.
        const Camera close{.eye = {0.4f, 0.3f, 2.6f}, .target = {0.9f, 0.2f, 0}, .aspect = 16.0f / 9.0f};
        const Frame near_with = drawn(world, close, 320, 180), near_without = drawn(world, close, 320, 180, {.cull_clusters = false});
        std::println("close to the ball: {} clusters outside the view and {} facing away left out of {}", near_with.stats.clusters_outside, near_with.stats.clusters_away, near_with.stats.clusters);
        expect(eq(bits(near_with.targets), bits(near_without.targets)) && gt(near_with.stats.clusters_outside, 0u));
    };
    "a thing that moved says by how many pixels; what stood still says 0; the first frame and a frame after `forget` say 0"_test = [] {
        scene::Scene world = yard();
        mesh::Pass pass;
        const Frame first = drawn(world, yard_camera, 320, 180, pass);
        std::uint32_t moving = 0;
        for (const math::Vec2 velocity : first.targets.velocity) moving += velocity.x != 0 || velocity.y != 0;
        expect(eq(moving, 0u)) << "nothing is known of the frame before the first";
        // The box goes 0.3 to the right.
        const math::Mat4 was = world.find(10)->world;
        expect(world.move(10, math::translation({0.3f, 0, 0}) * was).has_value());
        const Frame second = drawn(world, yard_camera, 320, 180, pass);
        // Where a point of the box was and is on the screen, by the matrices: its middle.
        const scene::View view = yard_camera.view();
        const auto on_screen = [&](math::Vec3 point) {
            const math::Vec4 clip = view.projection * view.view * math::Vec4{point, 1};
            return math::Vec2{(clip.x / clip.w * 0.5f + 0.5f) * 320, (0.5f - clip.y / clip.w * 0.5f) * 180};
        };
        std::uint32_t of_box = 0, of_others = 0, wrong = 0;
        double shift = 0;
        for (int y = 0; y < 180; ++y)
            for (int x = 0; x < 320; ++x) {
                const auto who = mesh::at(second.targets, x, y);
                if (!who) continue;
                const math::Vec2 velocity = second.targets.velocity[static_cast<std::size_t>(y) * 320 + x];
                if (who->instance == 10) ++of_box, shift += velocity.x, wrong += !(velocity.x > 5 && velocity.x < 20 && std::abs(velocity.y) < 2);
                else ++of_others, wrong += velocity.x != 0 || velocity.y != 0;
            }
        const float expected = on_screen({-1.9f, 0, 0}).x - on_screen({-2.2f, 0, 0}).x;
        std::println("the box moved 0.3: {} of its pixels say {:.2f} pixels on average (its middle moved {:.2f}); {} pixels of what stood still say 0", of_box, shift / of_box, expected, of_others);
        expect(eq(wrong, 0u) && gt(of_box, 500u) && lt(std::abs(shift / of_box - expected), 1.5));
        pass.forget();
        const Frame third = drawn(world, yard_camera, 320, 180, pass);
        moving = 0;
        for (const math::Vec2 velocity : third.targets.velocity) moving += velocity.x != 0 || velocity.y != 0;
        expect(eq(moving, 0u));
    };
    "either depth convention is the same picture: the nearest at 0, or at 1 with no far plane"_test = [] {
        const scene::Scene world = yard();
        Camera reversed = yard_camera;
        reversed.reversed = true;
        const Frame forward = drawn(world, yard_camera, 320, 180), back = drawn(world, reversed, 320, 180);
        std::uint32_t differing = 0;
        for (std::size_t pixel = 0; pixel < forward.targets.ids.size(); ++pixel) {
            const mesh::Pixel &one = forward.targets.ids[pixel], &other = back.targets.ids[pixel];
            differing += one.draw != other.draw || one.triangle != other.triangle;
        }
        std::println("forward and reversed depth: {} of {} pixels drawn by another triangle", differing, forward.targets.ids.size());
        expect(le(differing, 8u)) << "but where two surfaces meet, to the last bit of depth";
        expect(forward.targets.colour.size() == back.targets.colour.size());
    };
    "the picture is the same bits everywhere: the yard, pinned; and kept as a PNG"_test = [] {
        const scene::Scene world = yard();
        const Frame frame = drawn(world, yard_camera, 480, 270), again = drawn(world, yard_camera, 480, 270);
        expect(eq(bits(frame.targets), bits(again.targets)));
        const auto file = keep(frame.targets, "yard");
        // The same frame by who drew it: a colour a cluster of an instance.
        mesh::Targets who_drew = frame.targets;
        for (std::size_t pixel = 0; pixel < who_drew.ids.size(); ++pixel) {
            const mesh::Pixel id = who_drew.ids[pixel];
            const std::uint64_t mixed = id.draw == mesh::none ? 0 : math::mix64(std::uint64_t{id.draw} * 1000003u + id.cluster);
            who_drew.colour[pixel * 4] = id.draw == mesh::none ? 16 : static_cast<std::uint8_t>(64 + (mixed & 0xbf));
            who_drew.colour[pixel * 4 + 1] = id.draw == mesh::none ? 16 : static_cast<std::uint8_t>(64 + (mixed >> 8 & 0xbf));
            who_drew.colour[pixel * 4 + 2] = id.draw == mesh::none ? 20 : static_cast<std::uint8_t>(64 + (mixed >> 16 & 0xbf));
        }
        (void)keep(who_drew, "yard-ids");
        std::vector<mesh::Shown> shown;
        mesh::shown(frame.targets, shown);
        std::string who;
        for (const mesh::Shown& one : shown) who += std::format(" {}: {} px;", one.instance, one.pixels);
        std::println("the yard, 480 × 270:{} {} written, {} hidden; bits {:016x}; {}", who, frame.stats.written, frame.stats.hidden, bits(frame.targets), file.generic_string());
        expect(eq(shown.size(), std::size_t{5}));
        // The lamp glows whatever lights it.
        const auto lamp = std::ranges::find(shown, scene::Id{13}, &mesh::Shown::instance);
        expect(fatal(lamp != shown.end()));
        const std::size_t middle = (static_cast<std::size_t>((lamp->y0 + lamp->y1) / 2) * 480 + static_cast<std::size_t>((lamp->x0 + lamp->x1) / 2)) * 4;
        expect(gt(frame.targets.colour[middle], std::uint8_t{200}) && gt(frame.targets.colour[middle + 1], std::uint8_t{150}));
        expect(eq(bits(frame.targets), std::uint64_t{0xf29a9acc7a4440f9})) << std::format("{:016x}", bits(frame.targets));
    };
    "a shadow is where something stands between a surface and the light: a ball's on the floor is where the sun's rays through the ball land, by a ray through the meshes' trees"_test = [] {
        scene::Scene world;
        put(world, 1, floor_mesh, 1, math::translation({0, -1, 0}));
        put(world, 2, ball, 2, math::translation({0, 0.5f, 0}));
        const math::Vec3 sun = math::normalized(math::Vec3{-0.5f, -1, -0.3f});
        expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .toward = sun}).has_value());
        const Camera camera{.eye = {0, 6, 7}, .target = {-0.5f, -1, 0}, .aspect = 16.0f / 9.0f};
        const Frame lit = drawn(world, camera, 320, 180), shaded = drawn(world, camera, 320, 180, {}, true);
        // A floor's pixel, back in the world: where the ray through it meets y = −1.
        const scene::View view = camera.view();
        const math::Mat4 back = *math::inverse(view.projection * view.view);
        const auto on_floor = [&](int x, int y) {
            const float ndc_x = (static_cast<float>(x) + 0.5f) / 320 * 2 - 1, ndc_y = 1 - (static_cast<float>(y) + 0.5f) / 180 * 2;
            const math::Vec4 near = back * math::Vec4{ndc_x, ndc_y, 0, 1}, far = back * math::Vec4{ndc_x, ndc_y, 1, 1};
            const math::Vec3 from = near.xyz() * (1 / near.w), to = far.xyz() * (1 / far.w);
            return from + (to - from) * ((-1 - from.y) / (to.y - from.y));
        };
        std::uint32_t in_shadow = 0, agreeing = 0, at_the_edge = 0, wrong = 0, darker_elsewhere = 0;
        for (int y = 0; y < 180; ++y)
            for (int x = 0; x < 320; ++x) {
                const auto who = mesh::at(shaded.targets, x, y);
                const std::size_t pixel = (static_cast<std::size_t>(y) * 320 + x) * 4;
                const bool darker = shaded.targets.colour[pixel] < lit.targets.colour[pixel];
                if (!who || who->instance != 1) {
                    // The ball itself: lit where it faces the sun, as before (nothing stands between it and the sun).
                    darker_elsewhere += darker;
                    continue;
                }
                // The sun's ray back from this place passes the ball's middle within its radius: in its shadow.
                const math::Vec3 place = on_floor(x, y), to_ball = math::Vec3{0, 0.5f, 0} - place;
                const float along = -math::dot(to_ball, sun);
                const float apart = math::length(to_ball + sun * along);
                const bool expected = along > 0 && apart < 1;
                if (std::abs(apart - 1) < 0.06f) ++at_the_edge;  // a facet of the ball, a pixel's width
                else if (darker == expected) ++agreeing, in_shadow += darker;
                else ++wrong;
            }
        std::println("a ball over a floor: {} floor pixels in its shadow, {} agree with the sun's rays through a ball, {} wrong, {} at the edge; {} rays, {} met something", in_shadow, agreeing, wrong,
                     at_the_edge, shaded.stats.rays, shaded.stats.shadowed);
        expect(gt(in_shadow, 300u) && eq(wrong, 0u) && eq(darker_elsewhere, 0u));
        expect(gt(shaded.stats.rays, std::uint64_t{20000}) && gt(shaded.stats.shadowed, std::uint64_t{400}) && eq(lit.stats.rays, std::uint64_t{0}));
        // Shadows asked off: the picture without them, bit for bit.
        const Frame off = drawn(world, camera, 320, 180, {.shadows = false}, true);
        expect(eq(bits(off.targets), bits(lit.targets)));
    };
    "the yard with shadows: what is off the screen casts too; the picture pinned and kept"_test = [] {
        scene::Scene world = yard();
        // A slab off to the right of the view, between the sun and the yard.
        put(world, 20, box, 1, math::Transform{{9, 6, 6}, {}, {3, 0.2f, 3}}.matrix());
        const Frame frame = drawn(world, yard_camera, 480, 270, {}, true), plain = drawn(world, yard_camera, 480, 270);
        const auto file = keep(frame.targets, "yard-shadows");
        std::vector<mesh::Shown> shown;
        mesh::shown(frame.targets, shown);
        expect(std::ranges::find(shown, scene::Id{20}, &mesh::Shown::instance) == shown.end()) << "the slab reaches no pixel";
        std::uint32_t darker = 0, lighter = 0;
        for (std::size_t pixel = 0; pixel < frame.targets.ids.size(); ++pixel) {
            darker += frame.targets.colour[pixel * 4] < plain.targets.colour[pixel * 4];
            lighter += frame.targets.colour[pixel * 4] > plain.targets.colour[pixel * 4];
        }
        std::println("the yard with shadows: {} pixels darker than without, {} lighter; {} rays; bits {:016x}; {}", darker, lighter, frame.stats.rays, bits(frame.targets), file.generic_string());
        expect(gt(darker, 3000u) && eq(lighter, 0u));
        expect(eq(bits(frame.targets), std::uint64_t{0x67ba845e17a03e8c})) << std::format("{:016x}", bits(frame.targets));
    };
    "frames gathered over time are anti-aliasing: sixteen frames each sampled a little aside come to what sixteen samples a pixel give, far nearer than one frame is"_test = [] {
        const scene::Scene world = yard();
        constexpr int width = 240, height = 135, fine = 4;
        // The truth: sixteen samples a pixel, their light averaged.
        const Frame large = drawn(world, yard_camera, width * fine, height * fine);
        std::vector<math::Vec3> truth(static_cast<std::size_t>(width) * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                math::Vec3 sum;
                for (int dy = 0; dy < fine; ++dy)
                    for (int dx = 0; dx < fine; ++dx) sum = sum + large.targets.light[static_cast<std::size_t>(y * fine + dy) * (width * fine) + static_cast<std::size_t>(x * fine + dx)];
                truth[static_cast<std::size_t>(y) * width + x] = sum * (1.0f / (fine * fine));
            }
        const auto apart = [&](std::span<const math::Vec3> light) {
            double sum = 0;
            for (std::size_t pixel = 0; pixel < truth.size(); ++pixel)
                sum += std::abs(light[pixel].x - truth[pixel].x) + std::abs(light[pixel].y - truth[pixel].y) + std::abs(light[pixel].z - truth[pixel].z);
            return sum / static_cast<double>(truth.size());
        };
        const Frame one = drawn(world, yard_camera, width, height);
        mesh::Pass pass;
        mesh::Temporal gathered;
        std::vector<std::uint8_t> colour;
        mesh::Temporal::Told told;
        for (std::uint32_t number = 0; number < 16; ++number) {
            const Frame frame = drawn(world, yard_camera, width, height, pass, {.jitter = mesh::jitter_of(number)});
            // Nothing moved: the jitter is not a movement.
            if (number == 5) expect(std::ranges::all_of(frame.targets.velocity, [](math::Vec2 velocity) { return velocity.x == 0 && velocity.y == 0; }));
            told = gathered.resolve(frame.targets, colour);
        }
        const double alone = apart(one.targets.light), together = apart(gathered.light());
        std::println("against 16 samples a pixel: one frame is {:.5f} off a pixel, 16 frames gathered {:.5f} ({:.0f}% of it); the last frame carried {} pixels, {} were another thing's", alone, together,
                     100 * together / alone, told.carried, told.another);
        expect(lt(together * 2.5, alone)) << "less than 40% of one frame's error";
        expect(gt(told.carried, static_cast<std::uint32_t>(width * height * 9 / 10)));
        // The sequence: 16 different places within half a pixel, about the middle on average.
        math::Vec2 sum{0, 0};
        std::set<std::pair<float, float>> places;
        for (std::uint32_t number = 0; number < 16; ++number) {
            const math::Vec2 at = mesh::jitter_of(number);
            expect(le(std::abs(at.x), 0.5f) && le(std::abs(at.y), 0.5f));
            sum = {sum.x + at.x, sum.y + at.y}, places.insert({at.x, at.y});
        }
        expect(eq(places.size(), std::size_t{16}) && lt(std::abs(sum.x / 16), 0.05f) && lt(std::abs(sum.y / 16), 0.05f));
        // Kept for the README: one frame's edge and the gathered one's, the same crop.
        // The box's corner, each pixel six across: one frame on the left, the gathered frames on the right.
        constexpr int left = 55, top = 45, across = 70, down = 50, times = 6, gap = 12;
        mesh::Targets beside;
        beside.width = across * times * 2 + gap, beside.height = down * times;
        beside.colour.assign(static_cast<std::size_t>(beside.width) * beside.height * 4, 255);
        for (int y = 0; y < beside.height; ++y)
            for (int x = 0; x < across * times; ++x) {
                const std::size_t from = (static_cast<std::size_t>(top + y / times) * width + static_cast<std::size_t>(left + x / times)) * 4;
                std::copy_n(one.targets.colour.begin() + static_cast<std::ptrdiff_t>(from), 4, beside.colour.begin() + static_cast<std::ptrdiff_t>((static_cast<std::size_t>(y) * beside.width + x) * 4));
                std::copy_n(colour.begin() + static_cast<std::ptrdiff_t>(from), 4,
                            beside.colour.begin() + static_cast<std::ptrdiff_t>((static_cast<std::size_t>(y) * beside.width + x + across * times + gap) * 4));
            }
        (void)keep(beside, "edges");
    };
    "what moves leaves nothing behind: where the box was a frame ago is the floor again, and the box is the box where it is now"_test = [] {
        scene::Scene world = yard();
        mesh::Pass pass;
        mesh::Temporal gathered;
        std::vector<std::uint8_t> colour;
        Frame before;
        for (std::uint32_t number = 0; number < 8; ++number) {
            before = drawn(world, yard_camera, 320, 180, pass, {.jitter = mesh::jitter_of(number)});
            (void)gathered.resolve(before.targets, colour);
        }
        // The box goes a long way to the left in one frame.
        const math::Mat4 was = world.find(10)->world;
        expect(world.move(10, math::translation({-1.5f, 0, 0}) * was).has_value());
        const Frame moved = drawn(world, yard_camera, 320, 180, pass, {.jitter = mesh::jitter_of(8)});
        const mesh::Temporal::Told told = gathered.resolve(moved.targets, colour);
        // Where the box was and is no more, the result is what the frame shows there (the floor; the ball's side):
        // nothing of the box is left. And the box, where it is now, is the box.
        std::uint32_t left_behind = 0, trailing = 0, of_box = 0, box_wrong = 0;
        for (std::size_t pixel = 0; pixel < moved.targets.ids.size(); ++pixel) {
            const int x = static_cast<int>(pixel % 320), y = static_cast<int>(pixel / 320);
            const auto who = mesh::at(moved.targets, x, y), was = mesh::at(before.targets, x, y);
            const math::Vec3 now = moved.targets.light[pixel], result = gathered.light()[pixel];
            const float off = std::abs(now.x - result.x) + std::abs(now.y - result.y) + std::abs(now.z - result.z);
            if (who && who->instance == 10) ++of_box, box_wrong += off > 0.25f;
            else if (was && was->instance == 10) ++left_behind, trailing += off > 0.1f;
        }
        std::println("the box jumped 1.5 to the left: {} pixels began again; of the {} it left, {} are not what the frame shows there; of the box's {} now, {} are not the box's", told.another, left_behind,
                     trailing, of_box, box_wrong);
        expect(gt(told.another, 1000u) && gt(left_behind, 1500u) && le(trailing, left_behind / 100) && le(box_wrong, of_box / 20)) << "no trail where it was, no smear where it is";
    };
    "what is under a point, by a ray and nothing drawn, is what the frame's id buffer says there: the same instance and the same triangle, pixel after pixel"_test = [] {
        const scene::Scene world = yard();
        const mesh::Assets assets = library().assets(true);
        const Frame frame = drawn(world, yard_camera, 240, 135);
        std::uint32_t same = 0, other_triangle = 0, other_instance = 0, drawn_pixels = 0;
        double depth_apart = 0;
        for (int y = 0; y < 135; ++y)
            for (int x = 0; x < 240; ++x) {
                const auto by_frame = mesh::at(frame.targets, x, y);
                const auto by_ray = mesh::pick(world, yard_camera.view(), assets, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 240, 135);
                drawn_pixels += by_frame.has_value();
                if (by_frame.has_value() != by_ray.has_value() || (by_frame && by_frame->instance != by_ray->instance)) ++other_instance;
                else if (by_frame && by_frame->triangle != by_ray->triangle) ++other_triangle;
                else if (by_frame) ++same, depth_apart = std::max(depth_apart, static_cast<double>(std::abs(by_frame->depth - by_ray->depth)));
            }
        std::println("32,400 points of the yard: {} drawn; the ray and the frame name the same triangle at {}, another triangle of the same thing at {}, another thing at {}; depths within {:.2e}", drawn_pixels,
                     same, other_triangle, other_instance, depth_apart);
        // Where they part is along an edge: the frame snaps corners to 1/256 of a pixel, a ray does not.
        expect(gt(same, drawn_pixels * 995 / 1000) && le(other_instance, 40u) && lt(depth_apart, 1e-3));
        // Past everything: nothing. A hidden thing is not there to be picked; on another layer, not for this view.
        expect(!mesh::pick(world, yard_camera.view(), assets, 120, 5, 240, 135).has_value());
        scene::Scene hidden = yard();
        scene::Instance ball_hidden = *hidden.find(11);
        ball_hidden.hidden = true;
        expect(hidden.set(ball_hidden).has_value());
        const auto through = mesh::pick(hidden, yard_camera.view(), assets, 123, 71, 240, 135);
        const auto before = mesh::pick(world, yard_camera.view(), assets, 123, 71, 240, 135);
        expect(fatal(before.has_value() && through.has_value()));
        expect(eq(before->instance, scene::Id{11}) && eq(through->instance, scene::Id{1})) << "with the ball hidden the floor behind it is under the point";
        // The other depth convention: the same answers.
        Camera reversed = yard_camera;
        reversed.reversed = true;
        const auto again = mesh::pick(world, reversed.view(), assets, 123, 71, 240, 135);
        expect(again.has_value() && eq(again->instance, scene::Id{11}) && eq(again->triangle, before->triangle));
    };
    "fog along a ray is worked out, not stepped: none is all the light, even fog is e to the minus density times length, thinning fog is what many small steps add up to"_test = [] {
        const math::Vec3 eye{0, 2, 0};
        expect(eq(mesh::through({}, eye, {0, 0, -1}, 100.0f), 1.0f)) << "no density, no fog";
        const mesh::Fog even{.density = 0.05f};
        expect(lt(std::abs(mesh::through(even, eye, {0, 0, -1}, 10.0f) - static_cast<float>(math::det::exp(-0.5))), 1e-6f));
        const float once = mesh::through(even, eye, {1, 0, 0}, 7.0f), twice = mesh::through(even, eye, {1, 0, 0}, 14.0f);
        expect(lt(std::abs(twice - once * once), 1e-6f)) << "twice as far is the share of the share";
        expect(eq(mesh::through(even, eye, {0, 1, 0}, std::numeric_limits<float>::infinity()), 0.0f)) << "even fog for ever lets nothing through, whichever way";

        const mesh::Fog thinning{.density = 0.2f, .base = 0, .falloff = 0.4f};
        for (const math::Vec3 toward : {math::normalized(math::Vec3{1, 0.5f, 0}), math::normalized(math::Vec3{0, -0.3f, -1}), math::Vec3{0, 0, -1}, math::Vec3{0, 1, 0}}) {
            const float length = 5;
            double stepped = 0;
            const int steps = 20000;
            for (int step = 0; step < steps; ++step) {
                const double along = (step + 0.5) * length / steps;
                stepped += 0.2 * math::det::exp(-0.4 * (eye.y + toward.y * along)) * length / steps;
            }
            const float worked = mesh::through(thinning, eye, toward, length);
            expect(lt(std::abs(worked - static_cast<float>(math::det::exp(-stepped))), 1e-5f)) << std::format("toward y {}: {} against {}", toward.y, worked, math::det::exp(-stepped));
        }
        // Up for ever through thinning fog is a finite amount of it; level for ever is all of it.
        const float up = mesh::through(thinning, eye, {0, 1, 0}, std::numeric_limits<float>::infinity());
        expect(lt(std::abs(up - static_cast<float>(math::det::exp(-0.2 * math::det::exp(-0.8) / 0.4))), 1e-6f)) << up;
        expect(eq(mesh::through(thinning, eye, {1, 0, 0}, std::numeric_limits<float>::infinity()), 0.0f));
        // Higher up there is less of it.
        expect(gt(mesh::through(thinning, {0, 6, 0}, {1, 0, 0}, 10.0f), mesh::through(thinning, {0, 1, 0}, {1, 0, 0}, 10.0f)));
    };

    "the yard in fog: who drew each pixel and how deep is unchanged; each surface's light is between its own and the fog's, the far floor more the fog's than the near; the sky shows more the higher it is looked at"_test = [] {
        const scene::Scene world = yard();
        const mesh::Fog fog{.colour = {0.5f, 0.6f, 0.7f}, .density = 0.08f, .base = -1, .falloff = 0.35f};
        const Frame clear = drawn(world, yard_camera, 480, 270), foggy = drawn(world, yard_camera, 480, 270, {.fog = fog});
        expect(eq(clear.stats.fogged, std::uint64_t{0}));
        expect(eq(foggy.stats.fogged, foggy.stats.written + static_cast<std::uint64_t>(std::ranges::count(foggy.targets.ids, mesh::none, &mesh::Pixel::draw)))) << "every sample written, and the background's pixels";
        const auto apart = [&](math::Vec3 light) { return std::abs(light.x - fog.colour.x) + std::abs(light.y - fog.colour.y) + std::abs(light.z - fog.colour.z); };
        std::size_t outside = 0, changed = 0;
        for (std::size_t pixel = 0; pixel < clear.targets.ids.size(); ++pixel) {
            changed += clear.targets.ids[pixel].draw != foggy.targets.ids[pixel].draw || clear.targets.ids[pixel].triangle != foggy.targets.ids[pixel].triangle ||
                       clear.targets.depth[pixel] != foggy.targets.depth[pixel];
            if (clear.targets.ids[pixel].draw == mesh::none) continue;
            const math::Vec3 own = clear.targets.light[pixel], seen = foggy.targets.light[pixel];
            for (const auto [low, high, value] : {std::tuple{std::min(own.x, fog.colour.x), std::max(own.x, fog.colour.x), seen.x}, std::tuple{std::min(own.y, fog.colour.y), std::max(own.y, fog.colour.y), seen.y},
                                                  std::tuple{std::min(own.z, fog.colour.z), std::max(own.z, fog.colour.z), seen.z}})
                outside += value < low - 1e-5f || value > high + 1e-5f;
        }
        expect(eq(changed, std::size_t{0})) << "fog is light, not geometry";
        expect(eq(outside, std::size_t{0}));
        // Down the left edge of the picture (the floor, nothing standing on it there): how much of its own light is
        // left, the nearest row and the farthest.
        const auto own_share = [&](std::size_t pixel) { return apart(foggy.targets.light[pixel]) / std::max(apart(clear.targets.light[pixel]), 1e-6f); };
        const auto floor_at = [&](int row) { return static_cast<std::size_t>(row) * 480 + 20; };
        int farthest = 269;
        while (farthest > 0 && clear.targets.ids[floor_at(farthest - 1)].draw == clear.targets.ids[floor_at(269)].draw) --farthest;
        expect(fatal(clear.targets.ids[floor_at(269)].draw != mesh::none && farthest < 200)) << farthest;
        const float near_share = own_share(floor_at(269)), far_share = own_share(floor_at(farthest));
        expect(gt(near_share, far_share + 0.1f)) << std::format("near {} far {}", near_share, far_share);
        // The background: up the middle column, where nothing is drawn. The higher, the more of it shows.
        std::vector<float> sky;
        for (int row = 0; row < 270; ++row)
            if (clear.targets.ids[static_cast<std::size_t>(row) * 480 + 240].draw == mesh::none) sky.push_back(apart(foggy.targets.light[static_cast<std::size_t>(row) * 480 + 240]));
        expect(fatal(sky.size() > 10)) << sky.size();
        expect(std::ranges::is_sorted(sky, std::greater{})) << "from the top down: less and less of the background";
        expect(gt(sky.front(), sky.back() + 0.05f)) << std::format("top {} lowest {}", sky.front(), sky.back());
        const auto file = keep(foggy.targets, "yard-fog");
        std::println("the yard in fog: floor's own light left, near {:.3f} far {:.3f}; sky {:.3f} at the top, {:.3f} at its lowest; bits {:016x}; {}", near_share, far_share, sky.front(), sky.back(),
                     bits(foggy.targets), file.generic_string());
        expect(eq(bits(foggy.targets), std::uint64_t{0xc84a8f6fceb3b509})) << std::format("{:016x}", bits(foggy.targets));
    };
    "a polished ball has a highlight where the sun is mirrored to the eye, and a matt one has none; rougher, it is dimmer and wider; the picture's bits are the same twice"_test = [] {
        const Camera camera{.eye = {0, 0.5f, 5}, .target = {0, 0, 0}};
        const math::Vec3 toward_sun = math::normalized(math::Vec3{0.5f, 1, 0.6f});
        struct Seen {
            float brightest = 0;
            int   x = 0, y = 0, near_it = 0;  // pixels at least 0.8 of the brightest
            std::uint64_t bits = 0;
        };
        const auto ball_of = [&](std::uint32_t material) {
            scene::Scene world;
            put(world, 1, ball, material, math::translation({0, 0, 0}));
            expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .colour = {1, 1, 1}, .intensity = 1, .toward = -toward_sun}).has_value());
            const Frame frame = drawn(world, camera, 256, 256);
            Seen seen{.bits = bits(frame.targets)};
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x) {
                    const math::Vec3 light = frame.targets.light[static_cast<std::size_t>(y) * 256 + x];
                    const float sum = light.x + light.y + light.z;
                    if (frame.targets.ids[static_cast<std::size_t>(y) * 256 + x].draw != mesh::none && sum > seen.brightest) seen.brightest = sum, seen.x = x, seen.y = y;
                }
            for (const math::Vec3& light : frame.targets.light) seen.near_it += light.x + light.y + light.z >= 0.8f * seen.brightest;
            return seen;
        };
        const Seen matt = ball_of(matt_blue), shiny = ball_of(polished), softer = ball_of(satin);
        // Where the highlight should be: the point of the ball whose normal is halfway between the eye and the sun.
        const math::Vec3 half = math::normalized(math::normalized(camera.eye) + toward_sun);
        const scene::View view = camera.view();
        const math::Vec3 there = math::point(view.projection * view.view, half);
        const float at_x = (there.x * 0.5f + 0.5f) * 256, at_y = (0.5f - there.y * 0.5f) * 256;
        std::println("a ball under one sun, 256²: matt brightest {:.2f} at ({}, {}); polished {:.2f} at ({}, {}), mirrored at ({:.0f}, {:.0f}), {} pixels near it; satin {:.2f}, {} pixels near it", matt.brightest, matt.x,
                     matt.y, shiny.brightest, shiny.x, shiny.y, at_x, at_y, shiny.near_it, softer.brightest, softer.near_it);
        expect(gt(shiny.brightest, matt.brightest * 2)) << "a highlight is far brighter than anything matt";
        expect(lt(std::abs(static_cast<float>(shiny.x) + 0.5f - at_x), 4.0f) && lt(std::abs(static_cast<float>(shiny.y) + 0.5f - at_y), 4.0f)) << "where the sun is mirrored (not quite at the half-way point: it is seen from a finite distance)";
        expect(lt(softer.brightest, shiny.brightest) && gt(softer.near_it, shiny.near_it)) << "rougher: dimmer and wider";
        expect(eq(ball_of(polished).bits, shiny.bits));
    };

    "a metal has no colour of its own but its highlight's: away from the highlight it is dark, and the highlight is the metal's colour"_test = [] {
        const Camera camera{.eye = {0, 0.5f, 5}, .target = {0, 0, 0}};
        scene::Scene world;
        put(world, 1, ball, gold, math::translation({0, 0, 0}));
        expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .colour = {1, 1, 1}, .intensity = 1, .toward = math::normalized(math::Vec3{-0.5f, -1, -0.6f})}).has_value());
        const Frame frame = drawn(world, camera, 256, 256, {.ambient = {0, 0, 0}});
        math::Vec3 brightest{};
        std::size_t lit_squarely = 0, dark = 0;
        for (std::size_t pixel = 0; pixel < frame.targets.light.size(); ++pixel) {
            if (frame.targets.ids[pixel].draw == mesh::none) continue;
            const math::Vec3 light = frame.targets.light[pixel];
            if (light.x > brightest.x) brightest = light;
            // Facing the sun squarely enough that a matt ball would be bright there.
            if (math::dot(frame.targets.normal[pixel], math::normalized(math::Vec3{0.5f, 1, 0.6f})) > 0.6f) ++lit_squarely, dark += light.x < 0.08f;
        }
        std::println("a gold ball: its highlight ({:.2f}, {:.2f}, {:.2f}); of {} pixels the sun falls squarely on, {} are darker than 0.08", brightest.x, brightest.y, brightest.z, lit_squarely, dark);
        expect(gt(brightest.x, 1.0f)) << "a highlight";
        expect(lt(std::abs(brightest.y / brightest.x - 0.77f), 0.08f) && lt(std::abs(brightest.z / brightest.x - 0.34f), 0.12f)) << "of the metal's colour (whiter only at a glancing look)";
        expect(gt(dark * 3, lit_squarely)) << "nothing matt in it: off the highlight it sends the sun nowhere near the eye";
    };

    "the yard with a polished ball and a gold post: pinned, and kept"_test = [] {
        scene::Scene world;
        put(world, 1, floor_mesh, 1, math::translation({0, -1, 0}));
        put(world, 10, box, 0, math::Transform{{-2.2f, 0, 0}, math::Quat::about({0, 1, 0}, 0.6f), {1, 1, 1}}.matrix());
        put(world, 11, ball, polished, math::translation({0.4f, 0, 0.8f}));
        put(world, 12, post, gold, math::translation({2.6f, 0, -0.5f}));
        expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .colour = {1, 0.96f, 0.9f}, .intensity = 0.9f, .toward = {-0.4f, -1, -0.5f}}).has_value());
        expect(world.add(scene::Light{.id = 2, .kind = scene::LightKind::point, .colour = {1, 0.6f, 0.2f}, .intensity = 2.5f, .at = {1.2f, 1.6f, -1.5f}, .range = 6}).has_value());
        const Frame frame = drawn(world, yard_camera, 480, 270, {}, true);
        const auto file = keep(frame.targets, "yard-polished");
        std::println("the yard with a polished ball and a gold post: bits {:016x}; {}", bits(frame.targets), file.generic_string());
        expect(eq(bits(frame.targets), std::uint64_t{0x329f4ce273cd0cf1})) << std::format("{:016x}", bits(frame.targets));
    };
    "a metal mirrors what is about it: a gold ball under a sky and over the ground is the sky's colour through gold above and the ground's below; a matt ball asks nothing of it"_test = [] {
        const Camera camera{.eye = {0, 0, 5}, .target = {0, 0, 0}};
        const mesh::Environment about{.on = true};
        const auto ball_of = [&](std::uint32_t material, bool with) {
            scene::Scene world;
            put(world, 1, ball, material, math::translation({0, 0, 0}));
            return drawn(world, camera, 256, 256, {.ambient = {0, 0, 0}, .environment = with ? about : mesh::Environment{}});
        };
        const Frame golden = ball_of(gold, true), bare = ball_of(gold, false), matt = ball_of(matt_blue, true), matt_bare = ball_of(matt_blue, false);
        expect(eq(bits(matt.targets), bits(matt_bare.targets))) << "a matt surface is the same with it and without";
        const auto light_at = [&](const Frame& frame, int x, int y) { return frame.targets.light[static_cast<std::size_t>(y) * 256 + x]; };
        // Near the ball's top its normal is up and the eye's way is turned skyward; near its bottom, to the ground.
        const math::Vec3 top = light_at(golden, 128, 96), bottom = light_at(golden, 128, 160), middle = light_at(golden, 128, 128);
        std::println("a gold ball with a sky about it: near its top ({:.2f}, {:.2f}, {:.2f}), its middle ({:.2f}, {:.2f}, {:.2f}), near its bottom ({:.2f}, {:.2f}, {:.2f}); with nothing about it the middle is {:.2f}",
                     top.x, top.y, top.z, middle.x, middle.y, middle.z, bottom.x, bottom.y, bottom.z, light_at(bare, 128, 128).x);
        expect(eq(light_at(bare, 128, 128).x, 0.0f)) << "with nothing about it and no light, a metal is black";
        expect(gt(top.z / top.x, bottom.z / bottom.x * 1.3f)) << "bluer above (the sky through gold) than below (the ground through gold)";
        expect(gt(top.x + top.y + top.z, bottom.x + bottom.y + bottom.z)) << "and brighter";
        // Facing the eye squarely it mirrors what is behind the eye, at eye level: the level's colour through gold.
        expect(lt(std::abs(middle.y / middle.x - 0.77f), 0.05f) && lt(std::abs(middle.z / middle.x - 0.34f), 0.05f));
        const auto file = keep(golden.targets, "gold-ball");
        expect(eq(bits(golden.targets), std::uint64_t{0x66ca9a070b7d680d})) << std::format("{:016x} {}", bits(golden.targets), file.generic_string());
    };
    "light brighter than the display shows spreads: one bright pixel's excess is all there about it, evenly, and nothing dimmer spreads; with no radius the frame is its light through the curve"_test = [] {
        const int width = 64, height = 48;
        std::vector<math::Vec3> light(static_cast<std::size_t>(width) * height, math::Vec3{0.2f, 0.2f, 0.2f});
        light[24 * width + 32] = {9, 5, 1};
        mesh::Glow glow;
        std::vector<std::uint8_t> colour;
        const auto told = glow.show(light, width, height, {.threshold = 1, .radius = 3, .strength = 0.5f}, colour);
        expect(eq(told.bright, 1u));
        const auto spread = glow.spread();
        math::Vec3 sum{0, 0, 0};
        float uneven = 0;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const math::Vec3 here = spread[static_cast<std::size_t>(y) * width + x];
                sum = sum + here;
                if (x > 0 && y > 0) uneven = std::max({uneven, std::abs(here.x - spread[static_cast<std::size_t>(y) * width + (64 - x)].x), std::abs(here.x - spread[static_cast<std::size_t>(48 - y) * width + x].x)});
                if (std::abs(x - 32) > 9 || std::abs(y - 24) > 9) expect(eq(here.x, 0.0f)) << "three passes of 3 reach 9 and no farther";
            }
        std::println("one pixel of (9, 5, 1) over a threshold of 1: ({:.4f}, {:.4f}, {:.4f}) spread in all — its excess is (8, 4.444, 0.889); the most at one pixel {:.4f}; unevenness {:.2e}", sum.x, sum.y, sum.z,
                     spread[24 * width + 32].x, uneven);
        expect(lt(std::abs(sum.x - 8.0f), 1e-3f) && lt(std::abs(sum.y - 8.0f * 5 / 9), 1e-3f) && lt(std::abs(sum.z - 8.0f / 9), 1e-3f)) << "its excess, in its own colour, neither more nor less";
        expect(lt(uneven, 1e-6f)) << "the same to the left as to the right, above as below";
        expect(gt(spread[24 * width + 32].x, spread[24 * width + 35].x) && gt(spread[24 * width + 35].x, spread[24 * width + 38].x)) << "most where it came from";
        expect(gt(colour[(24 * width + 35) * 4], colour[(4 * width + 4) * 4])) << "its neighbours are lighter for it";
        // Nothing above the threshold: nothing spreads, and the frame is what the pass itself wrote.
        const Frame frame = drawn(yard(), yard_camera, 160, 90);
        const auto none = glow.show(frame.targets.light, 160, 90, {.threshold = 100, .radius = 6}, colour);
        expect(eq(none.bright, 0u));
        expect(colour == frame.targets.colour);
        (void)glow.show(frame.targets.light, 160, 90, {}, colour);
        expect(colour == frame.targets.colour) << "no radius";
    };
    "a harbour at night after rain: lamps and a spot with their shadows, a wet quay, fog, the lamps' glow — pinned, and kept"_test = [] {
        const scene::Scene world = harbour();
        const int width = 480, height = 270;
        const Frame frame = drawn(world, harbour_camera, width, height, harbour_night, true), unshadowed = drawn(world, harbour_camera, width, height, harbour_night);
        mesh::Targets shown = frame.targets;
        mesh::Glow glow;
        const auto told = glow.show(frame.targets.light, width, height, {.threshold = 1, .radius = 7, .strength = 0.6f}, shown.colour);
        const auto file = keep(shown, "harbour");
        (void)keep(frame.targets, "harbour-no-glow");
        std::uint32_t darker = 0, lighter = 0, glowing = 0, wet_bright = 0;
        for (std::size_t pixel = 0; pixel < frame.targets.light.size(); ++pixel) {
            const math::Vec3 with = frame.targets.light[pixel], without = unshadowed.targets.light[pixel];
            darker += with.x + with.y + with.z < (without.x + without.y + without.z) * 0.9f;
            lighter += with.x + with.y + with.z > (without.x + without.y + without.z) * 1.001f;
            glowing += shown.colour[pixel * 4] > frame.targets.colour[pixel * 4] + 2;
            const mesh::Pixel who = frame.targets.ids[pixel];
            if (who.draw != mesh::none && frame.targets.draws[who.draw].instance == 1 && std::max({with.x, with.y, with.z}) > 0.5f) ++wet_bright;
        }
        std::println("the harbour, 480 × 270: {} written, {} rays, {} of them stopped; {} pixels in a shadow, {} lighter for one; {} pixels brighter than the display, {} lighter by their glow; {} of the quay's pixels bright with a lamp's highlight; bits {:016x}; {}",
                     frame.stats.written, frame.stats.rays, frame.stats.shadowed, darker, lighter, told.bright, glowing, wet_bright, bits(shown), file.generic_string());
        expect(gt(darker, 2000u)) << "several lights, each with its shadows";
        expect(eq(lighter, 0u)) << "a shadow only takes light away";
        expect(gt(told.bright, 50u) && gt(glowing, told.bright * 4)) << "the globes are brighter than white, and it shows about them";
        expect(gt(wet_bright, 200u)) << "the wet quay throws the lamps back";
        expect(eq(bits(shown), std::uint64_t{0x754074c9761c2350})) << std::format("{:016x}", bits(shown));
    };
    "a texture is its picture at every size: a texel's middle is the texel, a pixel that covers many takes their mean, and between two sizes it is between them"_test = [] {
        // Four by four: black and white squares of two.
        std::vector<std::uint8_t> bytes(4 * 4 * 4, 255);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                if (((x / 2) + (y / 2)) % 2 == 0) bytes[static_cast<std::size_t>(y * 4 + x) * 4] = bytes[static_cast<std::size_t>(y * 4 + x) * 4 + 1] = bytes[static_cast<std::size_t>(y * 4 + x) * 4 + 2] = 0;
        const auto texture = mesh::Texture::made(bytes, 4, 4);
        expect(fatal(texture.has_value()));
        expect(eq(texture->size_count(), std::size_t{3})) << "4, 2 and 1 across";
        expect(eq(texture->light.size(), std::size_t{16 + 4 + 1}));
        expect(eq(texture->at({0.125f, 0.125f}, 1).x, 0.0f) && eq(texture->at({0.625f, 0.125f}, 1).x, 1.0f)) << "a texel's middle, at a pixel a texel";
        expect(eq(texture->at({0.5f, 0.125f}, 1).x, 0.5f)) << "between a black and a white: half";
        expect(eq(texture->at({1.125f, -0.875f}, 1).x, 0.0f)) << "it repeats";
        expect(eq(texture->at({0.25f, 0.25f}, 2).x, 0.0f)) << "at two texels a pixel: the size of 2 across, whose texels are the squares";
        expect(eq(texture->at({0.3f, 0.7f}, 4).x, 0.5f) && eq(texture->at({0.3f, 0.7f}, 64).x, 0.5f)) << "at four and beyond: the one texel, the mean of all";
        const float between = texture->at({0.25f, 0.25f}, 3).x;
        expect(eq(between, 0.25f)) << "halfway from the black square to the mean";
        expect(!mesh::Texture::made(bytes, 8, 8).has_value() && !mesh::Texture::made(bytes, 0, 4).has_value());
    };
    "a floor of tiles to the horizon: sharp near, and far away the mean of its tiles — no shimmer; who drew each pixel and how deep are the plain floor's; pinned, and kept"_test = [] {
        const auto yard_with = [](std::uint32_t floor, std::uint32_t material) {
            scene::Scene world;
            put(world, 1, floor, material, math::translation({0, -1, 0}));
            put(world, 10, box, 0, math::Transform{{-2.2f, 0, 0}, math::Quat::about({0, 1, 0}, 0.6f), {1, 1, 1}}.matrix());
            put(world, 11, ball, 2, math::translation({0.4f, 0, 0.8f}));
            expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .colour = {1, 0.96f, 0.9f}, .intensity = 0.9f, .toward = {-0.4f, -1, -0.5f}}).has_value());
            return world;
        };
        const Frame tiled = drawn(yard_with(tiled_floor, tiles), yard_camera, 480, 270), plain = drawn(yard_with(floor_mesh, 1), yard_camera, 480, 270);
        const auto file = keep(tiled.targets, "yard-tiles");
        // A row of the floor: how far its light is from its own mean (the tiles' contrast there).
        const auto spread = [&](int row) {
            double sum = 0, squares = 0;
            int count = 0;
            for (int x = 0; x < 480; ++x) {
                const std::size_t pixel = static_cast<std::size_t>(row) * 480 + x;
                if (tiled.targets.ids[pixel].draw == mesh::none || tiled.targets.draws[tiled.targets.ids[pixel].draw].id != 1) continue;
                sum += tiled.targets.light[pixel].y, squares += static_cast<double>(tiled.targets.light[pixel].y) * tiled.targets.light[pixel].y, ++count;
            }
            const double mean = count ? sum / count : 0;
            return std::pair(mean, count ? std::sqrt(std::max(squares / count - mean * mean, 0.0)) : 0.0);
        };
        const auto [near_mean, near_spread] = spread(250);
        const auto [far_mean, far_spread] = spread(74);
        std::size_t same_who = 0;
        for (std::size_t pixel = 0; pixel < tiled.targets.ids.size(); ++pixel) same_who += tiled.targets.depth[pixel] == plain.targets.depth[pixel];
        std::println("a floor of tiles: near the eye its light is {:.3f} ± {:.3f} along a row, by the horizon {:.3f} ± {:.3f}; {} samples looked up; bits {:016x}; {}", near_mean, near_spread, far_mean, far_spread,
                     tiled.stats.textured, bits(tiled.targets), file.generic_string());
        expect(gt(near_spread, 0.15)) << "near: light tiles and dark";
        expect(lt(far_spread, near_spread * 0.12)) << "far: a pixel covers many tiles and is their mean";
        expect(lt(std::abs(far_mean / near_mean - 1), 0.25)) << "and as light on the whole as the near floor (the sun's angle is the same)";
        expect(eq(same_who, tiled.targets.ids.size())) << "a texture changes no depth";
        expect(gt(tiled.stats.textured, 60000u) && eq(plain.stats.textured, 0u));
        expect(eq(bits(tiled.targets), std::uint64_t{0xb6962b046d17007d})) << std::format("{:016x}", bits(tiled.targets));
    };
    "the surface's other pictures: an even picture of roughness and metal, of emission, is the material that says the same numbers, bit for bit; a normal picture turns the surface toward a light or away, the same with the mesh's tangents as without; a shade picture takes its share of the light from everywhere"_test = [] {
        // A sheet facing the eye, a sun from the eye's right and a little before it, light from everywhere.
        const geom::ClusteredMesh sheet_mesh = *geom::clustered(geom::sheet(4, 4, 2, 2));
        geom::Mesh bare = sheet_mesh.mesh;
        bare.tangents.clear();
        const geom::ClusteredMesh no_tangents = *geom::clustered(bare);
        const std::array<const geom::ClusteredMesh*, 2> meshes{&sheet_mesh, &no_tangents};
        const auto even = [](std::uint8_t red, std::uint8_t green, std::uint8_t blue, mesh::Coding coding) {
            std::vector<std::uint8_t> bytes(8 * 8 * 4, 255);
            for (std::size_t texel = 0; texel < 64; ++texel) bytes[texel * 4] = red, bytes[texel * 4 + 1] = green, bytes[texel * 4 + 2] = blue;
            return *mesh::Texture::made(bytes, 8, 8, coding);
        };
        // 0: roughness 102/255, metal 51/255. 1: a colour. 2: the surface turned toward the picture's right.
        // 3: toward its left. 4: half the light from everywhere. 5: straight out (as near as a byte says).
        const std::array<mesh::Texture, 6> pictures{even(255, 102, 51, mesh::Coding::plain), even(200, 120, 40, mesh::Coding::colour), even(230, 128, 200, mesh::Coding::plain),
                                                     even(26, 128, 200, mesh::Coding::plain),  even(128, 0, 0, mesh::Coding::plain),    even(128, 128, 255, mesh::Coding::plain)};
        std::array<const mesh::Texture*, 6> textures{};
        for (std::size_t index = 0; index < pictures.size(); ++index) textures[index] = &pictures[index];
        const auto frame_of = [&](mesh::Material material, std::uint32_t which = 0) {
            scene::Scene world;
            // The sheet stood up to face +z: its picture's right is the world's +x, its up the world's +y.
            expect(fatal(world.add({.id = 1, .mesh = which, .material = 0, .world = math::Transform{{0, 0, 0}, math::Quat::about({1, 0, 0}, 1.5707964f), {1, 1, 1}}.matrix(), .bounds = geom::bounds(sheet_mesh.mesh)}).has_value()));
            expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .intensity = 1, .toward = {-0.8f, 0, -0.6f}}).has_value());
            const Camera camera{.eye = {0, 0, 6}};
            Frame frame;
            frame.targets.resize(96, 96);
            scene::Extracted seen;
            scene::extract(world, camera.view(), seen);
            mesh::Pass pass;
            const std::array<mesh::Material, 1> materials{material};
            pass.draw(world, seen, camera.view(), {meshes, materials, {}, textures}, frame.targets, {.ambient = {0.2f, 0.2f, 0.2f}});
            frame.stats = pass.last();
            return frame;
        };
        const auto middle = [](const Frame& frame) { return frame.targets.light[48 * 96 + 48]; };

        // Roughness and metal from an even picture: the material that says the products.
        const Frame from_picture = frame_of({.base = {0.7f, 0.5f, 0.3f}, .roughness = 1, .metallic = 1, .surface = 0});
        const Frame from_numbers = frame_of({.base = {0.7f, 0.5f, 0.3f}, .roughness = 102.0f / 255.0f, .metallic = 51.0f / 255.0f});
        expect(eq(bits(from_picture.targets), bits(from_numbers.targets)) && gt(from_picture.stats.textured, 1000u) && eq(from_numbers.stats.textured, 0u));
        // Emission from an even picture of a colour: the emission that is that colour's light.
        const math::Vec3 light = pictures[1].at({0.5f, 0.5f}, 1);
        const Frame glow_picture = frame_of({.base = {0.1f, 0.1f, 0.1f}, .emissive = {2, 1, 0.5f}, .glow = 1});
        const Frame glow_numbers = frame_of({.base = {0.1f, 0.1f, 0.1f}, .emissive = {2 * light.x, 1 * light.y, 0.5f * light.z}});
        expect(eq(bits(glow_picture.targets), bits(glow_numbers.targets)));

        // A normal picture: turned toward the sun (which is to the right) the sheet is lighter than flat, turned
        // away darker; as near straight out as a byte says, as good as flat. The G-buffer's normal is the turned one.
        const mesh::Material matt{.base = {0.8f, 0.8f, 0.8f}};
        const auto with_normal = [&](std::uint32_t picture, std::uint32_t which = 0, float scale = 1) {
            mesh::Material material = matt;
            material.normal = picture, material.normal_scale = scale;
            return frame_of(material, which);
        };
        const Frame flat = frame_of(matt), toward = with_normal(2), away = with_normal(3), out = with_normal(5);
        expect(gt(middle(toward).x, middle(flat).x * 1.2f) && lt(middle(away).x, middle(flat).x * 0.8f)) << middle(toward).x << " " << middle(flat).x << " " << middle(away).x;
        expect(lt(std::abs(middle(out).x - middle(flat).x), 0.01f));
        expect(gt(toward.targets.normal[48 * 96 + 48].x, 0.5f) && lt(away.targets.normal[48 * 96 + 48].x, -0.5f) && lt(std::abs(flat.targets.normal[48 * 96 + 48].x), 1e-6f));
        // Twice as far by the scale; none at 0.
        expect(gt(with_normal(2, 0, 2).targets.normal[48 * 96 + 48].x, toward.targets.normal[48 * 96 + 48].x + 0.1f));
        expect(lt(std::abs(with_normal(2, 0, 0).targets.normal[48 * 96 + 48].x), 1e-6f));
        // A mesh with no tangents: the picture's ways are worked out from its triangles — the same surface.
        const Frame worked_out = with_normal(2, 1);
        float apart = 0;
        for (std::size_t pixel = 0; pixel < toward.targets.light.size(); ++pixel) apart = std::max(apart, std::abs(toward.targets.light[pixel].x - worked_out.targets.light[pixel].x));
        expect(lt(apart, 1e-4f)) << apart;

        // Shade: half the light from everywhere, none of the sun's taken.
        mesh::Material shaded = matt;
        shaded.shade = 4;
        const Frame in_shade = frame_of(shaded);
        const float ambient = 0.8f * 0.2f, sun = middle(flat).x - ambient;
        expect(lt(std::abs(middle(in_shade).x - (ambient * (128.0f / 255.0f) + sun)), 1e-5f)) << middle(in_shade).x;
        shaded.shade_strength = 0;
        expect(eq(bits(frame_of(shaded).targets), bits(flat.targets))) << "a strength of 0: the picture says nothing";
        // A picture a material names that is not there, or a mesh with no texture coordinates: as if not named.
        mesh::Material absent = matt;
        absent.normal = 77, absent.surface = 77, absent.glow = 77, absent.shade = 77;
        expect(eq(bits(frame_of(absent).targets), bits(flat.targets)));
    };
    "a surface seen from both sides, and one cut out by its picture: behind a leaf's outline there is no leaf — not in the colour, not in who drew the pixel, not in its shadow, not for a ray — and no dark rim about it"_test = [] {
        const geom::ClusteredMesh sheet_mesh = *geom::clustered(geom::sheet(4, 4, 2, 2));
        const geom::Bvh sheet_tree = geom::bvh_of(sheet_mesh.mesh);
        const std::array<const geom::ClusteredMesh*, 1> meshes{&sheet_mesh};
        const std::array<const geom::Bvh*, 1> trees{&sheet_tree};
        // A picture whose left half is red and whole, whose right half is clear (and black, as clear texels are kept).
        std::vector<std::uint8_t> bytes(8 * 8 * 4, 0);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 4; ++x) bytes[static_cast<std::size_t>(y * 8 + x) * 4] = 255, bytes[static_cast<std::size_t>(y * 8 + x) * 4 + 3] = 255;
        const mesh::Texture half = *mesh::Texture::made(bytes, 8, 8);
        const std::array<const mesh::Texture*, 1> textures{&half};
        // The picture itself: what is clear weighs nothing in a mean — the coarsest size is red, half there.
        expect(eq(half.cover.size(), half.light.size()) && near(half.cover_at({0.25f, 0.5f}), 1.0f, 1e-6f) && near(half.cover_at({0.75f, 0.5f}), 0.0f, 1e-6f));
        const math::Vec3 coarsest = half.at({0.5f, 0.5f}, 8);
        expect(near(coarsest.x, 1.0f, 1e-5f) && near(coarsest.y, 0.0f, 1e-6f) && near(half.cover.back(), 0.5f, 1e-6f)) << coarsest.x;
        std::vector<std::uint8_t> solid(8 * 8 * 4, 255);
        expect(mesh::Texture::made(solid, 8, 8)->cover.empty()) << "a picture that hides everywhere carries no cover";

        // A sheet stood up to face +z at the middle (id 1); behind it a wall three times its size (id 2).
        const math::Mat4 upright = math::Transform{{0, 0, 0}, math::Quat::about({1, 0, 0}, 1.5707964f), {1, 1, 1}}.matrix();
        const math::Mat4 wall = math::Transform{{0, 0, -2}, math::Quat::about({1, 0, 0}, 1.5707964f), {3, 1, 3}}.matrix();
        struct Seen {
            Frame                       frame;
            std::optional<mesh::Source> left, through;  // what a ray finds through (30, 48) and through (59, 48)
        };
        const auto frame_of = [&](mesh::Material material, math::Vec3 eye, math::Vec3 sun, bool with_wall = false) {
            scene::Scene world;
            expect(fatal(world.add({.id = 1, .mesh = 0, .material = 0, .world = upright, .bounds = geom::bounds(sheet_mesh.mesh)}).has_value()));
            if (with_wall) expect(fatal(world.add({.id = 2, .mesh = 0, .material = 1, .world = wall, .bounds = geom::bounds(sheet_mesh.mesh)}).has_value()));
            expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .intensity = 1, .toward = sun}).has_value());
            const Camera camera{.eye = eye};
            Seen seen;
            seen.frame.targets.resize(96, 96);
            scene::Extracted extracted;
            scene::extract(world, camera.view(), extracted);
            mesh::Pass pass;
            const std::array<mesh::Material, 2> materials{material, mesh::Material{.base = {0.6f, 0.6f, 0.6f}}};
            const mesh::Assets assets{meshes, materials, with_wall ? std::span<const geom::Bvh* const>(trees) : std::span<const geom::Bvh* const>(), textures};
            pass.draw(world, extracted, camera.view(), assets, seen.frame.targets, {.background = {0, 0, 0.5f}, .ambient = {0.1f, 0.1f, 0.1f}});
            seen.frame.stats = pass.last();
            if (with_wall) {
                const mesh::Assets with_trees{meshes, materials, trees, textures};
                seen.left = mesh::pick(world, camera.view(), with_trees, 30.5f, 48.5f, 96, 96), seen.through = mesh::pick(world, camera.view(), with_trees, 59.5f, 48.5f, 96, 96);
            }
            return seen;
        };
        const auto who = [](const Frame& frame, int x, int y) { return mesh::at(frame.targets, x, y); };
        const auto light = [](const Frame& frame, int x, int y) { return frame.targets.light[static_cast<std::size_t>(y) * 96 + static_cast<std::size_t>(x)]; };
        const math::Vec3 before{0, 0, 6}, behind{0, 0, -6};

        // From behind, lit from behind: a surface of one side is not there; one of both is, facing the eye, and lit.
        const Seen one_side = frame_of({.base = {0.8f, 0.8f, 0.8f}}, behind, {0, 0, 1});
        const Seen both = frame_of({.base = {0.8f, 0.8f, 0.8f}, .two_sided = true}, behind, {0, 0, 1});
        expect(!who(one_side.frame, 48, 48).has_value() && eq(one_side.frame.stats.written, 0u) && gt(one_side.frame.stats.facing_away + one_side.frame.stats.clusters_away, 0u));
        const auto back_side = who(both.frame, 48, 48);
        expect(fatal(back_side.has_value()));
        expect(near(back_side->normal.z, -1.0f, 1e-5f) && near(light(both.frame, 48, 48).x, 0.8f * 1.1f, 1e-4f)) << back_side->normal.z << ' ' << light(both.frame, 48, 48).x;
        // From the front it is the surface it was: the same bits with and without.
        expect(eq(bits(frame_of({.base = {0.8f, 0.8f, 0.8f}, .two_sided = true}, before, {0, 0, -1}).frame.targets), bits(frame_of({.base = {0.8f, 0.8f, 0.8f}}, before, {0, 0, -1}).frame.targets)));

        // Cut out by its picture: the left half is there and red to its very edge; the right half is not.
        const mesh::Material leaf{.base = {1, 1, 1}, .texture = 0, .cutout = 0.5f};
        const Seen cut = frame_of(leaf, before, {0, 0, -1});
        expect(who(cut.frame, 30, 48).has_value() && !who(cut.frame, 60, 48).has_value() && gt(cut.frame.stats.cut, 500u));
        expect(near(light(cut.frame, 60, 48).z, 0.5f, 1e-6f)) << "the background shows through";
        int last = 0;
        for (int column = 0; column < 96; ++column)
            if (who(cut.frame, column, 48).has_value()) last = column;
        expect(ge(last, 46) && le(last, 49)) << last;
        expect(eq(std::bit_cast<std::uint32_t>(light(cut.frame, last, 48).x), std::bit_cast<std::uint32_t>(light(cut.frame, 30, 48).x)) && eq(light(cut.frame, last, 48).y, 0.0f))
            << "the last pixel of the leaf is as red as its middle: no dark rim";
        // Without the cut the surface is whole whatever its picture's cover says.
        const Seen whole = frame_of({.base = {1, 1, 1}, .texture = 0}, before, {0, 0, -1});
        expect(who(whole.frame, 60, 48).has_value() && eq(whole.frame.stats.cut, 0u));
        // By its own opacity alone: below the cut it is not there at all, above it is whole.
        expect(eq(frame_of({.opacity = 0.3f, .cutout = 0.5f}, before, {0, 0, -1}).frame.stats.written, 0u) && gt(frame_of({.opacity = 0.6f, .cutout = 0.5f}, before, {0, 0, -1}).frame.stats.written, 1000u));

        // Its shadow and a ray: a sun from the eye's left lays the leaf's shadow on the wall behind, a unit and a half
        // to the right — seen through the hole. The wall is dark where the leaf's red half is between it and the
        // sun, lit where the sun came through the hole.
        const math::Vec3 slanted{0.6f, 0, -0.8f};
        const Seen shadowed = frame_of(leaf, before, slanted, true);
        const auto in_shadow = who(shadowed.frame, 59, 48), in_light = who(shadowed.frame, 69, 48);
        expect(fatal(in_shadow.has_value() && in_light.has_value()));
        expect(eq(in_shadow->instance, scene::Id{2}) && eq(in_light->instance, scene::Id{2}));
        expect(near(light(shadowed.frame, 59, 48).x, 0.6f * 0.1f, 1e-4f) && near(light(shadowed.frame, 69, 48).x, 0.6f * (0.1f + 0.8f), 1e-4f))
            << light(shadowed.frame, 59, 48).x << ' ' << light(shadowed.frame, 69, 48).x;
        expect(fatal(shadowed.left.has_value() && shadowed.through.has_value()));
        expect(eq(shadowed.left->instance, scene::Id{1}) && eq(shadowed.through->instance, scene::Id{2})) << "a ray goes through the hole to the wall";
        // The same leaf whole: a ray stops at it.
        const Seen solid_leaf = frame_of({.base = {1, 1, 1}, .texture = 0}, before, slanted, true);
        expect(fatal(solid_leaf.through.has_value()));
        expect(eq(solid_leaf.through->instance, scene::Id{1}));
        // Seen from behind, a leaf of both sides is found by a ray there too, facing it.
        const Seen leaf_behind = frame_of({.base = {1, 1, 1}, .texture = 0, .cutout = 0.5f, .two_sided = true}, {0, 0, -1.5f}, {0, 0, 1}, true);
        expect(gt(leaf_behind.frame.stats.written, 100u) && gt(leaf_behind.frame.stats.cut, 100u));
    };

    "light followed along rays: an open floor under an even sky sends back the sky's light times its colour, exactly; beside a ball it is darker; before a red box in the sun it is redder — the same bits twice, pinned, and kept"_test = [] {
        constexpr int width = 160, height = 90;
        const auto place_of = [&](const Camera& camera, math::Vec3 at) {
            const scene::View view = camera.view();
            const math::Vec4 clip = (view.projection * view.view) * math::Vec4{at.x, at.y, at.z, 1};
            return std::pair(static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * width), static_cast<int>((0.5f - clip.y / clip.w * 0.5f) * height));
        };
        const auto about = [&](const mesh::Targets& targets, std::pair<int, int> at) {  // the mean of nine pixels
            math::Vec3 sum{0, 0, 0};
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) sum = sum + targets.light[static_cast<std::size_t>(std::clamp(at.second + dy, 0, height - 1)) * width + std::clamp(at.first + dx, 0, width - 1)];
            return sum * (1.0f / 9);
        };
        const mesh::Options even_sky{.background = {0.5f, 0.6f, 0.7f}};
        // A floor and nothing else, no light but the sky.
        {
            scene::Scene world;
            put(world, 1, floor_mesh, 1, math::translation({0, -1, 0}));
            mesh::Targets seen;
            seen.resize(width, height);
            const mesh::Traced told = mesh::traced(world, yard_camera.view(), library().assets(true), seen, even_sky, {.samples = 4, .bounces = 2});
            const math::Vec3 floor = seen.light[static_cast<std::size_t>(80) * width + 80], above = seen.light[static_cast<std::size_t>(5) * width + 80];
            expect(lt(std::abs(floor.x - 0.25f), 1e-6f) && lt(std::abs(floor.y - 0.3f), 1e-6f) && lt(std::abs(floor.z - 0.35f), 1e-6f)) << std::format("{} {} {}", floor.x, floor.y, floor.z);
            expect(eq(above.x, 0.5f) && eq(above.z, 0.7f)) << "and above the horizon, the sky";
            expect(gt(told.into_open, told.rays / 3));
        }
        // The yard with its lights taken away: the sky's light alone, less of it where the ball is over the floor.
        scene::Scene unlit = yard();
        expect(unlit.remove_light(1).has_value() && unlit.remove_light(2).has_value());
        mesh::Targets by_sky;
        by_sky.resize(width, height);
        (void)mesh::traced(unlit, yard_camera.view(), library().assets(true), by_sky, even_sky, {.samples = 32, .bounces = 2});
        const math::Vec3 open = about(by_sky, place_of(yard_camera, {4.5f, -1, 3})), by_ball = about(by_sky, place_of(yard_camera, {0.4f, -1, 1.95f}));
        // The yard as it is: the sun on the red box, and the floor before it.
        const scene::Scene world = yard();
        mesh::Targets seen, again, other;
        seen.resize(width, height), again.resize(width, height), other.resize(width, height);
        const auto from = std::chrono::steady_clock::now();
        const mesh::Traced told = mesh::traced(world, yard_camera.view(), library().assets(true), seen, even_sky, {.samples = 32, .bounces = 2});
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
        (void)mesh::traced(world, yard_camera.view(), library().assets(true), again, even_sky, {.samples = 32, .bounces = 2});
        (void)mesh::traced(world, yard_camera.view(), library().assets(true), other, even_sky, {.samples = 32, .bounces = 2, .seed = 2});
        const math::Vec3 lit_open = about(seen, place_of(yard_camera, {4.5f, -1, 3})), before_box = about(seen, place_of(yard_camera, {-1.6f, -1, 1.75f}));
        const auto file = keep(seen, "yard-traced");
        std::println("followed along rays, 160 × 90, 32 a pixel, 2 on: {} rays ({} into the open) in {:.2f} s; by the sky alone the open floor {:.3f}, beside the ball {:.3f}; in the sun the open floor's red over green {:.3f}, before the red box {:.3f}; bits {:016x}; {}",
                     told.rays, told.into_open, took, open.y, by_ball.y, lit_open.x / lit_open.y, before_box.x / before_box.y, bits(seen), file.generic_string());
        expect(lt(std::abs(open.y / 0.3f - 1), 0.05f)) << "open: all of the sky";
        expect(lt(by_ball.y, open.y * 0.85f)) << "beside the ball: less of it";
        expect(gt(before_box.x / before_box.y, lit_open.x / lit_open.y * 1.04f)) << "the box's red on the floor before it";
        expect(seen.light == again.light && seen.colour == again.colour) << "the same bits twice";
        expect(seen.light != other.light) << "another seed, other rays";
        expect(eq(bits(seen), std::uint64_t{0xf0b9ab79dca2b15c})) << std::format("{:016x}", bits(seen));
        // Kept larger for the README, beside the pass's own picture of the same yard.
        mesh::Targets large;
        large.resize(320, 180);
        (void)mesh::traced(world, yard_camera.view(), library().assets(true), large, even_sky, {.samples = 48, .bounces = 2});
        (void)keep(large, "yard-traced-large");
    };
    "a mirror followed along rays shows what is over it: a floor of white metal under an even sky is the sky; where a red ball stands on it, the ball's image is red"_test = [] {
        constexpr int width = 160, height = 90;
        const mesh::Options even_sky{.background = {0.5f, 0.6f, 0.7f}};
        scene::Scene world;
        put(world, 1, floor_mesh, mirror, math::translation({0, -1, 0}));
        mesh::Targets bare, seen, again;
        bare.resize(width, height), seen.resize(width, height), again.resize(width, height);
        (void)mesh::traced(world, yard_camera.view(), library().assets(true), bare, even_sky, {.samples = 32, .bounces = 2});
        put(world, 11, ball, 0, math::translation({0.4f, 0, 0.8f}));
        (void)mesh::traced(world, yard_camera.view(), library().assets(true), seen, even_sky, {.samples = 32, .bounces = 2});
        (void)mesh::traced(world, yard_camera.view(), library().assets(true), again, even_sky, {.samples = 32, .bounces = 2});
        // The ball's image in the floor is where the ball would be seen were it as far under the floor as it is over it.
        const scene::View view = yard_camera.view();
        const auto pixel_of = [&](math::Vec3 at) {
            const math::Vec4 clip = (view.projection * view.view) * math::Vec4{at.x, at.y, at.z, 1};
            return static_cast<std::size_t>((0.5f - clip.y / clip.w * 0.5f) * height) * width + static_cast<std::size_t>((clip.x / clip.w * 0.5f + 0.5f) * width);
        };
        const math::Vec3 open = bare.light[pixel_of({3.5f, -1, 3})], image = seen.light[pixel_of({0.4f, -2, 0.8f})], itself = seen.light[pixel_of({0.4f, 0, 0.8f})];
        const auto file = keep(seen, "mirror-traced");
        std::println("a floor of white metal under a sky of (0.5, 0.6, 0.7): ({:.3f}, {:.3f}, {:.3f}); the red ball ({:.3f}, {:.3f}, {:.3f}), its image in the floor ({:.3f}, {:.3f}, {:.3f}); {}", open.x, open.y, open.z,
                     itself.x, itself.y, itself.z, image.x, image.y, image.z, file.generic_string());
        expect(lt(std::abs(open.x / 0.5f - 1), 0.05f) && lt(std::abs(open.y / 0.6f - 1), 0.05f) && lt(std::abs(open.z / 0.7f - 1), 0.05f)) << "all of the sky, in its colour";
        expect(gt(image.x, image.y * 2) && gt(image.x, image.z * 2)) << "the image is the ball's red";
        expect(lt(std::abs(image.x / itself.x - 1), 0.5f)) << "and about as bright as the ball";
        expect(seen.light == again.light);
    };
    "the pass's light from everywhere beside what rays find: with the sky's colour for it the yard is near the traced yard on the open floor, and too light where things meet the floor — the measure of what a constant leaves out"_test = [] {
        constexpr int width = 160, height = 90;
        const math::Vec3 sky{0.5f, 0.6f, 0.7f};
        const scene::Scene world = yard();
        mesh::Targets followed;
        followed.resize(width, height);
        (void)mesh::traced(world, yard_camera.view(), library().assets(true), followed, {.background = sky}, {.samples = 64, .bounces = 2});
        const Frame constant = drawn(world, yard_camera, width, height, {.background = sky, .ambient = sky}, true), none = drawn(world, yard_camera, width, height, {.background = sky, .ambient = {0, 0, 0}}, true);
        // Over what is drawn: how far each is from the traced light, a pixel; and over the floor about the ball's foot.
        const auto apart = [&](const mesh::Targets& one, bool by_the_ball) {
            double sum = 0, over = 0;
            std::size_t count = 0;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
                    if (one.ids[pixel].draw == mesh::none) continue;
                    if (by_the_ball && (one.draws[one.ids[pixel].draw].id != 1 || std::abs(x - 84) > 14 || std::abs(y - 60) > 4)) continue;
                    const double difference = static_cast<double>(one.light[pixel].y) - followed.light[pixel].y;
                    sum += std::abs(difference), over += difference, ++count;
                }
            return std::pair(count ? sum / static_cast<double>(count) : 0.0, count ? over / static_cast<double>(count) : 0.0);
        };
        const auto [with_sky, with_sky_over] = apart(constant.targets, false);
        const auto [with_none, with_none_over] = apart(none.targets, false);
        const auto [by_ball, by_ball_over] = apart(constant.targets, true);
        std::println("the yard's green light, a pixel, against 64 rays a pixel: a constant of the sky's colour is {:.3f} off ({:+.3f} on the whole); none at all {:.3f} off ({:+.3f}); about the ball's foot the constant is {:+.3f} too light",
                     with_sky, with_sky_over, with_none, with_none_over, by_ball_over);
        expect(lt(with_sky * 2, with_none)) << "a constant of the sky's colour is much nearer than none";
        expect(gt(by_ball_over, 0.03)) << "and too light where the ball is over the floor: what it leaves out";
        expect(gt(by_ball_over, with_sky_over * 2)) << "there more than anywhere";
        // With rays for it: the light from everywhere only as far as the sky is open to the surface.
        const Frame open_only = drawn(world, yard_camera, width, height, {.background = sky, .ambient = sky, .occlusion_rays = 32, .occlusion_reach = 1000}, true);
        const auto [with_rays, with_rays_over] = apart(open_only.targets, false);
        const auto [by_ball_rays, by_ball_rays_over] = apart(open_only.targets, true);
        std::println("    with 32 rays a sample for it: {:.3f} off ({:+.3f} on the whole); about the ball's foot {:+.3f}; {} rays, {} met something", with_rays, with_rays_over, by_ball_rays_over,
                     open_only.stats.occlusion_rays, open_only.stats.occluded);
        expect(lt(std::abs(by_ball_rays_over), by_ball_over * 0.35)) << "the ball's foot is no longer too light";
        // On the whole the yard is a little darker than the traced one (−0.012), where the constant was a little
        // lighter (+0.007): a way that is shut is taken to send back the surface's own light from the sky, and the
        // traced yard's shut ways are things the sun is on. What the sample cannot know is stated, not hidden.
        expect(lt(with_rays, 0.025f) && lt(std::abs(with_rays_over), 0.02f)) << "and the whole yard stays near: " << with_rays << " " << with_rays_over;
        expect(eq(constant.stats.occlusion_rays, std::uint64_t{0}) && eq(open_only.stats.rays, constant.stats.rays)) << "the lights' rays are counted as before";
        const Frame again = drawn(world, yard_camera, width, height, {.background = sky, .ambient = sky, .occlusion_rays = 32, .occlusion_reach = 1000}, true);
        expect(eq(bits(again.targets), bits(open_only.targets))) << "a pixel's rays are its own: the same bits twice";
        (void)keep(open_only.targets, "yard-occlusion");
    };
    "tone: cut off at white a bright lamp is flat and white; bent toward white it keeps its shape and its colour; exposure scales first; no tone is the pass's own bytes"_test = [] {
        const mesh::Tone filmic{.filmic = true};
        // The curve: dark stays dark, a middle grey a little lifted, however bright never past white, always rising.
        float before = 0;
        for (const float light : {0.0f, 0.01f, 0.18f, 0.5f, 1.0f, 2.0f, 8.0f, 64.0f}) {
            const float shown = filmic.of({light, light, light}).x;
            expect(ge(shown, before) && le(shown, 1.04f)) << light;
            before = shown;
        }
        expect(eq(filmic.of({0, 0, 0}).x, 0.0f) && lt(std::abs(filmic.of({0.18f, 0.18f, 0.18f}).x - 0.267f), 0.005f) && gt(filmic.of({8, 8, 8}).x, 0.97f));
        expect(eq(mesh::Tone{.exposure = 2}.of({0.25f, 0.5f, 1}).y, 1.0f)) << "exposure alone: twice the light";
        // A lamp of (9, 5, 1.6): cut off, its red and green are both the display's white; bent, they still differ.
        const std::vector<math::Vec3> lamp(16, math::Vec3{9.0f, 5.0f, 1.6f}), dim(16, math::Vec3{0.2f, 0.3f, 0.1f});
        mesh::Glow glow;
        std::vector<std::uint8_t> cut, bent, plain, same;
        (void)glow.show(lamp, 4, 4, {}, cut);
        (void)glow.show(lamp, 4, 4, {}, bent, {.exposure = 0.25f, .filmic = true});
        expect(eq(int(cut[0]), 255) && eq(int(cut[1]), 255) && eq(int(cut[2]), 255)) << "flat white";
        expect(gt(int(bent[0]), int(bent[1]) + 4) && gt(int(bent[1]), int(bent[2]) + 20)) << std::format("{} {} {}", bent[0], bent[1], bent[2]);
        (void)glow.show(dim, 4, 4, {}, plain);
        (void)glow.show(dim, 4, 4, {}, same, {});
        expect(plain == same);
    };
}};
