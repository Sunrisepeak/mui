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

// The element HTML has for a role (the role then said by the element itself), else none: a `div` with the role.
std::string_view tag_of(const spec::SemanticNode& n) {
    const std::string_view aria = aria_of(n.role);
    if (aria == "heading") {
        const double level = n.value.is_number() ? n.value.as_double() : 2.0;
        static constexpr std::array<std::string_view, 6> headings{"h1", "h2", "h3", "h4", "h5", "h6"};
        return headings[static_cast<std::size_t>(std::clamp(static_cast<int>(level), 1, 6) - 1)];
    }
    for (const auto& [role, tag] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"link", "a"}, {"button", "button"}, {"main", "main"}, {"navigation", "nav"}, {"banner", "header"}, {"contentinfo", "footer"},
             {"paragraph", "p"}, {"list", "ul"}, {"listitem", "li"}, {"article", "article"}, {"region", "section"}, {"complementary", "aside"}})
        if (aria == role) return tag;
    return {};
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
    std::unordered_map<std::string_view, const spec::SemanticNode*> by_id;
    for (const spec::SemanticNode& n : snapshot.nodes) by_id.emplace(n.id, &n);
    std::string out;
    const auto element = [&](this auto const& self, const spec::SemanticNode& n, int depth) -> void {
        const std::string_view tag = tag_of(n);
        const std::string_view name = tag.empty() ? std::string_view("div") : tag;
        out += std::string(static_cast<std::size_t>(depth) * 2, ' ');
        out += std::format("<{} data-omui-target=\"{}\"", name, escaped(n.id, true));
        if (depth == 0 && !options.lang.empty()) out += std::format(" lang=\"{}\"", escaped(options.lang, true));
        if (tag == "button") out += " type=\"button\"";
        // A heading, a link, a button, a paragraph, an item: named by what they hold, as a reader and a search engine
        // read them (a heading's value is its level, said by its tag).
        const bool heading = n.role == Role::heading;
        const bool by_content = heading || tag == "a" || tag == "button" || tag == "p" || tag == "li";
        for (const auto& [key, value] : aria_attributes(n)) {
            // Before the program runs nothing is operated: no element takes the focus; a tag says its own role.
            if (key == "tabindex" || (key == "role" && !tag.empty())) continue;
            if ((key == "aria-level" && heading) || (key == "aria-label" && by_content)) continue;
            out += std::format(" {}=\"{}\"", key, escaped(value, true));
        }
        out += '>';
        std::string text = by_content ? n.name : element_text(n);
        if (by_content && !heading && n.value.type() == Value::Type::text && n.value.as_string() != n.name)
            text += (text.empty() ? "" : " ") + std::string(n.value.as_string());
        out += escaped(text, false);
        bool nested = false;
        for (const std::string& child : n.children)
            if (const auto found = by_id.find(child); found != by_id.end()) {
                if (!nested) out += '\n';
                nested = true;
                self(*found->second, depth + 1);
            }
        if (nested) out += std::string(static_cast<std::size_t>(depth) * 2, ' ');
        out += std::format("</{}>\n", name);
    };
    element(snapshot.nodes.front(), 0);
    return out;
}

}  // namespace omui::semantic
