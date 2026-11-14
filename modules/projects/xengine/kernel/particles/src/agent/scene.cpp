module xengine.particles.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.json;
import mlibs.math;

namespace xengine::particles::agent {
namespace {

std::string triple(Vec3 value) { return std::format("[{},{},{}]", value.x, value.y, value.z); }

// `[x, y, z]`, three finite numbers.
std::expected<std::optional<Vec3>, std::string> vector_of(const mlibs::agent::Request& request, std::string_view name) {
    const auto node = request[name];
    if (!node) return std::nullopt;
    const auto parts = node.items();
    std::array<double, 3> values{};
    for (std::size_t index = 0; index < 3; ++index) {
        const auto value = parts.size() == 3 ? parts[index].number() : std::nullopt;
        if (!value || !std::isfinite(*value)) return std::unexpected(std::format("`{}` is [x, y, z], three numbers", name));
        values[index] = *value;
    }
    return Vec3{static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2])};
}

constexpr std::int64_t most_listed = 1024, most_emitted = 4096;

}  // namespace

Scene::Scene(Cloud cloud, std::span<const Surface> world, Options options)
    : options_(options), cloud_(std::move(cloud)), world_(world), temperature_(cloud_.program().channel("temperature")) {
    options_.every = std::max<std::int64_t>(1, options_.every);
    copies_.emplace(frame(), cloud_);
}

std::expected<void, std::string> Scene::advance(Cloud& cloud, std::span<const Emission> emissions) const {
    spawns_.clear();
    for (const Emission& emission : emissions)
        for (std::uint32_t index = 0; index < emission.count; ++index) {
            Spawn spawn = emission.spawn;
            if (emission.spread > 0) {
                // A number of (frame, the emission's place, the particle's): the same spread in a replay and a branch.
                mlibs::math::SplitMix64 stream{static_cast<std::uint64_t>(emission.frame) * 0x9e3779b97f4a7c15ull ^ (std::uint64_t{emission.order} << 32 | index)};
                const auto part = [&] { return static_cast<float>(mlibs::math::unit24(stream.next()) * 2 - 1) * emission.spread; };
                const float x = part(), y = part(), z = part();
                spawn.velocity = spawn.velocity + Vec3{x, y, z};
            }
            spawns_.push_back(spawn);
        }
    cloud.emit(spawns_);
    return cloud.step(world_);
}

std::expected<void, std::string> Scene::step() {
    const auto stepped = advance(cloud_, pending_);
    given_.insert(given_.end(), pending_.begin(), pending_.end());
    pending_.clear();
    if (!stepped) return stepped;
    const std::int64_t now = frame();
    if (now % options_.every == 0) copies_.insert_or_assign(now, cloud_);
    // What is older than the history kept goes, but the copy a frame within it is reached from stays.
    while (copies_.size() > 1 && std::next(copies_.begin())->first <= now - options_.keep) copies_.erase(copies_.begin());
    std::erase_if(given_, [&](const Emission& emission) { return emission.frame < earliest(); });
    return {};
}

bool Scene::seek(std::int64_t target) {
    if (target > frame() || target < earliest()) return false;
    const auto copy = std::prev(copies_.upper_bound(target));
    Cloud cloud = copy->second;
    std::vector<Emission> those;
    for (std::int64_t at = copy->first; at < target; ++at) {
        those.clear();
        for (const Emission& emission : given_)
            if (emission.frame == at) those.push_back(emission);
        if (!advance(cloud, those)) return false;
    }
    cloud_ = std::move(cloud);
    // What came after the frame is another branch's.
    std::erase_if(given_, [&](const Emission& emission) { return emission.frame >= target; });
    copies_.erase(copies_.upper_bound(target), copies_.end());
    pending_.clear();
    return true;
}

std::string Scene::status() const {
    return std::format("{{\"frame\":{},\"fingerprint\":\"{:016x}\",\"particles\":{},\"earliest\":{},\"latest\":{}}}", frame(), cloud_.fingerprint(), cloud_.size(), earliest(),
                       frame());
}

std::string Scene::particle_json(const Cloud& cloud, std::size_t particle) const {
    const auto& materials = cloud.program().materials();
    const auto material = cloud.material(particle);
    std::string out = std::format("{{\"id\":{},\"material\":\"{}\",\"at\":{},\"velocity\":{},\"age\":{},\"left\":{}", cloud.ids()[particle],
                                  material < materials.size() ? materials[material].name : std::string("?"), triple(cloud.at()[particle]),
                                  triple(cloud.velocity()[particle]), cloud.ages()[particle], cloud.lives()[particle] - cloud.ages()[particle]);
    if (temperature_) out += std::format(",\"kelvin\":{}", cloud.value(*temperature_, particle));
    return out + "}";
}

