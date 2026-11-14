module xengine.audio.space;

import std;
import mlibs.math;
import mlibs.geom;

namespace xengine::audio::space {

namespace {

namespace math = mlibs::math;
namespace geom = mlibs::geom;
namespace det = mlibs::math::det;

constexpr double pi = 3.14159265358979323846;
constexpr float highest = 20000;  // Hz: what an ear hears at most
constexpr float shaded = 1500;    // Hz: the far ear's highest, the sound straight from the other side

// A one-pole low pass's step toward its input for a cutoff at `rate`: 1 − e^(−2π f / rate).
float pull_of(float cutoff, std::int32_t rate) noexcept {
    return static_cast<float>(1.0 - det::exp(-2.0 * pi * static_cast<double>(cutoff) / static_cast<double>(rate)));
}

float decibels(double sum_of_squares, std::int32_t frames) noexcept {
    if (!(sum_of_squares > 0) || frames <= 0) return -200;
    return static_cast<float>(10.0 * det::log(sum_of_squares / frames) / det::log(10.0));
}

}  // namespace

std::expected<std::shared_ptr<Space>, std::string> Space::make(Mixer& mixer, Options options) {
    if (!(options.speed > 0) || !(options.reference > 0) || !(options.air > 0)) return std::unexpected(std::string("a speed, a reference distance and the air's distance above 0"));
    if (options.most == 0) return std::unexpected(std::string("room for one sound at least (`most`)"));
    const std::int32_t bus = mixer.bus(options.bus);
    auto made = std::make_shared<Space>(std::move(options), mixer.rate(), bus);
    made->emitters_.resize(made->options_.most);
    made->heard_.reserve(made->options_.most);
    return made;
}

std::uint32_t Space::add(const Sound& sound) {
    sounds_.push_back(mono(sound, rate_));
    names_.push_back(sound.name);
    return static_cast<std::uint32_t>(sounds_.size() - 1);
}

std::expected<EmitterId, std::string> Space::emit(std::uint32_t sound, Vec3 at, float gain, std::string cause) {
    if (sound >= sounds_.size()) return std::unexpected(std::format("no sound {} (add it first: {} added)", sound, sounds_.size()));
    if (!std::isfinite(gain) || gain < 0) return std::unexpected(std::string("a gain is a finite number, not below 0"));
    const auto free = std::ranges::find_if(emitters_, [](const Emitter& emitter) { return !emitter.active; });
    if (free == emitters_.end()) return std::unexpected(std::format("{} sounds already: `most` is {}", options_.most, options_.most));
    *free = Emitter{.active = true, .id = ++last_, .sound = sound, .at = at, .gain = gain, .cause = std::move(cause)};
    return free->id;
}

bool Space::move(EmitterId id, Vec3 at) noexcept {
    for (Emitter& emitter : emitters_)
        if (emitter.active && emitter.id == id) {
            emitter.at = at, emitter.moved = true;
            return true;
        }
    return false;
}

std::size_t Space::sounding() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(emitters_, [](const Emitter& emitter) { return emitter.active; }));
}

Space::Between Space::between(Vec3 from, Vec3 to) const noexcept {
    std::uint32_t count = 0;
    float through = 1, bright = highest;
    for (const Wall& wall : walls_) {
        if (!wall.mesh || !wall.tree) continue;
        const std::optional<Mat4> back = math::inverse(wall.place);
        if (!back) continue;
        Vec3 start = math::point(*back, from);
        const Vec3 end = math::point(*back, to);
        // Every surface along the way, one after another (a little past each before looking for the next).
        while (count < options_.most_walls) {
            const Vec3 way = end - start;
            const float length = math::length(way);
            if (!(length > 1e-4f)) break;
            const Vec3 along = way * (1 / length);
            const auto hit = geom::meet(geom::Ray{start, along}, *wall.mesh, *wall.tree, length);
            if (!hit) break;
            bright = count == 0 ? std::min(bright, wall.bright) : std::min(bright, wall.bright) * 0.5f;
            ++count, through *= wall.through;
            start = start + along * (hit->distance + 1e-4f);
        }
    }
    return {count, through, bright};
}

