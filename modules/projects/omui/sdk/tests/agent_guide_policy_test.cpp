import std;
import omui;
import mlibs.testing;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

UTest cases{"sdk.agent_guide_policy", [] {
    "actual SDK guide generation labels generator policy and defers target authority to describe"_test = [] {
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
            expect(!reply.empty());
            const auto* policy = reply.front().get("data")->get("agent_policy");
            described_mode = policy->get("mode")->as_string();
            described_cap = policy->get("cap")->as_string();
            expect(!policy->get("input")->as_bool());
        };
        RunOptions options;
        options.headless = true;
        options.frames = 1;
        options.level = 0;
        options.agent_guide = dir;
        expect(run(std::move(app), options) == 0);
        const auto markdown = mcpplibs::os::fs::read(dir / "agent.md");
        expect(markdown.has_value()) << (markdown ? "" : markdown.error().message());
        if (!markdown) return;
        expect(markdown->find(std::format("Generator build profile: `{}`.", application_build_profile())) != std::string::npos);
        expect(markdown->find(std::format("| Effective preset | `{}` |", described_mode)) != std::string::npos);
        expect(markdown->find(std::format("| Effective effect cap | `{}` |", described_cap)) != std::string::npos);
        expect(markdown->find(std::format("| Policy source | `{}` |", source)) != std::string::npos);
        expect(markdown->find("| Input policy before a host/session narrows it | `false` |") != std::string::npos);
        expect(markdown->find("target artifact profile is not inferred") != std::string::npos);
        expect(markdown->find("omui page describe") != std::string::npos && markdown->find("`agent_policy`") != std::string::npos);
        expect(markdown->find("no target default preset or session grant is asserted") != std::string::npos);
        expect(markdown->find("session credentials") != std::string::npos);
        expect(!session_owner.empty() && markdown->find(session_owner) == std::string::npos);
        expect(markdown->find("development-session-") == std::string::npos);
        const auto json = mcpplibs::os::fs::read(dir / ".well-known" / "openmui-agent.json");
        expect(json.has_value());
        if (json) {
            const auto parsed = parse_json(*json);
            expect(parsed && parsed->get("openmui_agent")->as_string() == "0.1");
            expect(json->find(session_owner) == std::string::npos);
        }
    };
}};
