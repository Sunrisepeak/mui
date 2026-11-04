// Core features as small, running tools. Language examples describe public
// builders; the semantic state always comes from the mounted object.
export module openmui.site:feature_examples;

import std;
import openmui;

export namespace omui::site {

struct FeatureExamples {
    Signal<std::string> title = signal(std::string("A quieter workspace"));
    Signal<bool> quiet = signal(true);
    Signal<double> intensity = signal(40.0);
    Signal<double> radius = signal(16.0);
    Signal<double> pixel_amount = signal(0.0), pixel_size = signal(6.0);
    Signal<bool> pixel_device = signal(false);
    Signal<std::int64_t> marks = signal(std::int64_t(0));
    Signal<std::string> comparison_view = signal(std::string("styled"));
    Signal<std::string> style_name = signal(std::string("glass")), language = signal(std::string("cpp"));
    Signal<bool> styles_details = signal(false), interface_details = signal(false), interacting = signal(false), paused = signal(false);
    Signal<std::string> active_preview = signal(std::string());
    Signal<Value> observed = signal(Value{});
    Signal<std::string> status = signal(std::string());
    std::uint64_t generation = 0, seen_revision = ~std::uint64_t(0);
    std::string applied_style;
    // Cached handles keep idle visibility/interaction checks allocation-free.
    Runtime* observed_runtime = nullptr;
    NodeId observed_root;
    NodeId pause_node;
    std::array<NodeId, 5> preview_nodes{};
};

struct FeatureExamplesContext {
    Runtime** runtime = nullptr;
    Signal<std::string>* lang = nullptr;
    // Installs only the named pack into this context's style-feature scope.
    std::function<void(std::string)> style;
    std::function<void(std::string)> go;
    // The page builder's full mounted path, including its own explicit id.
    std::string root;
    std::string scope = "style-feature";
};

[[nodiscard]] Element styles_feature(FeatureExamples&, const FeatureExamplesContext&, float width = 900);
[[nodiscard]] Element interface_feature(FeatureExamples&, const FeatureExamplesContext&, float width = 900);
void observe_feature_examples(FeatureExamples&, const FeatureExamplesContext&);
void cancel_feature_examples(FeatureExamples&);
// A single real semantic action. This module owns no timer or automatic
// playback; a visible-page coordinator can call this hook when appropriate.
void feature_examples_demo_step(FeatureExamples&, const FeatureExamplesContext&, std::string_view page);

}  // namespace omui::site
