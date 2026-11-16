module omui.showcase.quay.sim;

import std;
import mlibs.math;
import mlibs.hash;

namespace quay {

namespace {

namespace math = mlibs::math;
namespace geom = mlibs::geom;
namespace space = xengine::audio::space;
namespace det = mlibs::math::det;

constexpr float turn_speed = 2.6f, look_speed = 1.8f;   // radians a second at a full axis
constexpr float walk_speed = 3.5f, run_speed = 6.0f;    // m/s
constexpr float jump_speed = 4.5f;
constexpr std::uint32_t keeper_cooldown = 9;             // steps between shots (held: 6.7 a second)
constexpr float reach_of_a_shot = 80;
constexpr float warden_radius = 0.45f, warden_height = 1.8f, warden_eye_height = 1.6f;
constexpr float sight = 28, sight_angle = 1.13f;         // m; radians from its facing (65°)
constexpr float hearing = -58;                           // dB against full: the faintest a warden hears
constexpr std::uint32_t warden_cooldown = 70;            // steps between a warden's shots
constexpr std::int32_t warden_damage = 12;
constexpr std::uint32_t search_steps = 6 * steps_per_second;
constexpr std::uint32_t beacon_steps = steps_per_second;  // the beacon worked a second
constexpr float beacon_reach = 2.2f;
constexpr float fuse_reach = 1.6f;
constexpr float sweep_speed = 0.35f;                     // radians a second, a lookout's beam
constexpr float beam_angle = 0.2f;                       // radians either side of the beam's bearing it lights
constexpr float beam_falls = 14;                         // metres out from the lamp the beam's middle meets the quay
constexpr std::uint32_t horn_steps = 90;                 // steps between a lookout's calls while it sees
constexpr float lamp_radius = 0.4f;                      // a lookout's lamp, as a shot meets it

Vec3 flat(Vec3 value) noexcept { return {value.x, 0, value.z}; }
float horizontal(Vec3 one, Vec3 other) noexcept { return math::length(flat(one - other)); }

// A unit number of (frame, who): the same every run.
float unit_of(std::uint64_t frame, std::uint64_t who) noexcept { return static_cast<float>(math::unit24(math::mix64(frame * 0x9e3779b97f4a7c15ull ^ (who + 1)))); }

// Where a ray from `from` along `way` (a unit) first meets a standing warden's body (a cylinder), within `most`.
std::optional<float> meets(const Warden& warden, Vec3 from, Vec3 way, float most) noexcept {
    const float base = warden.at.y, top = warden.at.y + warden_height;
    const float ox = from.x - warden.at.x, oz = from.z - warden.at.z;
    const float a = way.x * way.x + way.z * way.z, b = 2 * (ox * way.x + oz * way.z), c = ox * ox + oz * oz - warden_radius * warden_radius;
    std::optional<float> best;
    if (a > 1e-8f) {
        const float inside = b * b - 4 * a * c;
        if (inside >= 0) {
            const float root = std::sqrt(inside);
            for (const float t : {(-b - root) / (2 * a), (-b + root) / (2 * a)}) {
                const float y = from.y + way.y * t;
                if (t >= 0 && t <= most && y >= base && y <= top && (!best || t < *best)) best = t;
            }
        }
    }
    // Its top, from above or below.
    if (std::abs(way.y) > 1e-8f)
        for (const float cap : {top, base}) {
            const float t = (cap - from.y) / way.y;
            const float x = ox + way.x * t, z = oz + way.z * t;
            if (t >= 0 && t <= most && x * x + z * z <= warden_radius * warden_radius && (!best || t < *best)) best = t;
        }
    return best;
}

// Where a ray from `from` along `way` meets a ball, within `most`.
std::optional<float> meets_ball(Vec3 centre, float radius, Vec3 from, Vec3 way, float most) noexcept {
    const Vec3 to = from - centre;
    const float b = math::dot(to, way), c = math::dot(to, to) - radius * radius, inside = b * b - c;
    if (inside < 0) return std::nullopt;
    const float t = -b - std::sqrt(inside);
    return t >= 0 && t <= most ? std::optional<float>(t) : std::nullopt;
}

Vec3 bearing_of(float bearing) noexcept {
    return {static_cast<float>(det::sin(static_cast<double>(bearing))), 0, static_cast<float>(-det::cos(static_cast<double>(bearing)))};
}
float bearing_toward(Vec3 from, Vec3 to) noexcept {
    return static_cast<float>(det::atan2(static_cast<double>(to.x - from.x), static_cast<double>(-(to.z - from.z))));
}
float apart_by(float from, float to) noexcept {
    float apart = to - from;
    while (apart > 3.14159265f) apart -= 6.2831853f;
    while (apart < -3.14159265f) apart += 6.2831853f;
    return apart;
}

}  // namespace

namespace level {
Level harbour() {
    Level made{.name = "harbour", .start = {-28, 0, 0}, .facing = 1.5707964f, .beacon = {49, 0, 0}, .tide = 240, .rounds = 60};
    auto& boxes = made.boxes;
    // The quay and the breakwater, their edges, the warehouse wall behind.
    boxes.push_back({{0, -0.5f, 0}, {32, 0.5f, 13}});
    boxes.push_back({{41, -0.5f, 0}, {9, 0.5f, 2.2f}});
    for (const float side : {13.2f, -13.2f}) boxes.push_back({{0, 0.55f, side}, {32, 0.55f, 0.2f}});
    boxes.push_back({{-32.2f, 1.5f, 0}, {0.2f, 1.5f, 13.4f}});
    for (const float side : {7.6f, -7.6f}) boxes.push_back({{32.2f, 0.55f, side}, {0.2f, 0.55f, 5.4f}});
    for (const float side : {2.35f, -2.35f}) boxes.push_back({{41, 0.55f, side}, {9, 0.55f, 0.15f}});
    boxes.push_back({{50.2f, 0.55f, 0}, {0.2f, 0.55f, 2.5f}});
    // The containers: along z (1.2 × 3 half) or along x (3 × 1.2), 2.6 m high; one stacked.
    const Vec3 along_z{1.2f, 1.3f, 3}, along_x{3, 1.3f, 1.2f};
    for (const auto& [x, z, half] : std::array<std::tuple<float, float, Vec3>, 12>{{{-20, -6, along_z}, {-20, 6, along_z}, {-11, 0, along_x}, {-4, -8, along_x}, {-4, 8, along_x},
                                                                                 {3, -2, along_z}, {3, 9, along_x}, {12, 4, along_z}, {12, -7, along_z}, {20, 0, along_x}, {24, 8, along_x},
                                                                                 {24, -8, along_x}}})
        boxes.push_back({{x, 1.3f, z}, half});
    boxes.push_back({{20, 3.9f, 0}, along_x});
    // Crates between them.
    for (const auto& [x, z] : std::array<std::pair<float, float>, 5>{{{-15, 3}, {-7, -3}, {7, 5}, {16, -3}, {27, 3}}}) boxes.push_back({{x, 0.5f, z}, {0.5f, 0.5f, 0.5f}, true});
    made.wardens = {{{-15, 0, -10}, {-15, 0, 10}}, {{0, 0, -11}, {0, 0, 11}}, {{16, 0, -11}, {16, 0, 11}}, {{34, 0, 0}, {47, 0, 0}}};
    return made;
}
Level flawed() {
    Level made = harbour();
    made.name = "flawed";
    made.boxes.push_back({{40, 1.3f, 0}, {1.2f, 1.3f, 2.3f}});
    return made;
}
Level second_night() {
    Level made = harbour();
    made.name = "second-night";
    made.tide = 300;
    // On masts 2 m over the stacked container (sweeping the quay's middle) and over the container by the warehouse
    // (its west end): high enough to see over the containers' tops.
    made.lookouts = {{{20, 7.2f, 0}, -2.4f, -0.8f}, {{-4, 4.6f, 8}, -2.9f, -1.6f}};
    made.wardens.push_back({{-26, 0, -11}, {-26, 0, 11}});
    made.fuses = {{-30.5f, 0, -11}, {27, 0, 11.5f}};
    return made;
}
std::optional<Level> named(std::string_view name) {
    if (name == "harbour") return harbour();
    if (name == "flawed") return flawed();
    if (name == "second-night") return second_night();
    return std::nullopt;
}
}  // namespace level

State start(const Level& level) {
    State state;
    auto statics = std::make_shared<Statics>();
    // docs: bodies
    auto bodies = rigid3::World::make();
    if (!bodies) return state;
    for (const Box& box : level.boxes) {
        if (box.crate) {
            const auto crate = bodies->add({.kind = rigid3::Kind::moving, .at = box.at}, rigid3::Shape::box(box.half), {.density = 300, .friction = 0.7f});
            if (crate) state.crates.push_back(*crate);
            continue;
        }
        (void)bodies->add({.kind = rigid3::Kind::fixed, .at = box.at}, rigid3::Shape::box(box.half));
        (void)geom::append(statics->mesh, geom::box(box.half), math::translation(box.at));
    }
    statics->tree = geom::bvh_of(statics->mesh);
    // Steel lets a tenth of a sound by, and dulls it above 800 Hz.
    statics->walls.push_back({.mesh = &statics->mesh, .tree = &statics->tree, .place = math::Mat4{}, .through = 0.15f, .bright = 800});
    const auto keeper = bodies->add(rigid3::CharacterDef{.at = level.start});
    // docs: end
    if (!keeper) return state;
    // docs: crowd
    auto ground = nav::World::build({.points = statics->mesh.positions, .corners = statics->mesh.indices}, {.radius = warden_radius, .height = warden_height});
    if (!ground) return state;
    for (const Patrol& round : level.wardens) {
        const auto walker = ground->add({.at = round.from, .radius = warden_radius, .height = warden_height, .speed = 2.6f});
        if (!walker) continue;
        (void)ground->target(*walker, round.to);
        state.wardens.push_back({.walker = *walker, .round = round, .at = round.from});
    }
    // docs: end
    state.keeper = {.body = *keeper, .at = level.start, .yaw = level.facing, .rounds = level.rounds};
    // docs: branching
    state.bodies = mlibs::simulation::Branching<rigid3::World>(std::move(*bodies));
    state.ground = mlibs::simulation::Branching<nav::World>(std::move(*ground));
    // docs: end
    state.statics = std::move(statics);
    state.beacon = level.beacon;
    state.tide = level.tide;
    state.level = level.name;
    for (const Lookout& post : level.lookouts) {
        Searchlight light{.post = post, .bearing = post.from};
        light.falls = flat(post.at) + bearing_of(light.bearing) * beam_falls;
        state.lookouts.push_back(light);
    }
    state.fuses = level.fuses;
    state.fused.assign(level.fuses.size(), 0);
    return state;
}

Vec3 eye(const State& state) noexcept { return state.keeper.at + Vec3{0, eye_height, 0}; }
Vec3 looking(const State& state) noexcept {
    const auto yaw = static_cast<double>(state.keeper.yaw), pitch = static_cast<double>(state.keeper.pitch);
    return {static_cast<float>(det::sin(yaw) * det::cos(pitch)), static_cast<float>(det::sin(pitch)), static_cast<float>(-det::cos(yaw) * det::cos(pitch))};
}
std::size_t fuses_off(const State& state) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(state.fused, [](std::uint32_t worked) { return worked < fuse_steps; }));
}
std::size_t standing(const State& state) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(state.wardens, [](const Warden& warden) { return warden.health > 0; }));
}
bool clear(const State& state, Vec3 from, Vec3 to) { return !state.bodies || !state.bodies->ray(from, to); }

