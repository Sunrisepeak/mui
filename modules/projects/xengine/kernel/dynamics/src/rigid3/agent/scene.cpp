module xengine.dynamics.rigid3.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.json;

namespace xengine::dynamics::rigid3::agent {
namespace {

std::string triple(Vec value) { return std::format("[{},{},{}]", value.x, value.y, value.z); }

// `[x, y, z]`, three finite numbers.
std::expected<std::optional<Vec>, std::string> vector_of(const mlibs::agent::Request& request, std::string_view name) {
    const auto node = request[name];
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

constexpr std::array<std::string_view, 6> joint_forms{"fixed", "hinge", "slider", "distance", "point", "cone"};
constexpr std::array<std::string_view, 3> motor_modes{"off", "velocity", "position"};
constexpr std::array<std::string_view, 4> grounds{"on_ground", "on_steep_ground", "not_supported", "in_air"};

std::string joint_json(const JointState& joint) {
    return std::format("{{\"id\":{},\"form\":\"{}\",\"one\":{},\"other\":{},\"value\":{},\"motor\":\"{}\",\"target\":{}}}", joint.id.value,
                       joint_forms[static_cast<std::size_t>(joint.form)], joint.one.value, joint.other.value, joint.value, motor_modes[static_cast<std::size_t>(joint.drive)],
                       joint.target);
}
std::string character_json(const CharacterState& character) {
    return std::format("{{\"id\":{},\"at\":{},\"velocity\":{},\"walk\":{},\"ground\":\"{}\",\"standing_on\":{},\"ground_at\":{},\"ground_normal\":{}}}", character.id.value,
                       triple(character.at), triple(character.velocity), triple(character.walk), grounds[static_cast<std::size_t>(character.ground)],
                       character.standing_on.value, triple(character.ground_point), triple(character.ground_normal));
}

}  // namespace

Scene::Scene(World world, Options options) : options_(options), world_(std::move(world)) {
    options_.every = std::max<std::int64_t>(1, options_.every);
    checkpoints_.emplace(frame(), world_.save());
}

void Scene::give(World& world, const Input& input) {
    switch (input.kind) {
        case Input::Kind::push: world.push(input.body, input.impulse, input.at); break;
        case Input::Kind::drive: (void)world.drive(input.joint, input.mode, input.target); break;
        case Input::Kind::walk: world.walk(input.character, input.velocity); break;
        case Input::Kind::jump: world.jump(input.character, input.speed); break;
    }
}

void Scene::step() {
    for (const Input& push : pending_) give(world_, push), given_.push_back(push);
    pending_.clear();
    world_.step();
    const std::int64_t now = frame();
    if (now % options_.every == 0) checkpoints_.insert_or_assign(now, world_.save());
    // What is older than the history kept goes, but the checkpoint a frame within it is reached from stays.
    while (checkpoints_.size() > 1 && std::next(checkpoints_.begin())->first <= now - options_.keep) checkpoints_.erase(checkpoints_.begin());
    std::erase_if(given_, [&](const Input& push) { return push.frame < earliest(); });
}

bool Scene::seek(std::int64_t target) {
    if (target > frame() || target < earliest()) return false;
    const auto checkpoint = std::prev(checkpoints_.upper_bound(target));
    if (!world_.restore(checkpoint->second)) return false;
    for (std::int64_t at = checkpoint->first; at < target; ++at) {
        for (const Input& push : given_)
            if (push.frame == at) give(world_, push);
        world_.step();
    }
    // What came after the frame is another branch's.
    std::erase_if(given_, [&](const Input& push) { return push.frame >= target; });
    checkpoints_.erase(checkpoints_.upper_bound(target), checkpoints_.end());
    pending_.clear();
    return true;
}

std::string Scene::status() const {
    return std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"earliest\":{},\"latest\":{}}}", frame(), world_.state_hash(), earliest(), frame());
}

std::string Scene::observed(const World& world, std::optional<std::int64_t> budget) const {
    const auto bodies = world.bodies();
    const auto awake = std::ranges::count_if(bodies, [&](BodyId body) {
        const auto pose = world.pose(body);
        return pose && pose->awake;
    });
    std::string out = std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"bodies\":{},\"awake\":{},\"joints\":{},\"characters\":{},\"contacts\":[", world.steps(), world.state_hash(),
                                  bodies.size(), awake, world.joints().size(), world.characters().size());
    const auto contacts = world.contacts();
    std::size_t shown = 0;
    for (; shown < contacts.size(); ++shown) {
        const Contact& contact = contacts[shown];
        std::string next = std::format("{}{{\"one\":{},\"other\":{},\"began\":{}", shown ? "," : "", contact.one.value, contact.other.value, contact.began);
        if (contact.sensor) next += ",\"sensor\":true";
        if (contact.began) next += std::format(",\"at\":{},\"normal\":{},\"approach\":{}", triple(contact.point), triple(contact.normal), contact.approach);
        next += "}";
        // What closes the answer (`],"more":N}`) still has to fit.
        if (budget && out.size() + next.size() + 22 > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget))) break;
        out += next;
    }
    return out + std::format("],\"more\":{}}}", contacts.size() - shown);
}