std::string Scene::observed(const Cloud& cloud, std::optional<std::int64_t> budget) const {
    const auto& materials = cloud.program().materials();
    std::vector<std::size_t> counts(materials.size(), 0);
    for (std::size_t particle = 0; particle < cloud.size(); ++particle)
        if (cloud.material(particle) < counts.size()) ++counts[cloud.material(particle)];
    std::string out = std::format("{{\"frame\":{},\"fingerprint\":\"{:016x}\",\"particles\":{},\"materials\":{{", cloud.steps(), cloud.fingerprint(), cloud.size());
    bool first = true;
    for (std::size_t material = 0; material < counts.size(); ++material)
        if (counts[material]) out += std::format("{}\"{}\":{}", first ? "" : ",", materials[material].name, counts[material]), first = false;
    out += std::format("}},\"events\":{},\"struck\":[", cloud.events().size());
    // Fastest first (at one speed, in the cloud's order).
    std::vector<Landed> struck(cloud.landed().begin(), cloud.landed().end());
    std::ranges::stable_sort(struck, std::ranges::greater{}, &Landed::speed);
    std::size_t shown = 0;
    for (; shown < struck.size(); ++shown) {
        const Landed& one = struck[shown];
        const std::string next = std::format("{}{{\"id\":{},\"material\":\"{}\",\"speed\":{},\"at\":{},\"normal\":{}}}", shown ? "," : "", one.particle,
                                             one.material < materials.size() ? materials[one.material].name : std::string("?"), one.speed, triple(one.at), triple(one.normal));
        // What closes the answer (`],"more":N}`) still has to fit.
        if (budget && out.size() + next.size() + 22 > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget))) break;
        out += next;
    }
    return out + std::format("],\"more\":{}}}", struck.size() - shown);
}

