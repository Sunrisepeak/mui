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
    std::vector<mesh::Material> materials{{{0.8f, 0.2f, 0.15f}, {}}, {{0.5f, 0.5f, 0.5f}, {}}, {{0.15f, 0.4f, 0.8f}, {}}, {{0.9f, 0.75f, 0.2f}, {}}, {{0.1f, 0.1f, 0.1f}, {0.9f, 0.5f, 0.1f}}};
    Library() {
        made.push_back(*geom::clustered(geom::box({1, 1, 1})));
        made.push_back(*geom::clustered(geom::sheet(200, 200, 4, 4)));
        made.push_back(*geom::clustered(geom::sphere(1, 32, 64), 32));
        made.push_back(*geom::clustered(geom::cylinder(0.7f, 2, 48), 32));
        made.push_back(*geom::clustered(geom::sheet(8, 8, 16, 16)));
        for (const geom::ClusteredMesh& one : made) meshes.push_back(&one), grown.push_back(geom::bvh_of(one.mesh));
        for (const geom::Bvh& tree : grown) trees.push_back(&tree);
    }
    // Without trees nothing casts a shadow; with them a light is hidden where something is between.
    [[nodiscard]] mesh::Assets assets(bool shadows = false) const { return {meshes, materials, shadows ? std::span<const geom::Bvh* const>(trees) : std::span<const geom::Bvh* const>()}; }
    [[nodiscard]] geom::Box3 bounds(std::uint32_t which) const { return geom::bounds(made[which].mesh); }
};
constexpr std::uint32_t box = 0, floor_mesh = 1, ball = 2, post = 3, sheet = 4;
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
            const math::Transform where{{unit() - 0.5f, unit() - 0.5f, unit() - 0.5f}, math::Quat::about(math::normalized(math::Vec3{unit() - 0.5f, unit() + 0.2f, unit() - 0.5f}), unit() * 6.28f), {1, 1, 1}};
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
}};
