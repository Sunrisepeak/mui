module oweb.shell;

import std;
import mlibs.json;
import omui.core;
import omui.view;
import omui.ui.base;
import omui.ui.markdown;
import omui.ui.markdown.views;

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

Element search_box(std::string label, Finder find) {
    using namespace omui;
    // What the box holds and what was found for it; an answer that comes after the words changed is not theirs.
    struct Asked {
        Signal<std::string>  query{std::string()};
        Signal<std::int64_t> answered{0};
        std::vector<Found>   found;
        bool                 any = false;  // an answer came for the words now in the box
    };
    auto asked = std::make_shared<Asked>();
    Element box = text_box({.id = "box", .label = label, .placeholder = label, .text = asked->query, .on_changed = [asked, find](std::string words) {
                                asked->query.set(words);
                                asked->any = false;
                                asked->found.clear();
                                asked->answered.set(asked->answered.peek() + 1);  // what was found was for other words
                                if (words.empty() || !find) return;
                                find(words, [asked, words](std::vector<Found> found) {
                                    if (asked->query.peek() != words) return;
                                    asked->found = std::move(found);
                                    asked->any = true;
                                    asked->answered.set(asked->answered.peek() + 1);
                                });
                            }});
    // Nothing found is said in the page's own words: the search's name and how many — the program has no words of
    // its own in a language (what a page says is the page's data).
    Element results = column({.id = "results", .gap = 10}, {dynamic([asked, label] {
                                 (void)asked->answered.get();
                                 std::vector<Element> out;
                                 if (asked->any && asked->found.empty()) out.push_back(text({.id = "none", .text = std::format("{} · 0", label), .style = "caption", .color = "text_muted"}));
                                 for (std::size_t at = 0; at < asked->found.size(); ++at) {
                                     const Found& one = asked->found[at];
                                     std::vector<Element> held;
                                     held.push_back(link({.id = std::format("h{}", at), .href = one.href, .text = one.title}));
                                     const std::string under = one.trail.empty() ? one.line : one.line.empty() ? one.trail : std::format("{} — {}", one.trail, one.line);
                                     if (!under.empty()) held.push_back(text({.id = std::format("l{}", at), .text = under, .style = "caption", .color = "text_muted", .max_lines = 2}));
                                     out.push_back(column({.id = std::format("f{}", at), .gap = 2}, std::move(held)));
                                 }
                                 return out;
                             })});
    std::vector<Element> held;
    held.push_back(std::move(box));
    held.push_back(std::move(results));
    Element part = column({.id = std::string(search_part), .gap = 8}, std::move(held));
    part.prop("role", Source{Value("group"), {}, {}});
    part.prop("label", Source{Value(std::move(label)), {}, {}});
    return part;
}

std::vector<Found> found_in(std::span<const Section> sections, std::string_view query, std::string_view top, std::size_t limit) {
    std::vector<Found> out;
    for (Hit& hit : search(sections, query, limit)) {
        std::string trail;
        for (const std::string& above : hit.section.trail) trail += std::format("{}{}", trail.empty() ? "" : " › ", above);
        std::string href = std::format("{}{}{}{}", top, hit.section.route.starts_with('/') ? hit.section.route.substr(1) : hit.section.route, hit.section.anchor.empty() ? "" : "#",
                                       hit.section.anchor);
        if (href.empty()) href = "./";  // the site's first page, from the site's top
        out.push_back({std::move(href), std::move(hit.section.title), std::move(trail), std::move(hit.excerpt)});
    }
    return out;
}

void install_examples() {
    omui::register_base_components();
    (void)omui::install_markdown_views();
}

bool holds_examples(std::string_view document, bool html) {
    // The page's HTML marks a block's language on its code (`<code class="language-view">`, as omui.semantic writes
    // it and mlibs.markdown reads it back); Markdown on its fence (```view, ~~~ view), indented by at most three.
    if (html) return document.contains("language-view");
    for (const auto line : std::views::split(document, '\n')) {
        std::string_view text(line.begin(), line.end());
        const std::size_t indent = text.find_first_not_of(' ');
        if (indent == std::string_view::npos || indent > 3) continue;
        text.remove_prefix(indent);
        const char fence = text.empty() ? '\0' : text.front();
        if ((fence != '`' && fence != '~') || !text.starts_with(std::string(3, fence))) continue;
        text.remove_prefix(std::min(text.find_first_not_of(fence), text.size()));
        text.remove_prefix(std::min(text.find_first_not_of(' '), text.size()));
        if (text.starts_with("view") && (text.size() == 4 || text[4] == ' ' || text[4] == '\r')) return true;
    }
    return false;
}

Element document(Parts& parts, const Layout& layout) {
    using namespace omui;
    // A page's live examples: its blocks of the language `view` run, as its builder ran them — by what the layout
    // names (the shell's own: view documents over the base components, found by name), installed once a page holds
    // one. A program that runs none carries none of it.
    if (layout.examples && holds_examples(parts.document, parts.html)) {
        static bool installed = false;
        if (!std::exchange(installed, true)) layout.examples();
    }
    Element main = column({.id = "main"}, {markdown({.id = std::string(document_part), .format = parts.html ? MarkdownFormat::html : MarkdownFormat::markdown,
                                                      .origin = MarkdownOrigin::app, .source = std::move(parts.document),
                                                      .on_link_activated = layout.follow})});
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
    if (!parts.search.empty()) inside.push_back(search_box(std::move(parts.search), std::move(layout.find)));
    // Onto more lines in a narrow window.
    if (!parts.pages.empty()) inside.push_back(navigation(row({.id = std::string(pages_part), .gap = 16, .wrap = "wrap"}, links(std::move(parts.pages))), parts.site));
    if (!parts.sections.empty()) inside.push_back(navigation(column({.id = std::string(contents_part), .gap = 4}, links(std::move(parts.sections))), parts.contents));
    inside.push_back(document(parts, layout));
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
            if (part.empty() && (name == pages_part || name == contents_part || name == turn_part || name == document_part || name == search_part)) {
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
        if (part == search_part) {
            // The search's name; what it found is the program's to find again.
            if (is_part) parts.search = node["label"].text().value_or({});
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
