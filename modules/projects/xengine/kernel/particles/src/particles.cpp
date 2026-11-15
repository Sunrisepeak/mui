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
        at_.push_back(spawn.at), velocity_.push_back(spawn.velocity), size_.push_back(spawn.size), age_.push_back(0), life_.push_back(spawn.life), id_.push_back(next_id_++), rise_.push_back(0);
    }
    return static_cast<std::uint32_t>(taken);
}

std::optional<std::pair<float, Vec3>> Cloud::first_hit(Vec3 from, Vec3 to) const noexcept {
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
    if (!(first < 1)) return std::nullopt;
    return std::pair{first, normal};
}

void Cloud::find_touches() {
    // Every pair that may meet this step: nearer than both their sizes (with room for a step's flight), found by
    // cells of the largest size, in the order of the first then the second.
    touches_.clear(), cells_.clear();
    const std::size_t count = size();
    float largest = 0;
    for (const float across : size_) largest = std::max(largest, across);
    if (!count || !(largest > 0)) return;
    const float side = largest * 1.5f;
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
                        const float reach = (size_[particle] + size_[other]) * 0.75f;
                        const float squared = math::dot(apart, apart);
                        if (squared < reach * reach) touches_.push_back({squared, particle, other});
                    }
                }
    }
    std::ranges::sort(touches_, [](const Candidate& one, const Candidate& other) { return one.one != other.one ? one.one < other.one : one.other < other.other; });
}

std::pair<Vec3, std::optional<Vec3>> Cloud::kept_off(Vec3 from, Vec3 to, float radius) const noexcept {
    // The way from `from` to `to`, looked along a radius further: where it meets a surface it stops a radius short
    // of it, the surface's facing turned toward where it came from.
    const Vec3 way = to - from;
    const float length = math::length(way);
    if (!(length > 0)) return {to, std::nullopt};
    const Vec3 ahead = way * (1 / length);
    const auto hit = first_hit(from, to + ahead * radius);
    if (!hit) return {to, std::nullopt};
    Vec3 normal = hit->second;
    if (math::dot(normal, way) > 0) normal = normal * -1.0f;
    const Vec3 met = from + ahead * ((length + radius) * hit->first);
    return {met + normal * radius, normal};
}

