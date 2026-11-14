module xengine.particles;

import std;
import mlibs.math;
import mlibs.geom;
import mlibs.hash;

namespace xengine::particles {

namespace {

namespace math = mlibs::math;
namespace geom = mlibs::geom;
namespace matter = xengine::matter;

// A cell of `near` a side, as one number (each way 21 bits about the middle).
std::uint64_t cell_of(std::int64_t x, std::int64_t y, std::int64_t z) noexcept {
    constexpr std::int64_t middle = 1 << 20;
    const auto part = [](std::int64_t value) { return static_cast<std::uint64_t>(std::clamp<std::int64_t>(value + middle, 0, (1 << 21) - 1)); };
    return part(x) << 42 | part(y) << 21 | part(z);
}
std::int64_t index_of(float value, float side) noexcept { return static_cast<std::int64_t>(std::floor(value / side)); }

}  // namespace

std::expected<Cloud, std::string> Cloud::make(std::shared_ptr<const matter::Program> program, const Options& options) {
    if (!program) return std::unexpected(std::string("no program of matter"));
    if (!(options.step > 0) || !(options.near > 0)) return std::unexpected(std::string("a step and a nearness above 0"));
    const auto temperature = program->channel("temperature");
    if (!temperature) return std::unexpected(std::string("the program has no channel `temperature`"));
    if (options.ambient != matter::none && options.ambient >= program->materials().size()) return std::unexpected(std::format("ambient material {} is not the program's", options.ambient));
    Cloud out;
    out.program_ = std::move(program), out.options_ = options;
    out.channels_ = static_cast<std::uint32_t>(out.program_->channels().size()), out.temperature_ = *temperature;
    return out;
}

double Cloud::value(std::uint32_t channel, std::size_t particle) const noexcept {
    if (channel >= channels_ || particle >= size()) return 0;
    return std::ldexp(static_cast<double>(state_[channel * size() + particle]), -static_cast<int>(program_->channels()[channel].fraction));
}

std::uint32_t Cloud::emit(std::span<const Spawn> spawns) {
    const std::size_t before = size();
    const std::size_t taken = std::min<std::size_t>(spawns.size(), options_.most > before ? options_.most - before : 0);
    if (!taken) return 0;
    const std::size_t after = before + taken;
    // The state laid out again in place, a channel a run, the new ones after the old: the last channel moved first
    // (each run goes to a place no earlier than its own).
    state_.resize(channels_ * after);
    for (std::uint32_t channel = channels_; channel-- > 0;)
        std::copy_backward(state_.begin() + static_cast<std::ptrdiff_t>(channel * before), state_.begin() + static_cast<std::ptrdiff_t>(channel * before + before),
                           state_.begin() + static_cast<std::ptrdiff_t>(channel * after + before));
    std::span<std::uint32_t> state = state_;
    for (std::size_t index = 0; index < taken; ++index) {
        const Spawn& spawn = spawns[index];
        const std::size_t particle = before + index;
        program_->place(state, after, particle, spawn.material);
        if (spawn.kelvin) {
            const double stored = std::ldexp(static_cast<double>(*spawn.kelvin), static_cast<int>(program_->channels()[temperature_].fraction));
            state[temperature_ * after + particle] = static_cast<std::uint32_t>(std::clamp(stored, 0.0, 4294967295.0));
        }
        at_.push_back(spawn.at), velocity_.push_back(spawn.velocity), size_.push_back(spawn.size), age_.push_back(0), life_.push_back(spawn.life), id_.push_back(next_id_++);
    }
    return static_cast<std::uint32_t>(taken);
}

std::expected<void, std::string> Cloud::step(std::span<const Surface> world) {
    landed_.clear(), events_.clear();
    const float dt = options_.step;
    const auto restitution = program_->column("restitution"), friction = program_->column("friction");
    // Each surface's way back from the world, and its normals' way there.
    facing_.clear();
    for (const Surface& surface : world) {
        if (!surface.mesh || !surface.tree) continue;
        const std::optional<Mat4> back = math::inverse(surface.place);
        if (back) facing_.push_back({&surface, *back, math::transposed(*back)});
    }
    // ── fly, and meet the world ──
    for (std::size_t particle = 0; particle < size(); ++particle) {
        Vec3& speed = velocity_[particle];
        speed = speed + options_.gravity * dt;
        speed = speed - speed * (options_.drag * dt);
        const Vec3 from = at_[particle], to = from + speed * dt;
        // The first surface its path meets.
        float first = 1;
        Vec3  normal{0, 0, 0};
        for (const Facing& one : facing_) {
            const Vec3 start = math::point(one.back, from), end = math::point(one.back, to);
            const Vec3 way = end - start;
            const float length = math::length(way);
            if (!(length > 0)) continue;
            const geom::Ray ray{start, way * (1 / length)};
            const auto hit = geom::meet(ray, *one.surface->mesh, *one.surface->tree, length);
            if (!hit || !(hit->distance / length < first)) continue;
            const geom::Mesh& mesh = *one.surface->mesh;
            const std::size_t corner = static_cast<std::size_t>(hit->item) * 3;
            const Vec3 a = mesh.positions[mesh.indices[corner]], b = mesh.positions[mesh.indices[corner + 1]], c = mesh.positions[mesh.indices[corner + 2]];
            const Vec3 facing = math::normalized(math::direction(one.normals, math::cross(b - a, c - a)));
            if (!(math::dot(facing, facing) > 0)) continue;
            first = hit->distance / length, normal = facing;
        }
        if (first < 1) {
            if (math::dot(normal, speed) > 0) normal = normal * -1.0f;  // the side it came from
            const matter::MaterialId material = state_[particle];
            const float keeps = restitution ? static_cast<float>(program_->number(material, *restitution)) : 0.4f;
            const float rubs = friction ? static_cast<float>(program_->number(material, *friction)) : 0.3f;
            const float into = -math::dot(speed, normal);
            const Vec3 along = speed + normal * into;
            const Vec3 met = from + (to - from) * first;
            landed_.push_back({id_[particle], material, into, met, normal});
            speed = along * (1 - rubs) + normal * (into * keeps);
            at_[particle] = met + normal * 1e-4f;
        } else {
            at_[particle] = to;
        }
        age_[particle] += dt;
    }
    // ── what has lived its life is gone (the rest keep their order) ──
    std::size_t kept = 0;
    const std::size_t before = size();
    kept_.clear();
    for (std::size_t particle = 0; particle < before; ++particle)
        if (age_[particle] < life_[particle]) kept_.push_back(particle);
    const std::vector<std::size_t>& from_place = kept_;
    if (from_place.size() != before) {
        kept = from_place.size();
        const auto keep = [&](auto& values) {
            for (std::size_t index = 0; index < kept; ++index) values[index] = values[from_place[index]];
            values.resize(kept);
        };
        keep(at_), keep(velocity_), keep(size_), keep(age_), keep(life_), keep(id_);
        // In place: each value goes to a place no later than its own (kept ≤ before, index ≤ from_place[index]).
        for (std::uint32_t channel = 0; channel < channels_; ++channel)
            for (std::size_t index = 0; index < kept; ++index) state_[channel * kept + index] = state_[channel * before + from_place[index]];
        state_.resize(channels_ * kept);
    }
    ++steps_;
    const std::size_t count = size();
    if (!count) return {};
    // ── one tick of matter: each beside those near it, and the air ──
    // A sample of the air for each particle (`count` more instances after them), set again each step.
    const bool bath = options_.ambient != matter::none;
    const std::size_t instances = count * (bath ? 2 : 1);
    input_.assign(channels_ * instances, 0), output_.assign(channels_ * instances, 0);
    for (std::uint32_t channel = 0; channel < channels_; ++channel)
        std::copy_n(state_.begin() + static_cast<std::ptrdiff_t>(channel * count), count, input_.begin() + static_cast<std::ptrdiff_t>(channel * instances));
    if (bath) {
        const double stored = std::ldexp(static_cast<double>(options_.ambient_kelvin), static_cast<int>(program_->channels()[temperature_].fraction));
        for (std::size_t sample = count; sample < instances; ++sample) {
            program_->place(input_, instances, sample, options_.ambient);
            input_[temperature_ * instances + sample] = static_cast<std::uint32_t>(std::clamp(stored, 0.0, 4294967295.0));
        }
    }
    // Who is beside whom: every pair nearer than `near`, found by cells; then taken the nearest first (at one
    // distance, by their places) while both have room for one more.
    pairs_.clear(), cells_.clear(), candidates_.clear();
    const float side = options_.near, reach = side * side;
    for (std::uint32_t particle = 0; particle < count; ++particle)
        cells_.push_back({cell_of(index_of(at_[particle].x, side), index_of(at_[particle].y, side), index_of(at_[particle].z, side)), particle});
    std::ranges::sort(cells_);
    for (std::uint32_t particle = 0; particle < count; ++particle) {
        const std::int64_t x = index_of(at_[particle].x, side), y = index_of(at_[particle].y, side), z = index_of(at_[particle].z, side);
        for (std::int64_t dx = -1; dx <= 1; ++dx)
            for (std::int64_t dy = -1; dy <= 1; ++dy)
                for (std::int64_t dz = -1; dz <= 1; ++dz) {
                    const std::uint64_t key = cell_of(x + dx, y + dy, z + dz);
                    for (auto found = std::ranges::lower_bound(cells_, std::pair{key, std::uint32_t{0}}); found != cells_.end() && found->first == key; ++found) {
                        const std::uint32_t other = found->second;
                        if (other <= particle) continue;
                        const Vec3 apart = at_[other] - at_[particle];
                        const float squared = math::dot(apart, apart);
                        if (squared < reach) candidates_.push_back({squared, particle, other});
                    }
                }
    }
    std::ranges::sort(candidates_, [](const Candidate& one, const Candidate& other) {
        return one.apart != other.apart ? one.apart < other.apart : one.one != other.one ? one.one < other.one : one.other < other.other;
    });
    degree_.assign(count, 0);
    for (const Candidate& candidate : candidates_)
        if (degree_[candidate.one] < options_.most_near && degree_[candidate.other] < options_.most_near) {
            pairs_.push_back({candidate.one, candidate.other, 1});
            ++degree_[candidate.one], ++degree_[candidate.other];
        }
    if (bath)
        for (std::uint32_t particle = 0; particle < count; ++particle) pairs_.push_back({particle, static_cast<std::uint32_t>(count + particle), options_.ambient_weight});
    const auto stepped = program_->step({instances, pairs_}, input_, output_, workspace_, &events_);
    if (!stepped) return std::unexpected(std::format("matter's step: {}", stepped.error().message));
    for (std::uint32_t channel = 0; channel < channels_; ++channel)
        std::copy_n(output_.begin() + static_cast<std::ptrdiff_t>(channel * instances), count, state_.begin() + static_cast<std::ptrdiff_t>(channel * count));
    std::erase_if(events_, [count](const matter::Event& event) { return event.instance >= count; });  // the air's own
    return {};
}

std::uint64_t Cloud::fingerprint() const noexcept {
    mlibs::hash::Fnv1a hash;
    const auto add = [&](const auto& values) { hash.bytes(std::as_bytes(std::span(values))); };
    add(at_), add(velocity_), add(size_), add(age_), add(life_), add(id_), add(state_);
    const std::array<std::uint64_t, 1> counted{steps_};
    add(counted);
    return hash.value();
}

}  // namespace xengine::particles
