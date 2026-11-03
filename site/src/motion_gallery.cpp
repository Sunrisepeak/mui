module openmui.site;
import std;
import openmui;
import :motion_gallery;

namespace omui::site {
namespace {
Runtime* runtime(const MotionGalleryContext& c) { return c.runtime ? *c.runtime : nullptr; }
std::string words(const MotionGalleryContext& c, std::string_view en, std::string_view zh) {
    return std::string(c.lang && c.lang->get() == "zh" ? zh : en);
}
bool within(std::string_view path, std::string_view root) {
    return path == root || (path.size() > root.size() && path.starts_with(root) && path[root.size()] == '/');
}
void release(MotionGallery& g, Runtime* rt, bool endpoint) {
    if (rt) for (const auto& h : g.held) {
        if (endpoint) rt->seek_motion(h.node, h.duration);
        rt->release_motion(h.node);
    }
    g.held.clear(); g.anchor = {}; g.duration = 0;
}
void finish_pending(MotionGallery& g, Runtime* rt) {
    if (g.pending && rt) for (const auto& m : rt->motions()) if (within(m.path, g.scene_root)) {
        rt->seek_motion(m.node, m.end - m.start); rt->release_motion(m.node);
    }
    g.pending = false;
}
void rates(MotionGallery& g, Runtime* rt) {
    if (!rt) return;
    for (const auto& h : g.held) rt->set_motion_rate(h.node, g.direction * std::clamp(g.speed.peek(), 0.1, 3.0));
}
void begin(MotionGallery& g, const MotionGalleryContext& c) {
    release(g, runtime(c), true);
    g.scene.end.set(!g.scene.end.peek());
    g.pending = true; g.started = true;
    if (auto* rt = runtime(c)) { g.requested_at = rt->now().count(); rt->request_frame(); }
}
void prepare(MotionGallery& g, const MotionGalleryContext& c, bool creative, const MotionCaseInfo& info) {
    if (g.active_id == info.id && g.creative == creative && g.scene_root == c.root + "/stage/scene") return;
    const bool play_intent = g.active_id.empty() || g.playing.peek() || g.resume_on_view || (g.interactive.peek() && g.resume_after_interaction);
    release(g, runtime(c), true);
    g.active_id = info.id; g.creative = creative; g.stage_root = c.root + "/stage"; g.scene_root = g.stage_root + "/scene";
    g.scene = MotionSceneState{};
    g.pending = false; g.started = false; g.resetting = false; g.resume_on_view = false; g.stage = {}; g.direction = 1; g.seek_target = -1;
    g.interactive.set(false); g.playing.set(play_intent); g.progress.set(0); g.page.set(0);
    g.style.set(std::string(info.style)); g.curve.set("default"); g.stagger.set(-1);
    if (c.style) c.style(std::string(info.style));
}
bool contains_focus(Runtime& rt, NodeId stage) {
    for (NodeId node = rt.focused(); rt.valid(node); node = rt.parent(node)) if (node == stage) return true;
    return false;
}
bool on_screen(Runtime& rt, NodeId stage) {
    if (!rt.valid(stage)) return false;
    Rect area = rt.drawn_bounds(stage).intersection({0, 0, rt.size().width, rt.size().height});
    for (NodeId parent = rt.parent(stage); rt.valid(parent) && !area.empty(); parent = rt.parent(parent))
        if (rt.type(parent).clips || rt.get(parent, "clip").as_bool() || (rt.get(parent, "overflow").as_string() == "clip" || rt.get(parent, "overflow").as_string() == "scroll"))
            area = area.intersection(rt.drawn_bounds(parent));
    return !area.empty();
}
void capture(MotionGallery& g, Runtime& rt) {
    release(g, &rt, false);
    ++g.collections;
    // This is the only catalogue-wide allocation/scan: a new drawing was
    // requested. On ordinary ticks only the longest held handle is read.
    for (const auto& m : rt.motions()) if (within(m.path, g.scene_root) && rt.hold_motion(m.node)) {
        const double length = m.end - m.start;
        g.held.push_back({m.node, length});
        if (length > g.duration) { g.duration = length; g.anchor = m.node; }
    }
    g.pending = false;
    if (g.resetting) {
        release(g, &rt, true); g.resetting = false; g.progress.set(g.scene.end.peek() ? 1 : 0); g.playing.set(false); return;
    }
    if (g.held.empty()) { g.playing.set(false); g.progress.set(g.scene.end.peek() ? 1 : 0); return; }
    const double position = g.seek_target >= 0 ? g.seek_target : g.direction < 0 ? 1 : 0;
    g.seek_target = -1;
    for (const auto& h : g.held) {
        rt.pause_motion(h.node);
        rt.seek_motion(h.node, position * g.duration);  // common seconds, not a separate normalized clock per control
    }
    rates(g, &rt);
    if (g.playing.peek()) for (const auto& h : g.held) rt.play_motion(h.node);
    g.progress.set(position);
}
std::string group_name(std::string_view id, bool zh) {
    static constexpr std::array groups{
        std::array<std::string_view, 3>{"appearance", "Surface & emphasis", "表面与强调"},
        std::array<std::string_view, 3>{"transform", "Position & shape", "位置与形状"},
        std::array<std::string_view, 3>{"layout", "Layout & structure", "布局与结构"},
        std::array<std::string_view, 3>{"selection", "Selection", "选择反馈"},
        std::array<std::string_view, 3>{"text", "Text & numbers", "文字与数字"},
        std::array<std::string_view, 3>{"data", "Values & progress", "数值与进度"},
        std::array<std::string_view, 3>{"presence", "Arrive & leave", "出现与离开"},
        std::array<std::string_view, 3>{"rhythm", "Sequence & rhythm", "顺序与节奏"},
        std::array<std::string_view, 3>{"organise", "Organise", "整理"},
        std::array<std::string_view, 3>{"control", "Control", "控制"},
        std::array<std::string_view, 3>{"workflow", "Workflow", "流程"},
        std::array<std::string_view, 3>{"choose", "Choose", "选择"},
        std::array<std::string_view, 3>{"media", "Media", "媒体"},
        std::array<std::string_view, 3>{"create", "Create", "创作"},
        std::array<std::string_view, 3>{"plan", "Plan", "计划"},
        std::array<std::string_view, 3>{"read", "Read", "阅读"},
        std::array<std::string_view, 3>{"inspect", "Inspect", "查看"},
        std::array<std::string_view, 3>{"communicate", "Communicate", "沟通"}};
    for (const auto& g : groups) if (g[0] == id) return std::string(g[zh ? 2 : 1]);
    return std::string(id);
}
std::string lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}
std::string escape(std::string_view value) {
    std::string out;
    for (char c : value) switch (c) { case '&': out += "&amp;"; break; case '<': out += "&lt;"; break; case '>': out += "&gt;"; break; case '"': out += "&quot;"; break; default: out += c; }
    return out;
}
Element in_details(Element element, Signal<bool> open) {
    // Keep stable control ids and their own feedback, while closed details
    // contribute neither layout space nor compact semantics.
    element.prop("display", derive([open] { return std::string(open.get() ? "flex" : "none"); }));
    return element;
}
Element controls(MotionGallery& g, const MotionGalleryContext& c, float width) {
    return column({.id = "transport", .gap = 12}, {
        row({.id = "buttons", .gap = 8, .align = "center", .wrap = "wrap"}, {
            button({.id = "play", .text = computed([&g, c] { return words(c, g.playing.get() ? "Pause" : "Play", g.playing.get() ? "暂停" : "播放"); }),
                    .variant = ButtonVariant::primary, .on_activated = [&g, c] { if (g.playing.peek()) motion_gallery_pause(g, c); else motion_gallery_play(g, c); }}),
            in_details(button({.id = "reverse", .text = words(c, "Reverse", "反向"), .on_activated = [&g, c] { motion_gallery_reverse(g, c); }}), g.detail),
            in_details(button({.id = "reset", .text = words(c, "Reset", "重置"), .on_activated = [&g, c] { motion_gallery_reset(g, c); }}), g.detail),
            in_details(button({.id = "state-a", .text = "A", .on_activated = [&g, c] { motion_gallery_reset(g, c); }}), g.detail),
            in_details(button({.id = "state-b", .text = "B", .on_activated = [&g, c] {
                release(g, runtime(c), true); g.scene.end.set(true); g.interactive.set(false); g.playing.set(false); g.resume_on_view = false;
                g.pending = true; g.resetting = true; if (auto* rt = runtime(c)) { g.requested_at = rt->now().count(); rt->request_frame(); }
            }}), g.detail),
            button({.id = "try", .text = computed([&g, c] { return words(c, g.interactive.get() ? "Watch motion" : "Try it", g.interactive.get() ? "观看动效" : "亲手试试"); }),
                    .on_activated = [&g, c] { motion_gallery_interact(g, c, !g.interactive.peek()); }}),
            button({.id = "details", .text = computed([&g, c] { return words(c, g.detail.get() ? "Close details" : "Details", g.detail.get() ? "收起详情" : "查看详情"); }),
                    .variant = ButtonVariant::ghost, .on_activated = [&g, c] { const bool show = !g.detail.peek(); g.detail.set(show); if (c.open_detail) c.open_detail(show ? g.active_id : ""); }})}),
        in_details(slider({.id = "seek", .label = words(c, "Position in this transition", "当前过渡的位置"), .max = 1.0, .min = 0.0, .step = 0.001, .value = g.progress,
                .on_changed = [&g, c](double p) { motion_gallery_seek(g, c, p); }, .width = Dim(width)}), g.detail),
        text({.id = "hint", .text = computed([&g, c] {
            if (auto* rt = runtime(c); rt && (rt->reduce_motion() || rt->motion_level() == MotionLevel::off))
                return words(c, "Motion is off. Try the controls, or open Details for states A/B.", "动效已关闭，可操作控件，或打开详情切换 A/B 状态。");
            return g.interactive.get() ? words(c, "You are using the real interface.", "正在操作真实界面。")
                                       : words(c, "Hover or focus the canvas to use it. Every object keeps its identity.", "将指针移入或聚焦画布，即可操作。同一对象始终保持连续。");
        }), .style = "caption", .color = "text_muted", .max_width = Dim(width)})});
}
Element detail(MotionGallery& g, const MotionGalleryContext& c, const MotionCaseInfo& info, float width) {
    auto recompose = [&g, c] { g.interactive.set(false); g.playing.set(true); g.resetting = false; g.direction = 1; begin(g, c); };
    return column({.id = "details", .padding = 20, .gap = 16, .background = "surface_variant", .radius = 16}, {
        text({.id = "components", .text = std::string(info.components), .style = "label", .max_width = Dim(std::max(1.0f, width - 40))}),
        text({.id = "explanation", .text = words(c, "One clock coordinates the scene and each control's own reusable recipe. Shorter motions finish first.",
            "同一时钟统筹场景与各控件自身的可复用配方，较短的动效先完成。"), .max_width = Dim(std::max(1.0f, width - 40))}),
        row({.id = "settings", .gap = 16, .wrap = "wrap"}, {
            select({.id = "curve", .label = words(c, "Timing", "时间曲线"), .labels = std::vector<std::string>{words(c,"Designed","设计默认"),words(c,"Quick","轻快"),words(c,"Gentle spring","柔和弹簧"),words(c,"Slow","舒缓")},
                    .options = std::vector<std::string>{"default", "motion.fast", "motion.spring.gentle", "motion.slow"}, .value = g.curve,
                    .on_changed = [recompose](std::string) { recompose(); }, .width = 200}),
            toggle({.id = "loop", .checked = g.loop, .label = words(c, "Loop while visible", "可见时循环")})}),
        slider({.id = "speed", .label = words(c, "Playback speed", "播放速度"), .max = 2.0, .min = 0.25, .step = 0.25, .value = g.speed,
                .on_changed = [&g, c](double) { rates(g, runtime(c)); }, .width = Dim(std::max(100.0f, width - 40))}),
        slider({.id = "stagger", .label = words(c, "Delay between pieces (seconds)", "部件错开间隔（秒）"), .max = 0.10, .min = 0.0, .step = 0.005,
                .value = Source{std::nullopt, [&g, base = info.stagger] { return Value(g.stagger.get() < 0 ? base : g.stagger.get()); }, [&g](const Value& v) { g.stagger.set(v.as_double()); }},
                .on_changed = [recompose](double) { recompose(); }, .width = Dim(std::max(100.0f, width - 40))}),
        text({.id = "config-label", .text = words(c, "Motion configuration · JSON", "动效配置 · JSON"), .style = "label"}),
        text({.id = "config-scope", .text = words(c, "This is the scene root's configuration, not a complete exported project. Controls retain their framework defaults.",
            "这是场景根节点的配置，并非完整工程导出；控件保留框架默认配方。"), .style = "caption", .color = "text_muted", .max_width = Dim(std::max(1.0f, width - 40))}),
        code_editor({.id = "config", .language = CodeEditorLanguage::json, .lines = 5, .readonly = true,
                     .text = computed([&g, info] { (void)g.curve.get(); (void)g.stagger.get(); return motion_gallery_config(g, info).json(); }), .width = Dim(std::max(100.0f, width - 40))}),
        button({.id = "copy", .text = words(c, "Copy configuration", "复制配置"), .on_activated = [&g, c, info] { if (auto* rt = runtime(c)) rt->set_clipboard_text(motion_gallery_config(g, info).json()); g.copied.set(true); }})});
}
}  // namespace