Vec3 Space::where(const Emitter& emitter, double when) noexcept {
    const std::size_t count = std::min(emitter.count, kept);
    const auto at = [&](std::size_t back) -> const std::pair<std::int64_t, Vec3>& { return emitter.been[(emitter.count - 1 - back) % kept]; };  // 0: the latest
    const auto& latest = at(0);
    if (when >= static_cast<double>(latest.first)) {
        if (count < 2) return latest.second;
        const auto& before = at(1);
        const double span = static_cast<double>(latest.first - before.first);
        if (!(span > 0)) return latest.second;
        const double past = std::min(when - static_cast<double>(latest.first), span);  // on at its last speed, a step at most
        return latest.second + (latest.second - before.second) * static_cast<float>(past / span);
    }
    for (std::size_t back = 1; back < count; ++back) {
        const auto& earlier = at(back);
        if (when >= static_cast<double>(earlier.first)) {
            const auto& later = at(back - 1);
            const double span = static_cast<double>(later.first - earlier.first);
            const float share = span > 0 ? static_cast<float>((when - static_cast<double>(earlier.first)) / span) : 0.0f;
            return earlier.second + (later.second - earlier.second) * share;
        }
    }
    return at(count - 1).second;  // before the oldest kept
}

std::pair<double, float> Space::heard_at(const Emitter& emitter, Vec3 ear, std::int64_t now) const noexcept {
    const double rate = static_cast<double>(rate_), speed = static_cast<double>(options_.speed);
    // t − τ = |emitter(τ) − ear| ÷ speed: from the emitter's latest place, a few steps (each nearer by v ÷ speed).
    double distance = math::length(where(emitter, static_cast<double>(now)) - ear);
    for (int step = 0; step < 6; ++step) distance = math::length(where(emitter, static_cast<double>(now) - distance / speed * rate) - ear);
    return {static_cast<double>(now - emitter.start) - distance / speed * rate, static_cast<float>(distance)};
}

