import std;
import openmui.core;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite s{"core.agent_profile"};
    s.test("independent providers receive the same typed preset and source metadata", [] {
        auto dev = agent_policy_for_profile("dev");
        auto release = agent_policy_for_profile("release");
        auto custom = agent_policy_for_profile("embedded-custom");
        check(dev.mode == AgentMode::development && dev.source == "profile:dev");
        check(dev.enabled && dev.input && cap_name(dev.cap) == "external");
        check(release.mode == AgentMode::dual_user && release.source == "profile:release");
        check(custom.mode == AgentMode::dual_user);
        auto explicit_mode = agent_policy_for_profile("release", AgentMode::development);
        check(explicit_mode.mode == AgentMode::development && explicit_mode.source.find("explicit-mode:development") == 0);
        AgentAccess provider;
        provider.set_policy(dev);
        auto session = provider.open_session();
        check(provider.decide("control.run", Effect::external, false, {"agent", "embedded", {}}, session.get()) == openas::Decision::run);
    });
    return s.run(argc, argv);
}