const MotionCaseInfo* find_motion_case(std::string_view id, bool creative) {
    for (const auto& info : creative ? creative_motions() : basic_motions()) if (info.id == id) return &info;
    return nullptr;
}
Value motion_gallery_config(const MotionGallery& g, const MotionCaseInfo& info) {
    const std::string_view order = g.stagger.peek() > 0 && info.order == "together" ? "document" : info.order;
    Value main = Value{}.set("use", info.use).set("order", order).set("stagger", g.stagger.peek() < 0 ? info.stagger : g.stagger.peek())
        .set("rise", info.rise).set("grow", info.grow).set("stretch", info.stretch);
    if (g.curve.peek() != "default") main.set("curve", g.curve.peek());
    if (info.use == "liquid" || info.use == "ink") return Value(Value::List{main, Value("simple")});
    return main;
}
void motion_gallery_cancel(MotionGallery& g, const MotionGalleryContext& c) {
    // Preserve intent across an unmount. Repeated inactive ticks must not
    // overwrite a playing intent with the suspended transport's false.
    g.resume_on_view = g.resume_on_view || g.playing.peek();
    finish_pending(g, runtime(c));
    release(g, runtime(c), true); g.started = false; g.resetting = false; g.playing.set(false);
}
void motion_gallery_play(MotionGallery& g, const MotionGalleryContext& c) {
    g.resume_on_view = false;
    if (g.interactive.peek()) { motion_gallery_interact(g, c, false); }
    g.playing.set(true);
    if (g.held.empty()) { if (!g.pending) begin(g, c); }
    else if (auto* rt = runtime(c)) {
        if (g.direction > 0 && g.progress.peek() >= 1) motion_gallery_seek(g, c, 0);
        if (g.direction < 0 && g.progress.peek() <= 0) motion_gallery_seek(g, c, 1);
        rates(g, rt); for (const auto& h : g.held) rt->play_motion(h.node); g.playing.set(true);
    }
}
void motion_gallery_pause(MotionGallery& g, const MotionGalleryContext& c) {
    g.resume_on_view = false;
    g.playing.set(false);
    if (auto* rt = runtime(c)) for (const auto& h : g.held) rt->pause_motion(h.node);
}
void motion_gallery_reverse(MotionGallery& g, const MotionGalleryContext& c) {
    g.direction = -g.direction;
    if (g.held.empty()) { g.seek_target = g.direction < 0 ? 1 : 0; if (!g.pending) begin(g, c); }
    rates(g, runtime(c));
}
void motion_gallery_seek(MotionGallery& g, const MotionGalleryContext& c, double position) {
    position = std::clamp(position, 0.0, 1.0); g.progress.set(position); g.playing.set(false);
    if (g.held.empty()) { g.seek_target = position; if (!g.pending) begin(g, c); return; }
    if (auto* rt = runtime(c)) for (const auto& h : g.held) { rt->pause_motion(h.node); rt->seek_motion(h.node, position * g.duration); }
}
void motion_gallery_reset(MotionGallery& g, const MotionGalleryContext& c) {
    release(g, runtime(c), true); g.interactive.set(false); g.playing.set(false); g.resume_on_view = false; g.direction = 1;
    g.scene.end.set(false); g.resetting = true; g.pending = true; g.seek_target = -1;
    if (auto* rt = runtime(c)) { g.requested_at = rt->now().count(); rt->request_frame(); }
}
void motion_gallery_interact(MotionGallery& g, const MotionGalleryContext& c, bool interactive) {
    if (g.interactive.peek() == interactive) return;
    g.automatic_interaction = false;
    if (interactive) {
        g.resume_after_interaction = g.playing.peek();
        g.finish_preview = g.pending;
        finish_pending(g, runtime(c));
        release(g, runtime(c), true); g.playing.set(false); g.interactive.set(true);
    } else {
        g.interactive.set(false);
        if (g.resume_after_interaction) { g.playing.set(true); begin(g, c); }
    }
}
void motion_gallery_tick(MotionGallery& g, const MotionGalleryContext& c) {
    auto* rt = runtime(c); if (!rt || g.stage_root.empty()) return;
    // find() currently materialises a path string. Keep the stable node
    // handle on ordinary ticks; resolve again only after unmount/rebuild.
    const NodeId stage = rt->valid(g.stage) ? g.stage : rt->find(g.stage_root);
    if (!rt->valid(stage)) { motion_gallery_cancel(g, c); g.stage = {}; return; }
    if (stage != g.stage) { g.stage = stage; g.started = false; }
    const bool visible = g.visible.peek() && on_screen(*rt, stage);
    if (!visible) {
        g.resume_on_view = g.resume_on_view || g.playing.peek();
        finish_pending(g, rt); release(g, rt, true); g.playing.set(false); return;
    }
    const MotionLevel level = rt->reduce_motion() ? MotionLevel::off : rt->motion_level();
    if (level != g.last_level) { release(g, rt, true); g.pending = false; g.started = false; g.last_level = level; }
    const bool focused = contains_focus(*rt, stage);
    if (!g.interactive.peek() && focused) { motion_gallery_interact(g, c); g.automatic_interaction = true; }
    if (g.interactive.peek() && g.automatic_interaction && !focused && !rt->state(stage).hovered)
        motion_gallery_interact(g, c, false);
    if (g.interactive.peek()) {
        if (g.finish_preview) { g.pending = true; finish_pending(g, rt); g.finish_preview = false; }
        return;
    }
    if (g.resume_on_view && level == MotionLevel::full && !g.interactive.peek()) {
        g.resume_on_view = false; g.playing.set(true); begin(g, c);
    }
    if (!g.started) {
        g.started = true;
        if (g.playing.peek() && g.loop.peek() && level == MotionLevel::full) begin(g, c);
        else g.playing.set(false);
    }
    if (g.pending) { if (rt->now().count() > g.requested_at) capture(g, *rt); else rt->request_frame(); }
    if (!g.anchor) return;
    const auto clock = rt->motion_time(g.anchor);
    if (!clock) { release(g, rt, false); g.playing.set(false); return; }
    const double position = g.duration > 0 ? std::clamp(clock->second / g.duration, 0.0, 1.0) : 1;
    if (std::abs(g.progress.peek() - position) > 0.0001) g.progress.set(position);
    if (!g.playing.peek()) return;
    if ((g.direction > 0 && position >= 1) || (g.direction < 0 && position <= 0)) {
        if (g.loop.peek() && level == MotionLevel::full) { g.direction = -g.direction; rates(g, rt); }
        else motion_gallery_pause(g, c);
    }
    if (g.playing.peek()) rt->request_frame();
}