void Cloud::settle(std::span<const Vec3> wind) {
    // Particles that take room, as position-based dynamics does it (Macklin et al. 2014, "Unified particle
    // physics"): in substeps, each predicted where its speed takes it, then a few passes in a fixed order that put
    // apart what overlaps (the lighter moving more) and keep each a radius off the surface it is against, each
    // with friction on the way it slid (a slip shorter than friction × depth undone: what holds a heap up); its
    // speed is then how far it went. A contact that begins is a landing and keeps `restitution` of the speed in.
    const float dt = options_.step;
    const auto restitution = program_->column("restitution"), friction = program_->column("friction"), density = program_->column("density");
    const bool winds = wind.size() == size();
    const auto slips = [&](std::uint32_t particle) {  // a liquid or a gas has no friction (what holds it is viscosity)
        const matter::Phase phase = program_->phase(state_[particle]);
        return phase == matter::Phase::liquid || phase == matter::Phase::gas;
    };
    const auto grip_of = [&](std::uint32_t particle) {
        if (slips(particle)) return 0.0f;
        return friction ? static_cast<float>(std::clamp(program_->number(state_[particle], *friction), 0.0, 1.0)) : 0.3f;
    };
    const auto own = [&](std::size_t particle) { return density ? std::max(static_cast<float>(program_->number(state_[particle], *density)), 0.01f) : 1000.0f; };
    find_touches();
    // Each one's mass; a fluid lifted (or sunk) by the difference of its density from the fluids it touches — on
    // its own, from the air's — against gravity.
    mass_.resize(size()), lift_.assign(size(), 0);
    for (std::size_t particle = 0; particle < size(); ++particle) mass_[particle] = own(particle) * size_[particle] * size_[particle] * size_[particle];
    about_.assign(size(), Vec3{0, 0, 0});
    for (const Candidate& pair : touches_) {
        const Vec3 apart = at_[pair.other] - at_[pair.one];
        const float rest = (size_[pair.one] + size_[pair.other]) * 0.5f;
        if (!(math::dot(apart, apart) < rest * rest * 1.1f)) continue;
        if (slips(pair.other)) about_[pair.one].x += own(pair.other), about_[pair.one].y += 1;
        if (slips(pair.one)) about_[pair.other].x += own(pair.one), about_[pair.other].y += 1;
    }
    const float air = options_.ambient != matter::none && density ? static_cast<float>(program_->number(options_.ambient, *density)) : 0.0f;
    for (std::size_t particle = 0; particle < size(); ++particle)
        if (slips(static_cast<std::uint32_t>(particle))) lift_[particle] = about_[particle].y > 0 ? (about_[particle].x / about_[particle].y - own(particle)) / own(particle) : air / own(particle);
    // Substeps short enough that none goes past another unmet: no more than a quarter of the smallest size each.
    float fastest = 0, smallest = std::numeric_limits<float>::max();
    for (std::size_t particle = 0; particle < size(); ++particle)
        fastest = std::max(fastest, math::length(velocity_[particle]) + math::length(options_.gravity) * dt), smallest = std::min(smallest, size_[particle]);
    const std::uint32_t substeps = smallest > 0 ? static_cast<std::uint32_t>(std::clamp(std::ceil(fastest * dt / (smallest * 0.25f)), 1.0f, static_cast<float>(std::max(1u, options_.most_substeps)))) : 1u;
    const float part = dt / static_cast<float>(substeps), pull = math::length(options_.gravity);
    const Vec3 down = pull > 0 ? options_.gravity * (1 / pull) : Vec3{0, -1, 0};
    resting_.resize(size(), Vec3{0, 0, 0});
    for (std::uint32_t substep = 0; substep < substeps; ++substep) {
        if (substep > 0) find_touches();
        began_.assign(at_.begin(), at_.end());
        ground_.assign(size(), Vec3{0, 0, 0}), floor_at_.resize(size()), flown_.resize(size());
        // Predicted: gravity, the lift, the wind's drag; and the surface it will be against (on its way, or under it).
        for (std::size_t particle = 0; particle < size(); ++particle) {
            Vec3& speed = velocity_[particle];
            speed = speed + options_.gravity * part;
            if (lift_[particle] != 0) speed = speed - options_.gravity * (lift_[particle] * part);
            speed = speed - (speed - (winds ? wind[particle] : Vec3{0, 0, 0})) * (options_.drag * part);
            flown_[particle] = speed;  // the speed before the contacts: what a landing is measured by
            const float radius = size_[particle] * 0.5f;
            const Vec3 from = at_[particle], to = from + speed * part;
            at_[particle] = to;
            const Vec3 way = to - from;
            const float length = math::length(way);
            std::optional<std::pair<float, Vec3>> hit;
            Vec3 start = from, reach = to;
            if (length > 0) reach = to + way * (radius / length), hit = first_hit(from, reach);
            if (!hit) start = to, reach = to + down * (radius * 1.5f), hit = first_hit(start, reach);
            if (!hit) continue;
            Vec3 normal = hit->second;
            if (math::dot(normal, reach - start) > 0) normal = normal * -1.0f;
            ground_[particle] = normal, floor_at_[particle] = start + (reach - start) * hit->first;
        }
        for (std::uint32_t pass = 0; pass < options_.passes; ++pass) {
            // Each pair's correction from where all are now, gathered; then each particle moved by what it
            // gathered ÷ how many gave it (constraint averaging, Macklin et al., unrelaxed: no overshoot where many
            // meet, and none — no bounce the material does not have — where one does).
            delta_.assign(size(), Vec3{0, 0, 0}), given_.assign(size(), 0);
            for (const Candidate& pair : touches_) {
                const Vec3 apart = at_[pair.other] - at_[pair.one];
                const float rest = (size_[pair.one] + size_[pair.other]) * 0.5f, distance = math::length(apart);
                if (!(distance < rest)) continue;
                const Vec3 normal = distance > 0 ? apart * (1 / distance) : Vec3{0, 1, 0};
                const float one = 1 / mass_[pair.one], other = 1 / mass_[pair.other], depth = rest - distance, share = depth / (one + other);
                Vec3 push_one = normal * (-share * one), push_other = normal * (share * other);
                // Friction on the way they slid against each other this substep: a slip shorter than friction ×
                // depth undone (they hold), a longer one shortened by that much.
                const float grip = (slips(pair.one) || slips(pair.other) ? 0.0f : (grip_of(pair.one) + grip_of(pair.other)) * 0.5f) * depth;
                if (grip > 0) {
                    const Vec3 slid = (at_[pair.other] - began_[pair.other]) - (at_[pair.one] - began_[pair.one]);
                    const Vec3 across = slid - normal * math::dot(slid, normal);
                    const float length = math::length(across);
                    if (length > 0) {
                        const Vec3 undone = length < grip ? across : across * (grip / length);
                        push_one = push_one + undone * (one / (one + other)), push_other = push_other - undone * (other / (one + other));
                    }
                }
                delta_[pair.one] = delta_[pair.one] + push_one, delta_[pair.other] = delta_[pair.other] + push_other;
                ++given_[pair.one], ++given_[pair.other];
            }
            for (std::size_t particle = 0; particle < size(); ++particle)
                if (given_[particle]) at_[particle] = at_[particle] + delta_[particle] * (1.0f / static_cast<float>(given_[particle]));
            for (std::size_t particle = 0; particle < size(); ++particle) {
                const Vec3 facing = ground_[particle];
                if (!(math::dot(facing, facing) > 0)) continue;
                const float depth = size_[particle] * 0.5f - math::dot(at_[particle] - floor_at_[particle], facing);
                if (!(depth > 0)) continue;
                at_[particle] = at_[particle] + facing * depth;
                const Vec3 slid = at_[particle] - began_[particle];
                const Vec3 across = slid - facing * math::dot(slid, facing);
                const float length = math::length(across), grip = grip_of(static_cast<std::uint32_t>(particle)) * depth;
                if (length > 0 && grip > 0) at_[particle] = at_[particle] - (length < grip ? across : across * (grip / length));
            }
        }
        for (std::size_t particle = 0; particle < size(); ++particle) {
            // Never through a surface (a wall it was pushed into by others: where its way crosses one, it stops
            // just this side), and its speed: how far it went.
            if (const auto crossed = first_hit(began_[particle], at_[particle])) {
                Vec3 normal = crossed->second;
                if (math::dot(normal, at_[particle] - began_[particle]) > 0) normal = normal * -1.0f;
                at_[particle] = began_[particle] + (at_[particle] - began_[particle]) * crossed->first + normal * 1e-4f;
            }
            // A grain (not a liquid or a gas) that moved less than a tenth of what gravity gives one from rest in a
            // substep sleeps there: left where it began, still (particle sleeping, Macklin et al. 2014) — a grain on
            // a slope pulled at under a tenth of g stays, what lets a heap keep its slope.
            if (!slips(static_cast<std::uint32_t>(particle)) && math::length(at_[particle] - began_[particle]) < 0.1f * pull * part * part) at_[particle] = began_[particle];
            Vec3& speed = velocity_[particle];
            speed = (at_[particle] - began_[particle]) * (1 / part);
            const Vec3 facing = ground_[particle];
            if (!(math::dot(facing, facing) > 0)) continue;
            const float into = -math::dot(flown_[particle], facing);
            if (into > 0 && !(math::dot(resting_[particle], facing) > 0.5f)) {
                // A contact that began: a landing, and the speed back off it its restitution of the speed in.
                const matter::MaterialId material = state_[particle];
                landed_.push_back({id_[particle], material, into, at_[particle] - facing * (size_[particle] * 0.5f), facing});
                const float keeps = restitution ? static_cast<float>(program_->number(material, *restitution)) : 0.4f;
                speed = speed + facing * (keeps * into);
            }
        }
        resting_.assign(ground_.begin(), ground_.end());
    }
    // A lighter fluid under a heavier one it touches changes places with it — the lattice's exchange by density,
    // on particles — at the pace its buoyancy carries it: √(lift × g × size) a second (oil 2 cm across in water,
    // 0.15 m/s), a place when it has come its size. Each at most once a step, the pairs in order; nothing made or
    // lost (they trade places, each keeps its state and speed).
    swapped_.assign(size(), 0);
    for (std::size_t particle = 0; particle < size(); ++particle)
        if (lift_[particle] > 0 && about_[particle].y > 0) rise_[particle] += std::sqrt(lift_[particle] * pull * size_[particle]) * dt;
    for (const Candidate& pair : touches_) {
        if (!slips(pair.one) || !slips(pair.other) || swapped_[pair.one] || swapped_[pair.other]) continue;
        const Vec3 apart = at_[pair.other] - at_[pair.one];
        const float distance = math::length(apart), rest = (size_[pair.one] + size_[pair.other]) * 0.5f;
        if (!(distance > 0) || !(distance < rest * 1.1f)) continue;
        const float upward = -math::dot(apart, down) / distance;  // of the way from one to the other, up
        const bool one_under = upward > 0.5f, other_under = upward < -0.5f;
        const std::uint32_t lighter = own(pair.one) < own(pair.other) ? pair.one : pair.other;
        if (own(pair.one) == own(pair.other) || !((lighter == pair.one && one_under) || (lighter == pair.other && other_under)) || rise_[lighter] < size_[lighter]) continue;
        std::swap(at_[pair.one], at_[pair.other]);
        rise_[lighter] -= size_[lighter];
        swapped_[pair.one] = swapped_[pair.other] = 1;
    }
}

