module omui.site;

import std;
import omui;
import omui.ui.markdown;

namespace omui::site {

namespace {

auto words(const SiteLinks& links, std::string_view en, std::string_view zh) {
    return computed([language = links.language, en = std::string(en), zh = std::string(zh)] { return language && language->get() == "zh" ? zh : en; });
}

struct GuideEntry {
    std::string_view id, en, zh, summary_en, summary_zh;
};
constexpr std::array<GuideEntry, 3> guides{{
    {"hello", "Get started", "开始使用", "Install, make your first interface, and run it beside its source.", "安装、创建第一个界面，对照源码运行。"},
    {"features", "Core ideas", "核心概念", "Understand the semantic tree, agents, time and the framework's architecture.", "理解语义树、agent、时间与框架的结构。"},
    {"practices", "Practical guide", "实践指南", "Choose a language, compose components, and keep behaviour reproducible.", "选择语言、组合组件，让行为可以重现。"},
}};

std::string folded(std::string_view input) {
    std::string out(input);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

Element guide_result(const GuideEntry& guide, const SiteLinks& links) {
    return column({.id = std::string(guide.id), .padding = Edges(20.0f, 0.0f), .gap = 8, .border_color = "border", .border_width = 0}, {
        button({.id = "open", .text = words(links, guide.en, guide.zh), .variant = ButtonVariant::ghost,
                 .on_activated = [go = links.go, id = std::string(guide.id)] { if (go) go(id); }, .align_self = "start"}),
        text({.id = "summary", .text = words(links, guide.summary_en, guide.summary_zh), .style = "body", .color = "text_muted", .max_width = Dim(660.0f)})});
}

Element exhibit(const SiteLinks& links, std::string id, std::pair<std::string_view, std::string_view> title,
                std::pair<std::string_view, std::string_view> summary, float width, bool mark) {
    Element preview;
    if (mark) {
        preview = column({.id = "preview", .height = Dim(180.0f), .justify = "center", .align = "center", .background = "accent_subtle", .radius = 16},
                         {brand_mark("mark", 128)});
    } else {
        std::vector<Element> tracks;
        for (int i = 0; i < 3; ++i)
            tracks.push_back(row({.id = std::format("track{}", i), .gap = 10, .align = "center"}, {
                box({.id = "lead", .width = Dim(22 + i * 18), .height = Dim(14), .background = "surface", .radius = 4}),
                box({.id = "clip", .width = Dim(140 - i * 24), .height = Dim(26), .background = i == 1 ? "accent_subtle" : "accent", .radius = 5})}));
        preview = column({.id = "preview", .height = Dim(180), .padding = Edges(28), .gap = 12, .justify = "center", .align = "center", .background = "surface_variant", .radius = 16}, std::move(tracks));
    }
    return column({.id = id, .width = Dim(width), .min_width = Dim(0), .gap = 12}, {
        std::move(preview), text({.id = "title", .text = words(links, title.first, title.second), .style = "heading"}),
        text({.id = "summary", .text = words(links, summary.first, summary.second), .style = "body", .color = "text_muted"}),
        button({.id = "open", .text = words(links, "Open the program", "打开程序"), .variant = ButtonVariant::ghost,
                 .on_activated = [go = links.go, id] { if (go) go(id); }, .align_self = "start"})});
}

Element exhibits(const SiteLinks& links, float width) {
    const bool narrow = width < 700;
    const float card_width = narrow ? width : (width - 40) / 2;
    std::vector<Element> cards;
    cards.push_back(exhibit(links, "brand", {"A mark, made here.", "标志，也是程序。"},
                           {"Take apart the O and its inner letters. Reassemble them and take the vector original with you.", "展开 O 和它内部的字母，重新组装，再把矢量原件带走。"}, card_width, true));
    cards.push_back(exhibit(links, "timeline", {"Make time tangible.", "把时间握在手里。"},
                           {"Edit a real timeline: drag, trim, split, play and undo. Every edit has its inverse.", "编辑真实的时间线：拖动、修剪、切开、播放、撤销。每次修改都有它的反向。"}, card_width, false));
    return narrow ? column({.id = "works", .gap = 40}, std::move(cards)) : row({.id = "works", .gap = 40, .align = "start"}, std::move(cards));
}

}  // namespace

Element docs_directory(const SiteLinks& links, std::string current, bool vertical) {
    std::vector<Element> items;
    items.push_back(text({.id = "label", .text = std::string("DOCS"), .style = "caption", .color = "text_muted"}));
    auto add = [&](std::string id, std::string_view en, std::string_view zh) {
        Element b = button({.id = "to_" + id, .text = words(links, en, zh), .variant = current == id ? ButtonVariant::secondary : ButtonVariant::ghost,
                             .on_activated = [go = links.go, id] { if (go) go(id); }, .align_self = "start"});
        b.prop("selected", Source(Value(current == id)));
        items.push_back(std::move(b));
    };
    add("docs", "Overview", "概览");
    for (const auto& g : guides) add(std::string(g.id), g.en, g.zh);
    Element directory = vertical ? column({.id = "directory", .width = Dim(164), .gap = 8, .shrink = 0, .label = "Documentation"}, std::move(items))
                                 : row({.id = "directory", .gap = 8, .align = "center", .wrap = "wrap", .label = "Documentation"}, std::move(items));
    directory.prop("role", Source(Value("navigation")));
    return directory;
}

Element docs_landing(DocsState& docs, const SiteLinks& links, float width) {
    auto* state = &docs;
    Element heading = text({.id = "title", .text = words(links, "Build with understanding.", "从理解，到创造。"), .style = "title", .max_width = Dim(width)});
    heading.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{1})));
    return column({.id = "read", .min_width = Dim(0), .gap = 24, .grow = 1}, {
        text({.id = "eyebrow", .text = words(links, "OMUI / DOCUMENTATION", "OMUI / 文档"), .style = "label", .color = "accent"}),
        std::move(heading),
        text({.id = "lead", .text = words(links, "Start with a working interface. Then explore the ideas, APIs and practices behind it.", "先运行一个真正的界面，再深入它背后的概念、接口与实践。"), .style = "body", .max_width = Dim(680.0f)}),
        text_box({.id = "search", .label = words(links, "Search guides", "搜索指南"), .placeholder = words(links, "Search these guides", "搜索这些指南"), .text = docs.query, .width = Dim(std::min(width, 520.0f))}),
        dynamic([state, links] {
            const std::string query = folded(state->query.get());
            const std::string language = links.language ? links.language->get() : "en";
            std::vector<Element> results;
            for (const auto& g : guides) {
                std::string haystack = std::string(g.en) + " " + std::string(g.zh) + " " + std::string(g.summary_en) + " " + std::string(g.summary_zh);
                if (links.document) haystack += " " + std::string(links.document(g.id, language));
                if (query.empty() || folded(haystack).contains(query)) results.push_back(guide_result(g, links));
            }
            if (results.empty()) results.push_back(text({.id = "empty", .text = words(links, "No matching guide. Try a shorter phrase.", "没有匹配的指南，可以试试更短的词。"), .style = "body", .color = "text_muted"}));
            return std::vector<Element>{column({.id = "results", .gap = 4}, std::move(results))};
        }),
        markdown({.id = "resources", .origin = MarkdownOrigin::app, .source = words(links, "## Open standards\n\nomui implements the [openmui specification](https://github.com/openlifa/openmui). Agent policy follows [openas](https://github.com/openlifa/openas).\n",
                                                   "## 开放标准\n\nomui 实现 [openmui 规范](https://github.com/openlifa/openmui)，agent 的授权策略遵循 [openas](https://github.com/openlifa/openas)。\n"),
                   .on_link_activated = [open = links.open_url](std::string href) { if (open) open(std::move(href)); },
                   .max_width = Dim(680.0f)})});
}

