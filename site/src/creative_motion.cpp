// Useful little workspaces, each composed from real components, one motion
// model and the framework's scoped material effects. No scene owns a clock.
module openmui.site;

import std;
import openmui;
import :motion_catalog;

namespace omui::site {
namespace {

constexpr std::array<MotionCaseInfo, 20> combinations{{
    // Edge-to-edge reflow uses the bounded tween recipe: a spring's valid
    // overshoot would carry the final card outside this usable workspace.
    // Each control retains the framework's own default state feedback.
    {"packing-board", "A bag, ready to go", "整理一只出行包", "organise", "Pack a list, then carry the same items into cards.", "勾好行李，再把同一批物品收拢成卡片。", "checkbox + card + button", "glass", "simple", "document", 0.035},
    {"inbox-triage", "Give a message a home", "给消息一个归处", "organise", "Move a reviewed message from the inbox into its archive.", "把读过的消息从收件箱移到归档。", "avatar + badge + card + button", "ink", "ink", "document", 0.025},
    {"focus-session", "Find a quieter hour", "留一段专注时间", "control", "Tune a session and turn its settings into a focused readout.", "调整专注时长，把设置收成清晰的运行状态。", "slider + progress + toggle + button", "neon", "spring", "distance", 0.025},
    {"release-steps", "From ready to released", "从准备到发布", "workflow", "Keep progress, review steps and a release receipt together.", "让步骤、进度与发布回执保持连续。", "stepper + progress + badge + button", "retro", "morph", "document", 0.045},
    {"price-comparison", "Choose a billing rhythm", "选一种付费节奏", "choose", "Compare monthly and yearly plans without losing their identity.", "在月付和年付之间对照，同一方案始终可辨。", "segmented + card + badge + button", "glass", "spring", "together"},
    {"tag-sorting", "Small words, clear groups", "把关键词归好类", "organise", "Edit a topic and arrange its removable labels into groups.", "编辑主题，再把可移除的标签整理成组。", "text_box + chip + button", "ink", "ink", "document", 0.035},
    {"playback-queue", "Let the next track arrive", "让下一首自然到来", "media", "Promote a queued track into the now-playing space.", "把队列中的下一首提升为正在播放。", "button + progress + slider + badge", "neon", "spring", "document", 0.03},
    {"palette-studio", "A colour becomes a palette", "让颜色长成色板", "create", "Choose an ink and blend it into a compact palette.", "选择墨色，调节混合量，收成一套色板。", "color_picker + slider + button", "liquid", "liquid", "distance", 0.02, 0, 1, 0.15f},
    {"appointment-card", "An appointment takes shape", "让约定成形", "plan", "Choose a day and time, then keep them in an appointment card.", "选择日期与时间，再把约定收成一张卡片。", "date_picker + time_picker + badge + button", "glass", "spring", "document", 0.025},
    {"reading-mode", "Make room for the words", "给文字留出空间", "read", "Adjust text scale and fold the reading tools out of the way.", "调整字号，再把阅读工具收拢到一旁。", "toggle + slider + card + button", "ink", "ink", "together"},
    {"transfer-center", "Files find their place", "让文件各就其位", "workflow", "Move a file from its upload queue to a completed record.", "把文件从待传队列移到完成记录。", "progress + badge + card + button", "retro", "morph", "document", 0.04},
    {"split-notes", "Two notes, one thought", "两份笔记，一个想法", "create", "Write a note and change the split between draft and preview.", "编辑笔记，再调整草稿与预览之间的空间。", "split_view + text_box + button", "ink", "spring", "together"},
    {"team-presence", "Gather around an idea", "围绕一个想法聚合", "communicate", "Bring available collaborators around their shared review.", "把在线的协作者聚到同一次审阅旁。", "avatar + chip + toggle + button", "glass", "spring", "distance", 0.035},
    {"volume-mixer", "A mix comes together", "把声音混成一体", "media", "Balance a track and condense the mixer into a master meter.", "调节声轨，再把混音台收成总线电平。", "slider + progress + number_input + button", "neon", "morph", "document", 0.02},
    {"consent-receipt", "A choice, clearly recorded", "让选择清楚留痕", "workflow", "Make a local preference and turn it into a readable receipt.", "选择一项本地偏好，再把它变成可读回执。", "checkbox + alert + card + button", "ink", "ink", "document", 0.03},
    {"search-command", "From search to a result", "从搜索走向结果", "choose", "Choose a local command and show its actual completion record.", "选择本地命令，并显示它实际产生的完成记录。", "combobox + badge + card + button", "retro", "morph", "document", 0.035},
    {"weather-plan", "Weather into a small plan", "把天气变成小计划", "plan", "Adjust a rain threshold and arrange a local outing plan.", "调整雨量阈值，再整理一份本地出行计划。", "segmented + slider + toggle + card + button", "liquid", "liquid", "distance", 0.02, 0, 1, 0.12f},
    {"budget-allocation", "Give a budget its shape", "让预算有个形状", "control", "Set a usable range and arrange its allocation summary.", "设定可用区间，再整理预算分配摘要。", "range_slider + number_input + progress + button", "neon", "spring", "together"},
    {"workspace-tabs", "Keep your place as you switch", "切换时，仍知身在何处", "read", "Switch a document workspace between a page and its activity.", "在文档与活动之间切换，保持位置线索。", "tabs + breadcrumb + card + button", "glass", "spring", "document", 0.025},
    {"issue-resolution", "An issue becomes a record", "把问题变成修复记录", "workflow", "Inspect a local issue, mark its checks and retain the outcome.", "查看本地问题、完成检查，再保留处理结果。", "alert + stepper + badge + button", "ink", "ink", "document", 0.045}
}};

std::string words(const MotionSceneContext& c, std::string_view en, std::string_view zh) {
    return std::string(c.language && c.language->get() == "zh" ? zh : en);
}
void changed(const MotionSceneContext& c) { if (c.changed) c.changed(); }
Element line(std::string id, std::string content, std::string style = "body", float width = 0) {
    TextOptions o; o.id = std::move(id); o.text = std::move(content); o.style = std::move(style);
    if (width > 0) o.max_width = Dim(width);
    return text(std::move(o));
}
Element heading(std::string id, std::string content, float width) {
    TextOptions o; o.id = std::move(id); o.text = std::move(content); o.style = "heading"; o.size = 23.0f; o.max_width = Dim(width);
    return text(std::move(o));
}
Element action(const MotionSceneContext& c, std::string id, std::string title, std::function<void()> edit, ButtonVariant variant = ButtonVariant::secondary) {
    ButtonOptions o; o.id = std::move(id); o.text = std::move(title); o.variant = variant;
    o.on_activated = [c, edit = std::move(edit)] { edit(); changed(c); };
    return button(std::move(o));
}
Element flip(const MotionSceneContext& c, std::string id, std::string a, std::string b) {
    return action(c, std::move(id), c.state->end.get() ? std::move(b) : std::move(a), [s = c.state] { s->end.set(!s->end.peek()); });
}
Element panel(std::string id, std::string title, float width, std::vector<Element> children) {
    CardOptions o; o.id = std::move(id); o.title = std::move(title); o.width = Dim(width);
    return card(std::move(o), std::move(children));
}
Element progress_line(std::string id, std::string label, double value, float width) {
    ProgressOptions o; o.id = std::move(id); o.label = std::move(label); o.value = std::clamp(value, 0.0, 1.0); o.width = Dim(width);
    return progress(std::move(o));
}
Element badge_line(std::string id, std::string label, BadgeTone tone = BadgeTone::accent) {
    BadgeOptions o; o.id = std::move(id); o.text = std::move(label); o.tone = tone; return badge(std::move(o));
}
Element range(const MotionSceneContext& c, std::string id, std::string label, float width, double min = 0, double max = 100) {
    SliderOptions o; o.id = std::move(id); o.label = std::move(label); o.min = min; o.max = max; o.value = Source{std::nullopt, [s = c.state, min, max] { return Value(std::clamp(s->value.get(), min, max)); }, [s = c.state, min, max](const Value& value) { s->value.set(std::clamp(value.as_double(), min, max)); }}; o.width = Dim(width);
    o.on_changed = [c](double) { changed(c); }; return slider(std::move(o));
}
Element toggle_setting(const MotionSceneContext& c, std::string id, std::string label) {
    ToggleOptions o; o.id = std::move(id); o.label = std::move(label); o.checked = c.state->checked;
    o.on_changed = [c](bool) { changed(c); }; return toggle(std::move(o));
}
Element number(const MotionSceneContext& c, std::string id, std::string label, float width, double min = 0, double max = 100) {
    NumberInputOptions o; o.id = std::move(id); o.label = std::move(label); o.min = min; o.max = max; o.step = 1.0; o.value = c.state->value; o.width = Dim(width);
    o.on_changed = [c](double) { changed(c); }; return number_input(std::move(o));
}
Source membership(MotionSceneState* s, std::string key) {
    return Source{std::nullopt, [s, key] { const auto items = s->items.get(); return Value(std::ranges::find(items, key) != items.end()); },
        [s, key](const Value& value) { auto items = s->items.peek(); std::erase(items, key); if (value.as_bool()) items.push_back(key); s->items.set(std::move(items)); }};
}
Source selected(MotionSceneState* s, std::vector<std::string> options, std::string fallback) {
    return Source{std::nullopt, [s, options, fallback] { const auto value = s->choice.get(); return Value(std::ranges::find(options, value) != options.end() ? value : fallback); },
        [s](const Value& value) { s->choice.set(std::string(value.as_string())); }};
}
Element choices(const MotionSceneContext& c, std::string id, std::string label, std::vector<std::string> options, std::vector<std::string> labels, std::string fallback, float width) {
    SegmentedOptions o; o.id = std::move(id); o.label = std::move(label); o.labels = std::move(labels);
    o.options = options; o.value = selected(c.state, std::move(options), std::move(fallback)); o.width = Dim(width);
    o.on_changed = [c](std::string) { changed(c); }; return segmented(std::move(o));
}
Element scene(const MotionSceneContext& c, std::string_view style, Element content) {
    // The controls' glass has something behind it to refract. The same ink
    // and track tokens make this backdrop follow the selected scoped style.
    const float w = std::max(100.0f, c.width), h = std::max(100.0f, c.height);
    Element art = stack({.id = "backdrop", .width = Dim(w), .height = Dim(h)}, {
        box({.id = "wash", .width = Dim(w * 0.56f), .height = Dim(h * 0.52f), .background = "accent", .radius = 48, .opacity = 0.09f, .translate_x = w * 0.42f, .translate_y = h * 0.14f}),
        box({.id = "island", .width = Dim(w * 0.38f), .height = Dim(h * 0.30f), .background = "accent_subtle", .radius = 36, .translate_x = w * 0.12f, .translate_y = h * 0.62f})});
    art.prop("pointer_events", Source(Value("none")));
    Element root = stack({.id = "composition", .width = Dim(w), .height = Dim(h), .background = "background"}, {std::move(art), std::move(content)});
    // The caller joins this canvas to the scoped pack. Its window rule
    // therefore supplies the local CRT/paper layer exactly once.
    (void)style;
    return root;
}

Element packing(const MotionSceneContext& c) {
    const bool end = c.state->end.get(); const float w = c.width;
    const int columns = w > 540 ? 3 : 2; const float cw = end ? std::floor((w - 10.0f * static_cast<float>(columns - 1)) / static_cast<float>(columns)) : w;
    std::vector<Element> items;
    for (const auto& [id, en, zh] : {std::tuple{"passport", "Passport", "护照"}, {"tickets", "Tickets", "车票"}, {"keys", "Keys", "钥匙"}}) {
        CheckboxOptions o; o.id = "packed"; o.label = words(c, en, zh); o.checked = membership(c.state, id); o.width = Dim(std::max(60.0f, cw - 32));
        o.on_changed = [c](bool) { changed(c); };
        items.push_back(panel(id, "", cw, {checkbox(std::move(o))}));
    }
    Element collection = column({.id = "items", .width = Dim(w), .gap = 10, .wrap = end ? "wrap" : "nowrap"}, std::move(items));
    collection.prop("direction", Source(Value(end ? "row" : "column")));
    return column({.id = "workspace", .width = Dim(w), .gap = 12}, {
        heading("title", words(c, "A little less to remember.", "少一点需要惦记的事。"), w),
        std::move(collection),
        action(c, "pack", words(c, "Pack the set", "全部装好"), [s = c.state] { s->items.set({"passport", "tickets", "keys"}); s->end.set(true); }, ButtonVariant::primary)});
}

Element inbox(const MotionSceneContext& c) {
    const bool archived = c.state->end.get(); const float w = c.width;
    auto message = [&](std::string id, std::string name, std::string subject, bool active) {
        return panel(std::move(id), "", w, {
            row({.id = "sender", .gap = 10, .align = "center"}, {avatar({.id = "face", .name = name, .size = 32.0f}), line("name", std::move(name), "label")}),
            line("subject", std::move(subject), "body", w - 32),
            badge_line("state", active ? words(c, "Archived", "已归档") : words(c, "Needs a reply", "待回复"), active ? BadgeTone::success : BadgeTone::accent)});
    };
    Element ada = message("ada", "Ada", words(c, "The sketches are ready.", "草图已经准备好了。"), archived);
    Element lin = line("lin", words(c, "Lin · A small change to review", "Lin · 一项小修改待审阅"), "caption", w);
    std::vector<Element> records;
    if (archived) { records.push_back(std::move(lin)); records.push_back(line("section", words(c, "FILED FOR LATER", "收好，留待以后"), "caption")); records.push_back(std::move(ada)); }
    else { records.push_back(std::move(ada)); records.push_back(std::move(lin)); }
    return column({.id = "workspace", .width = Dim(w), .gap = 12}, {
        row({.id = "title", .gap = 8, .align = "center"}, {heading("name", words(c, "Inbox", "收件箱"), w - 50), badge_line("count", archived ? "1" : "2")}),
        column({.id = "messages", .gap = 10}, std::move(records)),
        flip(c, "file", words(c, "Archive Ada's message", "归档 Ada 的消息"), words(c, "Bring it back", "放回收件箱"))});
}

Element focus(const MotionSceneContext& c) {
    const bool running = c.state->end.get(); const float w = c.width, in = w - 32;
    const auto minutes = static_cast<int>(std::clamp(c.state->value.get(), 10.0, 60.0));
    std::vector<Element> controls{
        line("duration", std::format("{} {}", minutes, words(c, "minutes", "分钟")), "heading", in),
        range(c, "minutes", words(c, "Session minutes", "专注分钟数"), in, 10, 60),
        toggle_setting(c, "quiet", words(c, "Quiet notifications", "安静通知"))};
    if (running) std::ranges::reverse(controls);
    controls.push_back(progress_line("progress", words(c, "Session plan", "专注计划"), running ? 1.0 : 0.0, in));
    controls.push_back(flip(c, "begin", words(c, "Start this session", "开始这一段"), words(c, "Change the plan", "调整计划")));
    return panel("session", running ? words(c, "Space to focus", "留白，专注") : words(c, "A quieter hour", "安静一点"), w, std::move(controls));
}

Element release(const MotionSceneContext& c) {
    const bool end = c.state->end.get(); const float w = c.width, in = w - 32;
    const auto step = std::clamp<std::int64_t>(c.state->count.get() - 3, 0, 2);
    StepperOptions steps; steps.id = "steps"; steps.label = words(c, "Release checks", "发布检查"); steps.free = true;
    steps.steps = std::vector<std::string>{words(c, "Check", "检查"), words(c, "Review", "审阅"), words(c, "Ship", "发布")}; steps.value = end ? std::int64_t{2} : step; steps.width = Dim(in);
    steps.on_changed = [c](std::int64_t value) { c.state->count.set(value + 3); c.state->end.set(value == 2); changed(c); };
    return panel("release", words(c, "Release desk", "发布台"), w, {
        stepper(std::move(steps)), progress_line("progress", words(c, "Release readiness", "发布准备"), end ? 1.0 : static_cast<double>(step) / 2.0, in),
        row({.id = "receipt", .gap = 8, .align = "center", .wrap = "wrap"}, {badge_line("state", end ? words(c, "Recorded", "已记录") : words(c, "Draft", "草稿"), end ? BadgeTone::success : BadgeTone::accent), line("version", "v0.1", "code")}),
        action(c, "next", end ? words(c, "Start a new draft", "开始新草稿") : words(c, "Complete next check", "完成下一项检查"), [s = c.state] { const auto n = s->count.peek(); if (s->end.peek()) { s->count.set(3); s->end.set(false); } else { s->count.set(std::min<std::int64_t>(5, n + 1)); s->end.set(n + 1 >= 5); } }), line("scope", words(c, "Local checklist; nothing is published.", "本地清单，不会对外发布。"), "caption", in)});
}

Element pricing(const MotionSceneContext& c) {
    const bool annual = c.state->end.get(); const float w = c.width;
    const bool wide = w > 500; const float cw = wide ? std::floor((w - 12) / 2) : w;
    auto offer = [&](std::string id, std::string title, int price, bool recommended) {
        return panel(id, std::move(title), cw, {line("price", std::format("${}", annual ? price * 10 : price), "heading", cw - 32),
            badge_line("period", annual ? words(c, "per year · 2 months saved", "每年 · 省两个月") : words(c, "per month", "每月")),
            action(c, "choose", c.state->choice.get() == id ? words(c, "Selected", "已选择") : words(c, "Choose plan", "选择方案"), [s = c.state, id] { s->choice.set(id); s->checked.set(true); }, recommended ? ButtonVariant::primary : ButtonVariant::secondary)});
    };
    Element plans = row({.id = "plans", .width = Dim(w), .gap = 12, .wrap = "wrap"}, {offer("solo", words(c, "Solo", "个人"), 8, false), offer("team", words(c, "Team", "团队"), 20, true)});
    if (!wide) {
        // A phone compares one selected offer, retaining both stable ids in
        // the wide arrangement and avoiding two tall cards in the viewport.
        plans = row({.id = "plans", .width = Dim(w), .gap = 10}, {
            action(c, "solo", words(c, "Solo", "个人"), [s = c.state] { s->choice.set("solo"); }),
            action(c, "team", words(c, "Team", "团队"), [s = c.state] { s->choice.set("team"); })});
        const bool team = c.state->choice.get() == "team";
        plans.children.push_back(line("cost", std::format("${}", (team ? 20 : 8) * (annual ? 10 : 1)), "heading", 90));
        plans.prop("wrap", Source(Value("wrap")));
    }
    SegmentedOptions billing; billing.id = "billing"; billing.label = words(c, "Billing rhythm", "付费节奏");
    billing.labels = std::vector<std::string>{words(c, "Monthly", "月付"), words(c, "Yearly", "年付")}; billing.options = std::vector<std::string>{"monthly", "yearly"}; billing.width = Dim(w);
    billing.value = Source{std::nullopt, [s = c.state] { return Value(s->end.get() ? "yearly" : "monthly"); }, [s = c.state](const Value& value) { s->end.set(value.as_string() == "yearly"); }};
    billing.on_changed = [c](std::string) { changed(c); };
    return column({.id = "workspace", .width = Dim(w), .gap = 16}, {heading("title", words(c, "Choose your rhythm.", "选一种节奏。"), w), segmented(std::move(billing)), std::move(plans),
        line("scope", words(c, "A local comparison; no checkout.", "本地对照，不会产生支付。"), "caption", w)});
}

Element tags(const MotionSceneContext& c) {
    const bool grouped = c.state->end.get(); const float w = c.width;
    const auto values = c.state->items.get();
    struct Topic { std::string raw, id, label; bool user = false; };
    std::vector<Topic> topics;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto& value = values[index];
        const auto split = value.find('|');
        if (split != std::string::npos) topics.push_back({value, value.substr(0, split), value.substr(split + 1), true});
        else {
            const bool predefined = value == "design" || value == "build" || value == "review";
            topics.push_back({value, predefined ? "topic-" + value : "topic-slot-" + std::to_string(index),
                value == "design" ? words(c, "Design", "设计") : value == "build" ? words(c, "Build", "构建") : value == "review" ? words(c, "Review", "审阅") : value, !predefined});
        }
    }
    if (grouped) std::ranges::sort(topics, {}, &Topic::label);
    std::vector<Element> chips;
    for (const auto& topic : topics) {
        ChipOptions o; o.id = topic.id; o.text = topic.label; o.removable = true;
        o.on_removed = [c, raw = topic.raw] { auto list = c.state->items.peek(); std::erase(list, raw); c.state->items.set(std::move(list)); changed(c); };
        Element item = chip(std::move(o)); item.prop("origin", Source(Value(topic.user ? "user_content" : "app"))); chips.push_back(std::move(item));
    }
    TextBoxOptions input; input.id = "topic"; input.label = words(c, "New topic", "新主题"); input.text = c.state->text; input.max_length = 8; input.disabled = values.size() >= 4; input.width = Dim(w);
    input.on_changed = [c](std::string) { changed(c); };
    input.on_submitted = [c](std::string title) { if (!title.empty()) { auto list = c.state->items.peek(); if (list.size() < 4) { const auto key = c.state->count.peek() + 1; c.state->count.set(key); list.push_back("topic-" + std::to_string(key) + "|" + title); c.state->items.set(std::move(list)); c.state->text.set(""); changed(c); } } };
    return column({.id = "workspace", .width = Dim(w), .gap = 12}, {heading("title", words(c, "A few useful words.", "收好几个关键词。"), w), text_box(std::move(input)),
        row({.id = "tags", .width = Dim(w), .gap = grouped ? 12.0f : 6.0f, .wrap = "wrap"}, std::move(chips)),
        flip(c, "sort", words(c, "Group the topics", "把主题归好"), words(c, "Loosen the group", "松开这一组")),
        line("hint", words(c, "Up to 4 · Enter adds · × removes", "最多4项 · 回车添加 · × 移除"), "caption", w)});
}

Element playback(const MotionSceneContext& c) {
    const bool next = c.state->end.get(); const float w = c.width, in = w - 32;
    const auto volume = static_cast<int>(std::clamp(c.state->value.get(), 0.0, 100.0));
    std::vector<Element> tracks{
        panel("paper", "", in, {line("title", "Paper boats", next ? "caption" : "heading", in - 32)}),
        panel("light", "", in, {line("title", "Window light", next ? "heading" : "caption", in - 32)})};
    if (next) std::ranges::reverse(tracks);
    return panel("player", words(c, "Listening room", "聆听室"), w, {
        column({.id = "queue", .gap = 8}, std::move(tracks)),
        progress_line("position", words(c, "Track position", "曲目位置"), next ? 0.16 : 0.72, in),
        row({.id = "transport", .gap = 10, .align = "center", .wrap = "wrap"}, {flip(c, "next", words(c, "Next track", "下一首"), words(c, "Previous track", "上一首")), badge_line("volume", std::format("{}%", volume))}),
        range(c, "volume", words(c, "Playback volume", "播放音量"), in)});
}

std::array<std::string, 4> palette_colours(const MotionSceneContext& c) {
    const auto ink = picking::parse_hex(c.state->text.get()).value_or(Color::hex(0x6366f1));
    const float contrast = static_cast<float>(std::clamp(c.state->value.get(), 0.0, 100.0) / 100.0);
    return {picking::hex(parts::mix(ink, Color::white(), 0.45f + 0.4f * contrast)),
            picking::hex(parts::mix(ink, Color::white(), 0.12f + 0.3f * contrast)),
            picking::hex(parts::mix(ink, Color::hex(0x10162f), 0.04f + 0.12f * contrast)),
            picking::hex(parts::mix(ink, Color::hex(0x10162f), 0.30f + 0.45f * contrast))};
}
std::string palette_output(const MotionSceneContext& c) {
    const auto colours = palette_colours(c);
    return std::format(":root {{\n  --palette-light: {};\n  --palette-soft: {};\n  --palette-ink: {};\n  --palette-shade: {};\n}}\n", colours[0], colours[1], colours[2], colours[3]);
}
Element palette(const MotionSceneContext& c) {
    const bool spread = c.state->end.get(); const float w = c.width;
    const auto mix = c.state->value.get();
    ColorPickerOptions picker; picker.id = "ink"; picker.label = words(c, "Palette ink", "色板墨色"); picker.width = Dim(w);
    picker.swatches = std::vector<std::string>{"#6366f1", "#0d9488", "#db2777", "#d97706"};
    picker.value = Source{std::nullopt, [s = c.state] { auto x = s->text.get(); return Value(picking::parse_hex(x) ? x : std::string("#6366f1")); }, [s = c.state](const Value& value) { s->text.set(std::string(value.as_string())); }};
    picker.on_changed = [c](std::string) { changed(c); };
    const auto colours = palette_colours(c); const float cell = (w - 18) / 4;
    std::vector<Element> swatches;
    for (std::size_t i = 0; i < colours.size(); ++i) {
        TextOptions hex; hex.id = "hex"; hex.text = colours[i]; hex.style = "code"; hex.size = w < 400 ? 11.0f : 14.0f; hex.max_width = Dim(cell);
        swatches.push_back(column({.id = "swatch-" + std::to_string(i), .width = Dim(cell), .gap = 5, .align = "center"}, {
            box({.id = "colour", .width = Dim(spread ? cell : std::min(cell, 48.0f)), .height = Dim(48), .background = colours[i], .radius = spread ? 10.0f : 24.0f}), text(std::move(hex))}));
    }
    ButtonOptions copy; copy.id = "copy"; copy.text = words(c, "Copy palette", "复制色板"); copy.disabled = !c.clipboard;
    copy.on_activated = [c] { if (c.clipboard) c.clipboard(palette_output(c)); };
    return column({.id = "workspace", .width = Dim(w), .gap = 10}, {heading("title", words(c, "Your colour family.", "一套可用的配色。"), w),
        color_picker(std::move(picker)), row({.id = "swatches", .height = Dim(72), .gap = 6, .align = "center"}, std::move(swatches)),
        range(c, "mix", words(c, "Light / dark contrast", "明暗对比"), w),
        row({.id = "actions", .gap = 8, .align = "center", .wrap = "wrap"}, {button(std::move(copy)), badge_line("amount", std::format("{}%", static_cast<int>(mix)))})});
}

Element appointment(const MotionSceneContext& c) {
    const bool booked = c.state->end.get(); const float w = c.width, in = w - 32;
    const auto raw_day = c.state->text.get(), raw_time = c.state->choice.get();
    const std::string day = picking::parse_date(raw_day) ? raw_day : "2026-11-12";
    const std::string time = TimePicker::minutes_of(raw_time) ? raw_time : "09:30";
    DatePickerOptions date; date.id = "day"; date.label = words(c, "Review date", "审阅日期"); date.value = day; date.width = Dim(in);
    date.on_changed = [c](std::string value) { c.state->text.set(std::move(value)); changed(c); };
    TimePickerOptions hour; hour.id = "time"; hour.label = words(c, "Review time", "审阅时间"); hour.step = 5; hour.value = time; hour.width = Dim(in);
    hour.on_changed = [c](std::string value) { c.state->choice.set(std::move(value)); changed(c); };
    std::vector<Element> form{date_picker(std::move(date)), time_picker(std::move(hour)), badge_line("status", booked ? words(c, "Reserved locally", "已在本地安排") : words(c, "Choose a moment", "选一个时刻"), booked ? BadgeTone::success : BadgeTone::accent)};
    if (booked) std::rotate(form.begin(), form.end() - 1, form.end());
    form.push_back(flip(c, "reserve", words(c, "Keep this appointment", "保留这项约定"), words(c, "Change the appointment", "修改约定")));
    return panel("appointment", booked ? words(c, "A moment kept", "约定已收好") : words(c, "Make a little time", "留一点时间"), w, std::move(form));
}

Element reading(const MotionSceneContext& c) {
    const bool quiet = c.state->end.get(); const float w = c.width;
    const float size = static_cast<float>(std::clamp(c.state->value.get(), 16.0, 26.0));
    TextOptions prose; prose.id = "prose"; prose.text = words(c, "A little space makes the next step clear.", "一点留白，就能让下一步变得清晰。");
    prose.style = "body"; prose.max_lines = 3; prose.size = size; prose.max_width = Dim(w - 32);
    Element tools = row({.id = "tools", .width = Dim(w), .gap = 10, .align = "center", .wrap = "wrap"}, {toggle_setting(c, "notes", words(c, "Show notes", "显示注释")), range(c, "scale", words(c, "Reading size", "阅读字号"), std::min(w, 180.0f), 16, 26)});
    std::vector<Element> rows;
    if (!quiet) rows.push_back(std::move(tools));
    rows.push_back(panel("page", words(c, "Room to think", "留白，思考"), w, {text(std::move(prose))}));
    if (quiet) rows.push_back(std::move(tools));
    rows.push_back(flip(c, "focus", words(c, "Focus on the words", "专注文字"), words(c, "Return to the desk", "回到书桌")));
    return column({.id = "workspace", .width = Dim(w), .gap = 12}, std::move(rows));
}

Element transfers(const MotionSceneContext& c) {
    const bool done = c.state->end.get(); const float w = c.width;
    std::vector<Element> files;
    for (const auto& [id, name, size] : {std::tuple{"notes", "notes.md", "12 KB"}, {"assets", "assets.zip", "3.2 MB"}}) {
        files.push_back(panel(id, "", w, {row({.id = "record", .gap = 8, .align = "center", .wrap = "wrap"}, {line("name", name, "code"), badge_line("size", size)}),
            progress_line("progress", words(c, "Transfer progress", "传输进度"), done ? 1.0 : (std::string_view(id) == "notes" ? 0.7 : 0.25), w - 32)}));
    }
    if (done) std::ranges::reverse(files);
    return column({.id = "workspace", .width = Dim(w), .gap = 12}, {heading("title", done ? words(c, "Filed and ready.", "收好了，可以继续。") : words(c, "A small transfer queue.", "一个小小的传输队列。"), w),
        column({.id = "files", .gap = 10}, std::move(files)),
        flip(c, "transfer", words(c, "Mark the batch received", "将这批标为已接收"), words(c, "Prepare another batch", "再准备一批")),
        line("scope", words(c, "Sample queue records; no files are sent.", "示例队列记录，不会发送文件。"), "caption", w)});
}

Element notes(const MotionSceneContext& c) {
    const bool review = c.state->end.get(); const float w = c.width;
    const bool vertical = w < 500 ? !review : review;
    TextBoxOptions draft; draft.id = "draft"; draft.label = words(c, "Draft note", "笔记草稿"); draft.text = c.state->text; draft.max_length = 48; const double ratio = std::clamp(c.state->value.get() / 100.0, 0.4, 0.6);
    const float editor_width = vertical ? w - 20 : static_cast<float>(ratio * static_cast<double>(w - 6)) - 20;
    const float preview_width = vertical ? w - 20 : static_cast<float>((1 - ratio) * static_cast<double>(w - 6)) - 20;
    draft.width = Dim(std::max(60.0f, editor_width));
    draft.on_changed = [c](std::string) { changed(c); };
    SplitViewOptions split; split.id = "panes"; split.label = words(c, "Draft and preview", "草稿与预览"); split.min = 0.4;
    split.ratio = Source{std::nullopt, [s = c.state] { return Value(std::clamp(s->value.get() / 100.0, 0.4, 0.6)); }, [s = c.state](const Value& value) { s->value.set(value.as_double() * 100.0); }};
    split.vertical = vertical; split.width = Dim(w); split.height = Dim(240); split.on_changed = [c](double) { changed(c); };
    Element first = column({.id = "editor", .padding = Edges(10), .gap = 8, .background = "surface_variant"}, {line("label", words(c, "DRAFT", "草稿"), "caption"), text_box(std::move(draft))});
    Element preview = column({.id = "preview", .padding = Edges(10), .gap = 8, .background = "surface"}, {line("label", words(c, "PREVIEW", "预览"), "caption"), text({.id = "note", .text = c.state->text, .style = "body", .max_lines = 2, .origin = "user_content", .size = 14.0f, .max_width = Dim(std::max(60.0f, preview_width))})});
    std::vector<Element> panes; panes.push_back(std::move(first)); panes.push_back(std::move(preview));
    return column({.id = "workspace", .width = Dim(w), .gap = 12}, {heading("title", words(c, "Two views of one note.", "一份笔记，两种视角。"), w), split_view(std::move(split), std::move(panes)),
        action(c, "swap", vertical ? words(c, "Views side by side", "并排阅读") : words(c, "Stack the views", "上下阅读"), [s = c.state] { s->end.set(!s->end.peek()); })});
}

Element team(const MotionSceneContext& c) {
    const bool gathered = c.state->end.get(); const float w = c.width;
    std::vector<Element> people;
    for (const auto& [id, name] : {std::pair{"ada", "Ada"}, std::pair{"lin", "Lin"}, std::pair{"kai", "Kai"}}) {
        Element face = avatar({.id = "face", .name = name, .size = gathered ? 48.0f : 32.0f, .status = c.state->checked.get() ? AvatarStatus::online : AvatarStatus::away});
        people.push_back(gathered ? column({.id = id, .gap = 4, .align = "center"}, {std::move(face), line("name", name, "caption")})
                                  : row({.id = id, .gap = 10, .align = "center"}, {std::move(face), line("name", name, "body")}));
    }
    Element group = column({.id = "people", .width = Dim(w), .gap = gathered ? 18.0f : 10.0f}, std::move(people));
    group.prop("direction", Source(Value(gathered ? "row" : "column")));
    ChipOptions topic; topic.id = "topic"; topic.selectable = true; topic.selected = c.state->enabled; topic.text = words(c, "Design review", "设计审阅"); topic.on_toggled = [c](bool) { changed(c); };
    return column({.id = "workspace", .width = Dim(w), .gap = 16}, {heading("title", words(c, "A review, together.", "一起做一次审阅。"), w),
        std::move(group), chip(std::move(topic)), toggle_setting(c, "availability", words(c, "Team available for review", "团队可以参与审阅")),
        flip(c, "gather", words(c, "Gather the team", "聚到一起"), words(c, "See the people", "看看每个人"))});
}

Element mixer(const MotionSceneContext& c) {
    const bool master = c.state->end.get(); const float w = c.width, in = w - 32;
    const double level = std::clamp(c.state->value.get(), 0.0, 100.0);
    std::vector<Element> channels;
    for (const auto& [id, en, zh, scale] : {std::tuple{"voice", "Voice", "人声", 1.0}, {"room", "Room", "空间", 0.62}, {"music", "Music", "音乐", 0.82}}) {
        channels.push_back(column({.id = id, .width = Dim(master ? std::floor((in - 16) / 3) : in), .gap = 6}, {line("name", words(c, en, zh), "caption"), progress_line("level", words(c, "Channel level", "声轨电平"), level * scale / 100.0, master ? std::floor((in - 16) / 3) : in)}));
    }
    Element meters = column({.id = "meters", .width = Dim(in), .gap = 8}, std::move(channels)); meters.prop("direction", Source(Value(master ? "row" : "column")));
    return panel("mixer", words(c, "A small sound desk", "小小混音台"), w, {std::move(meters), range(c, "level", words(c, "Master level", "总线电平"), in),
        number(c, "gain", words(c, "Level percent", "电平百分比"), std::min(in, 180.0f)), flip(c, "condense", words(c, "Bring the mix together", "把声音聚合"), words(c, "Open the channels", "展开声轨"))});
}

Element consent(const MotionSceneContext& c) {
    const bool receipt = c.state->end.get(); const float w = c.width, in = w - 32;
    CheckboxOptions opt; opt.id = "preference"; opt.label = words(c, "Remember this local preference", "记住这项本地偏好"); opt.checked = c.state->checked; opt.width = Dim(in);
    opt.on_changed = [c](bool) { changed(c); };
    AlertOptions summary; summary.id = "receipt"; summary.title = receipt ? words(c, "Preference recorded", "偏好已记录") : words(c, "A local choice", "一项本地选择"); summary.message = c.state->checked.get() ? words(c, "Remembered for this example.", "在当前例子中记住。") : words(c, "Continue without remembering.", "继续，但不记住。"); summary.tone = receipt ? AlertTone::success : AlertTone::info; summary.width = Dim(in);
    std::vector<Element> content{checkbox(std::move(opt)), alert(std::move(summary))}; if (receipt) std::ranges::reverse(content);
    content.push_back(flip(c, "record", words(c, "Record this choice", "记录这个选择"), words(c, "Review the choice", "重新查看选择")));
    return panel("choice", words(c, "Your choice", "你的选择"), w, std::move(content));
}

Element command(const MotionSceneContext& c) {
    const bool compact = c.state->end.get(); const float w = c.width, in = w - 32;
    ComboboxOptions cmd; cmd.id = "command"; cmd.label = words(c, "Local view command", "本地视图命令");
    cmd.labels = std::vector<std::string>{words(c, "Compact the view", "收拢视图"), words(c, "Count a note", "记下一条笔记"), words(c, "Reset the view", "重置视图")};
    cmd.options = std::vector<std::string>{"compact", "count", "reset"}; cmd.value = selected(c.state, {"compact", "count", "reset"}, "compact"); cmd.width = Dim(in);
    cmd.on_changed = [c](std::string) { c.state->checked.set(false); changed(c); };
    const auto count = std::max<std::int64_t>(0, c.state->count.get() - 3);
    std::vector<Element> body{
        combobox(std::move(cmd)),
        row({.id = "result", .gap = 10, .align = "center", .wrap = "wrap"}, {line("count", std::format("{} {}", count, words(c, "notes", "条笔记")), compact ? "heading" : "body", in), badge_line("status", c.state->checked.get() ? words(c, "Applied", "已应用") : words(c, "Choose a command", "选一个命令"), c.state->checked.get() ? BadgeTone::success : BadgeTone::accent)}),
        action(c, "run", words(c, "Apply command", "应用命令"), [s = c.state] {
            const auto command = s->choice.peek();
            if (command == "count") s->count.set(s->count.peek() + 1);
            else if (command == "reset") { s->count.set(3); s->value.set(40); s->end.set(false); }
            else s->end.set(!s->end.peek());
            s->checked.set(true);
        })};
    if (compact) std::swap(body[0], body[1]);
    return panel("terminal", words(c, "Command desk", "命令台"), w, std::move(body));
}

Element weather(const MotionSceneContext& c) {
    const bool plan = c.state->end.get(); const float w = c.width, in = w - 32;
    const double threshold = c.state->value.get(); const bool outside = threshold >= 50 && !c.state->checked.get();
    const bool tomorrow = c.state->choice.get() == "tomorrow";
    Element day = choices(c, "day", words(c, "Plan day", "计划日期"), {"today", "tomorrow"}, {words(c, "Today", "今天"), words(c, "Tomorrow", "明天")}, "today", in);
    Element summary = row({.id = "summary", .gap = 12, .align = "center", .wrap = "wrap"}, {
        line("temperature", std::format("{}°", tomorrow ? 21 : 24), "heading"),
        badge_line("place", plan ? (outside ? words(c, "Park walk", "去公园走走") : words(c, "A quiet café", "找间安静咖啡馆")) : words(c, "Sample forecast", "示例天气"))});
    Element rain = range(c, "rain", words(c, "Rain tolerance", "可接受的雨量"), in);
    Element indoors = toggle_setting(c, "indoors", words(c, "Prefer indoors", "更想待在室内"));
    std::vector<Element> body;
    if (plan) { body.push_back(std::move(summary)); body.push_back(std::move(indoors)); body.push_back(std::move(day)); body.push_back(std::move(rain)); }
    else { body.push_back(std::move(day)); body.push_back(std::move(summary)); body.push_back(std::move(rain)); body.push_back(std::move(indoors)); }
    body.push_back(flip(c, "plan", words(c, "Make a small plan", "排个小计划"), words(c, "Read the weather", "看看天气")));
    return panel("forecast", plan ? words(c, "An afternoon plan", "下午的小计划") : words(c, "Weather card", "天气卡片"), w, std::move(body));
}

Element budget(const MotionSceneContext& c) {
    const bool allocated = c.state->end.get(); const float w = c.width, in = w - 32;
    const double low = std::clamp(c.state->value.get(), 0.0, 100.0), high = std::clamp(c.state->secondary.get(), low, 100.0);
    RangeSliderOptions bounds; bounds.id = "available"; bounds.label = words(c, "Budget range", "预算区间"); bounds.low = c.state->value; bounds.high = c.state->secondary; bounds.step = 5.0; bounds.width = Dim(in);
    bounds.on_changed = [c](double, double) { changed(c); };
    std::vector<Element> body{range_slider(std::move(bounds)), number(c, "reserve", words(c, "Minimum reserve", "最低预留"), std::min(in, 180.0f)),
        progress_line("allocation", words(c, "Allocated share", "分配比例"), allocated ? (high - low) / 100.0 : low / 100.0, in),
        line("summary", allocated ? std::format("{}% {} · {}% {}", static_cast<int>(high - low), words(c, "available", "可用"), static_cast<int>(low), words(c, "reserved", "预留")) : words(c, "Find a comfortable range.", "先找一个合适的区间。"), "body", in),
        flip(c, "allocate", words(c, "Arrange the budget", "整理预算"), words(c, "Adjust the range", "调整区间"))};
    return panel("budget", words(c, "Budget desk", "预算台"), w, std::move(body));
}

Element workspace(const MotionSceneContext& c) {
    const bool activity = c.state->end.get(); const float w = c.width, in = w - 32;
    TabsOptions pages; pages.id = "pages"; pages.selected = activity ? std::string("activity") : std::string("document"); pages.width = Dim(in);
    pages.on_changed = [c](std::string value) { c.state->end.set(value == "activity"); changed(c); };
    return panel("workspace", words(c, "Notebook", "笔记本"), w, {
        breadcrumb({.id = "path", .items = std::vector<std::string>{words(c, "Studio", "工作室"), words(c, "Notebook", "笔记本")}, .width = Dim(in)}),
        tabs(std::move(pages), {tab({.id = "document", .text = words(c, "Document", "文档")}), tab({.id = "activity", .text = words(c, "Activity", "活动")})}),
        heading("title", activity ? words(c, "A small trail of changes", "一点改变的轨迹") : words(c, "Ideas for the next release", "下一次发布的想法"), in),
        line("body", activity ? words(c, "Ada reviewed · Lin edited · You saved", "Ada 已审阅 · Lin 已编辑 · 你已保存") : words(c, "Make the important things easy to find.", "让重要的事情容易找到。"), "body", in),
        action(c, "save", words(c, "Save a local revision", "保存一个本地版本"), [s = c.state] { s->count.set(s->count.peek() + 1); s->end.set(true); }),
        badge_line("revision", std::format("r{}", c.state->count.get()))});
}

Element issue(const MotionSceneContext& c) {
    const bool resolved = c.state->end.get(); const float w = c.width, in = w - 32;
    AlertOptions problem; problem.id = "issue"; problem.title = resolved ? words(c, "Issue reviewed", "问题已审阅") : words(c, "A check needs attention", "有一项检查需要处理");
    problem.message = resolved ? words(c, "The local result is recorded below.", "本地处理结果已经记在下方。") : words(c, "A keyboard path stopped early.", "一条键盘路径提前中断了。"); problem.tone = resolved ? AlertTone::success : AlertTone::warning; problem.width = Dim(in);
    StepperOptions steps; steps.id = "checks"; steps.label = words(c, "Resolution checks", "处理步骤"); steps.free = true; steps.steps = std::vector<std::string>{words(c, "Find", "定位"), words(c, "Fix", "修复"), words(c, "Verify", "验证")}; steps.value = resolved ? std::int64_t{2} : std::int64_t{0}; steps.width = Dim(in);
    steps.on_changed = [c](std::int64_t value) { c.state->end.set(value == 2); c.state->count.set(value + 3); changed(c); };
    return panel("record", words(c, "Repair log", "修复记录"), w, {alert(std::move(problem)), stepper(std::move(steps)),
        row({.id = "result", .gap = 8, .align = "center", .wrap = "wrap"}, {badge_line("state", resolved ? words(c, "Verified", "已验证") : words(c, "Open", "待处理"), resolved ? BadgeTone::success : BadgeTone::accent), line("reference", "#014", "code")}),
        flip(c, "resolve", words(c, "Mark the local check done", "完成这项本地检查"), words(c, "Reopen for review", "重新审阅"))});
}

}  // namespace