Element motion_gallery(MotionGallery& g, const MotionGalleryContext& c, bool creative, float width) {
    width = std::max(240.0f, width);
    const bool zh = c.lang && c.lang->get() == "zh";
    const auto entries = creative ? creative_motions() : basic_motions();
    const auto* info = find_motion_case(g.selected.get(), creative);
    if (!info) info = find_motion_case(creative ? "palette-studio" : "list-grid", creative);
    if (!info && !entries.empty()) info = &entries.front();
    if (!info) return text({.id = "motion_gallery", .text = words(c, "No motion designs are available.", "暂无可用动效。")});
    prepare(g, c, creative, *info);
    const float inset = width < 520 ? 8.0f : 16.0f;
    const float scene_width = std::max(200.0f, width - 2 * inset);
    const float scene_height = info->id == "palette-studio" ? 300.0f : width < 520 && info->id == "list-grid" ? 280.0f : width < 520 ? 430.0f : 360.0f;
    const MotionCaseInfo current = *info;
    auto scene = box({.id = "scene", .width = Dim(scene_width), .height = Dim(scene_height)}, {dynamic([&g, c, current, creative, scene_width, scene_height] {
        MotionSceneContext context{.state = &g.scene, .language = c.lang, .width = scene_width, .height = scene_height,
            .changed = [&g, c] { if (!g.interactive.peek()) { g.pending = true; if (auto* rt = runtime(c)) { g.requested_at = rt->now().count(); rt->request_frame(); } } },
            .clipboard = [c](std::string text) { if (auto* rt = runtime(c)) rt->set_clipboard_text(std::move(text)); }};
        return std::vector<Element>{creative ? creative_motion_scene(current.id, context) : basic_motion_scene(current.id, context)};
    })});
    scene.key = std::string(info->id);
    scene.prop("label", Source(Value(std::string(zh ? current.zh : current.en))))
         .prop("theme_scope", Source(Value(c.scope))).prop("overlay_boundary", Source(Value(true)))
         .prop("motion", derive([&g, current] { (void)g.curve.get(); (void)g.stagger.get(); return motion_gallery_config(g, current); }));
    auto stage = column({.id = "stage", .width = Dim(width), .height = Dim(scene_height + 2 * inset), .padding = Edges(inset), .background = "surface_variant", .radius = 20, .clip = true,
                         .label = words(c, "Interactive motion canvas", "可交互动效画布"), .on = {
        {"pointer_enter", [&g, c](const Value&) { motion_gallery_interact(g, c); g.automatic_interaction = true; }},
        {"focus", [&g, c](const Value&) { motion_gallery_interact(g, c); g.automatic_interaction = true; }},
        {"pointer_leave", [&g, c](const Value&) {
            if (auto* rt = runtime(c); rt && within(rt->path(rt->focused()), c.root + "/stage")) return;
            motion_gallery_interact(g, c, false);
        }} }}, {std::move(scene)});
    stage.prop("focusable", Source(Value(true)));
    auto title = text({.id = "title", .text = std::string(zh ? info->zh : info->en), .style = "title", .size = width < 520 ? 30 : 38, .max_width = Dim(width)});
    title.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{1})));
    std::vector<Element> content{
        text({.id = "eyebrow", .text = words(c, creative ? "OMUI / CREATIVE DEMOS" : "OMUI / BASIC MOTION", creative ? "OMUI / 创意演示" : "OMUI / 基础动画"), .style = "label", .color = "accent"}),
        row({.id = "heading", .gap = 12, .align = "center", .wrap = "wrap"}, {
            std::move(title),
            badge({.id = "count", .text = std::format("{} {}", entries.size(), words(c, "designs", "项设计"))})}),
        text({.id = "purpose", .text = std::string(zh ? info->purpose_zh : info->purpose_en), .color = "text_muted", .max_width = Dim(width)}),
        button({.id = "choose", .text = g.browser_open.get() ? words(c, "Close picker", "收起目录")
                    : std::format("{} {}", words(c, creative ? "Choose creative demo" : "Choose animation", creative ? "选择创意" : "选择动画"), entries.size()),
                .variant = ButtonVariant::ghost, .on_activated = [&g] { g.browser_open.set(!g.browser_open.peek()); }, .align_self = "start"})};
    if (g.browser_open.get()) {
        std::vector<std::string> groups{"all"}, group_labels{words(c, "All designs", "全部设计")};
        for (const auto& e : entries) if (std::ranges::find(groups, e.group) == groups.end()) { groups.emplace_back(e.group); group_labels.push_back(group_name(e.group, zh)); }
        std::vector<const MotionCaseInfo*> filtered;
        const std::string query = lower(g.query.get()), category = g.category.get();
        for (const auto& e : entries) {
            if (category != "all" && category != e.group) continue;
            const std::string haystack = lower(std::format("{} {} {} {} {}", e.id, e.en, e.zh, e.components, zh ? e.purpose_zh : e.purpose_en));
            if (query.empty() || haystack.find(query) != std::string::npos) filtered.push_back(&e);
        }
        constexpr std::size_t per_page = 8;
        const auto pages = std::max<std::size_t>(1, (filtered.size() + per_page - 1) / per_page);
        const std::size_t page = std::min<std::size_t>(static_cast<std::size_t>(std::max<std::int64_t>(0, g.page.get())), pages - 1);
        std::vector<Element> choices;
        const float item_width = width >= 700 ? (width - 12) / 2 : width;
        for (std::size_t i = page * per_page; i < std::min(filtered.size(), (page + 1) * per_page); ++i) {
            const auto& e = *filtered[i];
            choices.push_back(button({.id = std::string(e.id), .text = std::string(zh ? e.zh : e.en),
                .variant = e.id == info->id ? ButtonVariant::primary : ButtonVariant::secondary,
                .on_activated = [&g, c, id = std::string(e.id)] {
                    motion_gallery_cancel(g, c); g.browser_open.set(false); g.selected.set(id);
                    if (g.detail.peek() && c.open_detail) c.open_detail(id);
                    if (auto* rt = runtime(c)) {
                        const NodeId stage = rt->valid(g.stage) ? g.stage : rt->find(g.stage_root);
                        if (rt->valid(stage)) (void)rt->act(stage, "scroll_into_view");
                    }
                }, .width = Dim(item_width)}));
        }
        if (choices.empty()) choices.push_back(text({.id = "empty", .text = words(c, "No designs match. Try a different word or category.", "没有匹配项，换个词或分类试试。"), .color = "text_muted", .max_width = Dim(width)}));
        content.push_back(column({.id = "browse", .gap = 16}, {
            text({.id = "title", .text = words(c, "Find the next idea.", "找到下一个灵感。"), .style = "heading"}),
            row({.id = "filters", .gap = 12, .wrap = "wrap"}, {
                text_box({.id = "search", .label = words(c, "Search designs", "搜索设计"), .placeholder = words(c, "Search motion or component", "搜索动效或组件"), .text = g.query,
                          .on_changed = [&g](std::string) { g.page.set(0); }, .width = Dim(std::min(width, 320.0f))}),
                select({.id = "category", .label = words(c, "Category", "分类"), .labels = group_labels, .options = groups, .value = g.category,
                        .on_changed = [&g](std::string) { g.page.set(0); }, .width = Dim(std::min(width, 220.0f))})}),
            row({.id = "choices", .gap = 12, .wrap = "wrap"}, std::move(choices)),
            row({.id = "pagination", .gap = 12, .align = "center"}, {
                button({.id = "previous", .disabled = page == 0, .text = words(c, "Previous", "上一页"), .on_activated = [&g] { g.page.set(std::max<std::int64_t>(0, g.page.peek() - 1)); }}),
                text({.id = "position", .text = std::format("{} / {}", page + 1, pages), .style = "caption", .color = "text_muted"}),
                button({.id = "next", .disabled = page + 1 >= pages, .text = words(c, "Next", "下一页"), .on_activated = [&g, pages] { g.page.set(std::min<std::int64_t>(static_cast<std::int64_t>(pages - 1), g.page.peek() + 1)); }})})}));
    }
    content.push_back(std::move(stage));
    content.push_back(controls(g, c, width));
    if (g.detail.get()) content.push_back(detail(g, c, *info, width));
    return column({.id = "motion_gallery", .width = Dim(width), .gap = 20}, std::move(content));
}
std::string motion_gallery_html(bool zh, bool creative, std::string_view id) {
    const auto* info = find_motion_case(id, creative);
    std::string out = std::format("<section><h1>{}</h1><p>{}</p>", zh ? (creative ? "创意演示" : "基础动画") : (creative ? "Creative demos" : "Basic motion"),
        zh ? "在同一棵语义树上，组合组件、动效和风格。" : "Components, motion and materials composed on one semantic tree.");
    if (info) out += std::format("<h2>{}</h2><p>{}</p><p>{}</p>", escape(zh ? info->zh : info->en), escape(zh ? info->purpose_zh : info->purpose_en), escape(info->components));
    else for (const auto& entry : creative ? creative_motions() : basic_motions())
        out += std::format("<p><a href=\"#/{}{}/{}\">{}</a> — {}</p>", zh ? "zh/" : "", creative ? "demos" : "motion", entry.id, escape(zh ? entry.zh : entry.en), escape(zh ? entry.purpose_zh : entry.purpose_en));
    return out + "</section>";
}
}  // namespace omui::site
