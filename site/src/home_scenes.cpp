module omui.site;

import std;
import omui;

namespace omui::site {
namespace {

auto words(const HomeScenesSite& site, std::string_view en, std::string_view zh) {
    return computed([lang = site.lang, en = std::string(en), zh = std::string(zh)] { return lang && lang->get() == "zh" ? zh : en; });
}

// A scene: its heading, at most one line, then the thing itself.
Element scene(const HomeScenesSite& site, std::string id, std::pair<std::string_view, std::string_view> heading,
              std::pair<std::string_view, std::string_view> line, Element shown) {
    Element title = text({.id = "heading", .text = words(site, heading.first, heading.second), .style = "title", .align = "center", .max_width = Dim(820.0f)});
    title.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{2})));
    return column({.id = std::move(id), .padding = Edges(56.0f, 0.0f), .gap = 14, .align = "center"},
                  {std::move(title),
                   text({.id = "line", .text = words(site, line.first, line.second), .style = "body", .color = "text_muted", .align = "center", .max_width = Dim(640.0f)}),
                   std::move(shown)});
}

// ① The whole page as pixels, as its structure, as an agent reads it: the site's own lens.
Element views_scene(const HomeScenesSite& site, float width) {
    Signal<std::string>* lens = site.lens;
    Signal<bool>* tools = site.tools;
    Element choose = segmented({.id = "lens",
                                .label = words(site, "View the page as", "这一页看作"),
                                .labels = computed([lang = site.lang] {
                                    return lang && lang->get() == "zh" ? std::vector<std::string>{"像素", "结构", "Agent"}
                                                                       : std::vector<std::string>{"Pixels", "Structure", "Agent"};
                                }),
                                .options = std::vector<std::string>{"pixels", "tree", "agent"},
                                .value = lens ? Prop<std::string>(*lens) : Prop<std::string>(std::string("pixels")),
                                // The foot's lenses shown with it: the way back is where the lens is.
                                .on_changed = [tools](std::string chosen) { if (tools && chosen != "pixels") tools->set(true); },
                                .width = Dim(std::min(width, 360.0f))});
    return scene(site, "views", {"One tree, every view", "一棵语义树，多种视图"},
                 {"The same page as pixels, as its structure, as an agent reads it.", "同一页：像素、结构、Agent 读到的样子。"}, std::move(choose));
}

// ② A line switching language: the letters both share stay put, the rest
// give way (hanim's readable text transition: never an illegible blend).
Element glyphs_scene(HomeScenes& state, const HomeScenesSite& site, float width) {
    Signal<bool> other = state.other;
    Element line = text({.id = "words",
                         .text = computed([lang = site.lang, other] {
                             const bool zh = (lang && lang->get() == "zh") != other.get();
                             return std::string(zh ? "omui 绘制每一个字形" : "omui draws every glyph");
                         }),
                         .style = "heading",
                         .align = "center",
                         .size = width < 600 ? 30.0f : 44.0f});
    line.prop("motion", Source(Value("morph")));  // a change of text: glyph by glyph
    Element toggle = button({.id = "switch",
                             .text = computed([lang = site.lang, other] {
                                 const bool zh = (lang && lang->get() == "zh") != other.get();
                                 return std::string(zh ? "English" : "中文");
                             }),
                             .variant = ButtonVariant::secondary,
                             .on_activated = [other] { other.set(!other.peek()); }});
    return scene(site, "glyphs", {"Only what changed moves", "只动变了的部分"},
                 {"Switch the language: the letters both share stay put; the rest give way to the other's.", "切换语言：共有的字母不动，其余的换成另一种语言的字。"},
                 column({.id = "shown", .gap = 20, .align = "center"}, {std::move(line), std::move(toggle)}));
}

