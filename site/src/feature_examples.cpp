module openmui.site;

import std;
import openmui;
import :feature_examples;
import :gallery;

namespace omui::site {
namespace {

Runtime* feature_runtime(const FeatureExamplesContext& c) { return c.runtime ? *c.runtime : nullptr; }
std::string feature_words(const FeatureExamplesContext& c, std::string_view en, std::string_view zh) {
    return std::string(c.lang && c.lang->get() == "zh" ? zh : en);
}
Element feature_text(std::string id, std::string value, float width, std::string style = "body") {
    TextOptions o; o.id = std::move(id); o.text = std::move(value); o.style = std::move(style); o.max_width = Dim(width);
    return text(std::move(o));
}
Element feature_heading(const FeatureExamplesContext& c, std::string_view en, std::string_view zh, float width) {
    auto e = feature_text("heading", feature_words(c, en, zh), width, "heading");
    e.prop("role", Source(Value("heading"))).prop("level", Source(Value(1))); return e;
}
Element feature_button(std::string id, std::string label, std::function<void()> action, ButtonVariant variant = ButtonVariant::secondary) {
    ButtonOptions o; o.id = std::move(id); o.text = std::move(label); o.variant = variant; o.on_activated = std::move(action);
    return button(std::move(o));
}
Element feature_actions(std::string id, std::vector<Element> children) {
    return row({.id = std::move(id), .gap = 8, .align = "center", .wrap = "wrap"}, std::move(children));
}
Element preview_controls(FeatureExamples* s, const FeatureExamplesContext& c, float width) {
    auto pause = feature_button("preview_pause", s->paused.get() ? feature_words(c, "Resume preview", "继续预览") : feature_words(c, "Pause preview", "暂停预览"),
        [s] { s->paused.set(!s->paused.peek()); }, ButtonVariant::ghost);
    return row({.id = "preview_controls", .width = Dim(width), .gap = 8, .align = "center", .wrap = "wrap"}, {
        feature_text("mode", s->interacting.get() ? feature_words(c, "Interactive", "交互中") : s->paused.get() ? feature_words(c, "Preview paused", "预览已暂停") : feature_words(c, "Scripted preview", "脚本预览"), std::max(80.0f, width - 150), "caption"), std::move(pause)});
}
Element source_text(std::string id, std::string content, float width) {
    auto e = feature_text(std::move(id), std::move(content), width, "code");
    e.prop("origin", Source(Value("user_content"))); return e;
}
void interaction(Element& e, FeatureExamples* s, std::string sample) {
    e.on("focus", [s, sample](const Value&) { s->interacting.set(true); s->active_preview.set(sample); });
}
void preview_interaction(Element& e, FeatureExamples* s, std::string sample) {
    e.on("pointer_enter", [s, sample](const Value&) { s->interacting.set(true); s->active_preview.set(sample); });
    // No focus(), synthetic pointer or input interception: native controls
    // enter interaction by normal hover, Tab, touch or semantic actions.
    e.on("pointer_leave", [s, sample](const Value&) {
        if (s->active_preview.peek() == sample) { s->active_preview.set(""); s->interacting.set(false); }
    });
}
void mark(FeatureExamples* s, const FeatureExamplesContext& c) {
    const auto before = s->marks.peek(); s->marks.set(before + 1);
    if (auto* rt = feature_runtime(c)) rt->taken_back_by([s, before] { s->marks.set(before); }, [s, before] { s->marks.set(before + 1); });
}
Element quiet_control(FeatureExamples* s, const FeatureExamplesContext& c, float width, std::string preview) {
    ToggleOptions o; o.id = "quiet"; o.label = feature_words(c, "Quiet", "安静"); o.checked = s->quiet; o.width = Dim(width);
    auto e = toggle(std::move(o)); interaction(e, s, std::move(preview)); return e;
}
Element level_control(FeatureExamples* s, const FeatureExamplesContext& c, float width, std::string preview) {
    SliderOptions o; o.id = "level"; o.label = feature_words(c, "Level", "强度"); o.min = 0; o.max = 100; o.step = 1; o.value = s->intensity; o.width = Dim(width);
    auto e = slider(std::move(o)); interaction(e, s, std::move(preview)); return e;
}
Element mark_control(FeatureExamples* s, const FeatureExamplesContext& c, std::string preview) {
    auto e = feature_button("mark", feature_words(c, "Mark", "记号"), [s, c] { mark(s, c); }, ButtonVariant::primary);
    interaction(e, s, std::move(preview)); return e;
}
Element title_control(FeatureExamples* s, const FeatureExamplesContext& c, float width, std::string preview) {
    TextBoxOptions o; o.id = "title"; o.label = feature_words(c, "Note title", "笔记标题"); o.max_length = 48; o.text = s->title; o.width = Dim(width);
    auto e = text_box(std::move(o)); interaction(e, s, std::move(preview)); return e;
}

Value chart_effect(const FeatureExamples& state) {
    return Value{}.set("name", "pixelate").set("size", state.pixel_size.get())
        .set("amount", state.pixel_amount.get() / 100.).set("device_pixels", state.pixel_device.get() ? 1. : 0.);
}
Element session_backdrop(FeatureExamples* s, const FeatureExamplesContext& c, float width, float height, bool glass, bool scoped) {
    const float level = static_cast<float>(std::clamp(s->intensity.get(), 0.0, 100.0) / 100.0);
    const bool quiet = s->quiet.get(); const auto cues = s->marks.get();
    // Build the geometry when data changes. Painting only submits the stored
    // path and fixed grid; it owns no clock and allocates no path per frame.
    xrender::Path trace;
    trace.move_to({0, 184}).line_to({width * 0.19f, 126}).line_to({width * 0.38f, 188 - level * 55})
        .line_to({width * 0.57f, 115 + level * 45}).line_to({width * 0.78f, 200 - level * 70}).line_to({width, 138});
    PaintOptions chart; chart.id = "chart"; chart.width = Dim(width); chart.height = Dim(height);
    chart.label = feature_words(c, "Local session level and cue chart", "本地会话强度与记号图");
    chart.paint = [trace = std::move(trace), level, quiet, cues, width, height, glass](PaintContext& ctx) {
        ctx.list.fill_rect({0, 0, width, height}, glass ? Color::hex(0x102638) : ctx.theme.color("surface"));
        const auto grid = glass ? Color::hex(0x8bd3e6, 0.46f) : ctx.theme.color("text_muted").with_alpha(0.25f);
        for (int i = 0; i < 9; ++i) ctx.list.fill_rect({static_cast<float>(i) * width / 8, 88, 1, height - 88}, grid);
        for (int i = 0; i < 7; ++i) ctx.list.fill_rect({0, 96 + static_cast<float>(i) * 26, width, 1}, grid);
        const auto signal = glass ? Color::hex(quiet ? 0x67e8f9 : 0xfbbf24) : ctx.theme.color("accent");
        ctx.list.stroke_path(trace, {.width = 5, .cap = xrender::LineCap::round, .join = xrender::LineJoin::round}, xrender::Paint::solid(signal.with_alpha(0.85f)));
        const int marks = static_cast<int>(std::clamp<std::int64_t>(cues, 0, 6));
        for (int i = 0; i < marks; ++i) ctx.list.fill_rect({width * (static_cast<float>(i) + 1) / 7, 92, 3, height - 100}, Color::hex(0xfbbf24, 0.7f));
        ctx.list.fill_rect({0, height - 10, width * level, 5}, signal);
    };
    const float inside = std::max(68.0f, width - 24);
    auto series = feature_text("series", feature_words(c, std::format("{:.0f} / 100 · {} cues", s->intensity.get(), cues), std::format("{:.0f} / 100 · {} 记号", s->intensity.get(), cues)), inside, "caption");
    series.prop("translate_x", Source(Value(12.0))).prop("translate_y", Source(Value(height - 38))).prop("size", Source(Value(11.0)));
    auto drawing = painter(std::move(chart));
    // Labels and real controls remain siblings in their original paint order.
    if (scoped) drawing.prop("pixel_effect", Source(chart_effect(*s)));
    auto e = stack({.id = "backdrop", .width = Dim(width), .height = Dim(height)}, {std::move(drawing), std::move(series)});
    e.prop("pointer_events", Source(Value("none"))); return e;
}
Element notification_tabs(FeatureExamples* s, const FeatureExamplesContext& c, float width, std::string preview) {
    TabsOptions o; o.id = "lens"; o.variant = TabsVariant::bar; o.width = Dim(width); o.label = feature_words(c, "Notifications", "通知");
    o.selected = Source{std::nullopt, [s] { return Value(s->quiet.get() ? "off" : "on"); }, [s](const Value& value) { s->quiet.set(value.as_string() == "off"); }};
    std::vector<Element> items;
    for (const auto& [id, en, zh] : {std::tuple{"off", "Off", "静"}, std::tuple{"on", "On", "响"}}) {
        TabOptions item; item.id = id; item.text = feature_words(c, en, zh); item.grow = 1;
        auto tab_item = tab(std::move(item)); tab_item.prop("padding", Source(Value(4.0))); interaction(tab_item, s, preview); items.push_back(std::move(tab_item));
    }
    return tabs(std::move(o), std::move(items));
}
Element control_group(FeatureExamples* s, const FeatureExamplesContext& c, std::string id, std::string label, float width, bool scoped) {
    const float inside = std::max(68.0f, width - 26);
    constexpr float height = 280;
    auto backdrop = session_backdrop(s, c, width, height, scoped && s->style_name.get() == "glass", scoped);
    auto mark_button = mark_control(s, c, id); mark_button.prop("variant", Source(Value("ghost")));
    auto controls = column({.id = "controls", .width = Dim(width), .height = Dim(height), .padding = Edges(12), .gap = 8}, {
        feature_text("label", std::move(label), inside, "label"), quiet_control(s, c, inside, id), level_control(s, c, inside, id), std::move(mark_button), notification_tabs(s, c, inside, id)});
    auto e = stack({.id = id, .width = Dim(width), .height = Dim(height), .background = "surface", .border_color = "border", .border_width = 1,
        .radius = static_cast<float>(std::clamp(s->radius.get(), 4.0, 28.0)), .clip = true}, {std::move(backdrop), std::move(controls)});
    e.prop("undo_scope", Source(Value(true))); preview_interaction(e, s, id);
    if (scoped) e.prop("theme_scope", Source(Value(c.scope)));
    return e;
}

Element useful_preview(FeatureExamples* s, const FeatureExamplesContext& c, std::string id, float width, bool note) {
    const float inside = std::max(120.0f, width - 26);
    std::vector<Element> controls{feature_text("heading", note ? feature_words(c, "A reading note", "一张阅读笔记") : feature_words(c, "A quieter session", "一段安静的时间"), inside, "label")};
    if (note) controls.push_back(title_control(s, c, inside, id));
    else controls.push_back(level_control(s, c, inside, id));
    controls.push_back(quiet_control(s, c, inside, id)); controls.push_back(mark_control(s, c, id));
    controls.push_back(feature_text("state", s->active_preview.get() == id ? feature_words(c, "Interactive · your changes are shared", "正在交互 · 修改会同步") : feature_words(c, "Hover or Tab into the real controls", "悬停或 Tab 进入真实控件"), inside, "caption"));
    auto e = column({.id = id, .width = Dim(width), .padding = Edges(12), .gap = 12, .background = "surface", .border_color = "border", .border_width = 1,
        .radius = static_cast<float>(std::clamp(s->radius.get(), 4.0, 28.0)), .clip = true}, std::move(controls));
    e.prop("theme_scope", Source(Value(c.scope))).prop("undo_scope", Source(Value(true))); preview_interaction(e, s, id); return e;
}

std::string object_path(const FeatureExamplesContext& c) { return c.root + "/workspace/object"; }
Value object_snapshot(const FeatureExamplesContext& c) {
    auto* rt = feature_runtime(c); if (!rt || !rt->valid(rt->find(object_path(c)))) return {};
    Value::List nodes;
    for (const auto suffix : {"title", "quiet", "level", "mark", "marks"}) {
        const auto id = rt->find(object_path(c) + "/" + suffix); if (!rt->valid(id)) continue;
        const auto n = rt->semantic_node(id);
        Value::List actions; for (const auto& action : n.actions) actions.emplace_back(action.name);
        Value::List states; for (const auto state : {State::checked, State::disabled, State::readonly}) if (n.states.has(state)) states.emplace_back(std::string(spec::name(state)));
        nodes.push_back(Value{}.set("id", n.id).set("role", std::string(spec::name(n.role))).set("name", n.name).set("value", n.value)
            .set("origin", std::string(spec::name(n.origin))).set("state", Value(std::move(states))).set("actions", Value(std::move(actions))));
    }
    return Value(std::move(nodes));
}
const Value* observed_node(const Value& nodes, std::string_view suffix) {
    if (const auto* list = nodes.list()) for (const auto& node : *list) if (const auto* id = node.get("id"); id && id->as_string().ends_with(suffix)) return &node;
    return nullptr;
}
Value observed_value(const Value& nodes, std::string_view suffix, Value fallback) {
    const auto* node = observed_node(nodes, suffix); const auto* value = node ? node->get("value") : nullptr;
    // Range controls expose their value together with min/max/step.
    if (value) if (const auto* scalar = value->get("value")) return *scalar;
    return value ? *value : fallback;
}
std::string expression(FeatureExamples* s, const FeatureExamplesContext& c) {
    const auto actual = object_snapshot(c); const auto language = s->language.get();
    const auto title = observed_value(actual, "/title", Value(s->title.get()));
    const auto quiet = observed_value(actual, "/quiet", Value(s->quiet.get()));
    const auto level = observed_value(actual, "/level", Value(s->intensity.get()));
    const std::array<Sample, 3> samples{{
        {"text_box", {{"id", "title"}, {"label", feature_words(c, "Note title", "笔记标题")}, {"max_length", 48}, {"text", title}}},
        {"toggle", {{"id", "quiet"}, {"checked", quiet}, {"label", feature_words(c, "Quiet", "安静")}}},
        {"slider", {{"id", "level"}, {"label", feature_words(c, "Level", "强度")}, {"max", 100.0}, {"min", 0.0}, {"step", 1.0}, {"value", level}}}
    }};
    std::string out; for (const auto& sample : samples) out += (out.empty() ? "" : "\n\n") + code_of(language, sample);
    return out;
}
Element language_picker(FeatureExamples* s, float width) {
    SegmentedOptions o; o.id = "language"; o.options = std::vector<std::string>{"cpp", "ts", "python", "lua"};
    o.labels = std::vector<std::string>{"C++", "TS", "Python", "Lua"}; o.value = s->language; o.width = Dim(width); return segmented(std::move(o));
}

Element interface_object(FeatureExamples* s, const FeatureExamplesContext& c, float width) {
    const float inside = std::max(120.0f, width - 34);
    auto e = column({.id = "object", .width = Dim(width), .padding = Edges(16), .gap = 14, .background = "surface", .border_color = "border", .border_width = 1, .radius = 16}, {
        feature_text("label", feature_words(c, "Your workspace", "你的工作台"), inside, "label"), title_control(s, c, inside, "object"), quiet_control(s, c, inside, "object"),
        level_control(s, c, inside, "object"), mark_control(s, c, "object"), feature_text("marks", feature_words(c, std::format("{} local cues", s->marks.get()), std::format("{} 个本地记号", s->marks.get())), inside, "caption")});
    e.prop("undo_scope", Source(Value(true))); preview_interaction(e, s, "object"); return e;
}
Element semantic_readout(FeatureExamples* s, const FeatureExamplesContext& c, float width) {
    const auto& nodes = s->observed.get(); const float inside = std::max(120.0f, width - 34);
    std::vector<Element> content{feature_text("heading", feature_words(c, "The same object's meaning", "同一对象的语义"), inside, "label")};
    if (!nodes.list() || nodes.list()->empty()) content.push_back(feature_text("waiting", feature_words(c, "Reading the mounted controls…", "正在读取已挂载的控件……"), inside, "caption"));
    else {
        auto title = feature_text("title", feature_words(c, "Title: ", "标题：") + observed_value(nodes, "/title", {}).json(), inside);
        title.prop("origin", Source(Value("user_content"))); content.push_back(std::move(title));
        const bool quiet = observed_value(nodes, "/quiet", {}).as_bool(); const double level = observed_value(nodes, "/level", {}).as_double();
        content.push_back(feature_text("quiet", quiet ? feature_words(c, "Quiet · checked", "安静 · 已开启") : feature_words(c, "Quiet · unchecked", "安静 · 已关闭"), inside));
        content.push_back(feature_text("level", feature_words(c, std::format("Level · {:.0f} of 100", level), std::format("强度 · {:.0f} / 100", level)), inside));
        const auto* cues = observed_node(nodes, "/marks"); const auto* cue_name = cues ? cues->get("name") : nullptr;
        if (cue_name) content.push_back(feature_text("cues", std::string(cue_name->as_string()), inside));
        const auto* mark_node = observed_node(nodes, "/mark"); const auto* actions = mark_node ? mark_node->get("actions") : nullptr;
        const bool has_activate = actions && actions->list() && std::ranges::any_of(*actions->list(), [](const Value& x) { return x.as_string() == "activate"; });
        content.push_back(feature_text("action", has_activate ? feature_words(c, "Mark · activate is available", "记号 · 可调用 activate") : feature_words(c, "Reading available actions…", "正在读取可用动作……"), inside, "caption"));
    }
    content.push_back(feature_button("act", feature_words(c, "Act on the real Mark button", "操作真实的记号按钮"), [s, c] { feature_examples_demo_step(*s, c, "interface"); }));
    return column({.id = "meaning", .width = Dim(width), .padding = Edges(16), .gap = 14, .background = "surface_variant", .radius = 16}, std::move(content));
}

}  // namespace

void cancel_feature_examples(FeatureExamples& s) { ++s.generation; s.interacting.set(false); s.active_preview.set(""); }

void observe_feature_examples(FeatureExamples& s, const FeatureExamplesContext& c) {
    auto* rt = feature_runtime(c); if (!rt || c.root.empty()) return;
    const auto root = rt->find(c.root); if (!rt->valid(root)) return;
    const auto revision = rt->semantic_revision();
    const bool refresh = revision != s.seen_revision || s.observed_runtime != rt || s.observed_root != root;
    if (refresh) {
        s.observed_runtime = rt; s.observed_root = root; std::size_t index = 0;
        s.pause_node = rt->find(c.root + "/preview_controls/preview_pause");
        for (const auto suffix : {"comparison/default", "comparison/styled", "previews/sound", "previews/note", "workspace/object"})
            s.preview_nodes[index++] = rt->find(c.root + "/" + suffix);
        // A narrow comparison mounts one full-width specimen. Keep the first
        // handle usable by the visible-page coordinator in either view.
        if (!rt->valid(s.preview_nodes[0])) s.preview_nodes[0] = s.preview_nodes[1];
    }
    bool interacting = false;
    // Explicit Resume must be able to resume while its own button has focus.
    for (auto focus = rt->focused(); focus != s.pause_node && rt->valid(focus); focus = rt->parent(focus)) if (focus == root) { interacting = true; break; }
    for (const auto id : s.preview_nodes) if (rt->valid(id) && rt->state(id).hovered) interacting = true;
    s.interacting.set(interacting);
    if (!interacting && !s.active_preview.peek().empty()) s.active_preview.set("");
    if (!refresh) return;
    s.seen_revision = revision; const auto data = object_snapshot(c); if (data != s.observed.peek()) s.observed.set(data);
}

void feature_examples_demo_step(FeatureExamples& s, const FeatureExamplesContext& c, std::string_view page) {
    if (page != "interface" && page != "styles") return;
    auto* rt = feature_runtime(c); if (!rt || c.root.empty() || !rt->valid(rt->find(c.root))) return;
    auto id = rt->find(page == "interface" ? object_path(c) + "/mark" : c.root + "/comparison/styled/controls/quiet");
    if (page == "styles" && !rt->valid(id)) id = rt->find(c.root + "/comparison/default/controls/quiet");
    if (!rt->valid(id)) return;
    Invocation call; call.actor = {"agent", "feature-preview-simulated", {}}; call.remote = true;
    const auto result = rt->act(id, page == "interface" ? "activate" : "toggle", call);
    s.status.set(result.exit_code() == std::optional<std::int64_t>(0) ? feature_words(c, "Scripted preview · real semantic action accepted · reversible", "脚本预览 · 真实语义动作 · 可撤销") : feature_words(c, "Scripted preview · runtime refused or stopped the action", "脚本预览 · 运行时拒绝或阻止了动作"));
}

Element styles_feature(FeatureExamples& state, const FeatureExamplesContext& context, float width) {
    auto* s = &state; const auto c = context; width = std::max(240.0f, width);
    return column({.id = "styles_feature", .width = Dim(width), .gap = 20}, {dynamic([s, c, width] {
        if (s->applied_style != s->style_name.get() && c.style) { c.style(s->style_name.peek()); s->applied_style = s->style_name.peek(); }
        const bool narrow = width < 620;
        const float half = (width - 12) / 2;
        ComboboxOptions pack; pack.id = "style"; pack.label = feature_words(c, "Material", "材质"); pack.options = std::vector<std::string>{"glass", "ink", "neon", "retro", "liquid"};
        pack.labels = std::vector<std::string>{feature_words(c, "Glass", "玻璃"), feature_words(c, "Ink", "水墨"), feature_words(c, "Neon", "霓虹"), feature_words(c, "Retro", "复古"), feature_words(c, "Liquid", "液态")};
        pack.value = s->style_name; pack.width = Dim(std::min(width, 320.0f)); pack.on_changed = [s, c](std::string name) { if (c.style) { c.style(name); s->applied_style = name; } };
        std::vector<Element> samples;
        if (narrow) {
            SegmentedOptions view; view.id = "view"; view.label = feature_words(c, "Compare material", "材质对照");
            view.options = std::vector<std::string>{"default", "styled"};
            view.labels = std::vector<std::string>{feature_words(c, "Default", "默认"), feature_words(c, "Styled", "材质效果")};
            view.value = s->comparison_view; view.width = Dim(width);
            samples.push_back(segmented(std::move(view)));
            const bool styled = s->comparison_view.get() == "styled";
            samples.push_back(control_group(s, c, styled ? "styled" : "default", styled ? feature_words(c, "Styled", "风格化") : feature_words(c, "Default", "默认"), width, styled));
        } else {
            samples.push_back(control_group(s, c, "default", feature_words(c, "Default", "默认"), half, false));
            samples.push_back(control_group(s, c, "styled", feature_words(c, "Styled", "风格化"), half, true));
        }
        auto comparison = narrow ? column({.id = "comparison", .width = Dim(width), .gap = 12}, std::move(samples))
                                 : row({.id = "comparison", .width = Dim(width), .gap = 12, .align = "start"}, std::move(samples));
        std::vector<Element> content{feature_heading(c, "Same meaning. A new material.", "同一语义，不同材质。", width),
            feature_text("lead", narrow ? feature_words(c, "Switch views to compare the same state. Your changes stay with the object.", "切换视图，对照同一状态。你的修改会保留在同一对象中。")
                                        : feature_words(c, "Both sides share one model. Change either; see the same state through a different style.", "两侧共享一个模型。改变任意一侧，看看相同状态如何以另一种风格呈现。"), width),
            combobox(std::move(pack)), preview_controls(s, c, width),
            feature_text("material_use", feature_words(c, "Notification controls float over your local session's level and cue marks.", "通知控件浮在本地会话的强度曲线与记号之上。"), width, "caption"),
            std::move(comparison),
            feature_actions("actions", {feature_button("demonstrate", feature_words(c, "Try a semantic action", "试一次语义动作"), [s, c] { feature_examples_demo_step(*s, c, "styles"); }),
                feature_button("details", s->styles_details.get() ? feature_words(c, "Close details", "关闭详情") : feature_words(c, "Details", "详情"), [s] { s->styles_details.set(!s->styles_details.peek()); }, ButtonVariant::ghost)})};
        if (!s->status.get().empty()) content.push_back(feature_text("status", s->status.get(), width, "caption"));
        if (s->styles_details.get()) {
            SliderOptions radius; radius.id = "radius"; radius.label = feature_words(c, "Sample corner radius", "样张圆角"); radius.min = 4; radius.max = 28; radius.step = 1; radius.value = s->radius; radius.width = Dim(width);
            SliderOptions amount; amount.id = "pixel_amount"; amount.label = feature_words(c, "Chart pixelation", "图表像素化强度");
            amount.min = 0; amount.max = 100; amount.step = 1; amount.value = s->pixel_amount; amount.width = Dim(width);
            SliderOptions cell; cell.id = "pixel_size"; cell.label = feature_words(c, "Block size", "像素块大小");
            cell.min = 1; cell.max = 16; cell.step = 1; cell.value = s->pixel_size; cell.width = Dim(width);
            ToggleOptions unit; unit.id = "pixel_device"; unit.label = feature_words(c, "Use device pixels", "使用设备像素单位"); unit.checked = s->pixel_device;
            content.push_back(column({.id = "details", .width = Dim(width), .gap = 12}, {
                feature_text("radius_label", feature_words(c, std::format("Corner radius · {:.0f} px", s->radius.get()), std::format("圆角 · {:.0f} px", s->radius.get())), width, "caption"),
                slider(std::move(radius)),
                feature_text("pixel_amount_label", feature_words(c, std::format("Pixelation · {:.0f}%", s->pixel_amount.get()), std::format("像素化强度 · {:.0f}%", s->pixel_amount.get())), width, "caption"),
                slider(std::move(amount)),
                feature_text("pixel_size_label", feature_words(c, std::format("Block size · {:.0f} px", s->pixel_size.get()), std::format("像素块大小 · {:.0f} px", s->pixel_size.get())), width, "caption"),
                slider(std::move(cell)), toggle(std::move(unit)),
                feature_text("pixel_method", feature_words(c, "Pixelate only the styled chart, then combine it with any material above. Labels and controls keep their own drawing. At zero strength the chart is unchanged, including on high-DPI displays.",
                    "只把风格化视图中的图表像素化，可与上面的材质组合。文字与控件保留各自的绘制；强度为零时图表像素保持原样，高 DPI 下也一样。"), width, "caption"),
                source_text("pixel_source", "pixel_effect = " + chart_effect(*s).json(), width),
                feature_text("method", feature_words(c, "The material view and the workspaces below share a scoped style. Glass refracts controls over the drawn backdrop; Ink uses paper and line effects; Neon glows; Retro uses CRT; Liquid merges the slider handle into its track. The renderer chooses a supported tier and respects reduced motion.",
                    "材质视图与下方工作区共享限定作用域的风格。玻璃让控件折射背后的图形；水墨使用纸张与线条效果；霓虹发光；复古使用 CRT；液态让滑块手柄与轨道融合。渲染器按支持的分档呈现，并尊重减少动效设置。"), width, "caption"),
                source_text("scope", "theme::apply_style(runtime, " + Value(s->style_name.get()).json() + ", " + Value(c.scope).json() + ", false);\n// Apply theme_scope only to the specimen roots.", width)}));
            const float sample_width = narrow ? width : (width - 16) / 2;
            auto previews = std::vector<Element>{useful_preview(s, c, "sound", sample_width, false), useful_preview(s, c, "note", sample_width, true)};
            content.push_back(narrow ? column({.id = "previews", .width = Dim(width), .gap = 16}, std::move(previews)) : row({.id = "previews", .width = Dim(width), .gap = 16, .align = "start"}, std::move(previews)));
        }
        return content;
    })});
}

Element interface_feature(FeatureExamples& state, const FeatureExamplesContext& context, float width) {
    auto* s = &state; const auto c = context; width = std::max(240.0f, width);
    return column({.id = "interface_feature", .width = Dim(width), .gap = 20}, {dynamic([s, c, width] {
        const bool narrow = width < 680; const float part = narrow ? width : (width - 20) / 2;
        std::vector<Element> panels{interface_object(s, c, part), semantic_readout(s, c, part)};
        std::vector<Element> content{feature_heading(c, "One interface. Every way in.", "一套接口，多种入口。", width),
            feature_text("lead", feature_words(c, "Edit the object. Its pixels and semantic values change together; changing the example language keeps that same object alive.", "编辑对象，像素与语义值随之同步变化。切换示例语言时，仍是同一个对象。"), width),
            preview_controls(s, c, width),
            narrow ? column({.id = "workspace", .width = Dim(width), .gap = 20}, std::move(panels)) : row({.id = "workspace", .width = Dim(width), .gap = 20, .align = "start"}, std::move(panels)),
            feature_button("toggle_details", s->interface_details.get() ? feature_words(c, "Close details", "关闭详情") : feature_words(c, "Details · languages and API", "详情 · 语言与 API"), [s] { s->interface_details.set(!s->interface_details.peek()); }, ButtonVariant::ghost)};
        if (!s->status.get().empty()) content.push_back(feature_text("status", s->status.get(), width, "caption"));
        if (s->interface_details.get()) {
            content.push_back(column({.id = "details", .width = Dim(width), .gap = 12}, {language_picker(s, width),
                feature_text("label", feature_words(c, "Public builder API expressions with current values. Add imports, signal bindings and event handlers in your own application. This page executes the C++ model; it does not run the other language examples in the browser.",
                    "使用当前值生成的公开 builder API 表达式。在应用中补上导入、信号绑定与事件处理。本页面运行 C++ 模型，其他语言示例并未在浏览器中执行。"), width, "caption"),
                source_text("source", expression(s, c), width), feature_button("copy", feature_words(c, "Copy current API expressions", "复制当前 API 表达式"), [s, c] {
                    if (auto* rt = feature_runtime(c)) { rt->set_clipboard_text(expression(s, c)); s->status.set(feature_words(c, "Current expressions sent to the clipboard.", "当前表达式已交给剪贴板。")); }
                }), source_text("semantic", s->observed.get().json(), width)}));
        }
        return content;
    })});
}

}  // namespace omui::site
