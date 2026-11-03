module openmui.site;

import std;
import openmui;

namespace omui::site {
namespace {

auto landing_words(const HomeLandingSite& site, std::string_view en, std::string_view zh) {
    return computed([lang = site.lang, en = std::string(en), zh = std::string(zh)] { return lang && lang->get() == "zh" ? zh : en; });
}

void landing_agent(HomeLanding& state, const HomeLandingSite& site) {
    state.manual.set(true);
    if (!site.runtime || !*site.runtime) { state.outcome.set("unavailable"); return; }
    auto& runtime = **site.runtime;
    Invocation request;
    request.actor = Actor{"agent", "home-scripted-demo", {}};
    const auto answer = runtime.act(runtime.find(site.root + "/scene/controls/assemble"), "activate", request);
    state.outcome.set(answer.exit_code() == std::optional<int>(0) ? "acted" : "blocked");
}

Element landing_actions(const HomeLandingSite& site) {
    return row({.id = "actions", .gap = 12, .align = "center", .wrap = "wrap"}, {
        button({.id = "explore", .text = landing_words(site, "Explore components", "探索组件"), .variant = ButtonVariant::primary,
            .on_activated = [go = site.go] { if (go) go("components"); }}),
        button({.id = "creative", .text = landing_words(site, "Creative demos", "创意演示"), .variant = ButtonVariant::ghost,
            .on_activated = [go = site.go] { if (go) go("explore"); }})});
}

Element landing_scene(HomeLanding& state, const HomeLandingSite& site, float width, bool narrow) {
    auto* model = &state;
    const float size = std::min(narrow ? 188.0f : 248.0f, width * 0.64f);
    std::vector<Element> controls{
        button({.id = "assemble", .text = computed([model, lang = site.lang] {
            const bool zh = lang && lang->get() == "zh";
            return std::string(model->unfolded.get() ? (zh ? "重新组装" : "Bring together") : (zh ? "展开标志" : "Take apart"));
        }), .variant = ButtonVariant::secondary, .on_activated = [model] {
            model->manual.set(true); model->unfolded.set(!model->unfolded.peek()); model->outcome.set("");
        }}),
        dynamic([model, site] {
            if (model->mode.get() != "agent") return std::vector<Element>{};
            return std::vector<Element>{button({.id = "delegate", .text = landing_words(site, "Let the agent do it", "让 agent 来操作"), .variant = ButtonVariant::primary,
                .on_activated = [model, site] { landing_agent(*model, site); }})};
        }),
        button({.id = "pause", .disabled = computed([model] { return model->status.get() == "reduced"; }), .text = computed([model, lang = site.lang] {
            const bool zh = lang && lang->get() == "zh";
            return std::string(model->paused.get() || model->manual.get() ? (zh ? "播放预览" : "Play preview") : (zh ? "暂停预览" : "Pause preview"));
        }), .variant = ButtonVariant::ghost, .on_activated = [model, site] {
            const bool resume = model->paused.peek() || model->manual.peek();
            model->paused.set(!resume); model->manual.set(false);
            if (site.runtime && *site.runtime) (*site.runtime)->focus((*site.runtime)->find(site.root + "/scene/controls/pause"));
        }})};
    Element scene = column({.id = "scene", .width = Dim(width), .gap = narrow ? 14.0f : 20.0f, .align = "center"}, {
        segmented({.id = "perspective", .label = landing_words(site, "Perspective", "视角"),
            .labels = computed([lang = site.lang] { return lang && lang->get() == "zh" ? std::vector<std::string>{"看见", "读懂", "协作"} : std::vector<std::string>{"Pixels", "Structure", "Agent"}; }),
            .options = std::vector<std::string>{"pixels", "structure", "agent"}, .value = state.mode,
            .on_changed = [model](std::string) { model->outcome.set(""); }, .width = Dim(std::min(width, 306.0f))}),
        stack({.id = "visual", .width = Dim(width), .height = Dim(narrow ? 238.0f : 320.0f), .justify = "center", .align = "center"}, {
            animated_brand_mark("mark", size, state.unfolded)}),
        text({.id = "caption", .text = computed([model, lang = site.lang] {
            const bool zh = lang && lang->get() == "zh";
            if (model->mode.get() == "structure") return model->part_names.get() + (model->unfolded.get() ? (zh ? " · 已展开" : " · Apart") : (zh ? " · 已组装" : " · Together"));
            if (model->mode.get() == "agent") return std::string(zh ? "脚本 agent 示范，操作下面同一个真实按钮。" : "A simulated agent uses the same real button below.");
            return std::string(zh ? "五条矢量路径，由 omui 自己绘制。" : "Five vector paths. Drawn by omui itself.");
        }), .style = "caption", .color = "text_muted", .align = "center", .max_width = Dim(width)}),
        row({.id = "controls", .width = Dim(width), .gap = 8, .justify = "center", .align = "center", .wrap = "wrap"}, std::move(controls)),
        text({.id = "status", .text = computed([model, lang = site.lang] {
            const bool zh = lang && lang->get() == "zh";
            if (model->outcome.get() == "acted") return std::string(zh ? "已由脚本 agent 完成，动作可在 actor 记录中查看。" : "The simulated agent acted. Its action is in the actor log.");
            if (model->outcome.get() == "blocked") return std::string(zh ? "这次操作未获允许，标志保持原状。" : "This action was not allowed. The mark is unchanged.");
            if (model->outcome.get() == "unavailable") return std::string(zh ? "当前宿主没有提供运行时操作。" : "This host does not provide runtime actions.");
            const auto status = model->status.get();
            if (status == "reduced") return std::string(zh ? "尊重你的减少动态效果设置，仍可直接操作。" : "Reduced motion respected. The controls remain usable.");
            if (status == "paused") return std::string(zh ? "预览已暂停，可随时操作。" : "Preview paused. You can still use it.");
            if (status == "interactive") return std::string(zh ? "现在由你操作。" : "Now it is yours to use.");
            return std::string(zh ? "移入或聚焦这个区域，即可交互。" : "Move into or focus this area to interact.");
        }), .style = "caption", .color = "text_muted", .align = "center", .max_width = Dim(width)})});
    return scene;
}
}  // namespace

Element home_landing(HomeLanding& state, const HomeLandingSite& site, float width) {
    if (!state.introduced && site.runtime && *site.runtime && ((*site.runtime)->reduce_motion() || (*site.runtime)->motion_level() != MotionLevel::full)) {
        state.unfolded.set(false); state.introduced = true;
    }
    state.scene_path = site.root + "/scene";
    state.visual_path = state.scene_path + "/visual";
    state.pause_path = state.scene_path + "/controls/pause";
    const bool narrow = width < 900;
    const float gap = 64, copy_width = narrow ? width : (width - gap) * 0.46f;
    const float scene_width = narrow ? width : width - copy_width - gap;
    std::vector<Element> words{
        text({.id = "eyebrow", .text = "omui", .style = "label", .color = "accent"}),
        text({.id = "title", .text = landing_words(site, "One interface.\nEvery perspective.", "一个界面，\n不止一种可能。"), .style = "heading", .size = narrow ? (width < 370 ? 34.0f : 38.0f) : 48.0f, .max_width = Dim(copy_width)}),
        text({.id = "lead", .text = landing_words(site, "Build once. Let people and agents use the same UI.", "一次构建，让人与 agent 操作同一界面。"),
            .style = "body", .color = "text_muted", .max_width = Dim(copy_width)})};
    words[1].prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{1})));
    words[1].prop("label", derive([lang = site.lang] { return lang && lang->get() == "zh" ? std::string("一个界面，不止一种可能。") : std::string("One interface. Every perspective."); }));
    if (!narrow) words.push_back(landing_actions(site));
    Element copy = column({.id = "intro", .width = Dim(copy_width), .gap = narrow ? 16.0f : 24.0f}, std::move(words));
    Element scene = landing_scene(state, site, scene_width, narrow);
    if (!narrow) return row({.id = "landing", .width = Dim(width), .min_height = Dim(560), .gap = gap, .align = "center"}, {std::move(copy), std::move(scene)});
    return column({.id = "landing", .width = Dim(width), .gap = 28}, {std::move(copy), std::move(scene), landing_actions(site)});
}

