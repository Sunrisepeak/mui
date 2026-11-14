module omui.showcase.quay.headless;

import std;
import mlibs.math;

namespace quay {

namespace {

namespace math = mlibs::math;
namespace det = mlibs::math::det;
namespace space = xengine::audio::space;

constexpr double degrees = 180 / 3.14159265358979323846;
constexpr float heard_from = -60;  // dB at the ears: the faintest a person picks out of the rain

// Where something is from the watchkeeper's eye: bearing (to the right +), elevation above where the eyes look,
// distance. Degrees and metres.
struct Seen {
    double bearing = 0, elevation = 0, distance = 0;
};
Seen seen_from(const State& state, Vec3 at) {
    const Vec3 eye = quay::eye(state), toward = at - eye;
    const auto yaw = static_cast<double>(state.keeper.yaw);
    const double forward = det::sin(yaw) * toward.x - det::cos(yaw) * toward.z, right = det::cos(yaw) * toward.x + det::sin(yaw) * toward.z;
    const double across = det::sqrt(forward * forward + right * right);
    return {.bearing = det::atan2(right, forward) * degrees, .elevation = det::atan2(static_cast<double>(toward.y), across) * degrees - static_cast<double>(state.keeper.pitch) * degrees,
            .distance = static_cast<double>(math::length(toward))};
}

}  // namespace

std::string observe(const State& state, bool full) {
    const Watchkeeper& keeper = state.keeper;
    std::string out = std::format(R"({{"frame":{},"outcome":"{}","why":"{}","tide":{:.1f},)", state.frame, outcome_names[static_cast<std::size_t>(state.outcome)], state.why, state.tide);
    out += std::format(R"("keeper":{{"health":{},"rounds":{},"struck":{},"lighting":{:.2f}}},)", keeper.health, keeper.rounds, keeper.hurt < 30 ? "true" : "false",
                       std::min(1.0, static_cast<double>(keeper.lighting) / steps_per_second));
    const Seen lamp = seen_from(state, state.beacon + Vec3{0, 1.2f, 0});
    out += std::format(R"("beacon":{{"bearing":{:.2f},"elevation":{:.2f},"distance":{:.2f}}},)", lamp.bearing, lamp.elevation, lamp.distance);

    // The wardens in view: within the view's angle and the night's reach, chest or head clear of everything.
    std::vector<Seen> wardens;
    const Vec3 eye = quay::eye(state);
    for (const Warden& warden : state.wardens) {
        if (warden.health <= 0) continue;
        const Vec3 chest = warden.at + Vec3{0, 1.2f, 0};
        const Seen seen = seen_from(state, chest);
        if (seen.distance > seen_within || std::abs(seen.bearing) > view_half_angle || std::abs(seen.elevation) > 40) continue;
        if (clear(state, eye, chest) || clear(state, eye, warden.at + Vec3{0, 1.65f, 0})) wardens.push_back(seen);
    }
    std::ranges::sort(wardens, {}, &Seen::distance);
    out += R"("wardens":[)";
    for (std::size_t index = 0; index < wardens.size(); ++index)
        out += std::format(R"({}{{"bearing":{:.2f},"elevation":{:.2f},"distance":{:.2f}}})", index ? "," : "", wardens[index].bearing, wardens[index].elevation, wardens[index].distance);

    // What was heard lately: others' sounds, by the rule the speakers play by.
    out += R"(],"heard":[)";
    const auto yaw = static_cast<double>(keeper.yaw);
    const space::Listener ears{.at = eye, .forward = {static_cast<float>(det::sin(yaw)), 0, static_cast<float>(-det::cos(yaw))}, .up = {0, 1, 0}};
    bool first = true;
    for (const Noise& noise : state.recent) {
        if (noise.by == 0) continue;
        const space::Reach reached = space::reach(ears, noise.at, state.statics->walls);
        const float level = noise.loudness + reached.level;
        if (level < heard_from) continue;
        out += std::format(R"({}{{"what":"{}","bearing":{:.1f},"level":{:.1f},"walls":{},"ago":{:.2f}}})", first ? "" : ",", noise.what, reached.azimuth, level, reached.walls,
                           static_cast<double>(state.frame - noise.frame) / steps_per_second);
        first = false;
    }

    // The way ahead as the eye meets it, at waist height across the view.
    out += std::format(R"(],"depth":{{"from":{},"every":{},"metres":[)", depth_from, depth_every);
    const Vec3 waist = keeper.at + Vec3{0, 0.8f, 0};
    for (int sample = 0; sample < depth_count; ++sample) {
        const double angle = yaw + static_cast<double>(depth_from + sample * depth_every) / degrees;
        const Vec3 to = waist + Vec3{static_cast<float>(det::sin(angle)), 0, static_cast<float>(-det::cos(angle))} * depth_reach;
        const auto met = state.bodies ? state.bodies->ray(waist, to) : std::nullopt;
        out += std::format("{}{:.2f}", sample ? "," : "", met ? static_cast<double>(met->fraction * depth_reach) : static_cast<double>(depth_reach));
    }
    out += "]}";

    if (full) {
        out += std::format(R"(,"at":{{"x":{:.3f},"y":{:.3f},"z":{:.3f},"yaw":{:.4f},"pitch":{:.4f}}},"all":[)", keeper.at.x, keeper.at.y, keeper.at.z, keeper.yaw, keeper.pitch);
        for (std::size_t index = 0; index < state.wardens.size(); ++index) {
            const Warden& warden = state.wardens[index];
            out += std::format(R"({}{{"x":{:.3f},"z":{:.3f},"health":{},"mind":"{}","sees":{}}})", index ? "," : "", warden.at.x, warden.at.z, warden.health,
                               mind_names[static_cast<std::size_t>(warden.mind)], warden.sees ? "true" : "false");
        }
        out += std::format(R"(],"hash":"{:016x}")", checksum(state));
    }
    return out + "}";
}

}  // namespace quay