std::span<const MotionCaseInfo> creative_motions() { return combinations; }

Element creative_motion_scene(std::string_view id, const MotionSceneContext& context) {
    if (!context.state) return line("missing-state", "No scene state", "body");
    Element content;
    if (id == "packing-board") content = packing(context);
    else if (id == "inbox-triage") content = inbox(context);
    else if (id == "focus-session") content = focus(context);
    else if (id == "release-steps") content = release(context);
    else if (id == "price-comparison") content = pricing(context);
    else if (id == "tag-sorting") content = tags(context);
    else if (id == "playback-queue") content = playback(context);
    else if (id == "palette-studio") content = palette(context);
    else if (id == "appointment-card") content = appointment(context);
    else if (id == "reading-mode") content = reading(context);
    else if (id == "transfer-center") content = transfers(context);
    else if (id == "split-notes") content = notes(context);
    else if (id == "team-presence") content = team(context);
    else if (id == "volume-mixer") content = mixer(context);
    else if (id == "consent-receipt") content = consent(context);
    else if (id == "search-command") content = command(context);
    else if (id == "weather-plan") content = weather(context);
    else if (id == "budget-allocation") content = budget(context);
    else if (id == "workspace-tabs") content = workspace(context);
    else if (id == "issue-resolution") content = issue(context);
    else return line("missing-scene", words(context, "This composition is not available.", "没有这个组合。"), "body");
    const auto found = std::ranges::find(combinations, id, &MotionCaseInfo::id);
    return scene(context, found->style, std::move(content));
}

}  // namespace omui::site