void cancel_home_landing(HomeLanding& state, const HomeLandingSite& site) {
    if (state.timer && site.runtime && *site.runtime) (*site.runtime)->cancel(std::exchange(state.timer, 0));
    ++state.generation;
}

void tick_home_landing(HomeLanding& state, const HomeLandingSite& site) {
    if (!site.runtime || !*site.runtime) return;
    auto& runtime = **site.runtime;
    const auto preview = runtime.find(state.visual_path);
    if (preview != state.preview) { cancel_home_landing(state, site); state.preview = preview; state.reading_active = false; }
    if (runtime.valid(preview) && state.mode.peek() == "structure") {
        if (!state.reading_active) {
            semantic::Tree tree(runtime);
            std::string names;
            for (const auto& node : tree.snapshot({.root = state.visual_path}).nodes) if (node.role == Role::image) {
                if (!names.empty()) names += " · ";
                names += node.name;
            }
            state.part_names.set(std::move(names)); state.reading_active = true;
        }
    } else state.reading_active = false;
    std::string_view status = "playing";
    const auto route = parse_site_route(runtime.route());
    if (!runtime.valid(preview) || !route || route->place != "home") status = "away";
    else if (runtime.reduce_motion() || runtime.motion_level() != MotionLevel::full) {
        if (!state.introduced) { state.unfolded.set(false); state.introduced = true; }
        status = "reduced";
    } else {
        const auto scene = runtime.find(state.scene_path);
        bool focused = false;
        for (auto node = runtime.focused(); runtime.valid(node); node = runtime.parent(node)) {
            if (node == runtime.find(state.pause_path)) break;
            if (node == scene) { focused = true; break; }
        }
        const auto bounds = runtime.screen_bounds(preview);
        const auto view = runtime.screen_bounds(runtime.find(site.root));
        const auto window = Rect{0, 64, runtime.size().width, std::max(0.0f, runtime.size().height - 104)};
        const auto visible = bounds.intersection(view).intersection(window);
        if (visible.width * visible.height < bounds.width * bounds.height * 0.4f) status = "offscreen";
        else if (state.manual.peek() || runtime.state(scene).hovered || focused) status = "interactive";
        else if (state.paused.peek()) status = "paused";
        // Reading the shared structure or preparing an agent action should
        // not change that object while someone is inspecting it.
        else if (state.mode.peek() != "pixels") status = "inspecting";
    }
    if (state.status.peek() != status) state.status.set(std::string(status));
    if (status != "playing") { if (state.timer) cancel_home_landing(state, site); return; }
    if (state.timer) return;
    const bool intro = !state.introduced;
    const auto generation = state.generation;
    const auto lifetime = std::weak_ptr<unsigned char>(state.lifetime);
    state.timer = runtime.after(seconds(intro ? 0.18 : 3.2), [model = &state, site, generation, lifetime, intro] {
        if (lifetime.expired() || model->generation != generation) return;
        model->timer = 0;
        if (intro) model->introduced = true;
        tick_home_landing(*model, site);
        if (model->status.peek() != "playing") return;
        if (intro) model->unfolded.set(false);
        else model->unfolded.set(!model->unfolded.peek());
    });
}

std::string home_landing_html(bool zh) {
    return zh ? "<p class=\"landing-eyebrow\">omui</p><h1 aria-label=\"一个界面，不止一种可能。\">一个界面，<br>不止一种可能。</h1><p class=\"landing-lead\">一次构建，让人与 agent 操作同一界面。</p><p class=\"landing-actions\"><a href=\"#/zh/components\">探索组件</a><a href=\"#/zh/demos\">创意演示</a></p>"
              : "<p class=\"landing-eyebrow\">omui</p><h1 aria-label=\"One interface. Every perspective.\">One interface.<br>Every perspective.</h1><p class=\"landing-lead\">Build once. Let people and agents use the same UI.</p><p class=\"landing-actions\"><a href=\"#/components\">Explore components</a><a href=\"#/demos\">Creative demos</a></p>";
}

}  // namespace omui::site
