import std;
import omui.core;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

UTest cases{"core.agent_profile", [] {
    "independent providers receive the same typed preset and source metadata"_test = [] {
        auto dev = agent_policy_for_profile("dev");
        auto release = agent_policy_for_profile("release");
        auto custom = agent_policy_for_profile("embedded-custom");
        expect(dev.mode == AgentMode::development && dev.source == "profile:dev");
        expect(dev.enabled && dev.input && cap_name(dev.cap) == "external");
        expect(release.mode == AgentMode::dual_user && release.source == "profile:release");
        expect(custom.mode == AgentMode::dual_user);
        auto explicit_mode = agent_policy_for_profile("release", AgentMode::development);
        expect(explicit_mode.mode == AgentMode::development && explicit_mode.source.find("explicit-mode:development") == 0);
        AgentAccess provider;
        provider.set_policy(dev);
        auto session = provider.open_session();
        expect(provider.decide("control.run", Effect::external, false, {"agent", "embedded", {}}, session.get()) == openas::Decision::run);
    };
}};
