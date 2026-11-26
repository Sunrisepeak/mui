module oweb.shell;

import std;
import mlibs.json;
import omui.core;
import omui.view;
import omui.ui.base;
import omui.ui.markdown;

namespace oweb::shell {

using omui::Element;
using omui::Source;
using omui::Value;

Element navigation(Element part, std::string_view label) {
    part.prop("role", Source{Value("navigation"), {}, {}});
    part.prop("label", Source{Value(std::string(label)), {}, {}});
    return part;
}

std::vector<Element> links(std::vector<Link> from) {
    std::vector<Element> out;
    out.reserve(from.size());
    for (Link& one : from) out.push_back(omui::link({.id = std::move(one.id), .href = std::move(one.href), .text = std::move(one.text)}));
    return out;
}

Element document(Parts& parts) {
    using namespace omui;
    Element main = column({.id = "main"}, {markdown({.id = std::string(document_part), .format = parts.html ? MarkdownFormat::html : MarkdownFormat::markdown,
                                                      .origin = MarkdownOrigin::app, .source = std::move(parts.document)})});
    main.prop("role", Source{Value("main"), {}, {}});
    return main;
}

Element view(std::vector<Element> held, Layout layout) {
    using namespace omui;
    // The view's root takes the name of where it is mounted (an application's view: `main`); the page is under it
    // either way, so its nodes are named the same in a window that scrolls it and in a view that does.
    if (layout.scrolls) return scroll({.id = "view", .width = mlibs::layout::Length::percent(100), .height = mlibs::layout::Length::percent(100)}, std::move(held));
    return column({.id = "view", .width = mlibs::layout::Length::percent(100)}, std::move(held));
}

omui::Element page(Parts parts, Layout layout) {
    using namespace omui;
    std::vector<Element> inside;
    // Onto more lines in a narrow window.
    if (!parts.pages.empty()) inside.push_back(navigation(row({.id = std::string(pages_part), .gap = 16, .wrap = "wrap"}, links(std::move(parts.pages))), parts.site));
    if (!parts.sections.empty()) inside.push_back(navigation(column({.id = std::string(contents_part), .gap = 4}, links(std::move(parts.sections))), parts.contents));
    inside.push_back(document(parts));
    if (!parts.beside.empty()) inside.push_back(navigation(row({.id = std::string(turn_part), .gap = 16, .wrap = "wrap"}, links(std::move(parts.beside))), parts.turn));
    // One column a reader's eye can follow, in the middle of a wide window: as wide as the window up to that.
    std::vector<Element> held;
    held.push_back(column({.id = "page", .width = mlibs::layout::Length::percent(100), .max_width = 760, .padding = 24, .gap = 16, .align_self = "center"}, std::move(inside)));
    return view(std::move(held), layout);
}

std::expected<Parts, std::string> read(std::string_view projection, const std::function<std::string(std::string_view id)>& markup) {
    const auto parsed = mlibs::json::Document::parse(projection, "the page's projection");
    if (!parsed) return std::unexpected(parsed.error().text());
    Parts parts;
    parts.html = true;
    bool found = false;
    const std::string under = std::format("{}/", mounted_at);
    for (const mlibs::json::Node& node : parsed->root()["nodes"].items()) {
        const std::string id = node["id"].text().value_or({});
        if (!id.starts_with(under)) continue;
        // The part the node is, or is in: the first of its names that is a part's. What stands in the document is
        // the document's, whatever it is called.
        std::string_view part, own;
        bool is_part = false;
        for (std::string_view rest = std::string_view(id).substr(under.size()); !rest.empty();) {
            const std::size_t cut = rest.find('/');
            const std::string_view name = rest.substr(0, cut);
            rest = cut == std::string_view::npos ? std::string_view{} : rest.substr(cut + 1);
            if (part.empty() && (name == pages_part || name == contents_part || name == turn_part || name == document_part)) {
                part = name;
                is_part = rest.empty();
            }
            own = name;
        }
        if (part.empty()) continue;
        if (part == document_part) {
            if (is_part && !found) parts.document = markup(id), found = true;
            continue;
        }
        std::string& label = part == pages_part ? parts.site : part == contents_part ? parts.contents : parts.turn;
        std::vector<Link>& held = part == pages_part ? parts.pages : part == contents_part ? parts.sections : parts.beside;
        if (is_part) label = node["label"].text().value_or({});
        else if (const auto href = node["href"].text()) held.push_back({std::string(own), *href, node["text"].text().value_or({})});
    }
    if (!found) return std::unexpected(std::format("the page has no document of a site's shell (no node called {} under {})", document_part, mounted_at));
    return parts;
}

}  // namespace oweb::shell
