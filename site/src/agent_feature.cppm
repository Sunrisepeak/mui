// Agent-native work: one real task, three ways to use its semantic surface.
export module openmui.site:agent_feature;

import std;
import openmui;

export namespace omui::site {

struct AgentFeature {
    Signal<std::string> mode = signal(std::string("use"));
    Signal<std::string> title = signal(std::string("Review an idea"));
    Signal<std::string> request_text = signal(std::string("Make the next step clear"));
    Signal<std::string> style_name = signal(std::string("glass"));
    Signal<std::string> layout = signal(std::string("list"));
    Signal<std::string> status = signal(std::string("idle"));
    Signal<bool> reviewed = signal(false), recorded = signal(false), running = signal(false), method = signal(false);
    Signal<double> spacing = signal(12.0);
    Signal<std::int64_t> requests = signal(std::int64_t(0));
    Signal<Value> observed = signal(Value{});
    Signal<std::string> request_json = signal(std::string()), result_json = signal(std::string());
    std::uint64_t generation = 0, seen_revision = ~std::uint64_t(0);
    std::string applied_style;
};

struct AgentFeatureContext {
    Runtime** runtime = nullptr;
    Signal<std::string>* lang = nullptr;
    std::function<void()> connect;
    std::function<void(std::string)> style;
    std::function<void(std::string)> open_mode;
    std::string root = "main/body/view/agent/page/agent_feature";
    std::string scope = "agent-design";
};

[[nodiscard]] Element agent_feature(AgentFeature&, const AgentFeatureContext&, float width = 900);
void cancel_agent_feature(AgentFeature&);
// Call only while this page is active, after runtime updates. Structural
// comparison excludes animation/hover/bounds so inspecting cannot feed itself.
void observe_agent_feature(AgentFeature&, const AgentFeatureContext&);
[[nodiscard]] std::string agent_feature_reproduction(const AgentFeature&, const AgentFeatureContext&);
[[nodiscard]] std::string agent_feature_design_prompt(const AgentFeature&, const AgentFeatureContext&, float width);

}  // namespace omui::site
