import std;
import openmui;
import mlibs.testing;
import mcpplibs.os;

using namespace omui;
using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite s{"sdk.agent_guide_policy"};
    s.test("actual SDK guide generation labels generator policy and defers target authority to describe", [] {
        const auto dir = scratch_dir("sdk-agent-guide-policy");
        std::string session_owner, source, described_mode, described_cap;
        AppSpec app;
        app.id = "guide-policy-cts";
        app.title = "Policy guide";
        app.agent_mode = AgentMode::development;
        app.agent_user.cap = openas::Cap{openas::Effect::read};
        app.agent_user.input = false;
        app.setup = [&](Runtime& rt) {
            auto session = rt.agent_access().open_session();
            session_owner = session->owner();
            source = rt.agent_access().effective().source;
            agent::Protocol protocol(rt);
            std::vector<Value> reply;
            protocol.request(R"({"op":"describe"})", [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
            check(!reply.empty());
            const auto* policy = reply.front().get("data")->get("agent_policy");
            described_mode = policy->get("mode")->as_string();
            described_cap = policy->get("cap")->as_string();
            check(!policy->get("input")->as_bool());
        };
        RunOptions options;
        options.headless = true;
        options.frames = 1;
        options.level = 0;
        options.agent_guide = dir;
        check(run(std::move(app), options) == 0);
        const auto markdown = mcpplibs::os::fs::read(dir / "agent.md");
        check(markdown.has_value(), markdown ? "" : markdown.error().message());
        if (!markdown) return;
        check(markdown->find(std::format("Generator build profile: `{}`.", application_build_profile())) != std::string::npos);
        check(markdown->find(std::format("| Effective preset | `{}` |", described_mode)) != std::string::npos);
        check(markdown->find(std::format("| Effective effect cap | `{}` |", described_cap)) != std::string::npos);
        check(markdown->find(std::format("| Policy source | `{}` |", source)) != std::string::npos);
        check(markdown->find("| Input policy before a host/session narrows it | `false` |") != std::string::npos);
        check(markdown->find("target artifact profile is not inferred") != std::string::npos);
        check(markdown->find("omui page describe") != std::string::npos && markdown->find("`agent_policy`") != std::string::npos);
        check(markdown->find("no target default preset or session grant is asserted") != std::string::npos);
        check(markdown->find("session credentials") != std::string::npos);
        check(!session_owner.empty() && markdown->find(session_owner) == std::string::npos);
        check(markdown->find("development-session-") == std::string::npos);
        const auto json = mcpplibs::os::fs::read(dir / ".well-known" / "openmui-agent.json");
        check(json.has_value());
        if (json) {
            const auto parsed = parse_json(*json);
            check(parsed && parsed->get("openmui_agent")->as_string() == "0.1");
            check(json->find(session_owner) == std::string::npos);
        }
    });
    return s.run(argc, argv);
}