void Scene::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    const auto invalid = [&](std::string_view why, std::string_view next = {}) { answer.fail(openas::error_code::invalid_argument, why, openas::exit_code::failed, next); };
    const auto steps_of = [&]() -> std::optional<std::int64_t> {
        const std::int64_t steps = request["steps"].integer().value_or(1);
        if (steps < 1 || steps > options_.most_steps) return std::nullopt;
        return steps;
    };
    const std::string no_steps = std::format("`steps` is 1 … {}", options_.most_steps);
    const auto most_of = [&]() -> std::optional<std::size_t> {
        const std::int64_t most = request["most"].integer().value_or(64);
        if (most < 1 || most > most_listed) return std::nullopt;
        return static_cast<std::size_t>(most);
    };
    const std::string no_most = std::format("`most` is 1 … {}", most_listed);
    // An emission from the request: `at` and `material` (needed, or neither when not), the rest optional.
    const auto emission_of = [&](bool needed) -> std::expected<std::optional<Emission>, std::string> {
        const auto at = vector_of(request, "at");
        if (!at) return std::unexpected(at.error());
        const auto material_name = request["material"].text();
        if (!*at && !material_name && !needed) return std::nullopt;
        if (!*at || !material_name) return std::unexpected(std::string("`at` ([x, y, z]) and `material` (a material's name) together"));
        const auto material = cloud_.program().material(*material_name);
        if (!material) return std::unexpected(std::format("no material `{}` in the world's definition of matter", *material_name));
        const auto velocity = vector_of(request, "velocity");
        if (!velocity) return std::unexpected(velocity.error());
        const double life = request["life"].number().value_or(1), size = request["size"].number().value_or(0.02), spread = request["spread"].number().value_or(0);
        if (!std::isfinite(life) || life <= 0) return std::unexpected(std::string("`life` is seconds above 0"));
        if (!std::isfinite(size) || size <= 0) return std::unexpected(std::string("`size` is metres above 0"));
        if (!std::isfinite(spread) || spread < 0) return std::unexpected(std::string("`spread` is m/s, not below 0"));
        const std::int64_t count = request["count"].integer().value_or(1);
        if (count < 1 || count > most_emitted) return std::unexpected(std::format("`count` is 1 … {}", most_emitted));
        Spawn spawn{.at = **at, .velocity = velocity->value_or(Vec3{0, 0, 0}), .size = static_cast<float>(size), .life = static_cast<float>(life), .material = *material};
        if (const auto kelvin = request["kelvin"].number()) {
            if (!std::isfinite(*kelvin) || *kelvin < 0) return std::unexpected(std::string("`kelvin` is a temperature, not below 0"));
            spawn.kelvin = static_cast<float>(*kelvin);
        }
        return Emission{.frame = frame(), .order = static_cast<std::uint32_t>(pending_.size()), .count = static_cast<std::uint32_t>(count), .spawn = spawn,
                        .spread = static_cast<float>(spread)};
    };

    if (operation == "particles.observe") {
        const auto budget = request["budget"].integer();
        const std::string seen = observed(cloud_, budget);
        if (budget && seen.size() > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget)))
            return invalid(std::format("`budget` {}: the least this answers, with every strike left out, is {} bytes", *budget, seen.size()));
        return answer.data(seen);
    }
    if (operation == "particles.list") {
        const std::int64_t first = request["first"].integer().value_or(0);
        const auto most = most_of();
        if (first < 0) return invalid("`first` is a place in the order, from 0");
        if (!most) return invalid(no_most);
        std::string out = std::format("{{\"frame\":{},\"particles\":{},\"list\":[", frame(), cloud_.size());
        const std::size_t from = std::min(static_cast<std::size_t>(first), cloud_.size()), to = std::min(from + *most, cloud_.size());
        for (std::size_t particle = from; particle < to; ++particle) out += (particle == from ? "" : ",") + particle_json(cloud_, particle);
        return answer.data(out + std::format("],\"more\":{}}}", cloud_.size() - to));
    }
    if (operation == "particles.near") {
        const auto at = vector_of(request, "at");
        if (!at || !*at) return invalid("`at` is [x, y, z], three numbers");
        const double radius = request["radius"].number().value_or(1);
        if (!std::isfinite(radius) || radius <= 0) return invalid("`radius` is metres above 0");
        const auto most = most_of();
        if (!most) return invalid(no_most);
        std::vector<std::pair<float, std::size_t>> found;
        const float reach = static_cast<float>(radius * radius);
        for (std::size_t particle = 0; particle < cloud_.size(); ++particle) {
            const Vec3 apart = cloud_.at()[particle] - **at;
            const float squared = mlibs::math::dot(apart, apart);
            if (squared <= reach) found.emplace_back(squared, particle);
        }
        std::ranges::sort(found);
        std::string out = std::format("{{\"frame\":{},\"found\":{},\"near\":[", frame(), found.size());
        const std::size_t shown = std::min(found.size(), *most);
        for (std::size_t index = 0; index < shown; ++index) out += (index ? "," : "") + particle_json(cloud_, found[index].second);
        return answer.data(out + std::format("],\"more\":{}}}", found.size() - shown));
    }
    if (operation == "particles.emit") {
        const auto emission = emission_of(true);
        if (!emission) return invalid(emission.error(), R"({"op":"particles.observe"})");
        pending_.push_back(**emission);
        return answer.data(std::format("{{\"effective\":{},\"frame\":{},\"count\":{}}}", frame() + 1, frame(), (*emission)->count));
    }
    if (operation == "particles.step") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        for (std::int64_t played = 0; played < *steps; ++played)
            if (const auto stepped = step(); !stepped) return answer.fail(openas::error_code::failed, stepped.error());
        return answer.data(status());
    }
    if (operation == "particles.seek") {
        const auto target = request["frame"].integer();
        if (!target) return invalid("`frame`: the frame to return to");
        if (!seek(*target))
            return answer.fail(openas::error_code::stale, std::format("frame {} is outside the history kept ({} … {})", *target, earliest(), frame()), openas::exit_code::failed,
                               std::format("{{\"op\":\"particles.seek\",\"frame\":{}}}", earliest()));
        return answer.data(status());
    }
    if (operation == "particles.branch") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        const auto emission = emission_of(false);
        if (!emission) return invalid(emission.error());
        Cloud future = cloud_;  // a cloud of its own: this one is not touched
        std::vector<Emission> first = pending_;
        if (*emission) first.push_back(**emission);
        for (std::int64_t played = 0; played < *steps; ++played)
            if (const auto stepped = advance(future, played ? std::span<const Emission>{} : std::span<const Emission>{first}); !stepped)
                return answer.fail(openas::error_code::failed, stepped.error());
        return answer.data(observed(future, std::nullopt));
    }
    invalid(std::format("`{}` is not one of particles' operations", operation), R"({"op":"describe"})");
}

}  // namespace xengine::particles::agent