void step(State& state, const Input& input) {
    state.noises.clear(), state.shots.clear();
    if (state.outcome != Outcome::playing || !state.bodies || !state.ground) {
        ++state.frame;
        return;
    }
    const xengine::Actions& actions = input.player;
    Watchkeeper& keeper = state.keeper;
    const auto character = state.bodies->character(keeper.body);
    const bool on_ground = character && character->ground == rigid3::Ground::on_ground;

    // ── the watchkeeper: turn, look, walk, jump ──
    keeper.yaw += static_cast<float>(actions.axis(2)) * turn_speed * step_seconds;
    keeper.pitch = std::clamp(keeper.pitch + static_cast<float>(actions.axis(3)) * look_speed * step_seconds, -1.35f, 1.35f);
    const auto yaw = static_cast<double>(keeper.yaw);
    const Vec3 forward{static_cast<float>(det::sin(yaw)), 0, static_cast<float>(-det::cos(yaw))}, right{static_cast<float>(det::cos(yaw)), 0, static_cast<float>(det::sin(yaw))};
    const bool running = actions.down(run_button);
    Vec3 walk = forward * static_cast<float>(actions.axis(1)) + right * static_cast<float>(actions.axis(0));
    const float pushed = math::length(walk);
    if (pushed > 1) walk = walk * (1 / pushed);
    walk = walk * (running ? run_speed : walk_speed);
    state.bodies->walk(keeper.body, walk);
    if (actions.went_down(jump_button) && on_ground) state.bodies->jump(keeper.body, jump_speed);
    // Footfalls: a running one is heard further.
    if (on_ground && math::length(walk) > 0.5f) {
        if (++keeper.stride >= (running ? 20u : 30u)) {
            keeper.stride = 0;
            state.noises.push_back({keeper.at, running ? -28.0f : -42.0f, 0, "footstep"});
        }
    } else {
        keeper.stride = 0;
    }

    // ── a shot ──
    if (keeper.cooldown > 0) --keeper.cooldown;
    if (actions.down(fire_button) && keeper.cooldown == 0 && keeper.rounds > 0) {
        keeper.cooldown = keeper_cooldown, --keeper.rounds;
        const Vec3 from = eye(state), way = looking(state);
        const Vec3 to = from + way * reach_of_a_shot;
        const auto wall = state.bodies->ray(from, to);
        float nearest = wall ? wall->fraction * reach_of_a_shot : reach_of_a_shot;
        std::uint32_t struck = 0;
        for (std::uint32_t index = 0; index < state.wardens.size(); ++index) {
            const Warden& warden = state.wardens[index];
            if (warden.health <= 0) continue;
            if (const auto met = meets(warden, from, way, nearest)) nearest = *met, struck = index + 1;
        }
        // A lookout's lamp nearer than all of that: put out a shot at a time.
        std::uint32_t lamp = 0;
        for (std::uint32_t index = 0; index < state.lookouts.size(); ++index)
            if (state.lookouts[index].health > 0)
                if (const auto met = meets_ball(state.lookouts[index].post.at, lamp_radius, from, way, nearest)) nearest = *met, lamp = index + 1, struck = 0;
        if (lamp) {
            --state.lookouts[lamp - 1].health;
        } else if (struck) {
            Warden& warden = state.wardens[struck - 1];
            if (--warden.health <= 0) state.ground->remove(warden.walker);
        } else if (wall && std::ranges::contains(state.crates, wall->body)) {
            state.bodies->push(wall->body, way * 30.0f, wall->point);
        } else if (wall) {
            if (state.marks.size() == kept_marks) state.marks.erase(state.marks.begin());
            state.marks.push_back({wall->point, wall->normal, state.frame});
        }
        state.shots.push_back({from, from + way * nearest, 0, struck, false});
        state.noises.push_back({from, 0, 0, "shot"});
    }
    state.bodies->step();
    if (const auto moved = state.bodies->character(keeper.body)) keeper.at = moved->at;
    ++keeper.hurt;

    const Vec3 head = eye(state);
    // ── the lookouts: each beam sweeps, or holds on the watchkeeper and calls ──
    for (std::uint32_t index = 0; index < state.lookouts.size(); ++index) {
        Searchlight& light = state.lookouts[index];
        if (light.health <= 0) {
            light.sees = false;
            continue;
        }
        const float toward = bearing_toward(light.post.at, keeper.at);
        light.sees = horizontal(light.post.at, keeper.at) < beam_reach && std::abs(apart_by(light.bearing, toward)) < beam_angle && clear(state, light.post.at, head);
        if (light.horn > 0) --light.horn;
        if (light.sees) {
            light.bearing += std::clamp(apart_by(light.bearing, toward), -sweep_speed * 3 * step_seconds, sweep_speed * 3 * step_seconds);
            light.falls = flat(keeper.at);
            if (light.horn == 0) {
                light.horn = horn_steps;
                state.noises.push_back({light.post.at, 0, 1000 + index, "horn"});
            }
        } else {
            const float goal = light.outward ? light.post.to : light.post.from;
            const float apart = apart_by(light.bearing, goal);
            light.bearing += std::clamp(apart, -sweep_speed * step_seconds, sweep_speed * step_seconds);
            if (std::abs(apart) < 0.01f) light.outward = !light.outward;
            light.falls = flat(light.post.at) + bearing_of(light.bearing) * beam_falls;
        }
    }

    // ── the wardens: what each hears and sees, then where it goes ──
    const bool keeper_quick = math::length(walk) > walk_speed + 0.1f;
    const std::size_t heard_before = state.noises.size();  // the wardens' own shots this step are heard next step
    for (std::uint32_t index = 0; index < state.wardens.size(); ++index) {
        Warden& warden = state.wardens[index];
        if (warden.health <= 0) continue;
        warden.heard = 0;
        if (warden.cooldown > 0) --warden.cooldown;
        const Vec3 warden_eye = warden.at + Vec3{0, warden_eye_height, 0};
        // Sight: near enough, within its angle, nothing between.
        const Vec3 toward = head - warden_eye;
        const float distance = math::length(toward);
        const Vec3 facing = math::normalized(flat(warden.facing));
        warden.sees = distance < sight && math::dot(math::normalized(flat(toward)), facing) >= static_cast<float>(det::cos(sight_angle)) && clear(state, warden_eye, head);
        // Hearing: the loudest of this step's noises it hears, and where it seems to come from.
        std::optional<Vec3> seems;
        float loudest = -1000;
        const space::Listener ears{.at = warden_eye, .forward = facing, .up = {0, 1, 0}};
        for (std::size_t which = 0; which < heard_before; ++which) {
            const Noise& noise = state.noises[which];
            if (noise.by == index + 1) continue;
            const space::Reach reached = space::reach(ears, noise.at, state.statics->walls);
            const float level = noise.loudness + reached.level;
            if (level < hearing) continue;
            ++warden.heard;
            if (level <= loudest) continue;
            loudest = level;
            // The way it came from, in the world, and how far a sound that loud seems — further through walls (a
            // sound through steel can seem hundreds of metres off): it goes that way 25 m at most, and listens again.
            const auto azimuth = static_cast<double>(reached.azimuth) * 3.14159265358979323846 / 180, elevation = static_cast<double>(reached.elevation) * 3.14159265358979323846 / 180;
            const Vec3 side = math::normalized(math::cross(facing, Vec3{0, 1, 0}));
            const Vec3 way = facing * static_cast<float>(det::cos(elevation) * det::cos(azimuth)) + side * static_cast<float>(det::cos(elevation) * det::sin(azimuth)) +
                             Vec3{0, static_cast<float>(det::sin(elevation)), 0};
            const float seems_far = std::min(25.0f, static_cast<float>(det::pow(10.0, static_cast<double>(-reached.level) / 20)));
            seems = flat(warden_eye + way * seems_far);
            // A lookout's horn says where its beam falls: the warden goes there, not to the horn.
            if (noise.by >= 1000 && noise.by - 1000 < state.lookouts.size()) seems = state.lookouts[noise.by - 1000].falls;
        }
        if (warden.sees) {
            warden.mind = Mind::chase, warden.heading_for = keeper.at;
            (void)state.ground->target(warden.walker, keeper.at);
            if (warden.cooldown == 0) {
                warden.cooldown = warden_cooldown;
                const float chance = std::clamp(0.85f - distance / 45, 0.15f, 0.85f) * (keeper_quick ? 0.75f : 1.0f);
                const bool hit = unit_of(state.frame, index) < chance;
                if (hit) keeper.health -= warden_damage, keeper.hurt = 0;
                const Vec3 wide = head + Vec3{unit_of(state.frame, index + 100) - 0.5f, unit_of(state.frame, index + 200) * 0.6f, unit_of(state.frame, index + 300) - 0.5f};
                state.shots.push_back({warden_eye, hit ? head : wide, index + 1, 0, hit});
                state.noises.push_back({warden_eye, 0, index + 1, "warden shot"});
            }
        } else if (seems) {
            warden.mind = Mind::search, warden.heading_for = *seems, warden.searching = search_steps;
            (void)state.ground->target(warden.walker, *seems);
        } else if (warden.mind == Mind::chase) {
            warden.mind = Mind::search, warden.searching = search_steps;  // where it last saw the watchkeeper
            (void)state.ground->target(warden.walker, warden.heading_for);
        } else if (warden.mind == Mind::search) {
            if (horizontal(warden.at, warden.heading_for) < 1.5f || (state.ground->walker(warden.walker) && state.ground->walker(warden.walker)->going == nav::Going::unreachable)) {
                // There: it looks about until it gives up.
                const auto turn = static_cast<double>(step_seconds);
                const Vec3 turned{static_cast<float>(facing.x * det::cos(turn) - facing.z * det::sin(turn)), 0, static_cast<float>(facing.x * det::sin(turn) + facing.z * det::cos(turn))};
                warden.facing = turned;
                if (warden.searching > 0) --warden.searching;
            }
            if (warden.searching == 0) {
                warden.mind = Mind::patrol;
                (void)state.ground->target(warden.walker, warden.outward ? warden.round.to : warden.round.from);
            }
        } else if (horizontal(warden.at, warden.outward ? warden.round.to : warden.round.from) < 1.0f) {
            warden.outward = !warden.outward;
            (void)state.ground->target(warden.walker, warden.outward ? warden.round.to : warden.round.from);
        }
    }
    state.ground->step();
    for (Warden& warden : state.wardens) {
        if (warden.health <= 0) continue;
        const auto walking = state.ground->walker(warden.walker);
        if (!walking) continue;
        warden.at = walking->at;
        if (warden.sees) warden.facing = math::normalized(flat(keeper.at - warden.at));
        else if (math::length(flat(walking->velocity)) > 0.2f) warden.facing = math::normalized(flat(walking->velocity));
    }

    // What was heard lately: this step's noises join, those older than half a second leave.
    for (Noise noise : state.noises) noise.frame = state.frame, state.recent.push_back(noise);
    std::erase_if(state.recent, [&](const Noise& noise) { return noise.frame + recent_steps <= state.frame; });

    // ── the beacon, the tide, the end ──
    for (std::size_t index = 0; index < state.fuses.size(); ++index)
        if (state.fused[index] < fuse_steps) {
            state.fused[index] = horizontal(keeper.at, state.fuses[index]) < fuse_reach && actions.down(use_button) ? state.fused[index] + 1 : 0;
            // Thrown: a clunk and the hum of the line, heard as a step is (next step, by the wardens).
            if (state.fused[index] == fuse_steps) state.noises.push_back({state.fuses[index] + Vec3{0, 1.2f, 0}, -30, 0, "fuse"});
        }
    keeper.lighting = fuses_off(state) == 0 && horizontal(keeper.at, state.beacon) < beacon_reach && actions.down(use_button) ? keeper.lighting + 1 : 0;
    state.tide -= step_seconds;
    if (keeper.lighting >= beacon_steps) state.outcome = Outcome::won, state.why = "the beacon is lit";
    else if (keeper.health <= 0) state.outcome = Outcome::lost, state.why = "the wardens stopped the watchkeeper";
    else if (keeper.at.y < -3) state.outcome = Outcome::lost, state.why = "the watchkeeper fell into the sea";
    else if (state.tide <= 0) state.outcome = Outcome::lost, state.why = "the tide covered the breakwater";
    ++state.frame;
}

