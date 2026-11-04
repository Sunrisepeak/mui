// The motion gallery's examples share a real scene model and one player.
// Scene builders describe endpoints; the runtime owns their movement.
export module openmui.site:motion_catalog;

import std;
import openmui;

export namespace omui::site {

struct MotionCaseInfo {
    std::string_view id, en, zh, group, purpose_en, purpose_zh, components;
    std::string_view style = "plain", use = "morph", order = "together";
    double stagger = 0;
    float rise = 0, grow = 1, stretch = 0;
    bool vertex_path = false;
    double bend = 0;
    std::string_view curve;
};

struct MotionSceneState {
    Signal<bool> end = signal(false), checked = signal(false), enabled = signal(true);
    Signal<double> value = signal(40.0), secondary = signal(70.0);
    Signal<std::int64_t> count = signal(std::int64_t{3});
    Signal<std::string> text = signal(std::string("Ada")), choice = signal(std::string("overview"));
    Signal<std::vector<std::string>> items = signal(std::vector<std::string>{"design", "build", "review"});
};

struct MotionSceneContext {
    MotionSceneState* state = nullptr;
    Signal<std::string>* language = nullptr;
    float width = 700, height = 360;
    // Call after a real control changes data, so the shared player follows
    // the new transition. Capture this context by value in event handlers.
    std::function<void()> changed;
    // Optional host capability for a useful scene's actual output.
    std::function<void(std::string)> clipboard;
};

[[nodiscard]] std::span<const MotionCaseInfo> basic_motions();
[[nodiscard]] std::span<const MotionCaseInfo> creative_motions();
// Return one scene with explicit stable child ids. The caller supplies its
// usable dimensions, a scoped style and the common motion/player root.
[[nodiscard]] Element basic_motion_scene(std::string_view id, const MotionSceneContext& context);
[[nodiscard]] Element creative_motion_scene(std::string_view id, const MotionSceneContext& context);

}  // namespace omui::site
