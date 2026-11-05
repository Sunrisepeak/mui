// Small motion designs. Each describes two meaningful drawings;
// the gallery's shared motion root owns time, matching and playback.
module openmui.site;

import std;
import openmui;
import :motion_catalog;
import :component_motion;

namespace omui::site {

namespace {

constexpr std::array<MotionCaseInfo, 42> examples{{
    {.id = "shape-morph", .en = "Shape the journey", .zh = "让形状自由流动", .group = "transform",
     .purpose_en = "A triangle becomes a circle. Shape each point's journey, then copy the reusable motion.",
     .purpose_zh = "三角形变为圆。调整每个点经过的路径，再复制可复用的动效配置。",
     .components = "painter, button", .vertex_path = true, .bend = 0.22, .curve = "motion.slow"},
    {.id="component-motion",.en="One note, many ways to move",.zh="一张便笺，自由编排",
     .group="transform",.purpose_en="Move the whole note, its title, or its controls. Hover to edit and copy the same note.",
     .purpose_zh="整张便笺、标题和控件分别编排。移入画布，即可编辑并复制这张便笺。",
     .components="text, text_box, checkbox, button, popover",.authored_program=true},
    {"surface-color", "Surface colour", "表面换色", "appearance", "A status surface changes its tone without changing its shape.", "状态表面换一种色调，形状与位置保持连续。", "box, text"},
    {"outline-weight", "Outline emphasis", "边框强调", "appearance", "A selected document gains a stronger outline.", "文档被选中时，用边框粗细传达强调。", "box, text"},
    {"corner-shape", "Corner shape", "圆角变形", "appearance", "A compact tile softens into a rounded token.", "紧凑图块过渡为圆润的标记。", "box, text"},
    {"depth", "Lift in depth", "层次阴影", "appearance", "A floating note changes shadow depth while its contents stay put.", "浮动便笺改变阴影层次，内容位置保持不变。", "box, text", "plain", "spring"},
    {"emphasis", "Primary action", "主次强调", "appearance", "The next useful action becomes primary; the alternative recedes.", "下一项有用的操作成为主操作，另一项退为次要。", "button, text"},
    {"slide-card", "Move across", "横移卡片", "transform", "A document travels between the two sides of its workspace.", "文档在工作区两侧之间移动，保留对象身份。", "box, text"},
    {"lift-card", "Lift a note", "抬升便笺", "transform", "A note rises out of the stack into the reading position.", "便笺从堆叠位置抬升到阅读位置。", "box, text", "plain", "spring"},
    {"scale-card", "Scale in place", "整体缩放", "transform", "A preview grows around its centre without relaying out its text.", "预览围绕中心放大，文字随同一个对象缩放。", "box, text"},
    {"turn-tile", "Turn a tile", "旋转图块", "transform", "A labelled tile changes orientation around its centre.", "带文字的图块围绕中心改变方向。", "box, text", "plain", "spring"},
    {"squash-chip", "Compact a token", "压缩标签", "transform", "A label trades height for width as it becomes compact.", "标签在变紧凑时，以高度换取宽度。", "box, text"},
    {"list-grid", "List to cards", "列表转卡片", "layout", "Three travel items keep their ids while their layout changes.", "三项出行事项保留 ID，从列表变成卡片。", "box, text"},
    {"reorder-list", "Reorder a queue", "列表重排", "layout", "The selected track moves to the front of the same queue.", "选中的曲目移到同一队列的开头。", "box, text"},
    {"split-ratio", "Share the space", "分栏比例", "layout", "A reading pane receives more room while its neighbour shrinks.", "阅读栏获得更多空间，相邻栏同步收窄。", "box, text"},
    {"accordion-size", "Expand a detail", "详情展开", "layout", "A summary expands into its details and moves the next item with it.", "摘要展开为详情，下一个项目跟着让出空间。", "box, text"},
    {"toolbar-reflow", "Reflow a toolbar", "工具栏重排", "layout", "Named actions move from a row to a compact vertical group.", "具名操作从横排变为紧凑的纵向工具组。", "button"},
    {"tab-indicator", "Selected tab", "标签指示", "selection", "The active tab's lens and its content change together.", "选中标签的指示与内容一起变化。", "tabs, text"},
    {"radio-focus", "One choice", "单选对焦", "selection", "A choice moves between two alternatives of the same control group.", "同一控件组里的选择在两个候选项之间移动。", "radio, text"},
    {"menu-highlight", "Menu highlight", "菜单高亮", "selection", "The selected navigation item changes position and emphasis.", "导航选中项的位置与强调状态发生变化。", "button, text"},
    {"toggle-thumb", "A setting changes", "开关滑块", "selection", "The thumb, track and status describe the same setting.", "滑块、轨道和状态文字共同呈现同一项设置。", "toggle, text"},
    {"range-handles", "Change a range", "范围手柄", "selection", "Two handles adjust a meaningful interval together.", "两个手柄一起调整一个有意义的区间。", "range_slider, text"},
    {"title-replace", "Keep shared letters", "标题替换", "text", "Related titles retain shared glyphs as the sentence changes.", "相关标题切换时，保留可以匹配的字形。", "text"},
    {"count-change", "A changing number", "数字变化", "text", "The number of completed files changes in its original place.", "已完成文件数在原来的位置变化。", "text, progress"},
    {"locale-label", "Change a language", "中英标签", "text", "The same navigation labels change language and keep their ids.", "同一组导航标签切换语言，保留对象 ID。", "text, box"},
    {"text-align", "Align a title", "文本对齐", "text", "A heading moves between editorial and centred alignment.", "标题在编辑式左对齐与居中对齐之间移动。", "text, box"},
    {"type-scale", "Type hierarchy", "字号层级", "text", "An overview label grows into the title of the current subject.", "概览中的标签变为当前主题的标题。", "text, box"},
    {"progress-fill", "Progress fills", "进度填充", "data", "A determinate track follows the actual selected progress value.", "确定进度条跟随当前选定的进度数值。", "progress, slider, text"},
    {"bars-rebalance", "Rebalance a chart", "柱图重分配", "data", "The same three categories redistribute their measured values.", "同样的三个分类重新分配数值，保留标签位置关系。", "box, text"},
    {"score-meter", "A score rises", "评分刻度", "data", "A score marker, number and filled track agree on one value.", "评分标记、数字与填充轨道表达同一个值。", "box, text"},
    {"badge-count", "A useful count", "徽标计数", "data", "An inbox's count changes without replacing the surrounding row.", "收件箱的数量变化，周围的行保持原样。", "badge, button, text"},
    {"step-advance", "Advance a step", "步骤推进", "data", "A setup moves from its first step to its review step.", "设置流程从第一步推进到复核步骤。", "stepper, text"},
    {"toast-arrive", "An update arrives", "消息进入", "presence", "A new notification enters a quiet inbox.", "一条新通知进入原本安静的收件箱。", "box, badge, text", "plain", "spring", "together", 0, 16, 0.96f},
    {"detail-reveal", "Reveal supporting text", "详情揭示", "presence", "A note reveals its supporting paragraph inside its own frame.", "便笺在自己的边界内揭示补充段落。", "box, text"},
    {"item-insert", "Insert a task", "插入一项", "presence", "A new task takes a place between two existing tasks.", "新任务插入已有任务之间，原有对象为它让位。", "box, text", "plain", "morph", "document", 0.025},
    {"item-remove", "Remove an item", "移除一项", "presence", "A finished item leaves and its neighbours close the gap.", "已完成事项离开，相邻对象合上空隙。", "box, text", "plain", "morph", "together", 0, -8, 0.96f},
    {"placeholder-content", "Content takes its place", "占位到内容", "presence", "The reserved places become a named person and a readable message.", "预留的位置变为具名人物与可阅读的消息。", "avatar, box, text"},
    {"stagger-list", "A list in order", "列表错开", "rhythm", "Reading-order delays reveal a short itinerary one item at a time.", "按阅读顺序错开，让短行程逐项出现。", "box, text", "plain", "morph", "document", 0.045, 14, 0.98f},
    {"stagger-grid", "A grid gathers", "网格错开", "rhythm", "Small, labelled tiles gather into their final grid in order.", "带标签的小图块依次汇入最终网格。", "box, text", "plain", "spring", "document", 0.035},
    {"distance-stack", "Near before far", "距离排序", "rhythm", "Related notes fan out, ordered by distance from the change.", "相关便笺展开，按距变化中心的距离排序。", "box, text", "plain", "spring", "distance", 0.045},
    {"paired-swap", "A paired exchange", "成对交换", "rhythm", "Two named objects exchange their places in the same transition.", "两个具名对象在同一次过渡中交换位置。", "box, text"},
    {"cascade-lines", "A thought unfolds", "逐行展开", "rhythm", "A short outline becomes three readable steps with a shared cadence.", "一段短提纲按统一节奏展开为三个可读步骤。", "text, box", "plain", "morph", "document", 0.055, 10, 1},
}};

struct Scene {
    MotionSceneContext c;
    std::string_view design;
    bool end, zh;
    float w, h;
    Scene(const MotionSceneContext& context, std::string_view id)
        : c(context), design(id), end(context.state->end.get()), zh(context.language && context.language->get() == "zh"),
          w(std::max(1.0f, context.width)), h(std::max(1.0f, context.height)) {}
    std::string say(std::string_view en, std::string_view han) const { return std::string(zh ? han : en); }
};

Element at(Element element, float x, float y) {
    element.prop("position", Source(Value("absolute"))).prop("left", Source(Value(static_cast<double>(x)))).prop("top", Source(Value(static_cast<double>(y))));
    return element;
}

Element label(std::string id, std::string value, float width, std::string style = "body", std::string color = "text") {
    return text({.id = std::move(id), .text = std::move(value), .style = std::move(style), .color = std::move(color), .max_width = Dim(width)});
}

Element note(std::string id, std::string title, std::string detail, float width, float height = 92, std::string fill = "surface") {
    return column({.id = std::move(id), .width = Dim(width), .height = Dim(height), .padding = Edges(14), .gap = 6,
                   .background = std::move(fill), .border_color = "border", .border_width = 1, .radius = 14}, {
        label("title", std::move(title), std::max(1.0f, width - 28), "label"),
        label("detail", std::move(detail), std::max(1.0f, width - 28), "caption", "text_muted")});
}

void outer_recipe(Element& element) {
    element.prop("motion", Source(Value("none")));
    for (auto& child : element.children) outer_recipe(child);
}

Element frame(const Scene& s, std::vector<Element> children) {
    Element result = box({.id = "composition", .width = Dim(s.w), .height = Dim(s.h)}, std::move(children));
    // These designs deliberately coordinate pieces in one outer recipe:
    // per-letter matching, reading order and distance delay need the full
    // drawing. Other examples keep each control's typed default motion.
    if (s.design == "title-replace" || s.design == "count-change" || s.design == "locale-label" || s.design == "text-align" || s.design == "type-scale" ||
        s.design == "stagger-list" || s.design == "stagger-grid" || s.design == "distance-stack" || s.design == "cascade-lines") outer_recipe(result);
    return result;
}

auto change(const Scene& s) {
    return [c = s.c] { c.state->end.set(!c.state->end.peek()); if (c.changed) c.changed(); };
}

Prop<bool> boolean(const MotionSceneContext& c) {
    return Source{std::nullopt, [p = c.state] { return Value(p->end.get() != p->checked.get()); },
                  [p = c.state](const Value& value) { p->checked.set(value.as_bool() != p->end.peek()); }};
}

Prop<std::string> binary_choice(const MotionSceneContext& c, std::string a, std::string b) {
    return Source{std::nullopt, [p = c.state, a, b] { return Value(p->end.get() != p->checked.get() ? b : a); },
                  [p = c.state, b](const Value& value) { p->checked.set((value.as_string() == b) != p->end.peek()); }};
}

Prop<double> quantity(const MotionSceneContext& c) {
    return Source{std::nullopt, [p = c.state] { return Value(p->end.get() ? p->secondary.get() : p->value.get()); },
                  [p = c.state](const Value& value) { (p->end.peek() ? p->secondary : p->value).set(std::clamp(value.as_double(), 0.0, 100.0)); }};
}

Element list_item(const Scene& s, std::string id, std::string en, std::string zh, std::string index, float width, bool selected = false) {
    return row({.id = std::move(id), .width = Dim(width), .height = Dim(62), .padding = Edges(12), .gap = 12, .align = "center",
                .background = selected ? "accent_subtle" : "surface", .border_color = "border", .border_width = 1, .radius = 12}, {
        text({.id = "index", .text = std::move(index), .style = "code", .color = "accent"}),
        label("name", s.say(en, zh), std::max(1.0f, width - 70), "label")});
}

}  // namespace

std::span<const MotionCaseInfo> basic_motions() { return examples; }

Element basic_motion_scene(std::string_view id, const MotionSceneContext& context) {
    if (!context.state) return text({.id = "unavailable", .text = "Choose a motion", .style = "body"});
    if(id=="component-motion")return component_motion_scene(context);
    const Scene s(context, id);
    const float card_w = std::min(s.w - 24, 300.0f), cx = (s.w - card_w) / 2;
    const float top = std::max(12.0f, (s.h - 210) / 2);

    if (id == "shape-morph") {
        auto drawing = painter({.id = "shape", .width = Dim(s.w), .height = Dim(240), .role = Role::image,
            .label = s.say(s.end ? "B: circle" : "A: triangle", s.end ? "B：圆形" : "A：三角形"),
            .paint = [end = s.end](PaintContext& ctx) {
                const float travel = std::min(160.0f, ctx.size.width * .42f);
                const float radius = std::min(44.0f, ctx.size.width * .13f);
                const float center = ctx.size.width / 2, cy = 120;
                const float x = center + (end ? .5f : -.5f) * travel;
                render::Path guide;
                guide.move_to({center - travel / 2, cy}).line_to({center + travel / 2, cy});
                ctx.list.stroke_path(guide, {.width = 1}, render::Paint::solid(ctx.theme.color("border")));
                render::Path shape;
                if (end) shape.add_circle({x, cy}, radius);
                else shape.move_to({x, cy - radius}).line_to({x - radius, cy + radius}).line_to({x + radius, cy + radius}).close();
                ctx.list.fill_path(shape, render::FillRule::nonzero, render::Paint::solid(ctx.theme.color("accent")));
            }});
        drawing.prop("motion", Source(Value("none"))); // the scene's public motion policy owns the transition
        return column({.id = "composition", .width = Dim(s.w), .height = Dim(s.h), .gap = 12, .justify = "center", .align = "center"}, {
            std::move(drawing),
            button({.id = "change", .text = s.say("Change shape", "切换形状"),
                    .variant = ButtonVariant::secondary, .on_activated = change(s)})});
    }
    if (id == "surface-color") {
        return frame(s, {at(note("status", s.say("A place for the next idea", "为下一个想法留个位置"), s.say("The surface carries the change.", "由表面来传达变化。"), card_w, 132,
                                s.end ? "accent_subtle" : "surface_variant"), cx, (s.h - 132) / 2)});
    }
    if (id == "outline-weight") {
        Element document = note("document", s.say("A selected document", "一份选中的文档"), s.say("Outline, not extra content.", "用边框表达选择。"), card_w, 132);
        document.prop("border_width", Source(Value(s.end ? 4.0 : 1.0))).prop("border_color", Source(Value(s.end ? "accent" : "border")));
        return frame(s, {at(std::move(document), cx, (s.h - 132) / 2)});
    }
    if (id == "corner-shape") {
        const float size = std::min(s.w - 40, 166.0f);
        return frame(s, {at(box({.id = "token", .width = Dim(size), .height = Dim(size), .padding = Edges(18), .justify = "center", .align = "center",
                                 .background = "accent_subtle", .border_color = "border", .border_width = 1, .radius = s.end ? size / 2 : 8},
                                {label("name", s.say("A softer shape", "柔和的形状"), size - 36, "heading")}), (s.w - size) / 2, (s.h - size) / 2)});
    }
    if (id == "depth") {
        const float width = std::min(s.w - 72, 300.0f);
        Element paper = note("paper", s.say("An idea worth keeping", "值得留下的想法"), s.say("The note stays; its depth changes.", "便笺留在原处，层次在改变。"), width, 132);
        paper.prop("shadow", Source(Value(s.end ? "lg" : "sm")));
        return frame(s, {at(std::move(paper), (s.w - width) / 2, (s.h - 132) / 2)});
    }
    if (id == "emphasis") {
        return frame(s, {at(column({.id = "actions", .width = Dim(card_w), .gap = 16}, {
            label("question", s.say("What comes next?", "接下来做什么？"), card_w, "heading"),
            button({.id = "continue", .text = s.say("Continue", "继续"), .variant = s.end ? ButtonVariant::primary : ButtonVariant::secondary, .on_activated = change(s)}),
            button({.id = "later", .text = s.say("Keep for later", "稍后再做"), .variant = s.end ? ButtonVariant::ghost : ButtonVariant::primary, .on_activated = change(s)})}), cx, top)});
    }
    if (id == "slide-card") {
        const float width = std::min(s.w * 0.65f, 240.0f);
        return frame(s, {at(note("document", s.say("The same document", "同一份文档"), "01 / 03", width, 104), s.end ? s.w - width - 12 : 12, (s.h - 104) / 2)});
    }
    if (id == "lift-card") {
        const float width = std::min(s.w - 72, 300.0f);
        Element paper = note("paper", s.say("Ready to read", "准备阅读"), s.say("A note rises into place.", "便笺抬升到眼前。"), width, 104);
        paper.prop("shadow", Source(Value(s.end ? "lg" : "sm")));
        return frame(s, {at(std::move(paper), (s.w - width) / 2, s.end ? 44 : s.h - 136)});
    }
    if (id == "scale-card") {
        Element preview = note("preview", s.say("Closer to the idea", "离想法更近一点"), s.say("Contents scale as one object.", "内容作为同一个对象缩放。"), card_w, 136, "accent_subtle");
        preview.prop("scale", Source(Value(s.end ? 1.0 : 0.72)));
        return frame(s, {at(std::move(preview), cx, (s.h - 136) / 2)});
    }
    if (id == "turn-tile") {
        const float size = std::min(s.w - 64, 150.0f);
        Element tile = column({.id = "tile", .width = Dim(size), .height = Dim(size), .padding = Edges(18), .gap = 8, .justify = "center", .align = "center",
                               .background = "accent_subtle", .border_color = "accent", .border_width = 1, .radius = 20, .rotation = s.end ? 12.0f : -12.0f}, {
            text({.id = "letter", .text = "A", .style = "display", .color = "accent", .size = 44}),
            label("title", s.say("A different angle", "换一个角度"), size - 36, "caption")});
        return frame(s, {at(std::move(tile), (s.w - size) / 2, (s.h - size) / 2)});
    }
    if (id == "squash-chip") {
        const float width = s.end ? std::min(s.w - 24, 230.0f) : std::min(s.w - 64, 150.0f), height = s.end ? 56.0f : 116.0f;
        Element token = box({.id = "token", .width = Dim(width), .height = Dim(height), .padding = Edges(12), .justify = "center", .align = "center",
                             .background = "accent_subtle", .radius = s.end ? 28.0f : 20.0f}, {label("title", s.say("Design review", "设计复核"), width - 24, "label")});
        return frame(s, {at(std::move(token), (s.w - width) / 2, (s.h - height) / 2)});
    }
    if (id == "list-grid") {
        const std::array<std::pair<std::string_view, std::string_view>, 3> names{{{"Passport", "证件"}, {"Tickets", "车票"}, {"Keys", "钥匙"}}};
        const int columns = s.w >= 520 ? 3 : 2;
        const float width = s.end ? std::floor((s.w - 24 - 12 * static_cast<float>(columns - 1)) / static_cast<float>(columns)) : s.w - 24;
        const float content_height = s.end ? (columns == 3 ? 98.0f : 208.0f) : 252.0f;
        const float origin_y = std::min(40.0f, std::max(12.0f, (s.h - content_height) * 0.5f));
        std::vector<Element> items;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const float x = s.end ? 12 + static_cast<float>(i % static_cast<std::size_t>(columns)) * (width + 12) : 12;
            const float y = s.end ? origin_y + static_cast<float>(i / static_cast<std::size_t>(columns)) * 110 : origin_y + static_cast<float>(i) * 88;
            items.push_back(at(note("item-" + std::to_string(i), s.say(names[i].first, names[i].second), s.say("Ready", "已收好"), width, s.end ? 98.0f : 76.0f), x, y));
        }
        return frame(s, std::move(items));
    }
    if (id == "reorder-list") {
        constexpr std::array<std::string_view, 3> en{"Quiet morning", "Open windows", "The way home"}, zh{"安静的早晨", "打开窗户", "回家的路"};
        const std::array<int, 3> order = s.end ? std::array{2, 0, 1} : std::array{0, 1, 2};
        std::vector<Element> items;
        for (std::size_t slot = 0; slot < order.size(); ++slot) {
            const auto i = static_cast<std::size_t>(order[slot]);
            items.push_back(at(list_item(s, "track-" + std::to_string(i), std::string(en[i]), std::string(zh[i]), "0" + std::to_string(i + 1), s.w - 24, i == 2), 12, 52 + static_cast<float>(slot) * 76));
        }
        return frame(s, std::move(items));
    }
    if (id == "split-ratio") {
        const float total = s.w - 24, gap = 10, left = std::floor((total - gap) * (s.end ? 0.65f : 0.38f)), right = total - gap - left;
        Element index = column({.id = "index", .width = Dim(left), .height = Dim(190), .padding = Edges(12), .gap = 12, .background = "surface_variant", .radius = 12}, {
            label("title", s.say("Library", "目录"), left - 24, "label"),
            box({.id = "line-0", .width = Dim(std::max(8.0f, left - 30)), .height = Dim(8), .background = "border", .radius = 4}),
            box({.id = "line-1", .width = Dim(std::max(8.0f, left - 42)), .height = Dim(8), .background = "border", .radius = 4})});
        return frame(s, {at(std::move(index), 12, top), at(note("reading", s.say("Reading", "阅读"), s.say("Give the current subject more space.", "为当前主题留出更多空间。"), right, 190, "accent_subtle"), 12 + left + gap, top)});
    }
    if (id == "accordion-size") {
        const float height = s.end ? 180.0f : 68.0f;
        std::vector<Element> detail{label("title", s.say("Why keep the object?", "为什么保留对象？"), card_w - 28, "label")};
        if (s.end) detail.push_back(label("answer", s.say("Its identity connects the old layout to the new one. You can follow the same object as the space changes.", "对象身份将旧布局与新布局连接起来。空间变化时，仍能跟随同一个对象。"), card_w - 28, "body"));
        return frame(s, {at(column({.id = "detail", .width = Dim(card_w), .height = Dim(height), .padding = Edges(14), .gap = 12, .background = "surface", .border_color = "border", .border_width = 1, .radius = 12}, std::move(detail)), cx, 32),
                         at(note("next", s.say("The next subject", "下一个主题"), s.say("Room for the details.", "为详情让出空间。"), card_w, 74), cx, 44 + height)});
    }
    if (id == "toolbar-reflow") {
        std::vector<Element> items;
        constexpr std::array<std::string_view, 3> en{"Write", "Review", "Share"}, zh{"撰写", "复核", "分享"};
        const float width = std::max(1.0f, std::min((s.w - 32) / 3, 120.0f));
        const float row_x = (s.w - (width * 3 + 16)) / 2;
        for (std::size_t i = 0; i < en.size(); ++i) {
            auto action = button({.id = "action-" + std::to_string(i), .text = s.say(en[i], zh[i]), .variant = i == 0 ? ButtonVariant::primary : ButtonVariant::secondary,
                                  .on_activated = change(s), .width = Dim(s.end ? std::min(card_w, 220.0f) : width)});
            items.push_back(at(std::move(action), s.end ? (s.w - std::min(card_w, 220.0f)) / 2 : row_x + static_cast<float>(i) * (width + 8), s.end ? 72 + static_cast<float>(i) * 58 : 138));
        }
        return frame(s, std::move(items));
    }
    if (id == "tab-indicator") {
        auto changed = [c = s.c](std::string) { if (c.changed) c.changed(); };
        return frame(s, {at(column({.id = "subject", .width = Dim(card_w), .gap = 22}, {
            tabs({.id = "tabs", .selected = binary_choice(s.c, "overview", "activity"), .on_changed = changed}, {
                tab({.id = "overview", .text = s.say("Overview", "概览")}), tab({.id = "activity", .text = s.say("Activity", "动态")})}),
            label("content", s.end != s.c.state->checked.get() ? s.say("The latest activity", "最近的动态") : s.say("Everything in one place", "一处了解全貌"), card_w, "heading")}), cx, top)});
    }
    if (id == "radio-focus") {
        auto selected = binary_choice(s.c, "weekly", "monthly");
        auto changed = [c = s.c](std::string) { if (c.changed) c.changed(); };
        return frame(s, {at(column({.id = "frequency", .width = Dim(card_w), .gap = 18}, {
            label("title", s.say("Send a digest", "发送一份摘要"), card_w, "heading"),
            radio({.id = "weekly", .group = selected, .label = s.say("Every week", "每周"), .value = "weekly", .on_selected = changed}),
            radio({.id = "monthly", .group = selected, .label = s.say("Every month", "每月"), .value = "monthly", .on_selected = changed})}), cx, top)});
    }
    if (id == "menu-highlight") {
        constexpr std::array<std::string_view, 3> keys{"inbox", "projects", "archive"}, en{"Inbox", "Projects", "Archive"}, zh{"收件箱", "项目", "归档"};
        const std::string selected = s.c.state->choice.get();
        const std::string active = std::ranges::find(keys, selected) != keys.end() ? selected : std::string(s.end ? "projects" : "inbox");
        std::vector<Element> items;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            Element action = button({.id = std::string(keys[i]), .text = s.say(en[i], zh[i]),
                .variant = active == keys[i] ? ButtonVariant::primary : ButtonVariant::ghost, .on_activated = [c = s.c, key = std::string(keys[i])] { c.state->choice.set(key); if (c.changed) c.changed(); }, .width = Dim(card_w)});
            action.prop("selected", Source(Value(active == keys[i])));
            items.push_back(std::move(action));
        }
        return frame(s, {at(column({.id = "menu", .width = Dim(card_w), .gap = 12}, std::move(items)), cx, top)});
    }
    if (id == "toggle-thumb") {
        return frame(s, {at(column({.id = "setting", .width = Dim(card_w), .gap = 22}, {
            label("title", s.say("A quieter workspace", "更安静的工作区"), card_w, "heading"),
            toggle({.id = "quiet", .checked = boolean(s.c), .label = s.say("Focus mode", "专注模式"), .on_changed = [c = s.c](bool) { if (c.changed) c.changed(); }}),
            label("status", s.end != s.c.state->checked.get() ? s.say("Notifications are paused.", "通知已暂停。") : s.say("Notifications can arrive.", "可以接收通知。"), card_w, "body", "text_muted")}), cx, top)});
    }
    if (id == "range-handles") {
        // Mirror the same interval at B. Both endpoint states remain fully
        // editable without storing out-of-range offsets in the shared model.
        Prop<double> low = Source{std::nullopt, [p = s.c.state] { return Value(p->end.get() ? 100 - p->secondary.get() : p->value.get()); },
            [p = s.c.state](const Value& value) { (p->end.peek() ? p->secondary : p->value).set(p->end.peek() ? 100 - value.as_double() : value.as_double()); }};
        Prop<double> high = Source{std::nullopt, [p = s.c.state] { return Value(p->end.get() ? 100 - p->value.get() : p->secondary.get()); },
            [p = s.c.state](const Value& value) { (p->end.peek() ? p->value : p->secondary).set(p->end.peek() ? 100 - value.as_double() : value.as_double()); }};
        return frame(s, {at(column({.id = "interval", .width = Dim(card_w), .gap = 28}, {
            label("title", s.say("A comfortable range", "合适的范围"), card_w, "heading"),
            range_slider({.id = "range", .high = high, .label = s.say("Desired range", "期望范围"), .low = low, .step = 5.0,
                          .on_changed = [c = s.c](double, double) { if (c.changed) c.changed(); }, .width = Dim(card_w)}),
            label("units", s.say("Two endpoints. One interval.", "两个端点，一个区间。"), card_w, "caption", "text_muted")}), cx, top)});
    }
    if (id == "title-replace") {
        return frame(s, {at(column({.id = "headline", .width = Dim(card_w), .gap = 18}, {
            label("eyebrow", "OMUI / TYPE", card_w, "caption", "accent"),
            text({.id = "title", .text = s.end ? s.say("Make room for everyone.", "为每一个人留出空间。") : s.say("Make room for ideas.", "为每一个想法留出空间。"), .style = "title", .size = 32, .max_width = Dim(card_w)}),
            label("caption", s.say("Related words, continuous identity.", "词语相关，身份连续。"), card_w, "caption", "text_muted")}), cx, top)});
    }
    if (id == "count-change") {
        const auto count = s.c.state->count.get() + (s.end ? 9 : 0);
        return frame(s, {at(column({.id = "files", .width = Dim(card_w), .gap = 16}, {
            label("label", s.say("Files ready", "文件已就绪"), card_w, "label", "text_muted"),
            text({.id = "number", .text = std::format("{:02}", count), .style = "display", .color = "accent", .size = 72}),
            progress({.id = "progress", .label = s.say("Files ready", "文件已就绪"), .value = std::clamp(static_cast<double>(count) / 16, 0.0, 1.0), .width = Dim(card_w)})}), cx, 52)});
    }
    if (id == "locale-label") {
        const bool chinese = s.end != s.zh;
        return frame(s, {at(column({.id = "navigation", .width = Dim(card_w), .gap = 22}, {
            label("title", chinese ? "同一个地方" : "The same place", card_w, "heading"),
            label("overview", chinese ? "概览" : "Overview", card_w, "body", "accent"),
            label("activity", chinese ? "最近动态" : "Recent activity", card_w, "body"),
            label("settings", chinese ? "偏好设置" : "Preferences", card_w, "body")}), cx, top)});
    }
    if (id == "text-align") {
        return frame(s, {at(column({.id = "article", .width = Dim(card_w), .gap = 18}, {
            text({.id = "title", .text = s.say("Room to think.", "留些空间，想一想。"), .style = "title", .align = s.end ? "center" : "start", .size = 30, .width = Dim(card_w)}),
            text({.id = "subtitle", .text = s.say("A clear thought deserves a clear place.", "清晰的想法，值得一个清晰的位置。"), .style = "body", .color = "text_muted", .align = s.end ? "center" : "start", .width = Dim(card_w)})}), cx, top)});
    }
    if (id == "type-scale") {
        return frame(s, {at(column({.id = "subject", .width = Dim(card_w), .gap = 20}, {
            text({.id = "title", .text = s.say("A new perspective", "一种新的视角"), .style = "heading", .color = s.end ? "text" : "text_muted", .size = s.end ? 34.0f : 18.0f, .max_width = Dim(card_w)}),
            box({.id = "rule", .width = Dim(s.end ? card_w : card_w * 0.45f), .height = Dim(3), .background = "accent", .radius = 1.5f}),
            label("body", s.say("From an overview label to the current subject.", "从概览中的标签，变为当前主题。"), card_w, "body", "text_muted")}), cx, top)});
    }
    if (id == "progress-fill") {
        const double current = s.end ? s.c.state->secondary.get() : s.c.state->value.get();
        return frame(s, {at(column({.id = "upload", .width = Dim(card_w), .gap = 24}, {
            label("title", s.say("Preparing your files", "正在整理文件"), card_w, "heading"),
            progress({.id = "progress", .label = s.say("Progress", "进度"), .value = current / 100, .width = Dim(card_w)}),
            label("value", std::format("{:.0f} / 100", current), card_w, "code", "accent"),
            slider({.id = "amount", .label = s.say("Prepared amount", "完成数量"), .max = 100.0, .step = 5.0, .value = quantity(s.c), .on_changed = [c = s.c](double) { if (c.changed) c.changed(); }, .width = Dim(card_w)})}), cx, top)});
    }
    if (id == "bars-rebalance") {
        constexpr std::array<std::string_view, 3> en{"Design", "Build", "Review"}, zh{"设计", "实现", "复核"};
        const std::array<float, 3> values = s.end ? std::array{0.72f, 0.42f, 0.86f} : std::array{0.35f, 0.82f, 0.52f};
        const float track = s.w - 48;
        std::vector<Element> rows;
        for (std::size_t i = 0; i < en.size(); ++i) rows.push_back(column({.id = "category-" + std::to_string(i), .width = Dim(track), .gap = 8}, {
            label("label", s.say(en[i], zh[i]), track, "label"),
            box({.id = "bar", .width = Dim(track * values[i]), .height = Dim(18), .background = i == 1 ? "text_muted" : "accent", .radius = 6})}));
        return frame(s, {at(column({.id = "chart", .width = Dim(track), .gap = 22}, std::move(rows)), 24, 44)});
    }
    if (id == "score-meter") {
        const float track = s.w - 40, amount = s.end ? 0.82f : 0.36f;
        const float marker = 52, left = 20 + (track - marker) * amount;
        return frame(s, {at(label("title", s.say("Room for improvement", "还有提升空间"), s.w - 40, "heading"), 20, 60),
            at(box({.id = "track", .width = Dim(track), .height = Dim(8), .background = "track", .radius = 4}), 20, 190),
            at(box({.id = "fill", .width = Dim(track * amount), .height = Dim(8), .background = "accent", .radius = 4}), 20, 190),
            at(box({.id = "marker", .width = Dim(marker), .height = Dim(42), .padding = Edges(8), .justify = "center", .align = "center", .background = "accent_subtle", .radius = 12},
                   {label("number", s.end ? "82" : "36", marker - 16, "label", "accent")}), left, 130),
            at(label("minimum", "0", 30, "caption", "text_muted"), 20, 218), at(label("maximum", "100", 34, "caption", "text_muted"), s.w - 54, 218)});
    }
    if (id == "badge-count") {
        const auto count = std::max(std::int64_t{0}, s.c.state->count.get() + (s.end ? 9 : 0));
        return frame(s, {at(column({.id = "inbox", .width = Dim(card_w), .padding = Edges(16), .gap = 20, .background = "surface", .border_color = "border", .border_width = 1, .radius = 16}, {
            row({.id = "summary", .gap = 12, .align = "center"}, {label("title", s.say("Your inbox", "你的收件箱"), card_w - 100, "heading"), badge({.id = "count", .count = count, .tone = BadgeTone::accent})}),
            label("message", s.say("A count that stays close to its subject.", "数量始终紧邻它所描述的对象。"), card_w - 32, "body", "text_muted"),
            button({.id = "add", .text = s.say("Receive one", "接收一条"), .variant = ButtonVariant::ghost,
                    .on_activated = [c = s.c] { c.state->count.set(c.state->count.peek() + 1); if (c.changed) c.changed(); }})}), cx, top)});
    }
    if (id == "step-advance") {
        Prop<std::int64_t> step = Source{std::nullopt, [p = s.c.state] { return Value(std::clamp((p->end.get() ? std::int64_t{2} : std::int64_t{0}) + p->count.get() - 3, std::int64_t{0}, std::int64_t{2})); },
            [p = s.c.state](const Value& value) { p->count.set(value.as_int() - (p->end.peek() ? 2 : 0) + 3); }};
        return frame(s, {at(column({.id = "setup", .width = Dim(card_w), .gap = 20}, {
            label("title", s.say("Ready for the next step", "准备好下一步"), card_w, "heading"),
            stepper({.id = "steps", .free = true, .label = s.say("Setup progress", "设置进度"),
                     .steps = s.zh ? std::vector<std::string>{"命名", "设置", "复核"} : std::vector<std::string>{"Name", "Set up", "Review"}, .value = step, .vertical = s.w < 420,
                     .on_changed = [c = s.c](std::int64_t) { if (c.changed) c.changed(); }, .width = Dim(card_w)})}), cx, 32)});
    }
    if (id == "toast-arrive") {
        std::vector<Element> parts{at(row({.id = "inbox", .width = Dim(card_w), .gap = 12, .align = "center"}, {
            label("title", s.say("Updates", "更新"), card_w - 70, "heading"), badge({.id = "count", .count = std::int64_t(s.end ? 1 : 0), .tone = BadgeTone::accent})}), cx, 64)};
        if (s.end) parts.push_back(at(note("notification", s.say("A new update", "一条新的更新"), s.say("The latest update is now in view.", "最近的更新来到眼前。"), card_w, 116, "accent_subtle"), cx, 126));
        else parts.push_back(at(label("empty", s.say("A quiet moment.", "安静的一刻。"), card_w, "body", "text_muted"), cx, 146));
        return frame(s, std::move(parts));
    }
    if (id == "detail-reveal") {
        std::vector<Element> content{label("title", s.say("One idea, more detail", "一个想法，更多细节"), card_w - 32, "heading")};
        if (s.end) content.push_back(label("body", s.say("A short description can grow into a useful explanation. Its frame reveals the space the thought needs.", "一句简短的描述，也可以展开为有用的解释。边界揭示出这个想法所需要的空间。"), card_w - 32, "body", "text_muted"));
        return frame(s, {at(column({.id = "note", .width = Dim(card_w), .height = Dim(s.end ? 220 : 92), .padding = Edges(16), .gap = 18,
                                   .background = "surface", .border_color = "border", .border_width = 1, .radius = 16, .clip = true}, std::move(content)), cx, 48)});
    }
    if (id == "item-insert" || id == "item-remove") {
        const bool inserted = id == "item-insert";
        const bool middle = inserted ? s.end : !s.end;
        std::vector<Element> parts;
        parts.push_back(at(list_item(s, "first", "Outline the idea", "列出想法", "01", s.w - 24), 12, 46));
        if (middle) parts.push_back(at(list_item(s, "middle", inserted ? "Ask for a second view" : "Review the draft", inserted ? "多看一种视角" : "复核草稿", "02", s.w - 24, true), 12, 122));
        parts.push_back(at(list_item(s, "last", "Share the result", "分享结果", "03", s.w - 24), 12, middle ? 198 : 122));
        return frame(s, std::move(parts));
    }
    if (id == "placeholder-content") {
        std::vector<Element> content;
        if (s.end) {
            content.push_back(row({.id = "person", .gap = 12, .align = "center"}, {avatar({.id = "portrait", .name = "Ada", .size = 40.0}), label("name", "Ada", card_w - 84, "heading")}));
            content.push_back(label("message", s.say("There is a little more room for a clear idea today.", "今天，多留了一点空间给清晰的想法。"), card_w - 32, "body"));
        } else {
            content.push_back(row({.id = "person", .gap = 12, .align = "center"}, {
                box({.id = "portrait", .width = Dim(40), .height = Dim(40), .background = "border", .radius = 20}),
                box({.id = "name", .width = Dim(card_w - 100), .height = Dim(14), .background = "border", .radius = 7})}));
            content.push_back(column({.id = "message", .gap = 10}, {
                box({.id = "line-0", .width = Dim(card_w - 32), .height = Dim(10), .background = "surface_variant", .radius = 5}),
                box({.id = "line-1", .width = Dim((card_w - 32) * 0.72f), .height = Dim(10), .background = "surface_variant", .radius = 5})}));
        }
        return frame(s, {at(column({.id = "message-card", .width = Dim(card_w), .height = Dim(188), .padding = Edges(16), .gap = 22,
                                   .background = "surface", .border_color = "border", .border_width = 1, .radius = 16}, std::move(content)), cx, top)});
    }
    if (id == "stagger-list") {
        std::vector<Element> parts{at(label("title", s.say("A day, in order", "依次展开的一天"), s.w - 24, "heading"), 12, 22)};
        constexpr std::array<std::string_view, 3> en{"Meet at the station", "Walk by the river", "A table for everyone"}, zh{"在车站碰面", "沿河散步", "一起坐下来"};
        if (s.end) for (std::size_t i = 0; i < en.size(); ++i)
            parts.push_back(at(list_item(s, "stop-" + std::to_string(i), std::string(en[i]), std::string(zh[i]), "0" + std::to_string(i + 1), s.w - 24), 12, 76 + static_cast<float>(i) * 76));
        else parts.push_back(at(label("summary", s.say("Three small stops ahead.", "前方有三个小小的停留。"), s.w - 24, "body", "text_muted"), 12, 86));
        return frame(s, std::move(parts));
    }
    if (id == "stagger-grid") {
        const int columns = s.w >= 500 ? 3 : 2;
        const float width = std::floor((s.w - 24 - 12 * static_cast<float>(columns - 1)) / static_cast<float>(columns));
        constexpr std::array<std::string_view, 6> en{"Ideas", "Notes", "People", "Places", "Plans", "Work"}, zh{"想法", "便笺", "人物", "地点", "计划", "作品"};
        std::vector<Element> parts;
        for (std::size_t i = 0; i < en.size(); ++i) {
            const float x = s.end ? 12 + static_cast<float>(i % static_cast<std::size_t>(columns)) * (width + 12) : (s.w - width) / 2;
            const float y = s.end ? 30 + static_cast<float>(i / static_cast<std::size_t>(columns)) * 94 : 128 + static_cast<float>(i) * 3;
            parts.push_back(at(box({.id = "tile-" + std::to_string(i), .width = Dim(width), .height = Dim(82), .padding = Edges(12), .justify = "center", .align = "center",
                                    .background = i % 2 == 0 ? "accent_subtle" : "surface_variant", .border_color = "border", .border_width = 1, .radius = 14},
                                   {label("title", s.say(en[i], zh[i]), width - 24, "label")}), x, y));
        }
        return frame(s, std::move(parts));
    }
    if (id == "distance-stack") {
        const float width = std::min((s.w - 36) / 2, 180.0f), height = 92;
        constexpr std::array<std::string_view, 3> en{"Now", "Next", "Later"}, zh{"现在", "接着", "稍后"};
        std::vector<Element> parts;
        const std::array<Point, 3> spread{{{12, 54}, {s.w - width - 12, 124}, {12, 216}}};
        for (std::size_t i = 0; i < en.size(); ++i) parts.push_back(at(note("note-" + std::to_string(i), s.say(en[i], zh[i]), "0" + std::to_string(i + 1), width, height,
            i == 0 ? "accent_subtle" : "surface"), s.end ? spread[i].x : (s.w - width) / 2 + static_cast<float>(i) * 3,
            s.end ? spread[i].y : (s.h - height) / 2 + static_cast<float>(i) * 3));
        return frame(s, std::move(parts));
    }
    if (id == "paired-swap") {
        const float width = std::min((s.w - 36) / 2, 190.0f), height = 104;
        return frame(s, {
            at(note("draft", s.say("Draft", "草稿"), s.say("First thought", "最初的想法"), width, height, "accent_subtle"), s.end ? s.w - width - 12 : 12, s.end ? 198 : 54),
            at(note("review", s.say("Review", "复核"), s.say("A second view", "另一个视角"), width, height, "surface_variant"), s.end ? 12 : s.w - width - 12, s.end ? 54 : 198)});
    }
    if (id == "cascade-lines") {
        std::vector<Element> lines{label("title", s.say("An idea becomes a plan.", "一个想法，变成计划。"), card_w, "heading")};
        if (s.end) {
            lines.push_back(label("first", s.say("01   Name what matters.", "01   为重要的事命名。"), card_w, "body", "accent"));
            lines.push_back(label("second", s.say("02   Give it room to grow.", "02   为它留出生长空间。"), card_w, "body"));
            lines.push_back(label("third", s.say("03   Make it useful to someone.", "03   让它对某个人有用。"), card_w, "body"));
        } else lines.push_back(label("summary", s.say("Three steps, when you need them.", "在需要时，展开三个步骤。"), card_w, "body", "text_muted"));
        return frame(s, {at(column({.id = "outline", .width = Dim(card_w), .gap = 24}, std::move(lines)), cx, top)});
    }
    return frame(s, {at(label("choose", s.say("Choose a motion from the collection.", "从目录中选择一种动画。"), card_w, "body"), cx, top)});
}

}  // namespace omui::site
