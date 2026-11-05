module openmui.site;
import std;
import openmui;
import :motion_gallery;

namespace omui::site {
namespace {
Runtime* runtime(const MotionGalleryContext& c) { return c.runtime ? *c.runtime : nullptr; }
animation::Playback* player(MotionGallery& g) {return g.component && g.component->playback?&*g.component->playback:nullptr;}
void record(MotionGallery& g,std::expected<void,animation::PlaybackFailure> result) {
    if(!result && g.component)g.component->error.set("This animation operation is unavailable.");
}
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
    if(g.authored_program){if(auto* p=player(g))record(g,p->rate((p->status().native->rate<0?-1:1)*std::clamp(g.speed.peek(),.1,3.)));return;}
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
    if(g.component)g.component->playback.reset();
    g.active_id = info.id; g.creative = creative; g.stage_root = c.root + "/stage"; g.scene_root = g.stage_root + "/scene";
    g.authored_program=info.authored_program;
    g.scene = MotionSceneState{};
    if(g.authored_program) {
        if(!g.component)g.component=std::make_shared<ComponentMotion>();
        g.scene.text.set(c.lang && c.lang->peek()=="zh"?"下一件想做的事":"My next idea");g.scene.count.set(0);
    }
    g.pending = false; g.started = false; g.resetting = false; g.resume_on_view = false; g.stage = {}; g.direction = 1; g.seek_target = -1;
    g.interactive.set(false); g.playing.set(play_intent); g.progress.set(0); g.page.set(0);
    g.style.set(std::string(info.style)); g.curve.set("default"); g.stagger.set(-1); g.bend.set(info.bend);
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
                if(g.authored_program){motion_gallery_seek(g,c,1);return;}
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
    if(info.authored_program && g.component) {
        auto study=g.component;const float inner=std::max(160.f,width-40);
        return column({.id="details",.padding=20,.gap=16,.background="surface_variant",.radius=16},{
            text({.id="explanation",.text=words(c,"Choose what moves. Editing the recipe restarts its preview; your note stays intact.",
                "选择哪些内容参与动画。更改配方会重新预览，便笺内容保持不变。"),.max_width=Dim(inner)}),
            select({.id="selection",.label=words(c,"Animate","动画对象"),
                .labels=std::vector<std::string>{words(c,"Whole note","整张便笺"),words(c,"Title and button","标题与按钮"),words(c,"Title only","仅标题")},
                .options=std::vector<std::string>{"together","parts","title"},.value=study->selection,
                .on_changed=[&g](std::string){g.progress.set(0);g.direction=1;g.playing.set(true);g.interactive.set(false);},.width=Dim(std::min(inner,260.f))}),
            toggle({.id="loop",.checked=g.loop,.label=words(c,"Loop while visible","可见时循环")}),
            slider({.id="speed",.label=words(c,"Playback speed","播放速度"),.max=2.,.min=.25,.step=.25,.value=g.speed,
                .on_changed=[&g,c](double){rates(g,runtime(c));},.width=Dim(inner)}),
            select({.id="style",.label=words(c,"Style in this scene","此场景的风格"),
                .labels=std::vector<std::string>{words(c,"Plain","原色"),words(c,"Glass","玻璃"),words(c,"Neon","霓虹"),words(c,"Retro","复古"),words(c,"Ink","水墨"),words(c,"Liquid","流体")},
                .options=std::vector<std::string>{"plain","glass","neon","retro","ink","liquid"},.value=g.style,
                .on_changed=[&g,c](std::string pack){motion_gallery_pause(g,c);if(c.style)c.style(std::move(pack));},.width=Dim(std::min(inner,260.f))}),
            text({.id="config-label",.text=words(c,"Reusable animation recipe · C++","可复用动画配方 · C++"),.style="label"}),
            text({.id="config-scope",.text=words(c,"Bind your nodes as card, title and save with omui.hanim. This recipe changes presentation; controls keep their data and feedback. Style is configured separately.",
                "通过 omui.hanim 将节点绑定为 card、title 和 save。配方改变呈现，控件保留数据与反馈；风格单独设置。"),.style="caption",.max_width=Dim(inner)}),
            code_editor({.id="config",.language=CodeEditorLanguage::cpp,.lines=8,.readonly=true,
                .text=computed([study,&g]{return component_motion_source(study->selection.get(),g.loop.get(),g.speed.get());}),.width=Dim(inner)}),
            button({.id="copy",.text=words(c,"Copy recipe","复制配方"),.on_activated=[study,&g,c]{if(auto* rt=runtime(c))rt->set_clipboard_text(component_motion_source(study->selection.peek(),g.loop.peek(),g.speed.peek()));g.copied.set(true);}}),
            text({.id="status",.text=study->error,.style="caption",.color="text_muted",.max_width=Dim(inner)})});
    }
    auto recompose = [&g, c] { g.interactive.set(false); g.playing.set(true); g.resetting = false; g.direction = 1; begin(g, c); };
    std::vector<Element> children{
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
                .on_changed = [recompose](double) { recompose(); }, .width = Dim(std::max(100.0f, width - 40))})};
    if (info.vertex_path) children.push_back(slider({.id = "bend", .label = words(c, "Path bend", "路径弯曲"),
        .max = .3, .min = -.3, .step = .01, .value = g.bend, .on_changed = [recompose](double) { recompose(); },
        .width = Dim(std::max(100.0f, width - 40))}));
    children.push_back(select({.id = "style", .label = words(c, "Style in this scene", "此场景的风格"),
        .labels = std::vector<std::string>{words(c,"Plain","原色"),words(c,"Glass","玻璃"),words(c,"Neon","霓虹"),words(c,"Retro","复古"),words(c,"Ink","水墨"),words(c,"Liquid","流体")},
        .options = std::vector<std::string>{"plain","glass","neon","retro","ink","liquid"}, .value = g.style,
        .on_changed = [c,recompose](std::string pack) { if (c.style) c.style(std::move(pack)); recompose(); }, .width = 200}));
    std::vector<Element> configuration{
        text({.id = "config-label", .text = words(c, "Motion configuration · JSON", "动效配置 · JSON"), .style = "label"}),
        text({.id = "config-scope", .text = words(c, "This is the scene root's configuration, not a complete exported project. Controls retain their framework defaults.",
            "这是场景根节点的配置，并非完整工程导出；控件保留框架默认配方。"), .style = "caption", .color = "text_muted", .max_width = Dim(std::max(1.0f, width - 40))}),
        text({.id = "style-scope", .text = words(c, "Style is applied to this scene separately from the copied motion configuration.",
            "风格仅应用到此场景，需单独设置，不包含在复制的动效配置中。"), .style = "caption", .color = "text_muted", .max_width = Dim(std::max(1.0f, width - 40))}),
        code_editor({.id = "config", .language = CodeEditorLanguage::json, .lines = 5, .readonly = true,
                     .text = computed([&g, info] { (void)g.curve.get(); (void)g.stagger.get(); (void)g.bend.get(); return motion_gallery_config(g, info).json(); }), .width = Dim(std::max(100.0f, width - 40))}),
        button({.id = "copy", .text = words(c, "Copy configuration", "复制配置"), .on_activated = [&g, c, info] { if (auto* rt = runtime(c)) rt->set_clipboard_text(motion_gallery_config(g, info).json()); g.copied.set(true); }})};
    for (auto& child : configuration) children.push_back(std::move(child));
    return column({.id = "details", .padding = 20, .gap = 16, .background = "surface_variant", .radius = 16}, std::move(children));
}
}  // namespace

