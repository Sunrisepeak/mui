module omui.semantic;

import std;
import omui.core;
import omui.view;

namespace omui::semantic {

namespace {

// ARIA prohibits naming these: their name is their text.
bool named_by_text(std::string_view aria) { return aria == "none" || aria == "generic" || aria == "paragraph" || aria == "code"; }

bool ranged(Role r) {
    return r == Role::slider || r == Role::spin_button || r == Role::progress || r == Role::meter || r == Role::scroll_bar || r == Role::timeline;
}

bool checkable(Role r) {
    return r == Role::checkbox || r == Role::radio || r == Role::switch_ || r == Role::menu_item_checkbox || r == Role::menu_item_radio;
}

bool selectable(Role r) { return r == Role::tab || r == Role::option || r == Role::tree_item; }

std::string value_text(const Value& v) {
    switch (v.type()) {
        case Value::Type::text: return std::string(v.as_string());
        case Value::Type::integer: return std::format("{}", v.as_int());
        case Value::Type::real: return std::format("{}", v.as_double());
        default: return {};
    }
}

std::string_view aria_of(Role role) { return spec::roles[static_cast<std::size_t>(role)].aria; }

// Text and attribute values as HTML reads them.
std::string escaped(std::string_view text, bool attribute) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"':
                if (attribute) {
                    out += "&quot;";
                    break;
                }
                [[fallthrough]];
            default: out += c;
        }
    }
    return out;
}

const Value* property(const spec::SemanticNode& n, std::string_view key) { return n.properties.get(key); }

std::string_view text_property(const spec::SemanticNode& n, std::string_view key) {
    const Value* stated = property(n, key);
    return stated && stated->type() == Value::Type::text ? stated->as_string() : std::string_view{};
}

// A table's group of rows that holds its header cells: its head.
bool heads(const spec::SemanticNode& group, const std::unordered_map<std::string_view, const spec::SemanticNode*>& by_id) {
    for (const std::string& row : group.children)
        if (const auto found = by_id.find(row); found != by_id.end())
            for (const std::string& cell : found->second->children)
                if (const auto held = by_id.find(cell); held != by_id.end()) return held->second->role == Role::column_header;
    return false;
}

// The element HTML has for a node (the role then said by the element itself), else none: a `div` with the role.
// `inside` is the element the node stands in: a group of rows is a table's head or body only there.
std::string_view tag_of(const spec::SemanticNode& n, std::string_view inside, const std::unordered_map<std::string_view, const spec::SemanticNode*>& by_id) {
    const std::string_view aria = aria_of(n.role);
    if (aria == "heading") {
        const Value* stated = property(n, "level");
        const double level = stated && stated->is_number() ? stated->as_double() : n.value.is_number() ? n.value.as_double() : 2.0;
        static constexpr std::array<std::string_view, 6> headings{"h1", "h2", "h3", "h4", "h5", "h6"};
        return headings[static_cast<std::size_t>(std::clamp(static_cast<int>(level), 1, 6) - 1)];
    }
    if (n.role == Role::list) return property(n, "ordered") && property(n, "ordered")->as_bool() ? "ol" : "ul";
    if (n.role == Role::group && inside == "table") return heads(n, by_id) ? "thead" : "tbody";
    if (n.role == Role::image) return property(n, "source") ? "img" : std::string_view{};
    if (n.role == Role::code) return text_property(n, "raw").empty() ? "pre" : std::string_view{};
    if (n.role == Role::checkbox) return "input";
    for (const auto& [role, tag] : std::initializer_list<std::pair<Role, std::string_view>>{
             {Role::link, "a"}, {Role::button, "button"}, {Role::main, "main"}, {Role::navigation, "nav"}, {Role::paragraph, "p"},
             {Role::list_item, "li"}, {Role::article, "article"}, {Role::region, "section"}, {Role::blockquote, "blockquote"},
             {Role::table, "table"}, {Role::row, "tr"}, {Role::cell, "td"}, {Role::column_header, "th"}, {Role::row_header, "th"},
             {Role::separator, "hr"}})
        if (n.role == role) return tag;
    for (const auto& [role, tag] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"banner", "header"}, {"contentinfo", "footer"}, {"complementary", "aside"}})
        if (aria == role) return tag;
    return {};
}

// The element a mark is in HTML; a formula and embedded markup have none of their own.
std::string_view tag_of(spec::MarkKind kind) {
    switch (kind) {
        case spec::MarkKind::emphasis: return "em";
        case spec::MarkKind::strong: return "strong";
        case spec::MarkKind::code: return "code";
        case spec::MarkKind::deletion: return "del";
        case spec::MarkKind::insertion: return "ins";
        case spec::MarkKind::underline: return "u";
        case spec::MarkKind::mark: return "mark";
        case spec::MarkKind::subscript: return "sub";
        case spec::MarkKind::superscript: return "sup";
        default: return {};
    }
}

}  // namespace

