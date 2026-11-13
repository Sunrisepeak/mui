import std;
import omui.agent;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {
std::vector<Value> talk(agent::Protocol& protocol, std::string_view request) {
    std::vector<Value> reply;
    protocol.request(request, [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
    return reply;
}
Value described_policy(agent::Protocol& protocol) {
    const auto reply = talk(protocol, R"({"op":"describe"})");
    expect(!reply.empty() && reply.back().get("exitCode")->as_int() == 0);
    return *reply.front().get("data")->get("agent_policy");
}
bool allowed(const Value& policy, std::string_view operation) {
    for (const auto& entry : *policy.get("operations")->list())
        if (entry.get("operation")->as_string() == operation) return entry.get("allowed")->as_bool();
    expect(false) << "the operation must appear in describe";
    return false;
}
}

static UTest cases{"agent.control_policy", [] {
    "a read cap does not advertise mutable development controls"_test = [] {
        auto access = std::make_shared<AgentAccess>();
        access->set_policy({.mode = AgentMode::development, .cap = openas::Cap{openas::Effect::read}});
        Runtime rt({.stages = Stages::semantics, .access = access});
        agent::Protocol protocol(rt);
        const auto policy = described_policy(protocol);
        expect(policy.get("development_grant")->as_bool());
        expect(!policy.get("development_controls")->as_bool());
        const auto reply = talk(protocol, R"({"op":"clock","action":"pause"})");
        expect(reply.back().get("exitCode")->as_int() == 1 && !rt.time_state().paused);
    };
    "a snapshot-only layer denies every development control despite a grant"_test = [] {
        auto access = std::make_shared<AgentAccess>();
        auto config = agent_policy_for_profile("dev");
        config.user.operations = std::vector<std::string>{"snapshot"};
        access->set_policy(config);
        Runtime rt({.stages = Stages::semantics, .access = access});
        agent::Protocol protocol(rt);
        const auto policy = described_policy(protocol);
        expect(policy.get("development_grant")->as_bool());
        expect(!policy.get("development_controls")->as_bool());
        expect(allowed(policy, "snapshot"));
        for (std::string_view operation : {"clock", "pipeline", "profile", "debug"}) expect(!allowed(policy, operation));
    };
    "a single preauthorized mutable control is reported without promising all controls"_test = [] {
        auto access = std::make_shared<AgentAccess>();
        auto config = agent_policy_for_profile("dev");
        config.user.operations = std::vector<std::string>{"clock"};
        access->set_policy(config);
        Runtime rt({.stages = Stages::semantics, .access = access});
        agent::Protocol protocol(rt);
        const auto policy = described_policy(protocol);
        expect(policy.get("development_controls")->as_bool() && allowed(policy, "clock"));
        expect(policy.get("development_controls_meaning")->as_string().contains("not all controls"));
        for (std::string_view operation : {"pipeline", "profile", "debug"}) expect(!allowed(policy, operation));
        expect(talk(protocol, R"({"op":"clock","action":"pause"})").back().get("exitCode")->as_int() == 0 && rt.time_state().paused);
        expect(talk(protocol, R"({"op":"debug","bounds":true})").back().get("exitCode")->as_int() == 1 && !rt.debug().bounds);
        access->revoke_all();
        expect(!described_policy(protocol).get("development_controls")->as_bool());
    };
}};
