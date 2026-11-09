import std;
import omui;
import mlibs.testing;
import mcpplibs.os;

using namespace omui;
using namespace mlibs::testing;

namespace {
int exit_of(const std::vector<Value>& reply) {
    return reply.empty() || !reply.back().get("exitCode") ? -100 : static_cast<int>(reply.back().get("exitCode")->as_int());
}
std::vector<Value> talk(agent::Protocol& protocol, std::string_view request) {
    std::vector<Value> reply;
    protocol.request(request, [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
    return reply;
}
RunOptions headless() {
    RunOptions options;
    options.headless = true;
    options.frames = 1;
    options.level = 0;
    return options;
}
} // namespace

int main(int argc, char** argv) {
    Suite s{"sdk.agent_startup"};
    s.test("the actual mcpp profile selects the automatic startup policy before setup", [] {
        const auto profile = application_build_profile();
        check(!profile.empty(), "this is generated build metadata, not a runtime environment fallback");
        const auto external = mcpplibs::os::env::get("OMUI_EXPECT_BUILD_PROFILE");
        const std::string_view expected_profile = external && !external->empty() ? std::string_view(*external) : profile;
        std::println("    SDK profile: actual={}, external expectation={}", profile, external.value_or("<unset>"));
        if (external && !external->empty())
            check(profile == *external, std::format("generated profile `{}` must match external build expectation `{}`", profile, *external));
        // An independent test oracle: do not derive the expected mode using
        // the same production helper that resolves the Runtime's preset.
        const auto expected = expected_profile == "dev" ? AgentMode::development : AgentMode::dual_user;
        int sent = 0;
        bool setup = false;
        AppSpec app;
        app.id = "policy-profile-cts";
        app.verbs.push_back({{.name = "mail.send", .effect = Effect::external, .answer = "send"}, [&](const Value&) -> VerbResult {
                                 ++sent;
                                 return Value{};
                             }});
        app.setup = [&](Runtime& rt) {
            setup = true;
            check(rt.agent_access_binding() != nullptr);
            const auto actual = rt.agent_access().effective().mode;
            std::println("    SDK automatic policy: actual profile={}, mode={}; expected profile={}, mode={}", profile,
                agent_mode_name(actual), expected_profile, agent_mode_name(expected));
            check(actual == expected);
            agent::Protocol protocol(rt);
            const auto reply = talk(protocol, R"({"op":"invoke","verb":"mail.send"})");
            std::println("    SDK default external request: actual exit={}, expected exit={}", exit_of(reply),
                expected == AgentMode::development ? 0 : 2);
            check(exit_of(reply) == (expected == AgentMode::development ? 0 : 2));
            const auto description = talk(protocol, R"({"op":"describe"})");
            const auto* policy = description.front().get("data")->get("agent_policy");
            check(policy->get("mode")->as_string() == agent_mode_name(expected));
            check(policy->get("source")->as_string() == std::format("profile:{}", expected_profile));
            check(policy->get("development_grant")->as_bool() == (expected == AgentMode::development));
        };
        check(run(std::move(app), headless()) == 0);
        check(setup && sent == (expected == AgentMode::development ? 1 : 0));
    });

    s.test("explicit deployment mode and all narrowing layers survive actual startup", [] {
        AppSpec app;
        app.id = "policy-layers-cts";
        app.agent_mode = AgentMode::development;
        app.agent_developer.cap = openas::Cap{openas::Effect::irreversible};
        app.agent_user.cap = openas::Cap{openas::Effect::read};
        app.agent_user.input = false;
        int sent = 0;
        app.verbs.push_back({{.name = "mail.send", .effect = Effect::external, .answer = "send"}, [&](const Value&) -> VerbResult {
                                 ++sent;
                                 return Value{};
                             }});
        app.setup = [&](Runtime& rt) {
            agent::Protocol protocol(rt, {.input = true});
            check(exit_of(talk(protocol, R"({"op":"invoke","verb":"mail.send","answer":"send"})")) == 1);
            check(exit_of(talk(protocol, R"({"op":"input","events":[{"type":"text","text":"x"}]})")) == 1);
            const auto state = rt.agent_access().effective();
            check(state.mode == AgentMode::development && cap_name(state.cap) == "read" && !state.input);
        };
        check(run(std::move(app), headless()) == 0 && sent == 0);
    });

    s.test("trusted startup options override the preset without changing caps or developer limits", [] {
        const std::array<std::string_view, 2> args{"--agent-mode", "dual-user"};
        auto options = RunOptions::parse(args);
        check(options && options->agent_mode == AgentMode::dual_user);
        const std::array<std::string_view, 2> invalid{"--agent-mode", "external"};
        check(!RunOptions::parse(invalid));
        AppSpec app;
        app.agent_mode = AgentMode::development;
        app.agent_developer.cap = openas::Cap{openas::Effect::read};
        auto policy = application_agent_policy(app, options->agent_mode);
        check(policy && policy->mode == AgentMode::dual_user && policy->developer.cap.has_value());
        app.agent = "development";
        check(!application_agent_policy(app), "deployment mode cannot be smuggled into the effect-cap parser");
    });

    s.test("two SDK startups preserve isolated instances and leave the legacy fallback unchanged", [] {
        const auto global_cap = agent_access().cap();
        std::shared_ptr<AgentAccess> first;
        std::shared_ptr<const AgentSession> session;
        AppSpec a;
        a.agent_mode = AgentMode::development;
        a.setup = [&](Runtime& rt) {
            first = rt.agent_access_binding();
            session = first->open_session();
        };
        check(run(std::move(a), headless()) == 0);
        AppSpec b;
        b.agent = "none";
        b.agent_mode = AgentMode::dual_user;
        b.setup = [&](Runtime& rt) { check(rt.agent_access_binding() != first && cap_name(rt.agent_access().cap()) == "none"); };
        check(run(std::move(b), headless()) == 0);
        check(first && session &&
              first->decide("mail.send", Effect::external, false, {"agent", "tester", {}}, session.get()) == openas::Decision::run);
        check(cap_name(agent_access().cap()) == cap_name(global_cap));
    });

    s.test("no-agent startup disables execution while retaining an explanation", [] {
        AppSpec app;
        app.agent_mode = AgentMode::development;
        app.setup = [&](Runtime& rt) {
            agent::Protocol p(rt);
            check(exit_of(talk(p, R"({"op":"snapshot"})")) == 1);
            const auto d = talk(p, R"({"op":"describe"})");
            check(exit_of(d) == 0 && !d.front().get("data")->get("agent_policy")->get("enabled")->as_bool());
        };
        auto options = headless();
        options.agent = false;
        check(run(std::move(app), options) == 0);
    });
    return s.run(argc, argv);
}