namespace {
std::int16_t axis_of(float value) noexcept { return static_cast<std::int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767); }
float angle_between(float from, float to) noexcept {
    float apart = to - from;
    while (apart > 3.14159265f) apart -= 6.2831853f;
    while (apart < -3.14159265f) apart += 6.2831853f;
    return apart;
}
}  // namespace

Input Guide::operator()(const State& state) {
    Input input;
    xengine::Actions& actions = input.player;
    const Vec3 from = eye(state), look = looking(state);
    std::optional<Vec3> target;
    float nearest = 40;
    for (const Warden& warden : state.wardens) {
        if (warden.health <= 0) continue;
        const Vec3 chest = warden.at + Vec3{0, 1.2f, 0};
        const float distance = math::length(chest - from);
        if (distance < nearest && math::dot(math::normalized(chest - from), look) > 0.3f && clear(state, from, chest)) nearest = distance, target = chest;
    }
    const auto aim_at = [&](Vec3 at) {
        const Vec3 way = math::normalized(at - from);
        const float yaw = static_cast<float>(det::atan2(static_cast<double>(way.x), static_cast<double>(-way.z)));
        const float pitch = static_cast<float>(det::atan2(static_cast<double>(way.y), det::sqrt(static_cast<double>(way.x * way.x + way.z * way.z))));
        const float turn = angle_between(state.keeper.yaw, yaw), tilt = pitch - state.keeper.pitch;
        actions.axes[2] = axis_of(turn * 8), actions.axes[3] = axis_of(tilt * 8);
        return std::abs(turn) + std::abs(tilt);
    };
    // A lookout whose beam is on it: its lamp first, when nothing nearer is.
    for (const Searchlight& light : state.lookouts) {
        if (light.health <= 0 || !light.sees) continue;
        const float distance = math::length(light.post.at - from);
        if (distance < nearest && clear(state, from, light.post.at + math::normalized(from - light.post.at) * (lamp_radius + 0.05f))) nearest = distance, target = light.post.at;
    }
    if (target) {
        if (aim_at(*target) < 0.02f) actions.held |= 1u << fire_button;
        return input;
    }
    // Where to: the nearest fuse box still off, then the beacon.
    Vec3 goal = state.beacon;
    float goal_distance = 1e9f;
    for (std::size_t index = 0; index < state.fuses.size(); ++index)
        if (state.fused[index] < fuse_steps && horizontal(state.fuses[index], state.keeper.at) < goal_distance) goal = state.fuses[index], goal_distance = horizontal(state.fuses[index], state.keeper.at);
    if (state.frame >= planned_ || goal_distance < 1e9f && (path_.empty() || horizontal(path_.back(), goal) > 1.0f)) {
        planned_ = state.frame + steps_per_second;
        if (const auto made = state.ground->path(state.keeper.at, goal); made && !made->corners.empty()) path_ = made->corners, next_ = 0;
    }
    while (next_ < path_.size() && horizontal(path_[next_], state.keeper.at) < 0.8f) ++next_;
    if (next_ < path_.size()) {
        if (aim_at({path_[next_].x, from.y, path_[next_].z}) < 0.6f) actions.axes[1] = axis_of(1);
    } else {
        actions.held |= 1u << use_button;
    }
    return input;
}

