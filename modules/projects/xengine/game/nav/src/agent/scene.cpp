module xengine.nav.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.json;

namespace xengine::nav::agent {
namespace {

std::string triple(Vec value) { return std::format("[{},{},{}]", value.x, value.y, value.z); }

// `[x, y, z]`, three finite numbers.
std::expected<std::optional<Vec>, std::string> vector_of(const mlibs::json::Node& node, std::string_view name) {
    if (!node) return std::nullopt;
    const auto parts = node.items();
    std::array<double, 3> values{};
    for (std::size_t index = 0; index < 3; ++index) {
        const auto value = parts.size() == 3 ? parts[index].number() : std::nullopt;
        if (!value || !std::isfinite(*value)) return std::unexpected(std::format("`{}` is [x, y, z], three numbers", name));
        values[index] = *value;
    }
    return Vec{static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2])};
}
std::expected<Vec, std::string> needed(const mlibs::agent::Request& request, std::string_view name) {
    const auto value = vector_of(request[name], name);
    if (!value) return std::unexpected(value.error());
    if (!*value) return std::unexpected(std::format("`{}` ([x, y, z]) is needed", name));
    return **value;
}

constexpr std::array<std::string_view, 6> goings{"idle", "walking", "arrived", "unreachable", "crossing", "off_mesh"};

std::string walker_json(const WalkerState& walker) {
    std::string ahead = "[";
    for (const Vec& corner : walker.ahead) ahead += (ahead.size() > 1 ? "," : "") + triple(corner);
    return std::format("{{\"id\":{},\"at\":{},\"velocity\":{},\"going\":\"{}\",\"target\":{},\"ahead\":{}]}}", walker.id.value, triple(walker.at), triple(walker.velocity),
                       goings[static_cast<std::size_t>(walker.going)], walker.target ? triple(*walker.target) : std::string("null"), ahead);
}

}  // namespace

Scene::Scene(World world, Options options) : options_(options), world_(std::move(world)) {
    options_.every = std::max<std::int64_t>(1, options_.every);
    checkpoints_.emplace(frame(), world_.save());
}

std::expected<std::uint32_t, std::string> Scene::give(World& world, const Input& input) {
    const auto failed = [](const Error& error) { return std::unexpected(error.text); };
    switch (input.kind) {
        case Input::Kind::build: {
            const auto built = world.rebuild(*input.ground, input.size);
            return built ? std::expected<std::uint32_t, std::string>(0) : failed(built.error());
        }
        case Input::Kind::add: {
            const auto id = world.add(input.walker);
            return id ? std::expected<std::uint32_t, std::string>(id->value) : failed(id.error());
        }
        case Input::Kind::remove: world.remove(WalkerId{input.id}); return 0;
        case Input::Kind::target: {
            const auto sent = world.target(WalkerId{input.id}, input.to);
            return sent ? std::expected<std::uint32_t, std::string>(0) : failed(sent.error());
        }
        case Input::Kind::stop: world.stop(WalkerId{input.id}); return 0;
        case Input::Kind::place: {
            const auto id = world.place(input.obstacle);
            return id ? std::expected<std::uint32_t, std::string>(id->value) : failed(id.error());
        }
        case Input::Kind::take: world.take(ObstacleId{input.id}); return 0;
        case Input::Kind::join: {
            const auto id = world.join(input.link);
            return id ? std::expected<std::uint32_t, std::string>(id->value) : failed(id.error());
        }
        case Input::Kind::unjoin: world.unjoin(LinkId{input.id}); return 0;
    }
    return 0;
}

std::expected<std::uint32_t, std::string> Scene::apply(Input input) {
    input.frame = frame();
    auto given = give(world_, input);
    if (given) given_.push_back(std::move(input));
    return given;
}

void Scene::step() {
    world_.step();
    const std::int64_t now = frame();
    if (now % options_.every == 0) checkpoints_.insert_or_assign(now, world_.save());
    // What is older than the history kept goes, but the checkpoint a frame within it is reached from stays.
    while (checkpoints_.size() > 1 && std::next(checkpoints_.begin())->first <= now - options_.keep) checkpoints_.erase(checkpoints_.begin());
    std::erase_if(given_, [&](const Input& input) { return input.frame < earliest(); });
}