// ③ A soft card let go; its time dragged back and played on, the same step for step.
Element soft_scene(HomeScenes& state, const HomeScenesSite& site, float width) {
    const float room = std::min(width, 560.0f);
    const std::string card_path = site.root + "/soft/shown/card";
    Runtime** runtime = site.runtime;
    const auto act = [runtime, card_path](std::string action) {
        if (runtime && *runtime) (void)(*runtime)->act((*runtime)->find(card_path), action);
    };
    Element controls = row({.id = "controls", .width = Dim(room), .gap = 12, .align = "center"},
                           {button({.id = "drop", .text = words(site, "Drop", "扔下去"), .variant = ButtonVariant::primary, .on_activated = [act] { act("drop"); }}),
                            button({.id = "play", .text = words(site, "Play on", "继续"), .variant = ButtonVariant::ghost, .on_activated = [act] { act("play"); }}),
                            slider({.id = "time", .label = "Time", .max = 3.0, .step = 0.01, .value = state.at, .grow = 1})});
    return scene(site, "soft", {"Drop it. Then pull time back.", "扔下去，再把时间拖回来。"},
                 {"A soft card lands; drag its time back — every step is the same each time.", "软卡片落地；把时间往回拖，每一步每次都一样。"},
                 column({.id = "shown", .gap = 12, .align = "center"},
                        {drop_card({.id = "card", .at = state.at, .span = 3.0, .width = Dim(room), .height = Dim(std::round(room * 0.56f))}), std::move(controls)}));
}

// ④ The page as vectors: saved as SVG, each shape named by the node that drew it.
Element vectors_scene(HomeScenes& state, const HomeScenesSite& site) {
    Runtime** runtime = site.runtime;
    HomeScenes* model = &state;
    const auto save = [runtime, model] {
        if (!runtime || !*runtime) return;
        Runtime& rt = **runtime;
        const auto drawn = rt.capture_svg();
        if (!drawn) {
            model->saved_error.set(drawn.error());
            return;
        }
        std::int64_t shapes = 0;
        for (std::size_t at = drawn->find("data-node=\""); at != std::string::npos; at = drawn->find("data-node=\"", at + 1)) ++shapes;
        const auto bytes = static_cast<std::int64_t>(drawn->size());
        if (!rt.offer_file({"omui-home.svg", "image/svg+xml", *drawn})) {
            model->saved_error.set("this host keeps no files");
            return;
        }
        model->saved_error.set(std::string());
        model->saved_bytes.set(bytes);
        model->saved_shapes.set(shapes);
    };
    Element said = text({.id = "saved",
                         .text = computed([lang = site.lang, model] {
                             const bool zh = lang && lang->get() == "zh";
                             if (const std::string why = model->saved_error.get(); !why.empty()) return std::format("{}{}", zh ? "没有保存：" : "Not saved: ", why);
                             const auto bytes = model->saved_bytes.get(), shapes = model->saved_shapes.get();
                             if (bytes == 0) return std::string();
                             return zh ? std::format("已保存 omui-home.svg：{} KB，{} 个图形各带画出它的节点名", (bytes + 512) / 1024, shapes)
                                       : std::format("Saved omui-home.svg: {} KB, {} shapes, each named by the node that drew it", (bytes + 512) / 1024, shapes);
                         }),
                         .style = "caption",
                         .color = "text_muted",
                         .align = "center"});
    return scene(site, "vectors", {"Any moment, as vectors", "任一刻，都是矢量"},
                 {"Save this page as SVG: every shape in it named by the node that drew it.", "把这一页存成 SVG：每个图形都带着画出它的节点名。"},
                 column({.id = "shown", .gap = 12, .align = "center"},
                        {button({.id = "save", .text = words(site, "Save as SVG", "存成 SVG"), .variant = ButtonVariant::primary, .on_activated = save}),
                         std::move(said)}));
}

}  // namespace

Element home_scenes(HomeScenes& state, const HomeScenesSite& site, float width) {
    return column({.id = "scenes", .width = Dim(width), .gap = 0, .align = "center"},
                  {views_scene(site, width), glyphs_scene(state, site, width), soft_scene(state, site, width), vectors_scene(state, site)});
}

}  // namespace omui::site