void Scene::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    const auto invalid = [&](std::string_view why) { answer.fail(openas::error_code::invalid_argument, why); };
    const auto steps_of = [&]() -> std::optional<std::int64_t> {
        const std::int64_t steps = request["steps"].integer().value_or(1);
        if (steps < 1 || steps > options_.most_steps) return std::nullopt;
        return steps;
    };
    const std::string no_steps = std::format("`steps` is 1 … {}", options_.most_steps);
    // `body` (an id of the world's) and `impulse`; `at` when given.
    const auto push_of = [&](bool needed) -> std::expected<std::optional<Input>, std::string> {
        const auto body = request["body"].integer();
        const auto impulse = vector_of(request, "impulse");
        if (!impulse) return std::unexpected(impulse.error());
        if (!body && !*impulse && !needed) return std::nullopt;
        if (!body || !*impulse) return std::unexpected(std::string("`body` (an id) and `impulse` ([x, y, z]) together"));
        const BodyId id{static_cast<std::uint32_t>(*body)};
        if (*body < 0 || *body > 0xfffffffe || !world_.has(id))
            return std::unexpected(std::format("no body {} in the world: rigid3.bodies lists them", *body));
        const auto at = vector_of(request, "at");
        if (!at) return std::unexpected(at.error());
        return Input{.kind = Input::Kind::push, .frame = frame(), .body = id, .impulse = **impulse, .at = *at};
    };

    if (operation == "rigid3.observe") {
        const auto budget = request["budget"].integer();
        const std::string seen = observed(world_, budget);
        if (budget && seen.size() > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget)))
            return invalid(std::format("`budget` {}: the least this answers, with every contact left out, is {} bytes", *budget, seen.size()));
        return answer.data(seen);
    }
    if (operation == "rigid3.bodies") {
        std::string out = "{\"bodies\":[";
        bool first = true;
        for (const BodyId body : world_.bodies()) {
            const auto pose = world_.pose(body);
            if (!pose) continue;
            out += std::format("{}{{\"id\":{},\"mass\":{},\"at\":{},\"turn\":[{},{},{},{}],\"velocity\":{},\"spin\":{},\"awake\":{}}}", first ? "" : ",", body.value, pose->mass,
                               triple(pose->at), pose->turn.x, pose->turn.y, pose->turn.z, pose->turn.w, triple(pose->velocity), triple(pose->spin), pose->awake);
            first = false;
        }
        return answer.data(out + "]}");
    }
    if (operation == "rigid3.ray") {
        const auto from = vector_of(request, "from"), to = vector_of(request, "to");
        if (!from || !to || !*from || !*to) return invalid("`from` and `to` are [x, y, z], three numbers each");
        const auto hit = world_.ray(**from, **to);
        if (!hit) return answer.data("{\"hit\":false}");
        return answer.data(std::format("{{\"hit\":true,\"body\":{},\"fraction\":{},\"at\":{},\"normal\":{}}}", hit->body.value, hit->fraction, triple(hit->point), triple(hit->normal)));
    }
    if (operation == "rigid3.cast") {
        const auto from = vector_of(request, "from"), to = vector_of(request, "to");
        if (!from || !to || !*from || !*to) return invalid("`from` and `to` are [x, y, z], three numbers each");
        const auto form = request["shape"].text().value_or("sphere");
        Shape shape = Shape::sphere(static_cast<float>(request["radius"].number().value_or(0.5)));
        if (form == "box") {
            const auto half = vector_of(request, "half");
            if (!half || !*half) return invalid("a box's `half` is [x, y, z], its half extents");
            shape = Shape::box(**half);
        } else if (form == "capsule") {
            shape = Shape::capsule(static_cast<float>(request["half_height"].number().value_or(0.5)), shape.radius);
        } else if (form != "sphere") {
            return invalid("`shape` is sphere (`radius`), box (`half`) or capsule (`half_height`, `radius`)");
        }
        if (!(shape.radius > 0) || !(shape.half_height >= 0) || !(shape.half.x > 0 && shape.half.y > 0 && shape.half.z > 0)) return invalid("a shape's sizes are above 0");
        const auto hit = world_.cast(shape, {}, **from, **to);
        if (!hit) return answer.data("{\"hit\":false}");
        return answer.data(std::format("{{\"hit\":true,\"body\":{},\"fraction\":{},\"at\":{},\"normal\":{}}}", hit->body.value, hit->fraction, triple(hit->point), triple(hit->normal)));
    }
    if (operation == "rigid3.joints") {
        std::string out = "{\"joints\":[";
        for (const JointId joint : world_.joints()) out += (out.back() == '[' ? "" : ",") + joint_json(*world_.joint(joint));
        return answer.data(out + "]}");
    }
    if (operation == "rigid3.drive") {
        const auto joint = request["joint"].integer();
        const JointId id{static_cast<std::uint32_t>(joint.value_or(0))};
        if (!joint || *joint < 1 || *joint > 0xfffffffe || !world_.joint(id)) return invalid(std::format("no joint {} in the world: rigid3.joints lists them", joint.value_or(0)));
        const JointState state = *world_.joint(id);
        if (state.form != Joint::Form::hinge && state.form != Joint::Form::slider)
            return invalid(std::format("joint {} is a {}: only a hinge and a slider have a motor", *joint, joint_forms[static_cast<std::size_t>(state.form)]));
        const auto mode = request["mode"].text().value_or("velocity");
        const auto found_mode = std::ranges::find(motor_modes, mode);
        if (found_mode == motor_modes.end()) return invalid("`mode` is off, velocity (`target` rad/s or m/s) or position (`target` rad or m)");
        const auto target = request["target"].number().value_or(0);
        if (!std::isfinite(target)) return invalid("`target` is a finite number");
        pending_.push_back({.kind = Input::Kind::drive, .frame = frame(), .joint = id, .mode = static_cast<Motor::Mode>(found_mode - motor_modes.begin()), .target = static_cast<float>(target)});
        return answer.data(std::format("{{\"effective\":{},\"frame\":{},\"joint\":{}}}", frame() + 1, frame(), *joint));
    }
    // `character`: an id of the world's.
    const auto character_of = [&]() -> std::optional<CharacterId> {
        const auto character = request["character"].integer();
        const CharacterId id{static_cast<std::uint32_t>(character.value_or(0))};
        if (!character || *character < 1 || *character > 0xfffffffe || !world_.character(id)) return std::nullopt;
        return id;
    };
    const std::string no_character = std::format("no character {} in the world: rigid3.where lists them", request["character"].integer().value_or(0));
    if (operation == "rigid3.move") {
        const auto character = character_of();
        if (!character) return invalid(no_character);
        const auto velocity = vector_of(request, "velocity");
        if (!velocity || !*velocity) return invalid("`velocity` is [x, y, z], the walk in m/s (y is left to gravity and jumps)");
        pending_.push_back({.kind = Input::Kind::walk, .frame = frame(), .character = *character, .velocity = **velocity});
        return answer.data(std::format("{{\"effective\":{},\"frame\":{},\"character\":{}}}", frame() + 1, frame(), character->value));
    }
    if (operation == "rigid3.jump") {
        const auto character = character_of();
        if (!character) return invalid(no_character);
        const auto speed = request["speed"].number().value_or(5);
        if (!std::isfinite(speed) || speed < 0) return invalid("`speed` is m/s upward, not below 0");
        pending_.push_back({.kind = Input::Kind::jump, .frame = frame(), .character = *character, .speed = static_cast<float>(speed)});
        // A jump leaves the ground only from the ground: rigid3.ground says whether it stands.
        return answer.data(std::format("{{\"effective\":{},\"frame\":{},\"character\":{},\"ground\":\"{}\"}}", frame() + 1, frame(), character->value,
                                       grounds[static_cast<std::size_t>(world_.character(*character)->ground)]));
    }
    if (operation == "rigid3.where") {
        if (request["character"]) {
            const auto character = character_of();
            if (!character) return invalid(no_character);
            return answer.data(character_json(*world_.character(*character)));
        }
        std::string out = "{\"characters\":[";
        for (const CharacterId character : world_.characters()) out += (out.back() == '[' ? "" : ",") + character_json(*world_.character(character));
        return answer.data(out + "]}");
    }
    if (operation == "rigid3.ground") {
        std::string out = std::format("{{\"frame\":{},\"grounds\":[", frame());
        for (const CharacterState& state : world_.grounds())
            out += std::format("{}{{\"character\":{},\"ground\":\"{}\",\"standing_on\":{},\"normal\":{}}}", out.back() == '[' ? "" : ",", state.id.value,
                               grounds[static_cast<std::size_t>(state.ground)], state.standing_on.value, triple(state.ground_normal));
        return answer.data(out + "]}");
    }
    if (operation == "rigid3.push") {
        const auto push = push_of(true);
        if (!push) return invalid(push.error());
        pending_.push_back(**push);
        // The step it is given in makes the frame after this one.
        return answer.data(std::format("{{\"effective\":{},\"frame\":{},\"body\":{}}}", frame() + 1, frame(), (*push)->body.value));
    }
    if (operation == "rigid3.step") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        for (std::int64_t played = 0; played < *steps; ++played) step();
        return answer.data(status());
    }
    if (operation == "rigid3.seek") {
        const auto target = request["frame"].integer();
        if (!target) return invalid("`frame`: the frame to return to");
        if (!seek(*target)) return answer.fail(openas::error_code::stale, std::format("frame {} is outside the history kept ({} … {})", *target, earliest(), frame()));
        return answer.data(status());
    }
    if (operation == "rigid3.branch") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        const auto push = push_of(false);
        if (!push) return invalid(push.error());
        auto future = world_.branch();  // a world of its own: this one is not touched
        if (!future) return answer.fail(openas::error_code::failed, future.error().text);
        for (const Input& pending : pending_) give(*future, pending);
        if (*push) give(*future, **push);
        for (std::int64_t played = 0; played < *steps; ++played) future->step();
        return answer.data(observed(*future, std::nullopt));
    }
}

}  // namespace xengine::dynamics::rigid3::agent