bool Scene::seek(std::int64_t target) {
    if (target > frame() || target < earliest()) return false;
    const auto checkpoint = std::prev(checkpoints_.upper_bound(target));
    if (!world_.restore(checkpoint->second)) return false;
    for (std::int64_t at = checkpoint->first; at < target; ++at) {
        for (const Input& input : given_)
            if (input.frame == at) (void)give(world_, input);
        world_.step();
    }
    // What came after the frame is another branch's.
    std::erase_if(given_, [&](const Input& input) { return input.frame >= target; });
    checkpoints_.erase(checkpoints_.upper_bound(target), checkpoints_.end());
    return true;
}

std::string Scene::status() const {
    return std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"earliest\":{},\"latest\":{}}}", frame(), world_.state_hash(), earliest(), frame());
}

std::string Scene::observed(const World& world) const {
    std::array<int, goings.size()> counts{};
    for (const WalkerId walker : world.walkers()) ++counts[static_cast<std::size_t>(world.walker(walker)->going)];
    std::string by_going;
    for (std::size_t going = 0; going < goings.size(); ++going) by_going += std::format("{}\"{}\":{}", going ? "," : "", goings[going], counts[going]);
    return std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"tiles\":{},\"polygons\":{},\"walkers\":{},\"going\":{{{}}},\"obstacles\":{},\"links\":{}}}", world.steps(),
                       world.state_hash(), world.tiles(), world.polygons(), world.walkers().size(), by_going, world.obstacles().size(), world.links().size());
}

