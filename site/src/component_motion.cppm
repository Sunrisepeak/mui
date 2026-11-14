export module omui.site:component_motion;
import std;
export import omui.hanim;
import omui;
import :motion_catalog;

export namespace omui::site {
struct ComponentMotion {
    Signal<std::string> selection=signal(std::string("together"));
    Signal<std::string> effect=signal(std::string("none"));
    Signal<std::string> error=signal(std::string());
    std::optional<animation::Playback> playback;
    std::string configured_selection,configured_effect;
    bool configured_loop=true;
    double configured_speed=1;
};
[[nodiscard]] Element component_motion_scene(const MotionSceneContext&);
[[nodiscard]] std::expected<hanim::Program,std::string> component_motion_program(hanim::ObjectRef,std::string_view selection,std::string_view effect="none");
[[nodiscard]] std::string component_motion_source(std::string_view selection,bool loop=true,double speed=1,std::string_view effect="none");
// Cold attachment/reconfiguration after paint; native update runs in the
// site's before-bounds stage. The sample edits real note data, never a timer.
[[nodiscard]] bool prepare_component_motion(ComponentMotion&,Runtime&,std::string_view scene_root,
                                            double progress,int direction,double speed,bool loop,bool requested);
}
