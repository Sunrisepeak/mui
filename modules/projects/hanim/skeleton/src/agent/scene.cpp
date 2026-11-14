module hanim.skeleton.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.json;

namespace hanim::skeleton::agent {
namespace {

std::string triple(Vec3 value) { return std::format("[{},{},{}]", value.x, value.y, value.z); }
std::string quad(const Quat& value) { return std::format("[{},{},{},{}]", value.x, value.y, value.z, value.w); }
std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char letter : text) {
        if (letter == '"' || letter == '\\') out += '\\';
        out += letter;
    }
    return out + "\"";
}
Vec3 place_of(const Mat4& matrix) { return {matrix.at(3, 0), matrix.at(3, 1), matrix.at(3, 2)}; }

constexpr std::array<std::string_view, 4> kinds{"above", "below", "trigger", "ended"};

std::string character_json(const Crowd& crowd, std::size_t index) {
    const Character& character = crowd.characters()[index];
    const StateMachine& stage = character.stage;
    const Machine& machine = stage.machine();
    std::string parameters = "{";
    for (std::size_t parameter = 0; parameter < machine.parameters.size(); ++parameter)
        parameters += std::format("{}{}:{}", parameter ? "," : "", json_text(machine.parameters[parameter]), stage.parameters()[parameter]);
    parameters += "}";
    std::string last = "null";
    if (stage.last().to != any_state) {
        std::string why = "[";
        if (stage.last().transition != any_state)
            for (const Condition& condition : machine.transitions[stage.last().transition].when)
                why += std::format("{}{{\"kind\":\"{}\",\"parameter\":{},\"value\":{}}}", why.size() > 1 ? "," : "", kinds[static_cast<std::size_t>(condition.kind)],
                                   condition.kind == Condition::Kind::ended ? std::string("null") : json_text(machine.parameters[condition.parameter]), condition.value);
        why += "]";
        last = std::format("{{\"from\":{},\"to\":{},\"step\":{},\"by\":\"{}\",\"when\":{}}}", stage.last().from == any_state ? std::string("null") : json_text(machine.states[stage.last().from].name),
                           json_text(machine.states[stage.last().to].name), stage.last().step, stage.last().transition == any_state ? "play" : "transition", why);
    }
    std::string layers = "[";
    const auto& rig = *crowd.rig();
    for (std::size_t layer = 0; layer < Player::layers; ++layer) {
        const Layer& on = character.player.layered()[layer];
        if (on.weight <= 0) continue;
        layers += std::format("{}{{\"layer\":{},\"clip\":{},\"weight\":{},\"additive\":{},\"mask\":{},\"time\":{}}}", layers.size() > 1 ? "," : "", layer, json_text(rig.clips[on.clip].name),
                              on.weight, on.additive, on.mask ? json_text(rig.skeleton.names()[*on.mask]) : std::string("null"), on.time);
    }
    layers += "]";
    std::string playing = "[";
    const Choice& chosen = character.player.chosen();
    for (std::uint32_t item = 0; item < chosen.count; ++item)
        playing += std::format("{}{{\"clip\":{},\"time\":{},\"weight\":{}}}", item ? "," : "", json_text(rig.clips[chosen.playing[item].clip].name), chosen.playing[item].time, chosen.playing[item].weight);
    playing += "]";
    const Motion& placed = character.player.placed();
    return std::format("{{\"character\":{},\"state\":{},\"played\":{},\"faded\":{},\"playing\":{},\"last\":{},\"parameters\":{},\"layers\":{},\"at\":{},\"turned\":{}}}", index,
                       json_text(machine.states[stage.current()].name), stage.played(), stage.faded(), playing, last, parameters, layers, triple(placed.moved), quad(placed.turned));
}

}  // namespace

Scene::Scene(Crowd crowd, Options options) : options_(options), crowd_(std::move(crowd)) {
    options_.every = std::max<std::int64_t>(1, options_.every);
    checkpoints_.emplace(frame(), crowd_.save());
}