void Scene::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    const auto invalid = [&](std::string_view why) { answer.fail(openas::error_code::invalid_argument, why); };
    const auto steps_of = [&]() -> std::optional<std::int64_t> {
        const std::int64_t steps = request["steps"].integer().value_or(1);
        if (steps < 1 || steps > options_.most_steps) return std::nullopt;
        return steps;
    };
    const std::string no_steps = std::format("`steps` is 1 … {}", options_.most_steps);
    const auto id_of = [&](std::string_view name) -> std::optional<std::uint32_t> {
        const auto value = request[name].integer();
        if (!value || *value < 1 || *value > 0xfffffffe) return std::nullopt;
        return static_cast<std::uint32_t>(*value);
    };
    const auto walker_of = [&]() -> std::optional<WalkerId> {
        const auto id = id_of("walker");
        if (!id || !world_.walker(WalkerId{*id})) return std::nullopt;
        return WalkerId{*id};
    };
    const std::string no_walker = std::format("no walker {} in the world: nav.where lists them", request["walker"].integer().value_or(0));

    if (operation == "nav.observe") return answer.data(observed(world_));
    if (operation == "nav.where") {
        if (request["walker"]) {
            const auto walker = walker_of();
            if (!walker) return invalid(no_walker);
            return answer.data(walker_json(*world_.walker(*walker)));
        }
        const auto budget = request["budget"].integer();
        std::string out = "{\"walkers\":[";
        std::size_t shown = 0;
        const auto walkers = world_.walkers();
        for (; shown < walkers.size(); ++shown) {
            const std::string next = (shown ? "," : "") + walker_json(*world_.walker(walkers[shown]));
            // What closes the answer (`],"more":N}`) still has to fit.
            if (budget && out.size() + next.size() + 22 > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget))) break;
            out += next;
        }
        out += std::format("],\"more\":{}}}", walkers.size() - shown);
        if (budget && out.size() > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget)))
            return invalid(std::format("`budget` {}: the least this answers, with every walker left out, is {} bytes", *budget, out.size()));
        return answer.data(out);
    }
    if (operation == "nav.path") {
        const auto from = needed(request, "from"), to = needed(request, "to");
        if (!from) return invalid(from.error());
        if (!to) return invalid(to.error());
        const auto way = world_.path(*from, *to);
        if (!way) return answer.fail(openas::error_code::not_found, way.error().text + ": nav.nearest finds the mesh near a place");
        std::string corners = "[";
        for (std::size_t corner = 0; corner < way->corners.size(); ++corner)
            corners += std::format("{}{{\"at\":{},\"onto_link\":{}}}", corner ? "," : "", triple(way->corners[corner]), static_cast<bool>(way->onto_link[corner]));
        return answer.data(std::format("{{\"reaches\":{},\"length\":{},\"corners\":{}],\"polygons\":{}}}", way->reaches, way->length, corners, way->polygons.size()));
    }
    if (operation == "nav.nearest") {
        const auto at = needed(request, "at");
        if (!at) return invalid(at.error());
        const auto reach = vector_of(request["reach"], "reach");
        if (!reach) return invalid(reach.error());
        const auto found = world_.nearest(*at, reach->value_or(Vec{2, 4, 2}));
        if (!found) return answer.data("{\"found\":false}");
        return answer.data(std::format("{{\"found\":true,\"at\":{},\"polygon\":{}}}", triple(found->at), found->polygon));
    }
    if (operation == "nav.ray") {
        const auto from = needed(request, "from"), to = needed(request, "to");
        if (!from) return invalid(from.error());
        if (!to) return invalid(to.error());
        const auto hit = world_.ray(*from, *to);
        if (!hit) return answer.fail(openas::error_code::not_found, hit.error().text + ": nav.nearest finds the mesh near a place");
        return answer.data(std::format("{{\"clear\":{},\"fraction\":{},\"at\":{},\"normal\":{}}}", hit->clear, hit->fraction, triple(hit->at), triple(hit->normal)));
    }
    if (operation == "nav.build") {
        const auto points = request["points"].items(), corners = request["corners"].items();
        auto ground = std::make_shared<Ground>();
        for (const auto& point : points) {
            const auto value = vector_of(point, "points");
            if (!value || !*value) return invalid("`points` is a list of [x, y, z]");
            ground->points.push_back(**value);
        }
        for (const auto& corner : corners) {
            const auto value = corner.integer();
            if (!value || *value < 0 || static_cast<std::size_t>(*value) >= ground->points.size()) return invalid("`corners` are indices of `points`, three a triangle");
            ground->corners.push_back(static_cast<std::uint32_t>(*value));
        }
        Walker size;
        size.radius = static_cast<float>(request["radius"].number().value_or(size.radius));
        size.height = static_cast<float>(request["height"].number().value_or(size.height));
        size.climb = static_cast<float>(request["climb"].number().value_or(size.climb));
        size.slope = static_cast<float>(request["slope"].number().value_or(size.slope));
        const auto built = apply({.kind = Input::Kind::build, .ground = std::move(ground), .size = size});
        if (!built) return invalid(built.error());
        return answer.data(observed(world_));
    }
    if (operation == "nav.add") {
        const auto at = needed(request, "at");
        if (!at) return invalid(at.error());
        WalkerDef walker{.at = *at};
        walker.radius = static_cast<float>(request["radius"].number().value_or(walker.radius));
        walker.speed = static_cast<float>(request["speed"].number().value_or(walker.speed));
        const auto added = apply({.kind = Input::Kind::add, .walker = walker});
        if (!added) return invalid(added.error());
        return answer.data(std::format("{{\"walker\":{},\"frame\":{}}}", *added, frame()));
    }
    if (operation == "nav.remove") {
        const auto walker = walker_of();
        if (!walker) return invalid(no_walker);
        (void)apply({.kind = Input::Kind::remove, .id = walker->value});
        return answer.data(std::format("{{\"removed\":{},\"frame\":{}}}", walker->value, frame()));
    }
    if (operation == "nav.target") {
        const auto walker = walker_of();
        if (!walker) return invalid(no_walker);
        const auto to = vector_of(request["to"], "to");
        if (!to) return invalid(to.error());
        if (!*to) {
            (void)apply({.kind = Input::Kind::stop, .id = walker->value});
            return answer.data(std::format("{{\"walker\":{},\"stopped\":true,\"frame\":{}}}", walker->value, frame()));
        }
        const auto sent = apply({.kind = Input::Kind::target, .id = walker->value, .to = **to});
        if (!sent) return answer.fail(openas::error_code::not_found, sent.error() + ": nav.nearest finds the mesh near a place");
        const auto state = world_.walker(*walker);
        return answer.data(std::format("{{\"walker\":{},\"going\":\"{}\",\"target\":{},\"frame\":{}}}", walker->value, goings[static_cast<std::size_t>(state->going)],
                                       state->target ? triple(*state->target) : std::string("null"), frame()));
    }
    if (operation == "nav.obstacle") {
        if (request["remove"]) {
            const auto id = id_of("remove");
            if (!id || !world_.obstacle(ObstacleId{*id})) return invalid("`remove`: an obstacle's id (nav.observe counts them)");
            (void)apply({.kind = Input::Kind::take, .id = *id});
            return answer.data(observed(world_));
        }
        const auto at = needed(request, "at");
        if (!at) return invalid(at.error());
        const auto form = request["shape"].text().value_or("cylinder");
        Obstacle obstacle;
        if (form == "cylinder") {
            obstacle = Obstacle::cylinder(*at, static_cast<float>(request["radius"].number().value_or(0.5)), static_cast<float>(request["height"].number().value_or(2)));
        } else if (form == "box") {
            const auto half = needed(request, "half");
            if (!half) return invalid(half.error());
            obstacle = Obstacle::box(*at, *half);
        } else {
            return invalid("`shape` is cylinder (`radius`, `height`; `at` its base) or box (`half`; `at` its centre)");
        }
        const auto placed = apply({.kind = Input::Kind::place, .obstacle = obstacle});
        if (!placed) return invalid(placed.error());
        return answer.data(std::format("{{\"obstacle\":{},\"tiles\":{},\"polygons\":{}}}", *placed, world_.tiles(), world_.polygons()));
    }
    if (operation == "nav.link") {
        if (request["remove"]) {
            const auto id = id_of("remove");
            if (!id || !world_.link(LinkId{*id})) return invalid("`remove`: a link's id (nav.observe counts them)");
            (void)apply({.kind = Input::Kind::unjoin, .id = *id});
            return answer.data(observed(world_));
        }
        const auto from = needed(request, "from"), to = needed(request, "to");
        if (!from) return invalid(from.error());
        if (!to) return invalid(to.error());
        const Link link{.from = *from, .to = *to, .radius = static_cast<float>(request["radius"].number().value_or(0.5)), .both_ways = request["both_ways"].boolean().value_or(true)};
        const auto joined = apply({.kind = Input::Kind::join, .link = link});
        if (!joined) return invalid(joined.error());
        return answer.data(std::format("{{\"link\":{},\"tiles\":{},\"polygons\":{}}}", *joined, world_.tiles(), world_.polygons()));
    }
    if (operation == "nav.step") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        for (std::int64_t played = 0; played < *steps; ++played) step();
        return answer.data(status());
    }
    if (operation == "nav.seek") {
        const auto target = request["frame"].integer();
        if (!target) return invalid("`frame`: the frame to return to");
        if (!seek(*target)) return answer.fail(openas::error_code::stale, std::format("frame {} is outside the history kept ({} … {})", *target, earliest(), frame()));
        return answer.data(status());
    }
    if (operation == "nav.branch") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        auto future = world_.branch();  // a world of its own: this one is not touched
        if (!future) return answer.fail(openas::error_code::failed, future.error().text);
        if (request["walker"]) {
            const auto walker = walker_of();
            if (!walker) return invalid(no_walker);
            const auto to = needed(request, "to");
            if (!to) return invalid(to.error());
            if (const auto sent = future->target(*walker, *to); !sent) return answer.fail(openas::error_code::not_found, sent.error().text);
        }
        for (std::int64_t played = 0; played < *steps; ++played) future->step();
        return answer.data(observed(*future));
    }
}

}  // namespace xengine::nav::agent