Element explore_landing(const SiteLinks& links, float width) {
    std::vector<Element> tools;
    for (const auto& [id, en, zh] : {std::tuple{"brand", "The mark", "标志构造"}, std::tuple{"timeline", "Timeline editor", "时间线编辑器"}, std::tuple{"reactions", "Reaction lab", "反应实验室"}, std::tuple{"release", "Release workspace", "发布工作台"}})
        tools.push_back(column({.id = id}, {button({.id = "open", .text = words(links, en, zh), .variant = ButtonVariant::ghost,
            .on_activated = [go = links.go, id] { if (go) go(id); }})}));
    return column({.id = "explore", .max_width = Dim(width), .gap = 12}, {
        text({.id = "title", .text = words(links, "More working tools", "更多可用工具"), .style = "label"}),
        row({.id = "works", .gap = 8, .wrap = "wrap"}, std::move(tools))});
}

Element home_destinations(const SiteLinks& links, float width) {
    return column({.id = "next", .gap = 32}, {
        text({.id = "eyebrow", .text = words(links, "MADE WITH OMUI", "用 OMUI 做成"), .style = "label", .color = "accent"}),
        text({.id = "title", .text = words(links, "The proof is something you can use.", "把可能，变成手里的工具。"), .style = "title", .max_width = Dim(780.0f)}),
        exhibits(links, width),
        row({.id = "build", .gap = 20, .align = "center", .wrap = "wrap"}, {
            text({.id = "invitation", .text = words(links, "Make the next one yours.", "下一件，由你来做。"), .style = "heading"}),
            button({.id = "start", .text = words(links, "Get started", "开始使用"), .variant = ButtonVariant::primary, .on_activated = [go = links.go] { if (go) go("hello"); }}),
            button({.id = "docs", .text = words(links, "Read the docs", "阅读文档"), .variant = ButtonVariant::ghost, .on_activated = [go = links.go] { if (go) go("docs"); }})})});
}

std::string docs_html(bool zh) {
    std::string out = zh ? "<h1>从理解，到创造。</h1><p>先运行一个真正的界面，再深入它背后的概念、接口与实践。</p><ul>"
                         : "<h1>Build with understanding.</h1><p>Start with a working interface, then explore its ideas, APIs and practices.</p><ul>";
    for (const auto& g : guides) out += std::format("<li><a href=\"#/{}{}\">{}</a> — {}</li>", zh ? "zh/" : "", canonical_route(g.id), zh ? g.zh : g.en, zh ? g.summary_zh : g.summary_en);
    return out + "</ul>\n";
}

std::string explore_html(bool zh) {
    return std::format("<h1>{}</h1><p>{}</p>{}", zh ? "可以亲手改变的作品。" : "Programs you can touch.",
                       zh ? "每个入口都通向可操作的程序。" : "Every exhibit opens a working program.", destinations_html(zh));
}

std::string destinations_html(bool zh) {
    return std::format("<h2>{}</h2><p><a href=\"#/{}explore/brand\">{}</a> · <a href=\"#/{}explore/timeline\">{}</a> · <a href=\"#/{}docs/start\">{}</a></p>\n",
                       zh ? "用 omui 做成" : "Made with omui", zh ? "zh/" : "", zh ? "标志，也是程序" : "A mark, made here", zh ? "zh/" : "",
                       zh ? "时间线编辑器" : "Timeline editor", zh ? "zh/" : "", zh ? "开始使用" : "Get started");
}

}  // namespace omui::site