const MotionCaseInfo* find_motion_case(std::string_view id, bool creative) {
    for (const auto& info : creative ? creative_motions() : basic_motions()) if (info.id == id) return &info;
    return nullptr;
}
Value motion_gallery_config(const MotionGallery& g, const MotionCaseInfo& info) {
    if(info.authored_program)return Value("none"); // only this root's state-morph slot; the public recipe is copied separately
    const std::string_view order = g.stagger.peek() > 0 && info.order == "together" ? "document" : info.order;
    Value main = Value{}.set("use", info.use).set("order", order).set("stagger", g.stagger.peek() < 0 ? info.stagger : g.stagger.peek())
        .set("rise", info.rise).set("grow", info.grow).set("stretch", info.stretch);
    if (g.curve.peek() != "default") main.set("curve", g.curve.peek());
    else if (!info.curve.empty()) main.set("curve", info.curve);
    if (info.vertex_path) main.set("vertex_path", Value{}.set("bend", g.bend.peek()));
    if (info.use == "liquid" || info.use == "ink") return Value(Value::List{main, Value("simple")});
    return main;
}
void motion_gallery_cancel(MotionGallery& g, const MotionGalleryContext& c) {
    // Preserve intent across an unmount. Repeated inactive ticks must not
    // overwrite a playing intent with the suspended transport's false.
    g.resume_on_view = g.resume_on_view || g.playing.peek();
    if(g.authored_program) {
        if(g.component)g.component->playback.reset();
        g.playing.set(false);g.started=false;return;
    }
    finish_pending(g, runtime(c));
    release(g, runtime(c), true); g.started = false; g.resetting = false; g.playing.set(false);
}
void motion_gallery_play(MotionGallery& g, const MotionGalleryContext& c) {
    if(g.authored_program) {
        g.resume_on_view=false;g.interactive.set(false);g.playing.set(true);
        if(auto* p=player(g))record(g,p->resume());
        if(auto* rt=runtime(c))rt->request_frame();return;
    }
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
    if(g.authored_program){g.resume_after_interaction=false;if(auto* p=player(g))record(g,p->pause());return;}
    if (auto* rt = runtime(c)) for (const auto& h : g.held) rt->pause_motion(h.node);
}
void motion_gallery_reverse(MotionGallery& g, const MotionGalleryContext& c) {
    g.direction = -g.direction;
    if(g.authored_program){if(auto* p=player(g))record(g,p->reverse());return;}
    if (g.held.empty()) { g.seek_target = g.direction < 0 ? 1 : 0; if (!g.pending) begin(g, c); }
    rates(g, runtime(c));
}
void motion_gallery_seek(MotionGallery& g, const MotionGalleryContext& c, double position) {
    position = std::clamp(position, 0.0, 1.0); g.progress.set(position); g.playing.set(false);
    if(g.authored_program) {
        g.resume_after_interaction=false;g.resume_on_view=false;
        if(auto* p=player(g))record(g,p->seek_progress(position));
        if(auto* rt=runtime(c))rt->request_frame();return;
    }
    if (g.held.empty()) { g.seek_target = position; if (!g.pending) begin(g, c); return; }
    if (auto* rt = runtime(c)) for (const auto& h : g.held) { rt->pause_motion(h.node); rt->seek_motion(h.node, position * g.duration); }
}
void motion_gallery_reset(MotionGallery& g, const MotionGalleryContext& c) {
    if(g.authored_program) {
        motion_gallery_seek(g,c,0);g.direction=1;g.interactive.set(false);
        if(auto* p=player(g))record(g,p->rate(g.speed.peek()));return;
    }
    release(g, runtime(c), true); g.interactive.set(false); g.playing.set(false); g.resume_on_view = false; g.direction = 1;
    g.scene.end.set(false); g.resetting = true; g.pending = true; g.seek_target = -1;
    if (auto* rt = runtime(c)) { g.requested_at = rt->now().count(); rt->request_frame(); }
}
void motion_gallery_interact(MotionGallery& g, const MotionGalleryContext& c, bool interactive) {
    if (g.interactive.peek() == interactive) return;
    g.automatic_interaction = false;
    if(g.authored_program) {
        if(interactive){g.resume_after_interaction=g.playing.peek();g.playing.set(false);}
        else if(g.resume_after_interaction)g.playing.set(true);
        g.interactive.set(interactive);
        if(auto* rt=runtime(c))rt->request_frame();return;
    }
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
void motion_gallery_update(MotionGallery& g,const MotionGalleryContext& c) {
    auto* rt=runtime(c);auto* native=player(g);
    if(!g.authored_program || !rt || !native || !native->attached())return;
    // Existing bindings can reconfigure before the current paint. First
    // attachment waits for the post-paint observer's actual node boundaries.
    const bool wanted=native->status().requested || g.playing.peek();
    if(!prepare_component_motion(*g.component,*rt,g.scene_root,g.progress.peek(),g.direction,g.speed.peek(),g.loop.peek(),wanted))return;
    const bool interacting=g.interactive.peek() || (rt->valid(g.stage) && (rt->state(g.stage).hovered || contains_focus(*rt,g.stage)));
    const auto updated=player(g)->update({.visible=g.visible.peek() && on_screen(*rt,g.stage),
        .foreground=!c.foreground || *c.foreground,.interacting=interacting});
    if(!updated){record(g,updated);motion_gallery_pause(g,c);}
}
void motion_gallery_tick(MotionGallery& g, const MotionGalleryContext& c) {
    auto* rt = runtime(c); if (!rt || g.stage_root.empty()) return;
    // find() currently materialises a path string. Keep the stable node
    // handle on ordinary ticks; resolve again only after unmount/rebuild.
    const NodeId stage = rt->valid(g.stage) ? g.stage : rt->find(g.stage_root);
    if (!rt->valid(stage)) { motion_gallery_cancel(g, c); g.stage = {}; return; }
    if (stage != g.stage) { g.stage = stage; g.started = false; }
    const bool visible = g.visible.peek() && on_screen(*rt, stage);
    if(g.authored_program && g.component) {
        const bool focused=contains_focus(*rt,stage);
        if(!g.interactive.peek() && focused){motion_gallery_interact(g,c);g.automatic_interaction=true;}
        if(g.interactive.peek() && g.automatic_interaction && !focused && !rt->state(stage).hovered)motion_gallery_interact(g,c,false);
        const bool wanted=g.playing.peek() || g.resume_on_view || g.resume_after_interaction;
        if(!player(g) || !player(g)->attached()) {
            if(!visible)return;
            if(!prepare_component_motion(*g.component,*rt,g.scene_root,g.progress.peek(),g.direction,g.speed.peek(),g.loop.peek(),wanted))return;
        }
        // Reconcile current visibility at the same latched time. A newly
        // visible scene wakes the next frame, without sampling a later pose
        // after paint or replaying hidden time.
        record(g,player(g)->update({.visible=visible,.foreground=!c.foreground || *c.foreground,
            .interacting=g.interactive.peek() || rt->state(stage).hovered || focused}));
        const auto status=player(g)->status();
        if(status.native) {
            g.duration=status.native->duration.count();g.direction=status.native->rate<0?-1:1;
            if(std::abs(g.progress.peek()-status.native->progress)>.0001)g.progress.set(status.native->progress);
            const bool playing=status.requested && status.suspended==animation::Suspension::none && status.native->state==hanim::PlaybackState::playing;
            if(g.playing.peek()!=playing)g.playing.set(playing);
        }
        g.started=true;return;
    }
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
    if (!info) info = find_motion_case(creative ? "palette-studio" : "shape-morph", creative);
    if (!info && !entries.empty()) info = &entries.front();
    if (!info) return text({.id = "motion_gallery", .text = words(c, "No motion designs are available.", "暂无可用动效。")});
    prepare(g, c, creative, *info);
    const float inset = width < 520 ? 8.0f : 16.0f;
    const float scene_width = std::max(200.0f, width - 2 * inset);
    const float scene_height = info->authored_program ? 360.0f : info->id == "palette-studio" || info->vertex_path ? 300.0f : width < 520 && info->id == "list-grid" ? 280.0f : width < 520 ? 430.0f : 360.0f;
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
         .prop("motion", derive([&g, current] { (void)g.curve.get(); (void)g.stagger.get(); (void)g.bend.get(); return motion_gallery_config(g, current); }));
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
    const auto entries = creative ? creative_motions() : basic_motions();
    const std::string route = std::string(zh ? "zh/" : "") + (creative ? "demos" : "motion");
    const auto* info = find_motion_case(id.empty() ? (creative ? "palette-studio" : "shape-morph") : id, creative);
    // app.cpp owns each outer route section, its anchor and live handoff root.
    // This fragment must not introduce another route target or hide the selected
    // title inside a catalogue. Keep only the overview's directory in the HTML:
    // repeating all 40/20 links on every detail route inflates the Toy package.
    std::string out = std::format("<section class=\"static-gallery\"><h1>{}</h1><p>{}</p>",
        zh ? (creative ? "创意演示" : "基础动画") : (creative ? "Creative demos" : "Basic motion"),
        zh ? "先看一个样张，再按用途选择其他设计。" : "Start with one example, then choose another by its purpose.");
    if (!id.empty()) out += std::format("<p><a href=\"#/{}\">{}</a></p>", route,
        zh ? "返回概览与分类目录" : "Back to the overview and catalogue");
    if (info) {
        out += std::format("<article data-gallery-current=\"{}\" style=\"padding:16px;border:1px solid #dedee4;border-radius:16px\"><h2>{}</h2><p>{}</p>",
            escape(info->id), escape(zh ? info->zh : info->en), escape(zh ? info->purpose_zh : info->purpose_en));
        // Readable HTML endpoints, explicitly static. They do not impersonate
        // a live omui control or a native frame exported by the renderer.
        if (info->id == "shape-morph") {
            out += std::format("<figure data-gallery-preview=\"static\" style=\"margin:16px 0\"><figcaption>{}</figcaption>"
                               "<p aria-hidden=\"true\" style=\"font-size:48px;letter-spacing:16px\">△ → ○</p><p>{}</p></figure>",
                zh ? "静态端点：A · 三角形 → B · 圆形" : "Static endpoints: A · triangle → B · circle",
                zh ? "界面就绪后，可以切换形状，在详情中调整顶点路径并复制配置。" : "When ready, change the shape, then open Details to tune the point path and copy its configuration.");
        } else if (info->id == "list-grid") {
            out += std::format("<figure data-gallery-preview=\"static\" style=\"margin:16px 0\"><figcaption>{}</figcaption><div style=\"display:flex;flex-wrap:wrap;gap:16px\">",
                zh ? "静态状态：同样三项，从列表排成卡片。" : "Static states: the same three items, from a list to cards.");
            for (const bool cards : {false, true}) {
                out += std::format("<div style=\"flex:1 1 12rem;min-width:0\"><p>{}</p><ul style=\"list-style:none;padding:0;margin:0;display:flex;gap:8px;flex-wrap:wrap;flex-direction:{}\">",
                    zh ? (cards ? "B · 卡片" : "A · 列表") : (cards ? "B · Cards" : "A · List"), cards ? "row" : "column");
                for (const auto& labels : {std::pair{"Passport", "证件"}, std::pair{"Tickets", "车票"}, std::pair{"Keys", "钥匙"}})
                    out += std::format("<li style=\"padding:10px;border:1px solid #dedee4;border-radius:10px\">{}</li>", zh ? labels.second : labels.first);
                out += "</ul></div>";
            }
            out += "</div></figure>";
        } else if (info->id == "palette-studio") {
            out += std::format("<figure data-gallery-preview=\"static\" style=\"margin:16px 0\"><figcaption>{}</figcaption>"
                               "<div style=\"display:flex;align-items:center;flex-wrap:wrap;gap:16px;padding:16px 0\">"
                               "<span aria-hidden=\"true\" style=\"display:inline-block;width:64px;height:64px;border-radius:16px;background:#6366f1\"></span>"
                               "<div><p>{}</p><code>#6366f1</code><p>{}</p></div></div></figure>",
                zh ? "静态起始墨色" : "Static starting ink", zh ? "靛蓝" : "Indigo",
                zh ? "界面就绪后，可选择颜色、调整明暗，并复制生成的 CSS 色板。" : "When the view is ready, choose an ink, adjust its contrast and copy the resulting CSS palette.");
        }
        out += std::format("<p>{}</p>", zh ? "动画播放与真实控件交互需要 JavaScript。" : "Animated playback and live controls require JavaScript.");
        if (id.empty()) out += std::format("<p><a href=\"#/{}/{}\">{}</a></p>", route, info->id,
            zh ? "查看这个样张" : "Open this example");
        out += std::format("<details data-gallery-details><summary>{}</summary><dl><dt>{}</dt><dd>{}</dd><dt>{}</dt><dd>{}</dd><dt>{}</dt><dd>{}</dd></dl></details></article>",
            zh ? "查看构成" : "About this design", zh ? "组件" : "Components", escape(info->components),
            zh ? "动效配方" : "Motion recipe", escape(info->use), zh ? "风格" : "Style", escape(info->style));
    } else out += std::format("<p role=\"status\">{}</p>", zh ? "没有找到这个样张。请返回概览，按用途选择一个设计。" : "This example was not found. Return to the overview to choose a design by its purpose.");
    if (id.empty()) {
        out += std::format("<details data-gallery-catalog><summary>{} · {}</summary><div role=\"region\" aria-label=\"{}\" style=\"max-height:24rem;overflow:auto;overscroll-behavior:contain\">",
            zh ? "选择其他设计" : "Choose another design", entries.size(), zh ? "分类目录" : "Design catalogue");
        std::vector<std::string_view> groups;
        for (const auto& entry : entries) if (std::ranges::find(groups, entry.group) == groups.end()) groups.push_back(entry.group);
        for (const auto group : groups) {
            const auto count = std::ranges::count(entries, group, &MotionCaseInfo::group);
            out += std::format("<details data-gallery-group=\"{}\"><summary>{} · {}</summary><ul>", escape(group), escape(group_name(group, zh)), count);
            for (const auto& entry : entries) if (entry.group == group)
                out += std::format("<li><a href=\"#/{}/{}\"{}>{}</a></li>", route, entry.id,
                    info && entry.id == info->id ? " aria-current=\"true\"" : "", escape(zh ? entry.zh : entry.en));
            out += "</ul></details>";
        }
        out += "</div></details>";
    }
    return out + "</section>";
}
}  // namespace omui::site