std::expected<void, Error> Scene::give(Crowd& crowd, const Input& input) {
    if (input.character >= crowd.characters().size()) return std::unexpected(Error{std::format("no character {}", input.character)});
    Character& character = crowd.characters()[input.character];
    switch (input.kind) {
        case Input::Kind::play: return character.stage.go(input.name, input.value);
        case Input::Kind::set: return character.stage.set(input.name, input.value);
        case Input::Kind::blend: return character.player.layer(input.layer, input.blend);
    }
    return {};
}

std::expected<void, Error> Scene::apply(Input input) {
    input.frame = frame();
    auto given = give(crowd_, input);
    if (given) given_.push_back(std::move(input));
    return given;
}

void Scene::step() {
    crowd_.step();
    const std::int64_t now = frame();
    if (now % options_.every == 0) checkpoints_.insert_or_assign(now, crowd_.save());
    while (checkpoints_.size() > 1 && std::next(checkpoints_.begin())->first <= now - options_.keep) checkpoints_.erase(checkpoints_.begin());
    std::erase_if(given_, [&](const Input& input) { return input.frame < earliest(); });
}

bool Scene::seek(std::int64_t target) {
    if (target > frame() || target < earliest()) return false;
    const auto checkpoint = std::prev(checkpoints_.upper_bound(target));
    if (!crowd_.restore(checkpoint->second)) return false;
    for (std::int64_t at = checkpoint->first; at < target; ++at) {
        for (const Input& input : given_)
            if (input.frame == at) (void)give(crowd_, input);
        crowd_.step();
    }
    // What came after the frame (inputs given at it too) is another branch's.
    std::erase_if(given_, [&](const Input& input) { return input.frame >= target; });
    checkpoints_.erase(checkpoints_.upper_bound(target), checkpoints_.end());
    return true;
}

std::string Scene::status() const {
    return std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"earliest\":{},\"latest\":{}}}", frame(), crowd_.state_hash(), earliest(), frame());
}

std::string Scene::observed(const Crowd& crowd, std::optional<std::int64_t> budget) const {
    const Rig& rig = *crowd.rig();
    const Machine& machine = *crowd.machine();
    std::string clips = "[";
    for (std::size_t index = 0; index < rig.clips.size(); ++index) {
        const Clip& clip = rig.clips[index];
        clips += std::format("{}{{\"name\":{},\"duration\":{},\"raw_bytes\":{},\"compressed_bytes\":{},\"error\":{},\"root_motion\":{}}}", index ? "," : "", json_text(clip.name), clip.duration,
                             clip.bytes(), clip.compressed ? clip.compressed->bytes() : 0, clip.compressed ? clip.compressed->error() : 0.0f, clip.root_motion.has_value());
    }
    std::string states = "[", parameters = "[";
    for (std::size_t index = 0; index < machine.states.size(); ++index) states += (index ? "," : "") + json_text(machine.states[index].name);
    for (std::size_t index = 0; index < machine.parameters.size(); ++index) parameters += (index ? "," : "") + json_text(machine.parameters[index]);
    std::string out = std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"rig\":{{\"joints\":{},\"clips\":{}],\"states\":{}],\"parameters\":{}]}},\"characters\":[", crowd.steps(),
                                  crowd.state_hash(), rig.skeleton.size(), clips, states, parameters);
    std::size_t shown = 0;
    const std::size_t count = crowd.characters().size();
    for (; shown < count; ++shown) {
        const std::string next = (shown ? "," : "") + character_json(crowd, shown);
        if (budget && out.size() + next.size() + 22 > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget))) break;
        out += next;
    }
    return out + std::format("],\"more\":{}}}", count - shown);
}

