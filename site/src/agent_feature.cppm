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
    Signal<bool> preview_paused = signal(false), preview_interactive = signal(false);
    Signal<std::string> preview_status = signal(std::string("idle"));
    std::array<bool, 3> preview_seen{};
    std::uint64_t preview_timer = 0;
    bool applying_preview = false, preview_manual = false;
    std::string preview_mode, cached_root_path;
    Value preview_guard;
    Runtime* preview_runtime = nullptr;
    NodeId preview_root{}, preview_stage{};
    std::array<NodeId, 3> task_nodes{};
    // Timer callbacks test the weak token before touching this model.
    std::shared_ptr<unsigned char> lifetime = std::make_shared<unsigned char>(0);
};

struct AgentFeatureContext {
    Runtime** runtime = nullptr;
    Signal<std::string>* lang = nullptr;
    std::function<void()> connect;
    std::function<void(std::string)> style;
    std::function<void(std::string)> open_mode;
    std::string root = "main/body/view/agent/page/agent_feature";
    std::string scope = "agent-design";
    // Host panels, background windows or other site-level occlusion.
    std::function<bool()> preview_blocked;
};

[[nodiscard]] Element agent_feature(AgentFeature&, const AgentFeatureContext&, float width = 900);
void cancel_agent_feature(AgentFeature&);
void takeover_agent_feature(AgentFeature&);
// A visible-page coordinator calls this after updates. Only a finite local
// script is scheduled; the runtime still owns every component's motion.
void tick_agent_feature(AgentFeature&, const AgentFeatureContext&);
// Call only while this page is active, after runtime updates. Structural
// comparison excludes animation/hover/bounds so inspecting cannot feed itself.
void observe_agent_feature(AgentFeature&, const AgentFeatureContext&);
[[nodiscard]] std::string agent_feature_reproduction(const AgentFeature&, const AgentFeatureContext&);
[[nodiscard]] std::string agent_feature_design_prompt(const AgentFeature&, const AgentFeatureContext&, float width);

}  // namespace omui::site
