module omui.showcase.quay.picture;

import std;
import mlibs.math;
import mlibs.geom;

namespace quay::picture {

namespace {

namespace math = mlibs::math;
namespace geom = mlibs::geom;
namespace det = mlibs::math::det;
namespace skeleton = hanim::skeleton;
using math::Quat;

constexpr double pi = 3.14159265358979323846;
// The meshes, by number.
constexpr std::uint32_t box_mesh = 0, ball_mesh = 1, post_mesh = 2, sea_mesh = 3, gun_mesh = 4, first_body = 5;
// The materials, by number.
enum Material : std::uint32_t {
    wet_quay, concrete, rust_red, steel_blue, sea_green, ochre, dull_grey, wood, iron, warden_rust, warm_globe, cold_globe, eye_patrol, eye_search, eye_chase, beacon_glass, gun_metal,
    flash, stone, sea, materials_count
};
constexpr std::array<Material, 5> paints{rust_red, steel_blue, sea_green, ochre, dull_grey};
// The warden's joints.
enum Joint : std::uint16_t { hips, spine, head, leg_left, leg_right, arm_left, arm_right };

// Lamps on posts about the quay (the picture's: the game has no lamps), warm or cold, and the floodlight.
struct Lamp {
    float x, z;
    bool  warm;
};
constexpr std::array<Lamp, 5> lamps{{{-24, 12.4f, true}, {-8, -12.4f, false}, {8, 12.4f, true}, {24, -12.4f, true}, {31, 2.8f, false}}};
constexpr float lamp_height = 5;

math::Mat4 placed(math::Vec3 at, math::Vec3 size, Quat turn = {}) { return math::Transform{at, turn, size}.matrix(); }
// An instance as it is now: what it is (`set` keeps where it was) and where (`move`).
void put(scene::Scene& world, const scene::Instance& instance) {
    (void)world.set(instance);
    (void)world.move(instance.id, instance.world);
}
// The turn about the way up that faces −z toward `facing` (the game's: forward is (sin yaw, 0, −cos yaw)).
Quat facing_turn(math::Vec3 facing) {
    const double yaw = det::atan2(static_cast<double>(facing.x), static_cast<double>(-facing.z));
    return Quat::about({0, 1, 0}, static_cast<float>(-yaw));
}

// A warden's body at rest, feet at the origin facing −z: legs, a torso, a head, two arms; each part bent by its
// own joint.
geom::Mesh warden_body() {
    geom::Mesh body;
    const auto part = [&](geom::Mesh shape, const math::Mat4& place, Joint joint) {
        shape.joints.assign(shape.positions.size(), {joint, 0, 0, 0});
        shape.weights.assign(shape.positions.size(), {1, 0, 0, 0});
        (void)geom::append(body, shape, place);
    };
    for (const auto& [side, joint] : {std::pair{-0.17f, leg_left}, std::pair{0.17f, leg_right}}) part(geom::cylinder(0.11f, 0.85f, 16), math::translation({side, 0.43f, 0}), joint);
    part(geom::box({0.3f, 0.33f, 0.2f}), math::translation({0, 1.2f, 0}), spine);
    part(geom::box({0.17f, 0.14f, 0.18f}), math::translation({0, 1.68f, 0}), head);
    part(geom::box({0.07f, 0.3f, 0.07f}), math::translation({-0.4f, 1.18f, 0}), arm_left);
    part(geom::box({0.07f, 0.3f, 0.07f}), math::translation({0.4f, 1.18f, 0}), arm_right);
    // The barrel on the right arm's end.
    part(geom::box({0.04f, 0.04f, 0.16f}), math::translation({0.4f, 0.86f, -0.1f}), arm_right);
    return body;
}

// The watchkeeper's rifle as held, its muzzle at the origin's −z end: a receiver, a barrel, a grip, a sight.
geom::Mesh rifle() {
    geom::Mesh gun;
    (void)geom::append(gun, geom::box({0.024f, 0.034f, 0.1f}), math::translation({0, 0, 0}));
    (void)geom::append(gun, geom::cylinder(0.009f, 0.24f, 12), math::Transform{{0, 0.012f, -0.21f}, Quat::about({1, 0, 0}, static_cast<float>(pi / 2)), {1, 1, 1}}.matrix());
    (void)geom::append(gun, geom::box({0.014f, 0.045f, 0.018f}), math::Transform{{0, -0.065f, 0.05f}, Quat::about({1, 0, 0}, -0.3f), {1, 1, 1}}.matrix());
    (void)geom::append(gun, geom::box({0.006f, 0.012f, 0.02f}), math::translation({0, 0.044f, -0.04f}));
    (void)geom::append(gun, geom::box({0.02f, 0.03f, 0.07f}), math::translation({0, -0.008f, 0.16f}));
    return gun;
}

}  // namespace

std::expected<Picture, std::string> Picture::make(const Level& level) {
    Picture made;
    // Meshes: a box of half 1, a ball of radius 1, a post (radius 1, height 2), the sea, then a body per warden.
    for (geom::Mesh shape : {geom::box({1, 1, 1}), geom::sphere(1, 16, 32), geom::cylinder(1, 2, 24), geom::sheet(600, 600, 4, 4), rifle()}) {
        auto clustered = geom::clustered(shape);
        if (!clustered) return std::unexpected(std::string("a mesh of the picture"));
        made.made_.push_back(std::move(*clustered));
    }
    auto body = geom::clustered(warden_body());
    if (!body) return std::unexpected(std::string("the warden's body"));
    made.body_ = std::move(*body);
    for (std::size_t index = 0; index < level.wardens.size(); ++index) made.made_.push_back(made.body_);
    for (const geom::ClusteredMesh& one : made.made_) made.meshes_.push_back(&one), made.grown_.push_back(geom::bvh_of(one.mesh));
    for (std::size_t index = 0; index < made.grown_.size(); ++index) made.trees_.push_back(index == sea_mesh ? nullptr : &made.grown_[index]);

    // The skeleton: hips at 0.85 m, the spine above, the head, the legs at the hips' sides, the arms at the shoulders.
    auto bones = skeleton::Skeleton::make({{"hips", -1, {.translation = {0, 0.85f, 0}}},
                                           {"spine", hips, {.translation = {0, 0.15f, 0}}},
                                           {"head", spine, {.translation = {0, 0.55f, 0}}},
                                           {"leg-left", hips, {.translation = {-0.17f, 0, 0}}},
                                           {"leg-right", hips, {.translation = {0.17f, 0, 0}}},
                                           {"arm-left", spine, {.translation = {-0.4f, 0.45f, 0}}},
                                           {"arm-right", spine, {.translation = {0.4f, 0.45f, 0}}}});
    if (!bones) return std::unexpected(bones.error().text);
    made.skeleton_ = std::move(*bones);
    made.posed_.assign(made.skeleton_.rest().begin(), made.skeleton_.rest().end());
    made.model_.resize(made.skeleton_.size()), made.skin_.resize(made.skeleton_.size());

    // Materials: base, glow, roughness, metal.
    auto& materials = made.materials_;
    materials.resize(materials_count);
    materials[wet_quay] = {{0.05f, 0.055f, 0.065f}, {}, 0.28f, 0};
    materials[concrete] = {{0.16f, 0.16f, 0.17f}, {}, 0.9f, 0};
    materials[rust_red] = {{0.32f, 0.07f, 0.04f}, {}, 0.55f, 0};
    materials[steel_blue] = {{0.05f, 0.12f, 0.28f}, {}, 0.5f, 0};
    materials[sea_green] = {{0.05f, 0.18f, 0.1f}, {}, 0.55f, 0};
    materials[ochre] = {{0.38f, 0.24f, 0.05f}, {}, 0.6f, 0};
    materials[dull_grey] = {{0.2f, 0.2f, 0.22f}, {}, 0.5f, 0};
    materials[wood] = {{0.3f, 0.18f, 0.08f}, {}, 0.85f, 0};
    materials[iron] = {{0.25f, 0.25f, 0.27f}, {}, 0.45f, 1};
    materials[warden_rust] = {{0.3f, 0.15f, 0.08f}, {}, 0.65f, 0.6f};
    materials[warm_globe] = {{0.1f, 0.1f, 0.1f}, {9.0f, 5.0f, 1.6f}};
    materials[cold_globe] = {{0.1f, 0.1f, 0.1f}, {3.0f, 5.0f, 8.0f}};
    materials[eye_patrol] = {{0.05f, 0.05f, 0.05f}, {0.4f, 2.5f, 2.0f}};
    materials[eye_search] = {{0.05f, 0.05f, 0.05f}, {4.0f, 2.2f, 0.3f}};
    materials[eye_chase] = {{0.05f, 0.05f, 0.05f}, {7.0f, 0.4f, 0.2f}};
    materials[beacon_glass] = {{0.2f, 0.12f, 0.05f}, {0.3f, 0.15f, 0.03f}};
    materials[gun_metal] = {{0.05f, 0.05f, 0.06f}, {}, 0.4f, 1};
    materials[flash] = {{1, 1, 1}, {30.0f, 18.0f, 6.0f}};
    materials[stone] = {{0.12f, 0.12f, 0.13f}, {}, 0.95f, 0};
    materials[sea] = {{0.01f, 0.014f, 0.02f}, {}, 0.08f, 0};

    // The fixed boxes: the quay's slab wet, the breakwater stone, low walls and the warehouse concrete, the
    // containers painted (by their place: the same each run); crates are the bodies' and placed each frame.
    auto& world = made.world_;
    const auto add = [&](scene::Id id, std::uint32_t which, std::uint32_t material, const math::Mat4& where, bool casts = true) {
        return world.add({.id = id, .mesh = which, .material = material, .world = where, .bounds = geom::bounds(made.made_[which].mesh), .casts = casts}).has_value();
    };
    bool fine = add(sea_id, sea_mesh, sea, math::translation({0, -1.2f, 0}), false);
    std::uint32_t box_number = 0, crate_number = 0;
    for (const Box& box : level.boxes) {
        if (box.crate) {
            made.crate_halves_.push_back(box.half);
            fine = fine && add(crate_ids + crate_number++, box_mesh, wood, placed(box.at, box.half));
            continue;
        }
        Material material = concrete;
        if (box_number == 0) material = wet_quay;
        else if (box_number == 1) material = stone;
        else if (box.half.y > 1.2f && box.half.y < 1.4f) material = paints[static_cast<std::size_t>(std::abs(static_cast<int>(box.at.x * 3 + box.at.z * 7 + box.at.y))) % paints.size()];
        fine = fine && add(box_ids + box_number++, box_mesh, material, placed(box.at, box.half));
    }
    // Lamps: an iron post, a globe that does not stand in its own light's way.
    for (std::size_t index = 0; index < lamps.size(); ++index) {
        const Lamp lamp = lamps[index];
        fine = fine && add(lamp_ids + index * 2, post_mesh, iron, placed({lamp.x, lamp_height / 2, lamp.z}, {0.09f, lamp_height / 2, 0.09f}));
        fine = fine && add(lamp_ids + index * 2 + 1, ball_mesh, lamp.warm ? warm_globe : cold_globe, placed({lamp.x, lamp_height, lamp.z}, {0.22f, 0.22f, 0.22f}), false);
        fine = fine && world.add(scene::Light{.id = 10 + index, .kind = scene::LightKind::point, .colour = lamp.warm ? math::Vec3{1, 0.62f, 0.25f} : math::Vec3{0.55f, 0.75f, 1},
                                              .intensity = 3.2f, .at = {lamp.x, lamp_height, lamp.z}, .range = 16})
                                 .has_value();
    }
    // The beacon: a post and its glass at the end of the breakwater.
    fine = fine && add(beacon_ids, post_mesh, iron, placed(level.beacon + math::Vec3{0, (beacon_height - 0.5f) / 2, 0}, {0.15f, (beacon_height - 0.5f) / 2, 0.15f}));
    fine = fine && add(beacon_ids + 1, ball_mesh, beacon_glass, placed(level.beacon + math::Vec3{0, beacon_height, 0}, {0.5f, 0.5f, 0.5f}), false);
    // The floodlight on the warehouse, the moon.
    fine = fine && world.add(scene::Light{.id = 1, .kind = scene::LightKind::spot, .colour = {1, 0.97f, 0.9f}, .intensity = 1.0f, .at = {-31.5f, 12, 0},
                                          .toward = math::normalized(math::Vec3{1, -0.4f, 0}), .range = 50, .cone = 0.95f, .inner_cone = 0.1f})
                             .has_value();
    fine = fine && world.add(scene::Light{.id = 2, .kind = scene::LightKind::sun, .colour = {0.6f, 0.7f, 1}, .intensity = 0.05f, .toward = {0.3f, -1, 0.4f}}).has_value();
    // The wardens, their eyes; the gun and its flash (placed each frame).
    for (std::size_t index = 0; index < level.wardens.size(); ++index) {
        fine = fine && add(warden_ids + index + 1, first_body + static_cast<std::uint32_t>(index), warden_rust, math::translation(level.wardens[index].from));
        fine = fine && add(eye_ids + index + 1, ball_mesh, eye_patrol, placed(level.wardens[index].from, {0.05f, 0.05f, 0.05f}), false);
    }
    fine = fine && add(gun_id, gun_mesh, gun_metal, math::Mat4{}, false);
    fine = fine && add(flash_id, ball_mesh, flash, math::Mat4{}, false);
    if (!fine) return std::unexpected(std::string("the scene refused an instance or a light"));

    made.options_ = {.background = {0.004f, 0.005f, 0.01f}, .ambient = {0.006f, 0.007f, 0.012f}, .fog = {.colour = {0.02f, 0.026f, 0.04f}, .density = 0.03f, .base = 0, .falloff = 0.3f},
                     .environment = {.on = true, .above = {0.01f, 0.014f, 0.03f}, .level = {0.035f, 0.04f, 0.06f}, .below = {0.004f, 0.004f, 0.005f}}};
    return made;
}

mesh::Assets Picture::assets(bool shadows) const noexcept {
    return {meshes_, materials_, shadows ? std::span<const geom::Bvh* const>(trees_) : std::span<const geom::Bvh* const>()};
}

// The wardens bent by their skeletons: walking (legs and arms swinging, the hips bobbing), standing to look about,
// aiming (the gun arm raised), or lying where they fell.
void Picture::pose(const State& state) {
    for (std::size_t index = 0; index < state.wardens.size(); ++index) {
        const Warden& warden = state.wardens[index];
        std::ranges::copy(skeleton_.rest(), posed_.begin());
        const bool down = warden.health <= 0;
        const bool still = warden.mind == Mind::search && math::length(math::Vec3{warden.heading_for.x - warden.at.x, 0, warden.heading_for.z - warden.at.z}) < 1.5f;
        const double phase = static_cast<double>(state.frame) * 2 * pi * 1.6 / steps_per_second + static_cast<double>(index) * 1.7;
        const float swing = down || still ? 0.0f : 0.5f, step = static_cast<float>(det::sin(phase));
        posed_[leg_left].rotation = Quat::about({1, 0, 0}, swing * step);
        posed_[leg_right].rotation = Quat::about({1, 0, 0}, -swing * step);
        posed_[arm_left].rotation = Quat::about({1, 0, 0}, -0.6f * swing * step);
        posed_[arm_right].rotation = warden.mind == Mind::chase ? Quat::about({1, 0, 0}, 1.45f) : Quat::about({1, 0, 0}, 0.6f * swing * step);
        posed_[hips].translation.y += 0.03f * swing * static_cast<float>(std::abs(det::cos(phase)));
        if (still) posed_[head].rotation = Quat::about({0, 1, 0}, static_cast<float>(det::sin(static_cast<double>(state.frame) * 0.05)) * 0.6f);
        if (down) posed_[hips].translation.y = 0.2f, posed_[hips].rotation = Quat::about({1, 0, 0}, static_cast<float>(pi / 2));
        skeleton::model(skeleton_, posed_, model_);
        skeleton::skin(skeleton_, model_, skin_);
        geom::ClusteredMesh& bent = made_[first_body + index];
        mesh::bend(body_, skin_, bent, room_);
        grown_[first_body + index] = geom::bvh_of(bent.mesh);
        const math::Mat4 where = placed(warden.at, {1, 1, 1}, facing_turn(warden.facing));
        put(world_, {.id = warden_ids + index + 1, .mesh = first_body + static_cast<std::uint32_t>(index), .material = warden_rust, .world = where, .bounds = geom::bounds(bent.mesh)});
        // Its eye: on the head's front, the colour of its mind; gone when it is down.
        const math::Vec3 eye_at = math::point(where * model_[head], {0, 0.05f, -0.19f});
        const Material eye = warden.mind == Mind::chase ? eye_chase : warden.mind == Mind::search ? eye_search : eye_patrol;
        put(world_, {.id = eye_ids + index + 1, .mesh = ball_mesh, .material = eye, .world = placed(eye_at, {0.05f, 0.05f, 0.05f}), .bounds = geom::bounds(made_[ball_mesh].mesh),
                          .hidden = down, .casts = false});
    }
    for (std::size_t index = 0; index < state.crates.size() && index < crate_halves_.size(); ++index)
        if (const auto body = state.bodies->pose(state.crates[index]))
            put(world_, {.id = crate_ids + index, .mesh = box_mesh, .material = wood, .world = placed(body->at, crate_halves_[index], body->turn), .bounds = geom::bounds(made_[box_mesh].mesh)});
}

// The beacon's light as it is worked (and lit), the flashes of this step's shots.
void Picture::lights(const State& state) {
    const float worked = std::min(1.0f, static_cast<float>(state.keeper.lighting) / steps_per_second), lit = state.outcome == Outcome::won ? 1.0f : worked;
    materials_[beacon_glass].emissive = math::Vec3{0.3f, 0.15f, 0.03f} + math::Vec3{24, 12, 3} * lit;
    const math::Vec3 lamp = state.beacon + math::Vec3{0, beacon_height, 0};
    (void)world_.remove_light(3);
    (void)world_.add(scene::Light{.id = 3, .kind = scene::LightKind::point, .colour = {1, 0.55f, 0.2f}, .intensity = 0.6f + 5 * lit, .at = lamp, .range = 30});
    for (const scene::Id id : flashes_) (void)world_.remove_light(id);
    flashes_.clear();
    for (const Shot& shot : state.shots) {
        const scene::Id id = 20 + flashes_.size();
        const math::Vec3 at = shot.by == 0 ? shot.from + looking(state) * 0.6f + math::Vec3{0, -0.1f, 0} : shot.from;
        if (world_.add(scene::Light{.id = id, .kind = scene::LightKind::point, .colour = {1, 0.75f, 0.4f}, .intensity = 4, .at = at, .range = 9})) flashes_.push_back(id);
    }
}

const mesh::Targets& Picture::draw(const State& state, const Frame& frame) {
    pose(state);
    lights(state);
    // The view from the watchkeeper's eye, 100° across.
    const math::Vec3 eye = quay::eye(state), look = looking(state);
    const double half_across = static_cast<double>(view_half_angle) * pi / 180;
    const auto vertical = static_cast<float>(2 * det::atan(det::sin(half_across) / det::cos(half_across) / static_cast<double>(view_aspect)));
    view_ = {.view = math::look_at(eye, eye + look, {0, 1, 0}), .projection = math::perspective(vertical, view_aspect, 0.05f, 160.0f)};
    // The gun, held low and right in the view; its flash at the muzzle on a shot of the watchkeeper's.
    const auto from_eye = math::inverse(view_.view).value_or(math::Mat4{});
    put(world_, {.id = gun_id, .mesh = gun_mesh, .material = gun_metal, .world = from_eye * placed({0.075f, -0.07f, -0.2f}, {0.5f, 0.5f, 0.5f}, Quat::about({0, 1, 0}, 0.05f)), .bounds = geom::bounds(made_[gun_mesh].mesh), .casts = false});
    const bool fired = std::ranges::any_of(state.shots, [](const Shot& shot) { return shot.by == 0; });
    put(world_, {.id = flash_id, .mesh = ball_mesh, .material = flash, .world = from_eye * placed({0.075f, -0.064f, -0.4f}, {0.025f, 0.025f, 0.035f}), .bounds = geom::bounds(made_[ball_mesh].mesh),
                      .hidden = !fired, .casts = false});

    seen_ = {};
    scene::extract(world_, view_, seen_);
    targets_.resize(frame.width, frame.height);
    mesh::Options options = options_;
    options.shadows = frame.shadows;
    pass_.draw(world_, seen_, view_, assets(frame.shadows), targets_, options);
    world_.settle();
    shown_ = targets_.colour;
    bright_ = frame.glow ? glow_.show(targets_.light, frame.width, frame.height, {.threshold = 1, .radius = 7.0f * static_cast<float>(frame.width) / 480, .strength = 0.6f}, shown_).bright : 0;
    return targets_;
}

scene::Id Picture::instance_at(std::size_t pixel) const noexcept {
    if (pixel >= targets_.ids.size()) return 0;
    const mesh::Pixel who = targets_.ids[pixel];
    return who.draw == mesh::none || who.draw >= targets_.draws.size() ? 0 : targets_.draws[who.draw].id;
}
std::uint32_t Picture::warden_at(std::size_t pixel) const noexcept {
    const scene::Id id = instance_at(pixel);
    return id > warden_ids && id <= warden_ids + 64 ? static_cast<std::uint32_t>(id - warden_ids) : 0;
}

}  // namespace quay::picture
