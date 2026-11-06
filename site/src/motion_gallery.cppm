// One persistent gallery and one handle-based player for the motion catalogue.
export module omui.site:motion_gallery;
import std;
import omui;
export import :motion_catalog;
export import :component_motion;

export namespace omui::site {
struct MotionGallery {
    Signal<std::string> selected = signal(std::string()), query = signal(std::string()), category = signal(std::string("all"));
    Signal<bool> detail = signal(false), browser_open = signal(false), playing = signal(true), loop = signal(true), interactive = signal(false), visible = signal(true);
    Signal<double> progress = signal(0.0), speed = signal(1.0), stagger = signal(-1.0);
    Signal<double> bend = signal(0.0);
    Signal<std::string> curve = signal(std::string("default")), style = signal(std::string("plain"));
    Signal<std::int64_t> page = signal(std::int64_t{0});
    Signal<bool> copied = signal(false);
    MotionSceneState scene;
    std::shared_ptr<ComponentMotion> component;
    bool authored_program=false;
    struct Held { NodeId node; double duration = 0; };
    std::vector<Held> held;
    NodeId anchor, stage;
    double duration = 0, requested_at = 0, seek_target = -1;
    int direction = 1;
    bool automatic_interaction = false, finish_preview = false, pending = false, started = false, resetting = false, resume_after_interaction = false, resume_on_view = false, creative = false;
    std::string active_id, scene_root, stage_root;
    std::uint64_t collections = 0;  // collections happen on a new change, never on every playing frame
    MotionLevel last_level = MotionLevel::full;
};
struct MotionGalleryContext {
    Runtime** runtime = nullptr;
    Signal<std::string>* lang = nullptr;
    std::function<void(std::string)> open_detail;
    std::function<void(std::string)> style;
    std::string root;
    std::string scope = "motion-preview";
    const bool* foreground = nullptr;
};
[[nodiscard]] const MotionCaseInfo* find_motion_case(std::string_view id, bool creative);
[[nodiscard]] Value motion_gallery_config(const MotionGallery& gallery, const MotionCaseInfo& info);
[[nodiscard]] Element motion_gallery(MotionGallery& gallery, const MotionGalleryContext& context, bool creative, float width);
// Called after paint by the site's frame stage. No timer or clock lives in a scene.
void motion_gallery_tick(MotionGallery& gallery, const MotionGalleryContext& context);
// Public program sampling is a pre-paint operation, after input and regions.
void motion_gallery_update(MotionGallery&,const MotionGalleryContext&);
void motion_gallery_cancel(MotionGallery& gallery, const MotionGalleryContext& context);
void motion_gallery_play(MotionGallery& gallery, const MotionGalleryContext& context);
void motion_gallery_pause(MotionGallery& gallery, const MotionGalleryContext& context);
void motion_gallery_reverse(MotionGallery& gallery, const MotionGalleryContext& context);
void motion_gallery_seek(MotionGallery& gallery, const MotionGalleryContext& context, double position);
void motion_gallery_reset(MotionGallery& gallery, const MotionGalleryContext& context);
void motion_gallery_interact(MotionGallery& gallery, const MotionGalleryContext& context, bool interactive = true);
[[nodiscard]] std::string motion_gallery_html(bool zh, bool creative, std::string_view id = {});
[[nodiscard]] std::span<const VerbDesc> motion_control_verbs();
[[nodiscard]] VerbResult motion_control(MotionGallery&,const MotionGalleryContext&,std::string_view,const Value&);
}  // namespace omui::site
