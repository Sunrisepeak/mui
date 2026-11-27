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
bool heads(const spec::SemanticNode& group, const NodeLookup& find) {
    for (const std::string& row : group.children)
        if (const spec::SemanticNode* found = find(row))
            for (const std::string& cell : found->children)
                if (const spec::SemanticNode* held = find(cell)) return held->role == Role::column_header;
    return false;
}

// The element HTML has for a node (the role then said by the element itself), else none: a `div` with the role.
// `inside` is the element the node stands in: a group of rows is a table's head or body only there.
std::string_view tag_of(const spec::SemanticNode& n, std::string_view inside, const NodeLookup& find) {
    const std::string_view aria = aria_of(n.role);
    if (aria == "heading") {
        const Value* stated = property(n, "level");
        const double level = stated && stated->is_number() ? stated->as_double() : n.value.is_number() ? n.value.as_double() : 2.0;
        static constexpr std::array<std::string_view, 6> headings{"h1", "h2", "h3", "h4", "h5", "h6"};
        return headings[static_cast<std::size_t>(std::clamp(static_cast<int>(level), 1, 6) - 1)];
    }
    if (n.role == Role::list) return property(n, "ordered") && property(n, "ordered")->as_bool() ? "ol" : "ul";
    if (n.role == Role::group && inside == "table") return heads(n, find) ? "thead" : "tbody";
    if (n.role == Role::image) return property(n, "source") ? "img" : std::string_view{};
    if (n.role == Role::code) return text_property(n, "raw").empty() ? "pre" : std::string_view{};
    if (n.role == Role::checkbox) return "input";
    for (const auto& [role, tag] : std::initializer_list<std::pair<Role, std::string_view>>{
             {Role::link, "a"}, {Role::button, "button"}, {Role::main, "main"}, {Role::navigation, "nav"}, {Role::paragraph, "p"}, {Role::figure, "figure"},
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

using Attributes = std::vector<std::pair<std::string, std::string>>;

// A node's attributes: its tag's own (where it is, where it leads, what it shows), then its ARIA — what the tag does
// not say already. `by_content`: the element is named by what it holds, as a reader and a search engine read it.
Attributes attributes_of(const spec::SemanticNode& n, std::string_view tag, bool by_content, HtmlFor where) {
    Attributes out;
    const auto put = [&](std::string_view key, std::string_view value) { out.emplace_back(std::string(key), std::string(value)); };
    const bool heading = n.role == Role::heading;
    if (const std::string_view anchor = text_property(n, "anchor"); !anchor.empty()) put("id", anchor);
    if (tag == "a")
        if (const std::string_view href = text_property(n, "href"); !href.empty()) put("href", href);
    if (tag == "img") {
        put("src", text_property(n, "source"));
        put("alt", n.name);
    }
    const std::string_view title = text_property(n, "title");
    if ((tag == "a" || tag == "img") && !title.empty()) put("title", title);
    if (tag == "ol")
        if (const Value* start = property(n, "start"); start && start->is_number() && start->as_int() != 1) put("start", std::format("{}", start->as_int()));
    if (tag == "td" || tag == "th")
        if (const std::string_view align = text_property(n, "align"); !align.empty()) put("align", align);
    if (n.role == Role::row_header) put("scope", "row");
    if (tag == "button") put("type", "button");
    // What a block of the document gave when it ran (S-16's `output`): said, so a reading of the document's source
    // from its HTML leaves it out.
    if (const Value* output = property(n, "output"); output && output->type() == Value::Type::boolean && output->as_bool()) put("data-output", {});
    // A checkbox as HTML has it; before the program runs it is not operated.
    if (tag == "input") {
        put("type", "checkbox");
        put("disabled", {});
        if (n.states.has(State::checked)) put("checked", {});
    }
    for (const auto& [key, value] : aria_attributes(n)) {
        // Before the program runs nothing is operated: no element takes the focus. A tag says its own role.
        if (key == "tabindex" && where == HtmlFor::document) continue;
        if (key == "role" && (!tag.empty() || value == "none")) continue;  // a div is no role already
        if ((key == "aria-level" && heading) || (key == "aria-label" && (by_content || tag == "img"))) continue;
        if (key == "aria-description" && !title.empty() && value == title) continue;  // its title says it
        if (tag == "input" && (key == "aria-checked" || key == "aria-disabled")) continue;  // its attributes say them
        put(key, value);
    }
    return out;
}

// Text as an element holds it: a line feed a line's break (S-17), but where lines are kept as written. `from` is
// where `text` begins in the node's text.
void runs(std::vector<Inline>& into, std::string_view text, bool lines_kept, std::size_t from = 0) {
    if (lines_kept) {
        if (!text.empty()) into.push_back({.text = std::string(text), .begin = from, .end = from + text.size()});
        return;
    }
    for (std::size_t at = 0; at <= text.size();) {
        const std::size_t feed = std::min(text.find('\n', at), text.size());
        if (feed > at) into.push_back({.text = std::string(text.substr(at, feed - at)), .begin = from + at, .end = from + feed});
        if (feed < text.size()) into.push_back({.tag = "br", .begin = from + feed, .end = from + feed + 1});
        at = feed + 1;
    }
}

// A node's text with its marks: each range the element of its meaning, a link's or an image's the child node it
// names — placed here, so it is not written again among the children (`placed` names them).
std::vector<Inline> marked(const spec::SemanticNode& n, std::string_view text, bool lines_kept, const NodeLookup& find, HtmlFor where, std::vector<std::string>& placed) {
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
    std::vector<Inline> out;
    std::size_t next = 0;
    const auto run = [&](this auto const& self, std::vector<Inline>& into, std::size_t from, std::size_t to) -> void {
        std::size_t cursor = from;
        while (next < n.marks.size() && bytes[next].first < to && bytes[next].first >= cursor) {
            const spec::Mark& mark = n.marks[next];
            const std::size_t begin = bytes[next].first, end = std::min(bytes[next].second, to);
            ++next;
            runs(into, text.substr(cursor, begin - cursor), lines_kept, cursor);
            const spec::SemanticNode* node = mark.node.empty() ? nullptr : find(mark.node);
            if (node) placed.push_back(node->id);
            const auto pass_inner = [&] {
                while (next < n.marks.size() && bytes[next].first < end) ++next;
            };
            if (mark.kind == spec::MarkKind::image && node) {
                // A picture in a line: its text alternative is the range. Over the canvas the canvas shows it.
                Inline image{.tag = where == HtmlFor::live ? "span" : "img", .node = node->id, .begin = begin, .end = end};
                if (where == HtmlFor::live) {
                    image.attributes = {{"role", "img"}, {"aria-label", std::string(text.substr(begin, end - begin))}};
                } else {
                    image.attributes = {{"src", std::string(text_property(*node, "source"))}, {"alt", std::string(text.substr(begin, end - begin))}};
                    if (const std::string_view title = text_property(*node, "title"); !title.empty()) image.attributes.emplace_back("title", std::string(title));
                }
                into.push_back(std::move(image));
                pass_inner();
            } else if (mark.kind == spec::MarkKind::link && node) {
                Inline link{.tag = "a", .node = node->id, .attributes = attributes_of(*node, "a", true, where), .begin = begin, .end = end};
                self(link.children, begin, end);
                into.push_back(std::move(link));
            } else if (mark.kind == spec::MarkKind::raw) {
                // Markup as markup only where the application wrote it (S-3), and only in a document: over the canvas
                // it is the text it is.
                if (n.origin == spec::Origin::app && where == HtmlFor::document) into.push_back({.text = std::string(text.substr(begin, end - begin)), .begin = begin, .end = end, .markup = true});
                else runs(into, text.substr(begin, end - begin), lines_kept, begin);
                pass_inner();
            } else if (const std::string_view tag = tag_of(mark.kind); !tag.empty()) {
                Inline element{.tag = std::string(tag), .begin = begin, .end = end};
                self(element.children, begin, end);
                into.push_back(std::move(element));
            } else {
                Inline element{.tag = "span", .attributes = {{"data-omui-mark", std::string(spec::name(mark.kind))}}, .begin = begin, .end = end};
                self(element.children, begin, end);
                into.push_back(std::move(element));
            }
            cursor = end;
        }
        runs(into, text.substr(cursor, to - cursor), lines_kept, cursor);
    };
    run(out, 0, text.size());
    return out;
}

// Attributes as a tag writes them: `disabled` and `checked` are said by being there.
void write(std::string& out, const Attributes& attributes) {
    for (const auto& [key, value] : attributes) {
        if (value.empty() && (key == "disabled" || key == "checked")) out += std::format(" {}", key);
        else out += std::format(" {}=\"{}\"", key, escaped(value, true));
    }
}

void write(std::string& out, const Inline& piece) {
    if (piece.tag.empty()) {
        out += piece.markup ? piece.text : escaped(piece.text, false);
        return;
    }
    if (piece.tag == "br") {
        out += "<br>\n";
        return;
    }
    out += std::format("<{}", piece.tag);
    if (!piece.node.empty()) out += std::format(" data-omui-target=\"{}\"", escaped(piece.node, true));
    write(out, piece.attributes);
    out += '>';
    if (piece.tag == "img") return;
    for (const Inline& inner : piece.children) write(out, inner);
    out += std::format("</{}>", piece.tag);
}

// A text's runs cut where its look sets white space apart (`Look::apart`), that white space an element of its own face;
// the elements in it the same way.
void set_apart(std::vector<Inline>& pieces, std::span<const Look::Span> apart) {
    std::vector<Inline> out;
    out.reserve(pieces.size());
    for (Inline& piece : pieces) {
        if (!piece.tag.empty()) {
            set_apart(piece.children, apart);
            out.push_back(std::move(piece));
            continue;
        }
        // Only a run whose bytes are the node's text's own (not markup the application wrote).
        if (piece.markup || piece.text.size() != piece.end - piece.begin) {
            out.push_back(std::move(piece));
            continue;
        }
        const auto run = [&](std::size_t from, std::size_t to) {
            Inline part;
            part.text = piece.text.substr(from - piece.begin, to - from);
            part.begin = from;
            part.end = to;
            return part;
        };
        std::size_t at = piece.begin;
        for (const Look::Span& span : apart) {
            if (span.end <= at || span.begin >= piece.end) continue;
            const std::size_t from = std::max<std::size_t>(span.begin, at), to = std::min<std::size_t>(span.end, piece.end);
            if (from > at) out.push_back(run(at, from));
            Inline own;
            own.tag = "span";
            own.attributes = {{"style", span.style}};
            own.children = {run(from, to)};
            own.begin = from;
            own.end = to;
            out.push_back(std::move(own));
            at = to;
        }
        if (at < piece.end) out.push_back(run(at, piece.end));
    }
    pieces = std::move(out);
}

}  // namespace

HtmlElement html_element(const spec::SemanticNode& n, std::string_view inside, const NodeLookup& find, HtmlFor where) {
    HtmlElement element;
    std::string_view tag = tag_of(n, inside, find);
    // What a person operates and what the canvas alone shows keep their ARIA form over the canvas.
    if (where == HtmlFor::live && (tag == "button" || tag == "input" || tag == "img")) tag = {};
    // A heading, a link, a button, a paragraph, an item, a cell: named by what they hold, as a reader and a search
    // engine read them (a heading's value is its level, said by its tag).
    const bool heading = n.role == Role::heading;
    const bool by_content = heading || tag == "a" || tag == "button" || tag == "p" || tag == "li" || tag == "td" || tag == "th" || tag == "pre";
    element.tag = tag.empty() ? std::string("div") : std::string(tag);
    element.attributes = attributes_of(n, tag, by_content, where);
    if (tag == "img" || tag == "input" || (tag == "hr" && n.children.empty())) {
        element.empty = true;
        return element;
    }
    if (tag == "pre") {
        // A block of code: its lines as written, its language a class (as CommonMark's own HTML has it).
        Inline code{.tag = "code"};
        if (const std::string_view language = text_property(n, "language"); !language.empty())
            code.attributes = {{"class", std::format("{}{}", language.starts_with("language-") ? "" : "language-", language)}};
        if (!n.name.empty()) code.children.push_back({.text = n.name, .begin = 0, .end = n.name.size()});
        element.content.push_back(std::move(code));
        element.lines_kept = true;
        return element;
    }
    const std::string_view raw = text_property(n, "raw");
    if (!raw.empty() && n.origin == spec::Origin::app && where == HtmlFor::document) {
        element.content.push_back({.text = n.name, .markup = true});  // markup the application wrote, as markup (S-16's `raw`)
        return element;
    }
    std::string text = by_content ? n.name : element_text(n);
    if (by_content && !heading && n.value.type() == Value::Type::text && n.value.as_string() != n.name) text += (text.empty() ? "" : " ") + std::string(n.value.as_string());
    if (!n.marks.empty() && text == n.name) {
        element.content = marked(n, text, false, find, where, element.placed);
    } else {
        element.lines_kept = !raw.empty();
        runs(element.content, text, element.lines_kept);
    }
    return element;
}

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
    const NodeLookup find = [&](std::string_view id) -> const Node* {
        const auto found = by_id.find(id);
        return found == by_id.end() ? nullptr : found->second;
    };
    std::string out;

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
        HtmlElement made = html_element(n, inside, find, HtmlFor::document);
        const bool bare = tight && inside == "li" && made.tag == "p";
        const std::string_view name = bare ? std::string_view("span") : std::string_view(made.tag);
        // With a look, the parts of its text drawn otherwise than the rest say how (a link's colour, code's face).
        if (options.look) {
            const auto parts = [&](this auto const& self, std::vector<Inline>& pieces) -> void {
                for (Inline& piece : pieces) {
                    if (piece.tag.empty() || piece.tag == "br" || piece.tag == "img") continue;
                    if (const std::string_view style = options.look->within(n.id, piece.begin); !style.empty()) piece.attributes.emplace_back("style", std::string(style));
                    self(piece.children);
                }
            };
            if (made.tag != "pre") {
                parts(made.content);
                if (const auto apart = options.look->set_apart(n.id); !apart.empty()) set_apart(made.content, apart);
            }
        }
        // Code that holds something (the button that copies it) under a look: the node's box is an element around
        // both — what the button is placed in — and the code inside takes its face from it.
        const bool boxed = options.look && made.tag == "pre" && !n.children.empty() && depth > 0;
        if (boxed) out += std::format("{}<div style=\"{}\">\n", indent, escaped(options.look->style(n.id), true));
        out += std::format("{}<{} data-omui-target=\"{}\"", indent, name, escaped(n.id, true));
        if (depth == 0 && !options.lang.empty()) out += std::format(" lang=\"{}\"", escaped(options.lang, true));
        if (options.look) {
            if (depth == 0) {
                // The faces its text is set in, by the names its declarations use.
                std::string faces;
                for (const Look::Font& font : options.look->fonts)
                    if (const std::string key = font_key(font.family); !std::format(" {} ", faces).contains(std::format(" {} ", key))) faces += (faces.empty() ? "" : " ") + key;
                out += std::format(" data-omui-look=\"{}\"", escaped(faces, true));
            }
            // A look of both schemes: the light values here, the dark ones in the page's sheet (`look_sheet`).
            const std::string style = std::format("{}{}", depth == 0 ? options.look->light : std::string(), options.look->style(n.id));
            if (!style.empty() && !boxed) out += std::format(" style=\"{}\"", escaped(style, true));
        }
        write(out, made.attributes);
        out += '>';
        if (made.empty) {  // elements that hold nothing
            out += '\n';
            return;
        }
        if (made.tag == "pre") {
            out += "<code";
            write(out, made.content.front().attributes);
            out += '>';
            out += escaped(n.name, false);
            // Its children (the button that copies it) stand after it: inside, their lines would be the code's.
            out += "\n</code></pre>\n";
            for (const std::string& child : n.children)
                if (const Node* found = find(child)) self(*found, depth, inside, false);
            if (boxed) out += std::format("{}</div>\n", indent);
            return;
        }
        for (const Inline& piece : made.content) write(out, piece);
        const Value* close = property(n, "tight");
        const bool held_tight = n.role == Role::list ? close && close->as_bool() : made.tag == "li" && tight;
        bool nested = false;
        // With a look: the view's nodes the tree does not state, around the children they hold (one element for
        // the children of one such node).
        std::vector<std::uint32_t> around;
        const auto wrapped = [&](std::span<const Look::Wrapper> wanted) {
            std::size_t same = 0;
            while (same < around.size() && same < wanted.size() && around[same] == wanted[same].node) ++same;
            while (around.size() > same) {
                out += std::format("{}</div>\n", std::string((static_cast<std::size_t>(depth) + around.size()) * 2, ' '));
                around.pop_back();
            }
            for (std::size_t at = same; at < wanted.size(); ++at) {
                around.push_back(wanted[at].node);
                out += std::format("{}<div style=\"{}\">\n", std::string((static_cast<std::size_t>(depth) + around.size()) * 2, ' '), escaped(wanted[at].style, true));
            }
        };
        for (const std::string& child : n.children) {
            if (std::ranges::contains(made.placed, child)) continue;
            if (const Node* found = find(child)) {
                if (!nested) out += '\n';
                nested = true;
                if (options.look) wrapped(options.look->around(child));
                self(*found, depth + 1 + static_cast<int>(around.size()), name, held_tight);
            }
        }
        if (options.look) wrapped({});
        if (nested) out += indent;
        out += std::format("</{}>\n", name);
    };
    element(snapshot.nodes.front(), 0, {}, false);
    return out;
}

}  // namespace omui::semantic