void Space::render(std::int64_t clock, std::int32_t frames, std::span<const BusBlock> buses) noexcept {
    heard_.clear();
    if (frames <= 0 || bus_ < 0 || static_cast<std::size_t>(bus_) >= buses.size()) return;
    const BusBlock& out = buses[static_cast<std::size_t>(bus_)];
    // The listener's frame and ears.
    const Vec3 forward = math::normalized(listener_.forward);
    const Vec3 right = math::normalized(math::cross(forward, listener_.up));
    const Vec3 up = math::cross(right, forward);
    const std::array<Vec3, 2> ears{listener_.at - right * listener_.head, listener_.at + right * listener_.head};
    const std::array<Vec3, 2> sides{right * -1.0f, right};
    for (Emitter& emitter : emitters_) {
        if (!emitter.active) continue;
        if (!emitter.started) emitter.started = true, emitter.start = clock;
        // Where the owner said it is, from this block on (the mixer may call a block in parts: a place once).
        if (emitter.moved) {
            emitter.moved = false;
            if (emitter.count > 0 && emitter.been[(emitter.count - 1) % kept].first == clock) emitter.been[(emitter.count - 1) % kept].second = emitter.at;
            else emitter.been[emitter.count++ % kept] = {clock, emitter.at};
        }
        const std::vector<float>& made = sounds_[emitter.sound];
        const auto length = static_cast<std::int64_t>(made.size());
        const auto [walls, through, bright] = between(listener_.at, emitter.at);
        // Each ear at the block's end: the sample it hears, and how loud and how bright.
        std::array<Ear, 2> target{};
        for (std::size_t ear = 0; ear < 2; ++ear) {
            const auto [read, distance] = heard_at(emitter, ears[ear], clock + frames);
            const Vec3 toward = emitter.at - ears[ear];
            const float apart = math::length(toward);
            const float facing = apart > 0 ? math::dot(toward * (1 / apart), sides[ear]) : 1.0f;  // 1: from this ear's side
            // The air's, and the head's shade on the ear the sound comes from the other side of (none ahead or on its side).
            const float shade = facing >= 0 ? highest : shaded + (highest - shaded) * (1 + facing);
            const float cutoff = std::min({highest / (1 + distance / options_.air), shade, bright});
            target[ear] = {read, distance, emitter.gain * options_.reference / std::max(distance, options_.reference) * through, pull_of(cutoff, rate_), 0};
        }
        if (emitter.ears[0].pull == 0 && emitter.ears[1].pull == 0) {  // its first block: from where it is
            for (std::size_t ear = 0; ear < 2; ++ear) {
                const auto [read, distance] = heard_at(emitter, ears[ear], clock);
                emitter.ears[ear] = {read, distance, target[ear].gain, target[ear].pull, 0};
            }
        }
        std::array<double, 2> energy{0, 0};
        bool still = false;  // whether any of it is still to come or still sounding
        for (std::int32_t frame = 0; frame < frames; ++frame) {
            const double share = static_cast<double>(frame) / static_cast<double>(frames);
            const float part_of = static_cast<float>(frame + 1) / static_cast<float>(frames);
            std::array<float, 2> sample{};
            for (std::size_t ear = 0; ear < 2; ++ear) {
                const Ear& from = emitter.ears[ear];
                const Ear& to = target[ear];
                const float gain = from.gain + (to.gain - from.gain) * part_of;
                const float pull = from.pull + (to.pull - from.pull) * part_of;
                // The sample of the sound this ear hears now, between its two nearest.
                const double read = from.read + (to.read - from.read) * share;
                float value = 0;
                if (read >= 0 && read < static_cast<double>(length - 1)) {
                    const auto first = static_cast<std::int64_t>(read);
                    const auto part = static_cast<float>(read - static_cast<double>(first));
                    value = made[static_cast<std::size_t>(first)] + (made[static_cast<std::size_t>(first + 1)] - made[static_cast<std::size_t>(first)]) * part;
                }
                if (read < static_cast<double>(length)) still = true;
                float& held = emitter.ears[ear].held;
                held += pull * (value * gain - held);
                if (std::abs(held) < 1e-20f) held = 0;  // denormals flushed by hand, the same everywhere
                sample[ear] = held;
                energy[ear] += static_cast<double>(held) * held;
            }
            out.left[static_cast<std::size_t>(frame)] += sample[0], out.right[static_cast<std::size_t>(frame)] += sample[1];
        }
        // What was heard, from where.
        const Vec3 toward = emitter.at - listener_.at;
        const float distance = math::length(toward);
        const Vec3 way = distance > 0 ? toward * (1 / distance) : forward;
        const float across = math::dot(way, right), ahead = math::dot(way, forward), above = std::clamp(math::dot(way, up), -1.0f, 1.0f);
        const double advanced = ((target[0].read - emitter.ears[0].read) + (target[1].read - emitter.ears[1].read)) * 0.5;
        heard_.push_back({.id = emitter.id,
                          .sound = emitter.sound,
                          .name = names_[emitter.sound],
                          .cause = emitter.cause,
                          .distance = distance,
                          .azimuth = static_cast<float>(det::atan2(across, ahead) * 180.0 / pi),
                          .elevation = static_cast<float>(det::atan2(above, std::sqrt(std::max(0.0f, 1 - above * above))) * 180.0 / pi),
                          .delay = (target[0].distance + target[1].distance) * 0.5f / options_.speed,
                          .doppler = static_cast<float>(advanced / static_cast<double>(frames)),
                          .walls = walls,
                          .left = decibels(energy[0], frames),
                          .right = decibels(energy[1], frames),
                          .arrived = target[0].read >= 0 || target[1].read >= 0});
        for (std::size_t ear = 0; ear < 2; ++ear) {
            const float held = emitter.ears[ear].held;
            emitter.ears[ear] = target[ear];
            emitter.ears[ear].held = held;
        }
        // Gone when all of it has passed both ears and their filters are quiet.
        if (!still && std::abs(emitter.ears[0].held) < 1e-9f && std::abs(emitter.ears[1].held) < 1e-9f) emitter.active = false;
    }
}

}  // namespace xengine::audio::space