std::uint64_t checksum(const State& state) noexcept {
    mlibs::hash::Fnv1a hash;
    const auto add = [&](auto value) { hash.bytes(std::as_bytes(std::span(&value, 1))); };
    const auto add_vec = [&](Vec3 value) { add(std::bit_cast<std::uint32_t>(value.x)), add(std::bit_cast<std::uint32_t>(value.y)), add(std::bit_cast<std::uint32_t>(value.z)); };
    add(state.bodies ? state.bodies->state_hash() : 0), add(state.ground ? state.ground->state_hash() : 0);
    const Watchkeeper& keeper = state.keeper;
    add_vec(keeper.at), add(std::bit_cast<std::uint32_t>(keeper.yaw)), add(std::bit_cast<std::uint32_t>(keeper.pitch));
    add(keeper.health), add(keeper.rounds), add(keeper.cooldown), add(keeper.lighting);
    for (const Warden& warden : state.wardens) {
        add(warden.health), add(static_cast<std::uint32_t>(warden.mind)), add_vec(warden.at), add_vec(warden.facing), add_vec(warden.heading_for), add(warden.searching), add(warden.cooldown);
    }
    for (const Mark& mark : state.marks) add_vec(mark.at), add_vec(mark.normal), add(mark.frame);
    // What the second night has (the harbour has none, and hashes as it did).
    for (const Searchlight& light : state.lookouts) add(std::bit_cast<std::uint32_t>(light.bearing)), add(light.health), add(light.horn), add(static_cast<std::uint32_t>(light.outward));
    for (const std::uint32_t worked : state.fused) add(worked);
    add(std::bit_cast<std::uint32_t>(state.tide)), add(state.frame), add(static_cast<std::uint32_t>(state.outcome));
    return hash.value();
}

}  // namespace quay