void Scene::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    const auto invalid = [&](std::string_view why) { answer.fail(openas::error_code::invalid_argument, why); };
    const Rig& rig = *crowd_.rig();
    const auto steps_of = [&]() -> std::optional<std::int64_t> {
        const std::int64_t steps = request["steps"].integer().value_or(1);
        if (steps < 1 || steps > options_.most_steps) return std::nullopt;
        return steps;
    };
    const std::string no_steps = std::format("`steps` is 1 … {}", options_.most_steps);
    const auto character_of = [&]() -> std::optional<std::uint32_t> {
        const auto value = request["character"].integer();
        if (!value || *value < 0 || static_cast<std::size_t>(*value) >= crowd_.characters().size()) return std::nullopt;
        return static_cast<std::uint32_t>(*value);
    };
    const std::string no_character = std::format("no character {}: there are {} (0 … {}); skeleton.observe lists them", request["character"].integer().value_or(-1),
                                                 crowd_.characters().size(), crowd_.characters().size() - 1);
    const auto joint_of = [&](std::string_view name) -> std::expected<std::uint32_t, std::string> {
        const auto text = request[name].text();
        if (!text) return std::unexpected(std::format("`{}`: a joint's name", name));
        const auto joint = rig.skeleton.find(*text);
        if (!joint) return std::unexpected(std::format("no joint {}: skeleton.sample without a joint lists every one", *text));
        return *joint;
    };

    if (operation == "skeleton.observe") {
        const auto budget = request["budget"].integer();
        const std::string out = observed(crowd_, budget);
        if (budget && out.size() > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget)))
            return invalid(std::format("`budget` {}: the least this answers, with every character left out, is {} bytes", *budget, out.size()));
        return answer.data(out);
    }
    if (operation == "skeleton.play") {
        const auto character = character_of();
        if (!character) return invalid(no_character);
        const auto state = request["state"].text();
        if (!state) return invalid("`state`: a state of the machine (skeleton.observe lists them)");
        const auto played = apply({.kind = Input::Kind::play, .character = *character, .name = std::string(*state), .value = static_cast<float>(request["fade"].number().value_or(0.2))});
        if (!played) return invalid(played.error().text + ": skeleton.observe lists the states");
        return answer.data(character_json(crowd_, *character));
    }
    if (operation == "skeleton.set") {
        const auto character = character_of();
        if (!character) return invalid(no_character);
        const auto parameter = request["parameter"].text();
        const auto value = request["value"].number();
        if (!parameter || !value) return invalid("`parameter` (a name) and `value` (a number)");
        const auto set = apply({.kind = Input::Kind::set, .character = *character, .name = std::string(*parameter), .value = static_cast<float>(*value)});
        if (!set) return invalid(set.error().text + ": skeleton.observe lists the parameters");
        return answer.data(character_json(crowd_, *character));
    }
    if (operation == "skeleton.blend") {
        const auto character = character_of();
        if (!character) return invalid(no_character);
        const auto weight = request["weight"].number();
        if (!weight) return invalid("`weight`: 0 … 1 (0 takes the layer away)");
        Layer layer{.weight = static_cast<float>(*weight), .speed = static_cast<float>(request["speed"].number().value_or(1)), .additive = request["additive"].boolean().value_or(false)};
        if (*weight > 0) {
            const auto name = request["clip"].text();
            const auto clip = name ? rig.clip(*name) : std::nullopt;
            if (!clip) return invalid(std::format("`clip`: a clip of the rig{} (skeleton.observe lists them)", name ? std::format(", not {}", *name) : ""));
            layer.clip = *clip;
        }
        if (request["mask"]) {
            const auto joint = joint_of("mask");
            if (!joint) return invalid(joint.error());
            layer.mask = *joint;
        }
        const auto index = request["layer"].integer().value_or(0);
        if (index < 0 || index >= static_cast<std::int64_t>(Player::layers)) return invalid(std::format("`layer` is 0 … {}", Player::layers - 1));
        const auto blended = apply({.kind = Input::Kind::blend, .character = *character, .layer = static_cast<std::size_t>(index), .blend = layer});
        if (!blended) return invalid(blended.error().text);
        return answer.data(character_json(crowd_, *character));
    }
    if (operation == "skeleton.joint") {
        const auto character = character_of();
        if (!character) return invalid(no_character);
        const auto joint = joint_of("joint");
        if (!joint) return invalid(joint.error());
        const Player& player = crowd_.characters()[*character].player;
        const Mat4 world = player.world(*joint), model = player.model()[*joint];
        const Transform local = player.pose()[*joint];
        return answer.data(std::format("{{\"character\":{},\"joint\":{},\"world\":{},\"model\":{},\"local\":{{\"at\":{},\"turned\":{}}}}}", *character, json_text(rig.skeleton.names()[*joint]),
                                       triple(place_of(world)), triple(place_of(model)), triple(local.translation), quad(local.rotation)));
    }
    if (operation == "skeleton.sample") {
        const auto name = request["clip"].text();
        const auto clip = name ? rig.clip(*name) : std::nullopt;
        if (!clip) return invalid(std::format("`clip`: a clip of the rig{} (skeleton.observe lists them)", name ? std::format(", not {}", *name) : ""));
        const auto time = request["time"].number();
        if (!time || !std::isfinite(*time)) return invalid("`time`: seconds, a number");
        const Clip& played = rig.clips[*clip];
        Pose pose(rig.skeleton.size()), raw(rig.skeleton.size());
        std::vector<Mat4> model(rig.skeleton.size()), raw_model(rig.skeleton.size());
        sample(played, rig.skeleton, static_cast<float>(*time), pose);
        hanim::skeleton::model(rig.skeleton, pose, model);
        // The error compression left here: the joint's place against the clip's own channels.
        sample_raw(played, rig.skeleton, static_cast<float>(*time), raw);
        if (played.root_motion && played.root_motion->joint < raw.size()) raw[played.root_motion->joint] = pose[played.root_motion->joint];
        hanim::skeleton::model(rig.skeleton, raw, raw_model);
        const Motion moved = motion(played, rig.skeleton, 0, static_cast<float>(*time));
        const auto entry = [&](std::uint32_t joint) {
            const float off = mlibs::math::length(place_of(model[joint]) - place_of(raw_model[joint]));
            return std::format("{{\"joint\":{},\"model\":{},\"error\":{}}}", json_text(rig.skeleton.names()[joint]), triple(place_of(model[joint])), off);
        };
        const std::string head = std::format("{{\"clip\":{},\"time\":{},\"duration\":{},\"compressed\":{},\"root_motion\":{{\"moved\":{},\"turned\":{}}},", json_text(played.name),
                                             std::clamp(static_cast<float>(*time), 0.0f, played.duration), played.duration, played.compressed.has_value(), triple(moved.moved), quad(moved.turned));
        if (request["joint"]) {
            const auto joint = joint_of("joint");
            if (!joint) return invalid(joint.error());
            return answer.data(head + std::format("\"joints\":[{}],\"more\":0}}", entry(*joint)));
        }
        const auto budget = request["budget"].integer();
        std::string out = head + "\"joints\":[";
        std::size_t shown = 0;
        for (; shown < rig.skeleton.size(); ++shown) {
            const std::string next = (shown ? "," : "") + entry(static_cast<std::uint32_t>(shown));
            if (budget && out.size() + next.size() + 22 > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget))) break;
            out += next;
        }
        out += std::format("],\"more\":{}}}", rig.skeleton.size() - shown);
        if (budget && out.size() > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget)))
            return invalid(std::format("`budget` {}: the least this answers, with every joint left out, is {} bytes", *budget, out.size()));
        return answer.data(out);
    }
    if (operation == "skeleton.step") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        for (std::int64_t played = 0; played < *steps; ++played) step();
        return answer.data(status());
    }
    if (operation == "skeleton.seek") {
        const auto target = request["frame"].integer();
        if (!target) return invalid("`frame`: the frame to return to");
        if (!seek(*target)) return answer.fail(openas::error_code::stale, std::format("frame {} is outside the history kept ({} … {})", *target, earliest(), frame()));
        return answer.data(status());
    }
    if (operation == "skeleton.branch") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        Crowd future = crowd_;  // a crowd of its own: this one is not touched
        if (request["character"]) {
            const auto character = character_of();
            if (!character) return invalid(no_character);
            const auto state = request["state"].text();
            if (!state) return invalid("`state`: where the character is sent first");
            if (const auto sent = future.characters()[*character].stage.go(*state); !sent) return invalid(sent.error().text);
        }
        for (std::int64_t played = 0; played < *steps; ++played) future.step();
        return answer.data(observed(future, std::nullopt));
    }
}

}  // namespace hanim::skeleton::agent
