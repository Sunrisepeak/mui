module xengine.dynamics.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.hash;
import mlibs.json;
import xengine.dynamics.inspect;

namespace xengine::dynamics::agent {

Body::Body(const SoftBody& initial, Options options) : options_(std::move(options)), timeline_(initial, options_.keep) {}

BodyInput Body::input_of(const Push& push) const noexcept {
    BodyInput input = options_.input;
    input.push_x += push.force_x;
    input.push_y += push.force_y;
    input.walk += push.walk;
    input.kick_x += push.kick_x;
    input.kick_y += push.kick_y;
    return input;
}

void Body::step() {
    timeline_.step(input_of(held_));
    held_.kick_x = held_.kick_y = 0;  // a kick is once
}

std::uint64_t Body::hash_of(const SoftBody& body) noexcept {
    mlibs::hash::Fnv1a hash;
    hash.u64(body.size());
    for (std::size_t index = 0; index < body.size(); ++index) hash.f64(body.x[index]).f64(body.y[index]).f64(body.velocity_x[index]).f64(body.velocity_y[index]);
    return hash.value();
}

std::expected<Body::Push, std::string> Body::push_of(const mlibs::agent::Request& request, Push held) {
    const auto pair = [&](std::string_view name, double& x, double& y) -> std::expected<void, std::string> {
        const auto node = request[name];
        if (!node) return {};
        const auto parts = node.items();
        const auto first = parts.size() == 2 ? parts[0].number() : std::nullopt;
        const auto second = parts.size() == 2 ? parts[1].number() : std::nullopt;
        if (!first || !second || !std::isfinite(*first) || !std::isfinite(*second)) return std::unexpected(std::format("`{}` is [x, y], two numbers", name));
        x = *first;
        y = *second;
        return {};
    };
    if (auto force = pair("force", held.force_x, held.force_y); !force) return std::unexpected(force.error());
    if (auto kick = pair("kick", held.kick_x, held.kick_y); !kick) return std::unexpected(kick.error());
    if (const auto walk = request["walk"]) {
        const auto value = walk.number();
        if (!value || !std::isfinite(*value)) return std::unexpected(std::string("`walk` is a number"));
        held.walk = *value;
    }
    return held;
}

std::string Body::status() const {
    return std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"earliest\":{},\"latest\":{}}}", timeline_.frame(), hash_of(timeline_.state()), timeline_.earliest(),
                       timeline_.latest());
}

std::string Body::observed(const SoftBody& body, std::int64_t frame, std::optional<std::int64_t> budget) const {
    const BodyReport report = xengine::dynamics::report(body);
    std::string out = std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"centre\":[{},{}],\"bounds\":[{},{},{},{}],\"area\":{},\"rest_area\":{},\"fastest\":{},\"contacts\":[",
                                  frame, hash_of(body), report.centre_x, report.centre_y, report.left, report.top, report.right, report.bottom, report.area,
                                  report.rest_area, report.fastest);
    std::size_t shown = 0;
    for (; shown < report.contacts.size(); ++shown) {
        const Contact& contact = report.contacts[shown];
        const std::string next = std::format("{}{{\"particle\":{},\"on\":\"{}\",\"at\":[{},{}]}}", shown ? "," : "", contact.particle,
                                             contact.kind == ContactKind::plane ? "plane" : "ledge", contact.x, contact.y);
        // What closes the answer (`],"more":N}`) still has to fit.
        if (budget && out.size() + next.size() + 22 > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget))) break;
        out += next;
    }
    return out + std::format("],\"more\":{}}}", report.contacts.size() - shown);
}

void Body::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    const auto invalid = [&](std::string_view why) { answer.fail(openas::error_code::invalid_argument, why); };
    const auto steps_of = [&]() -> std::optional<std::int64_t> {
        const std::int64_t steps = request["steps"].integer().value_or(1);
        if (steps < 1 || steps > options_.most_steps) return std::nullopt;
        return steps;
    };
    const std::string no_steps = std::format("`steps` is 1 … {}", options_.most_steps);
    const SoftBody& body = timeline_.state();

    if (operation == "body.observe") {
        const auto budget = request["budget"].integer();
        const std::string seen = observed(body, timeline_.frame(), budget);
        if (budget && seen.size() > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget)))
            return invalid(std::format("`budget` {}: the least this answers, with every contact left out, is {} bytes", *budget, seen.size()));
        return answer.data(seen);
    }
    if (operation == "body.particles") {
        std::string out = "{\"particles\":[";
        for (std::size_t index = 0; index < body.size(); ++index)
            out += std::format("{}{{\"at\":[{},{}],\"velocity\":[{},{}]}}", index ? "," : "", body.x[index], body.y[index], body.velocity_x[index], body.velocity_y[index]);
        return answer.data(out + "]}");
    }
    if (operation == "body.contacts") return answer.data(mlibs::agent::json_text(xengine::dynamics::explain(body)));
    if (operation == "body.push") {
        const auto push = push_of(request, {held_.force_x, held_.force_y, held_.walk, 0, 0});
        if (!push) return invalid(push.error());
        held_ = *push;
        // The step it is first held in makes the frame after this one.
        return answer.data(std::format("{{\"effective\":{},\"frame\":{},\"held\":{{\"force\":[{},{}],\"walk\":{}}}}}", timeline_.frame() + 1, timeline_.frame(),
                                       held_.force_x, held_.force_y, held_.walk));
    }
    if (operation == "body.step") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        for (std::int64_t played = 0; played < *steps; ++played) step();
        return answer.data(status());
    }
    if (operation == "body.seek") {
        const auto frame = request["frame"].integer();
        if (!frame) return invalid("`frame`: the frame to return to");
        if (!timeline_.seek(*frame))
            return answer.fail(openas::error_code::stale, std::format("frame {} is outside the history kept ({} … {})", *frame, timeline_.earliest(), timeline_.latest()));
        timeline_.branch();
        held_ = {};  // what was held belonged to the steps left behind
        return answer.data(status());
    }
    if (operation == "body.branch") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        auto push = push_of(request, {held_.force_x, held_.force_y, held_.walk, 0, 0});
        if (!push) return invalid(push.error());
        SoftBody future = body;  // a copy: the body is not touched
        for (std::int64_t played = 0; played < *steps; ++played) {
            SoftBodyModel::step(future, input_of(*push));
            push->kick_x = push->kick_y = 0;
        }
        return answer.data(observed(future, timeline_.frame() + *steps, std::nullopt));
    }
}

}  // namespace xengine::dynamics::agent