void Cloud::fly(std::span<const Surface> world, std::span<const Vec3> wind) {
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
    if (options_.touching) {
        settle(wind);
    } else {
        // ── fly, and meet the world ──
        const bool winds = wind.size() == size();
        for (std::size_t particle = 0; particle < size(); ++particle) {
            Vec3& speed = velocity_[particle];
            speed = speed + options_.gravity * dt;
            const Vec3 still = winds ? wind[particle] : Vec3{0, 0, 0};
            speed = speed - (speed - still) * (options_.drag * dt);
            const Vec3 from = at_[particle], to = from + speed * dt;
            if (const auto hit = first_hit(from, to)) {
                Vec3 normal = hit->second;
                if (math::dot(normal, speed) > 0) normal = normal * -1.0f;  // the side it came from
                const matter::MaterialId material = state_[particle];
                const float keeps = restitution ? static_cast<float>(program_->number(material, *restitution)) : 0.4f;
                const float rubs = friction ? static_cast<float>(program_->number(material, *friction)) : 0.3f;
                const float into = -math::dot(speed, normal);
                const Vec3 along = speed + normal * into;
                const Vec3 met = from + (to - from) * hit->first;
                landed_.push_back({id_[particle], material, into, met, normal});
                speed = along * (1 - rubs) + normal * (into * keeps);
                at_[particle] = met + normal * 1e-4f;
            } else {
                at_[particle] = to;
            }
        }
    }
    for (std::size_t particle = 0; particle < size(); ++particle) age_[particle] += dt;
    // ── what has lived its life is gone (the rest keep their order) ──
    const std::size_t before = size();
    kept_.clear();
    for (std::size_t particle = 0; particle < before; ++particle)
        if (age_[particle] < life_[particle]) kept_.push_back(particle);
    const std::vector<std::size_t>& from_place = kept_;
    if (from_place.size() != before) {
        const std::size_t kept = from_place.size();
        const auto keep = [&](auto& values) {
            for (std::size_t index = 0; index < kept; ++index) values[index] = values[from_place[index]];
            values.resize(kept);
        };
        keep(at_), keep(velocity_), keep(size_), keep(age_), keep(life_), keep(id_), keep(rise_);
        if (resting_.size() == before) keep(resting_);
        // In place: each value goes to a place no later than its own (kept ≤ before, index ≤ from_place[index]).
        for (std::uint32_t channel = 0; channel < channels_; ++channel)
            for (std::size_t index = 0; index < kept; ++index) state_[channel * kept + index] = state_[channel * before + from_place[index]];
        state_.resize(channels_ * kept);
    }
    ++steps_;
}

void Cloud::pair() {
    // Who is beside whom: every pair nearer than `near`, found by cells; then taken the nearest first (at one
    // distance, by their places) while both have room for one more.
    const std::size_t count = size();
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
}

void Cloud::stepped(std::span<const matter::Event> events, std::uint32_t first) {
    events_.clear();
    for (matter::Event event : events)
        if (event.instance >= first && event.instance < first + size()) event.instance -= first, events_.push_back(event);
}

std::expected<void, std::string> Cloud::step(std::span<const Surface> world) {
    fly(world);
    const std::size_t count = size();
    if (!count) return {};
    pair();
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
        for (std::uint32_t particle = 0; particle < count; ++particle) pairs_.push_back({particle, static_cast<std::uint32_t>(count + particle), options_.ambient_weight});
    }
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
    if (options_.touching) add(rise_);  // how far each lighter fluid is on its way up (taking room only)
    const std::array<std::uint64_t, 1> counted{steps_};
    add(counted);
    return hash.value();
}

}  // namespace xengine::particles