std::vector<std::pair<std::string, std::string>> aria_attributes(const spec::SemanticNode& n) {
    std::vector<std::pair<std::string, std::string>> a;
    auto put = [&](std::string_view k, std::string_view v) { a.emplace_back(std::string(k), std::string(v)); };
    const std::string_view aria = aria_of(n.role);
    // The root is no `application`: that role turns a screen reader's
    // reading off for all inside it, and the nodes inside say what they are.
    if (n.id != "app" && aria != "generic") put("role", aria);
    if (n.id != "app" && !n.name.empty() && !named_by_text(aria)) put("aria-label", n.name);
    if (!n.description.empty()) put("aria-description", n.description);
    const spec::StateSet s = n.states;
    if (s.has(State::disabled)) put("aria-disabled", "true");
    if (checkable(n.role)) put("aria-checked", s.has(State::mixed) ? "mixed" : s.has(State::checked) ? "true" : "false");
    if (n.role == Role::toggle_button) put("aria-pressed", s.has(State::mixed) ? "mixed" : s.has(State::checked) ? "true" : "false");
    if (selectable(n.role)) put("aria-selected", s.has(State::selected) ? "true" : "false");
    else if (s.has(State::selected)) put("aria-selected", "true");
    if (s.has(State::collapsible) || s.has(State::expanded)) put("aria-expanded", s.has(State::expanded) ? "true" : "false");
    if (s.has(State::busy)) put("aria-busy", "true");
    if (s.has(State::invalid)) put("aria-invalid", "true");
    if (s.has(State::required)) put("aria-required", "true");
    if (s.has(State::readonly)) put("aria-readonly", "true");
    if (s.has(State::multiline)) put("aria-multiline", "true");
    if (s.has(State::multiselectable)) put("aria-multiselectable", "true");
    if (s.has(State::modal)) put("aria-modal", "true");
    if (ranged(n.role)) {
        if (n.value.is_number()) put("aria-valuenow", value_text(n.value));
        else if (n.value.map()) {
            // A slider's value with its range ({value, min, max, step}): each its attribute.
            for (auto [key, attr] : {std::pair{"value", "aria-valuenow"}, {"min", "aria-valuemin"}, {"max", "aria-valuemax"}})
                if (const Value* f = n.value.get(key); f && f->is_number()) put(attr, value_text(*f));
        } else if (const std::string v = value_text(n.value); !v.empty()) put("aria-valuetext", v);
        if (n.role == Role::progress && n.value.is_number() && !n.value.map()) {
            put("aria-valuemin", "0");
            put("aria-valuemax", "1");
        }
    }
    // A heading's level (its value).
    if (n.role == Role::heading && n.value.is_number()) put("aria-level", value_text(n.value));
    // The page's live region speaks for a status (once, not twice).
    if (n.role == Role::status) put("aria-live", "off");
    if (s.has(State::focusable)) put("tabindex", "-1");
    std::ranges::sort(a);
    return a;
}

std::string element_text(const spec::SemanticNode& n) {
    std::string out;
    if (n.id != "app" && named_by_text(aria_of(n.role))) out = n.name;
    if (!ranged(n.role)) {
        const std::string v = value_text(n.value);
        if (!v.empty() && v != out) out = out.empty() ? v : std::format("{} {}", out, v);
    }
    return out;
}

