module oweb.shell;

import std;
import mlibs.json;
import omui.core;
import omui.view;
import omui.ui.base;
import omui.ui.markdown;

namespace oweb::shell {

namespace {

using omui::Element;
using omui::Source;
using omui::Value;

// A navigation: a part a reader's tools list and jump to, named.
Element navigation(Element part, std::string_view label) {
    part.prop("role", Source{Value("navigation"), {}, {}});
    part.prop("label", Source{Value(std::string(label)), {}, {}});
    return part;
}

std::vector<Element> links(std::vector<Link>& from) {
    std::vector<Element> out;
    out.reserve(from.size());
    for (Link& one : from) out.push_back(omui::link({.id = std::move(one.id), .href = std::move(one.href), .text = std::move(one.text)}));
    return out;
}

}  // namespace

omui::Element page(Parts parts, Layout layout) {
    using namespace omui;
    std::vector<Element> inside;
    // Onto more lines in a narrow window.
    if (!parts.pages.empty()) inside.push_back(navigation(row({.id = "nav", .gap = 16, .wrap = "wrap"}, links(parts.pages)), parts.site));
    if (!parts.sections.empty()) inside.push_back(navigation(column({.id = "toc", .gap = 4}, links(parts.sections)), parts.contents));
    Element main = column({.id = "main"}, {markdown({.id = "doc", .format = parts.html ? MarkdownFormat::html : MarkdownFormat::markdown, .origin = MarkdownOrigin::app,
                                                      .source = std::move(parts.document)})});
    main.prop("role", Source{Value("main"), {}, {}});
    inside.push_back(std::move(main));
    if (!parts.beside.empty()) inside.push_back(navigation(row({.id = "turn", .gap = 16, .wrap = "wrap"}, links(parts.beside)), parts.turn));
    // One column a reader's eye can follow, in the middle of a wide window: as wide as the window up to that.
    Element shell = column({.id = "page", .width = mlibs::layout::Length::percent(100), .max_width = 760, .padding = 24, .gap = 16}, std::move(inside));
    shell.prop("justify_self", Source{Value("center"), {}, {}});
    if (!layout.scrolls) return shell;
    std::vector<Element> scrolled;
    scrolled.push_back(std::move(shell));
    return scroll({.id = "view", .width = mlibs::layout::Length::percent(100), .height = mlibs::layout::Length::percent(100)}, std::move(scrolled));
}

std::string_view document_id(Layout layout) { return layout.scrolls ? "view/page/main/doc" : "page/main/doc"; }

std::expected<Parts, std::string> read(std::string_view projection, std::string_view document) {
    const auto parsed = mlibs::json::Document::parse(projection, "the page's projection");
    if (!parsed) return std::unexpected(parsed.error().text());
    Parts parts;
    parts.document = std::string(document);
    parts.html = true;
    bool found = false;
    // A node's place: the part it is in and its own id there, whatever stands above the page.
    const auto placed = [](std::string_view id, std::string_view part) -> std::optional<std::string_view> {
        const std::size_t at = id.rfind(part);
        if (at == std::string_view::npos || (at > 0 && id[at - 1] != '/')) return std::nullopt;
        return id.substr(at + part.size());
    };
    for (const mlibs::json::Node& node : parsed->root()["nodes"].items()) {
        const std::string id = node["id"].text().value_or({});
        const auto label = [&] { return node["label"].text().value_or({}); };
        const auto link = [&](std::string_view own) { return Link{std::string(own), node["href"].text().value_or({}), node["text"].text().value_or({})}; };
        if (const auto own = placed(id, "page/nav"); own && own->empty()) parts.site = label();
        else if (const auto own = placed(id, "page/nav/"); own && !own->contains('/')) parts.pages.push_back(link(*own));
        else if (const auto own = placed(id, "page/toc"); own && own->empty()) parts.contents = label();
        else if (const auto own = placed(id, "page/toc/"); own && !own->contains('/')) parts.sections.push_back(link(*own));
        else if (const auto own = placed(id, "page/turn"); own && own->empty()) parts.turn = label();
        else if (const auto own = placed(id, "page/turn/"); own && !own->contains('/')) parts.beside.push_back(link(*own));
        else if (const auto own = placed(id, "page/main/doc"); own && own->empty()) found = true;
    }
    if (!found) return std::unexpected("the page has no document of a site's shell (no node page/main/doc)");
    return parts;
}

}  // namespace oweb::shell