std::string static_html(const Snapshot& snapshot, const HtmlOptions& options) {
    if (snapshot.nodes.empty()) return {};
    using Node = spec::SemanticNode;
    std::unordered_map<std::string_view, const Node*> by_id;
    for (const Node& n : snapshot.nodes) by_id.emplace(n.id, &n);
    std::string out;

    // A node's attributes after its tag's own: its ARIA (what the tag does not say already), its anchor, where it leads.
    const auto attributes = [&](const Node& n, std::string_view tag, bool by_content) {
        const bool heading = n.role == Role::heading;
        if (const std::string_view anchor = text_property(n, "anchor"); !anchor.empty()) out += std::format(" id=\"{}\"", escaped(anchor, true));
        if (tag == "a")
            if (const std::string_view href = text_property(n, "href"); !href.empty()) out += std::format(" href=\"{}\"", escaped(href, true));
        if (tag == "img") out += std::format(" src=\"{}\" alt=\"{}\"", escaped(text_property(n, "source"), true), escaped(n.name, true));
        if (tag == "a" || tag == "img")
            if (const std::string_view title = text_property(n, "title"); !title.empty()) out += std::format(" title=\"{}\"", escaped(title, true));
        if (tag == "ol")
            if (const Value* start = property(n, "start"); start && start->is_number() && start->as_int() != 1) out += std::format(" start=\"{}\"", start->as_int());
        if (tag == "td" || tag == "th")
            if (const std::string_view align = text_property(n, "align"); !align.empty()) out += std::format(" align=\"{}\"", escaped(align, true));
        if (n.role == Role::row_header) out += " scope=\"row\"";
        if (tag == "button") out += " type=\"button\"";
        // A checkbox as HTML has it; before the program runs it is not operated.
        if (tag == "input") out += std::format(" type=\"checkbox\" disabled{}", n.states.has(State::checked) ? " checked" : "");
        const std::string_view title = text_property(n, "title");
        for (const auto& [key, value] : aria_attributes(n)) {
            // Before the program runs nothing is operated: no element takes the focus; a tag says its own role.
            if (key == "tabindex" || (key == "role" && (!tag.empty() || value == "none"))) continue;  // a div is no role already
            if ((key == "aria-level" && heading) || (key == "aria-label" && (by_content || tag == "img"))) continue;
            if (key == "aria-description" && !title.empty() && value == title) continue;  // its title says it
            if (tag == "input" && (key == "aria-checked" || key == "aria-disabled")) continue;  // its attributes say them
            out += std::format(" {}=\"{}\"", key, escaped(value, true));
        }
    };

    // Text as an element holds it: escaped, a line feed a line break (S-17) but where lines are kept as written.
    const auto write_text = [&](std::string_view text, bool lines_kept) {
        if (lines_kept) {
            out += escaped(text, false);
            return;
        }
        for (std::size_t at = 0; at <= text.size();) {
            const std::size_t feed = std::min(text.find('\n', at), text.size());
            out += escaped(text.substr(at, feed - at), false);
            if (feed < text.size()) out += "<br>\n";
            at = feed + 1;
        }
    };

    // A node's text with its marks: each range the element of its meaning, a link's or an image's the child node it
    // names — written in its place, so the children's loop passes it over. The children so placed are returned.
    const auto write_marked = [&](const Node& n, std::string_view text, bool lines_kept, std::vector<std::string_view>& placed) {
        // The marks' offsets as bytes, in one pass over the text.
        std::vector<std::pair<std::size_t, std::size_t>> bytes(n.marks.size());
        {
            std::vector<std::pair<std::uint32_t, std::size_t*>> wanted;
            for (std::size_t i = 0; i < n.marks.size(); ++i) {
                wanted.emplace_back(n.marks[i].start, &bytes[i].first);
                wanted.emplace_back(n.marks[i].end, &bytes[i].second);
            }
            std::ranges::sort(wanted, {}, [](const auto& entry) { return entry.first; });
            std::size_t byte = 0;
            std::uint32_t point = 0;
            for (const auto& [offset, into] : wanted) {
                while (byte < text.size() && point < offset) {
                    ++byte;
                    while (byte < text.size() && (static_cast<unsigned char>(text[byte]) & 0xC0) == 0x80) ++byte;
                    ++point;
                }
                *into = byte;
            }
        }
        std::size_t next = 0;
        const auto run = [&](this auto const& self, std::size_t from, std::size_t to) -> void {
            std::size_t cursor = from;
            while (next < n.marks.size() && bytes[next].first < to && bytes[next].first >= cursor) {
                const spec::Mark& mark = n.marks[next];
                const std::size_t begin = bytes[next].first, end = std::min(bytes[next].second, to);
                ++next;
                write_text(text.substr(cursor, begin - cursor), lines_kept);
                const auto child = mark.node.empty() ? by_id.end() : by_id.find(mark.node);
                const Node* node = child == by_id.end() ? nullptr : child->second;
                if (node) placed.push_back(node->id);
                if (mark.kind == spec::MarkKind::image && node) {
                    out += std::format("<img data-omui-target=\"{}\" src=\"{}\" alt=\"{}\"", escaped(node->id, true), escaped(text_property(*node, "source"), true),
                                       escaped(text.substr(begin, end - begin), true));
                    if (const std::string_view title = text_property(*node, "title"); !title.empty()) out += std::format(" title=\"{}\"", escaped(title, true));
                    out += '>';
                    while (next < n.marks.size() && bytes[next].first < end) ++next;  // its text alternative is said
                } else if (mark.kind == spec::MarkKind::link && node) {
                    out += std::format("<a data-omui-target=\"{}\"", escaped(node->id, true));
                    attributes(*node, "a", true);
                    out += '>';
                    self(begin, end);
                    out += "</a>";
                } else if (mark.kind == spec::MarkKind::raw) {
                    // Markup as markup only where the application wrote it (S-3).
                    if (n.origin == spec::Origin::app) out += text.substr(begin, end - begin);
                    else write_text(text.substr(begin, end - begin), lines_kept);
                    while (next < n.marks.size() && bytes[next].first < end) ++next;
                } else if (const std::string_view tag = tag_of(mark.kind); !tag.empty()) {
                    out += std::format("<{}>", tag);
                    self(begin, end);
                    out += std::format("</{}>", tag);
                } else {
                    out += std::format("<span data-omui-mark=\"{}\">", spec::name(mark.kind));
                    self(begin, end);
                    out += "</span>";
                }
                cursor = end;
            }
            write_text(text.substr(cursor, to - cursor), lines_kept);
        };
        run(0, text.size());
    };

    // `tight`: the node is in an item of a list whose items are set close (S-16's `tight`) — a paragraph there is no
    // block of its own, as CommonMark writes it.
    const auto element = [&](this auto const& self, const Node& n, int depth, std::string_view inside, bool tight) -> void {
        const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
        // A comment: what the source keeps and no reader is shown.
        if (const Value* comment = property(n, "comment")) {
            std::string text(comment->as_string());
            for (std::size_t at = text.find("-->"); at != std::string::npos; at = text.find("-->", at)) text.replace(at, 3, "--&gt;");  // it ends where it says
            out += std::format("{}<!--{}-->\n", indent, text);
            return;
        }
        const std::string_view tag = tag_of(n, inside, by_id);
        const bool bare = tight && inside == "li" && tag == "p";
        const std::string_view name = bare ? std::string_view("span") : tag.empty() ? std::string_view("div") : tag;
        // A heading, a link, a button, a paragraph, an item, a cell: named by what they hold, as a reader and a search
        // engine read them (a heading's value is its level, said by its tag).
        const bool heading = n.role == Role::heading;
        const bool by_content = heading || tag == "a" || tag == "button" || tag == "p" || tag == "li" || tag == "td" || tag == "th" || tag == "pre";
        out += std::format("{}<{} data-omui-target=\"{}\"", indent, name, escaped(n.id, true));
        if (depth == 0 && !options.lang.empty()) out += std::format(" lang=\"{}\"", escaped(options.lang, true));
        attributes(n, tag, by_content);
        out += '>';
        if (tag == "img" || tag == "input" || (tag == "hr" && n.children.empty())) {  // elements that hold nothing
            out += '\n';
            return;
        }
        std::vector<std::string_view> placed;
        if (tag == "pre") {
            // A block of code: its lines as written, its language a class (as CommonMark's own HTML has it).
            const std::string_view language = text_property(n, "language");
            out += language.empty() ? std::string("<code>") : std::format("<code class=\"{}{}\">", language.starts_with("language-") ? "" : "language-", escaped(language, true));
            out += escaped(n.name, false);
            // Its children (the button that copies it) stand after it: inside, their lines would be the code's.
            out += "\n</code></pre>\n";
            for (const std::string& child : n.children)
                if (const auto found = by_id.find(child); found != by_id.end()) self(*found->second, depth, inside, false);
            return;
        } else if (const std::string_view raw = text_property(n, "raw"); !raw.empty() && n.origin == spec::Origin::app) {
            out += n.name;  // markup the application wrote, as markup (S-16's `raw`)
        } else {
            std::string text = by_content ? n.name : element_text(n);
            if (by_content && !heading && n.value.type() == Value::Type::text && n.value.as_string() != n.name)
                text += (text.empty() ? "" : " ") + std::string(n.value.as_string());
            if (!n.marks.empty() && text == n.name) write_marked(n, text, false, placed);
            else write_text(text, !raw.empty());
        }
        const Value* close = property(n, "tight");
        const bool held_tight = n.role == Role::list ? close && close->as_bool() : tag == "li" && tight;
        bool nested = false;
        for (const std::string& child : n.children) {
            if (std::ranges::find(placed, std::string_view(child)) != placed.end()) continue;
            if (const auto found = by_id.find(child); found != by_id.end()) {
                if (!nested) out += '\n';
                nested = true;
                self(*found->second, depth + 1, name, held_tight);
            }
        }
        if (nested) out += indent;
        out += std::format("</{}>\n", name);
    };
    element(snapshot.nodes.front(), 0, {}, false);
    return out;
}

}  // namespace omui::semantic
