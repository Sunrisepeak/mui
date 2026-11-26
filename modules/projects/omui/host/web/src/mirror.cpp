module omui.host.web;

import std;
import omui.core;
import omui.view;
import omui.semantic;

namespace omui {

namespace {

std::string value_text(const Value& v) {
    switch (v.type()) {
        case Value::Type::text: return std::string(v.as_string());
        case Value::Type::integer: return std::format("{}", v.as_int());
        case Value::Type::real: return std::format("{}", v.as_double());
        default: return {};
    }
}

// What the live region says of a status: its name and its value.
std::string spoken(const spec::SemanticNode& n) {
    const std::string v = value_text(n.value);
    if (n.name.empty()) return v;
    return v.empty() ? n.name : std::format("{}: {}", n.name, v);
}

std::int64_t round(float v) { return static_cast<std::int64_t>(std::lround(v)); }
// A line's place over the canvas, to a quarter of a pixel.
double quarter(float v) { return std::round(static_cast<double>(v) * 4.0) / 4.0; }

// A piece of a line for the page. A run of text is cut into words — each with the white space after it — and each
// word says where the canvas draws it: `{"t": "word ", "x": left, "w": width}`, from the line's left edge, to a
// quarter of a pixel. So a word found or selected is on the word seen, whatever font the page has for it.
void words_of(Value::List& into, std::string_view text, std::size_t from, const TextLineBox& line) {
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    // Where the bytes [begin, end) are drawn: the edges of their graphemes.
    const auto extent = [&](std::size_t begin, std::size_t end) -> std::optional<std::pair<float, float>> {
        float left = std::numeric_limits<float>::infinity(), right = -std::numeric_limits<float>::infinity();
        for (auto stop = std::ranges::lower_bound(line.stops, begin, {}, &TextLineBox::Stop::begin); stop != line.stops.end() && stop->begin < end; ++stop)
            left = std::min(left, stop->left), right = std::max(right, stop->right);
        if (!(right >= left)) return std::nullopt;
        return std::pair{left, right};
    };
    for (std::size_t at = 0; at < text.size();) {
        // A word: what white space it begins with, its letters, the white space after it.
        std::size_t end = at;
        while (end < text.size() && blank(text[end])) ++end;
        while (end < text.size() && !blank(text[end])) ++end;
        while (end < text.size() && blank(text[end])) ++end;
        std::string word(text.substr(at, end - at));
        if (const auto drawn = extent(from + at, from + end))
            into.push_back(Value{}.set("t", std::move(word)).set("x", Value(quarter(drawn->first - line.bounds.x))).set("w", Value(quarter(drawn->second - drawn->first))));
        else into.emplace_back(std::move(word));  // drawn nowhere (a line's trailing space): text all the same
        at = end;
    }
}

// The pieces of a text between two of its bytes — a line of it: runs cut there into their words, the line's break
// left out (the line ends), an element kept with what of it lies between. A child node standing in the text is
// itself on the first line it is on; where it goes on into the next line an element like it stands there (the same
// attributes, `data-omui-part` naming the node), since one element is in one place.
Value::List cut(const std::vector<semantic::Inline>& pieces, const TextLineBox& line, std::unordered_set<std::string>& stood) {
    Value::List out;
    for (const semantic::Inline& piece : pieces) {
        if (piece.end <= line.begin || piece.begin >= line.end || piece.tag == "br") continue;
        const std::size_t begin = std::max(piece.begin, line.begin), end = std::min(piece.end, line.end);
        if (piece.tag.empty()) {
            std::string_view text = std::string_view(piece.text).substr(begin - piece.begin, end - begin);
            while (text.ends_with('\n')) text.remove_suffix(1);
            words_of(out, text, begin, line);
            continue;
        }
        Value::List inner;
        if (piece.children.empty()) {
            if (piece.begin < line.begin) continue;  // what holds nothing (a picture in a line) is on the line it begins on
        } else {
            inner = cut(piece.children, line, stood);
            if (inner.empty()) continue;
        }
        Value part = Value{}.set("tag", piece.tag);
        if (!piece.node.empty() && stood.insert(piece.node).second) {
            part.set("node", piece.node);  // its attributes are its own entry's
        } else if (!piece.attributes.empty() || !piece.node.empty()) {
            Value set = Value(Value::Map{});
            for (const auto& [key, value] : piece.attributes) set.set(key, value);
            if (!piece.node.empty()) set.set("data-omui-part", piece.node);
            part.set("set", std::move(set));
        }
        if (!inner.empty()) part.set("c", Value(std::move(inner)));
        out.push_back(std::move(part));
    }
    return out;
}

}  // namespace

DomMirror::DomMirror(Runtime& runtime) : tree_(runtime) {}

std::optional<Value> DomMirror::sync() {
    // Nothing read while nothing changed: most frames cost a comparison.
    const std::uint64_t revision = tree_.runtime().semantic_revision();
    if (!first_ && revision == synced_) return std::nullopt;
    synced_ = revision;
    spec::SemanticDelta delta = tree_.changes();
    if (delta.empty()) return std::nullopt;
    const bool first = std::exchange(first_, false);

    Value batch;
    if (first && !delta.added.empty()) batch.set("root", delta.added.front().id);

    Value::List removed;
    for (const std::string& id : delta.removed)
        if (elements_.erase(id)) removed.emplace_back(id);
    if (!elements_.contains(main_)) main_.clear();
    if (!removed.empty()) batch.set("remove", Value(std::move(removed)));

    std::vector<Value> out;  // one per element that changed, in the tree's order
    std::unordered_map<std::string, std::size_t> at;
    auto entry = [&](const std::string& id) -> Value& {
        auto [it, made] = at.try_emplace(id, out.size());
        if (made) out.push_back(Value{}.set("id", id));
        return out[it->second];
    };
    std::vector<std::string> regrouped, moved;
    std::string live, alert;
    std::string focus = elements_.contains(focused_) ? focused_ : std::string{};
    std::string focus_name = focused_name_;

    // The nodes of this batch by id, and each one's parent as the batch says it: an element is the one the static
    // projection writes for the node, which depends on what it stands in and, for its text's links, on its children.
    std::unordered_map<std::string_view, const spec::SemanticNode*> changed;
    std::unordered_map<std::string_view, std::string_view> parent_of;
    for (const auto* nodes : {&delta.added, &delta.updated})
        for (const spec::SemanticNode& n : *nodes) {
            changed.emplace(n.id, &n);
            for (const std::string& child : n.children) parent_of.emplace(child, n.id);
        }
    std::deque<spec::SemanticNode> looked_up;  // nodes outside the batch, read from the tree
    const semantic::NodeLookup find = [&](std::string_view id) -> const spec::SemanticNode* {
        if (const auto here = changed.find(id); here != changed.end()) return here->second;
        if (auto node = tree_.node(id)) {
            looked_up.push_back(std::move(*node));
            return &looked_up.back();
        }
        return nullptr;
    };
    const auto tag_around = [&](const std::string& id) -> std::string_view {
        std::string_view parent;
        if (const auto said = parent_of.find(id); said != parent_of.end()) parent = said->second;
        else if (const auto known = elements_.find(id); known != elements_.end()) parent = known->second.parent;
        const auto around = parent.empty() ? elements_.end() : elements_.find(std::string(parent));
        return around == elements_.end() ? std::string_view{} : std::string_view(around->second.tag);
    };
    // A piece of an element's own text for the page: a run a string, an element {"tag","c"}, a child node standing
    // in the text {"tag","node","c"} (its attributes are its own entry's).
    const auto piece = [](this auto const& self, const semantic::Inline& from) -> Value {
        if (from.tag.empty()) return Value(from.text);
        Value out = Value{}.set("tag", from.tag);
        if (!from.node.empty()) out.set("node", from.node);
        else if (!from.attributes.empty()) {
            Value set = Value(Value::Map{});
            for (const auto& [key, value] : from.attributes) set.set(key, value);
            out.set("set", std::move(set));
        }
        if (!from.children.empty()) {
            Value::List inner;
            for (const semantic::Inline& child : from.children) inner.push_back(self(child));
            out.set("c", Value(std::move(inner)));
        }
        return out;
    };
    std::vector<std::string> shown_again;  // children an element's text placed and places no more

    auto take = [&](const spec::SemanticNode& n, bool added) {
        const std::string_view inside = tag_around(n.id);
        Element& e = elements_[n.id];
        // What the element is and holds, as the static page has it (omui.semantic:html, D-236, D-286).
        semantic::HtmlElement made = semantic::html_element(n, inside, find, semantic::HtmlFor::live);
        // A tag that changed is another element: all of it is sent again.
        const bool fresh = added || made.tag != e.tag;
        if (fresh) {
            if (!e.inlined) entry(n.id).set("tag", made.tag);  // what stands in a text is the element its text makes it
            e.tag = made.tag;
            e.attrs.clear();
            e.text.clear();
        }
        std::vector<std::pair<std::string, std::string>> attrs = std::move(made.attributes);
        if (fresh || attrs != e.attrs) {
            Value set = Value(Value::Map{});
            for (const auto& [k, v] : attrs)
                if (std::ranges::find(e.attrs, std::pair{k, v}) == e.attrs.end()) set.set(k, v);
            Value::List unset;
            for (const auto& [k, v] : e.attrs)
                if (std::ranges::find(attrs, k, &std::pair<std::string, std::string>::first) == attrs.end()) unset.emplace_back(k);
            Value& o = entry(n.id);
            if (!set.map()->empty()) o.set("set", std::move(set));
            if (!unset.empty()) o.set("unset", Value(std::move(unset)));
            e.attrs = std::move(attrs);
        }
        if (n.role == Role::main) main_ = n.id;
        else if (main_ == n.id) main_.clear();
        // Its own text: one run as `text`, the elements its marks make as `content`. What stands in its parent's
        // text has its words there.
        if (!e.inlined) {
            // Where the canvas draws the text line by line (a paragraph, a heading, a block of code), each line's
            // words stand in the line's box: what is found or selected is then where it is seen. The lines are the
            // text's own bytes; where they are not (a text the component draws otherwise) the element holds its
            // words as they flow.
            const std::vector<semantic::Inline>& whole = made.tag == "pre" && !made.content.empty() ? made.content.front().children : made.content;
            std::size_t covered = 0;
            for (const semantic::Inline& from : whole) covered = std::max(covered, from.end);
            std::vector<TextLineBox> drawn;
            if (covered > 0 && n.bounds) drawn = tree_.runtime().text_lines(tree_.runtime().find(n.id));
            if (!drawn.empty() && (drawn.front().begin != 0 || drawn.back().end != covered)) drawn.clear();
            // Words without width were not drawn (no shaper gave them glyphs): nothing to stand them by.
            if (std::ranges::any_of(drawn, [](const TextLineBox& line) { return line.end > line.begin && !(line.bounds.width > 0); })) drawn.clear();
            Value::List pieces, lines;
            std::unordered_set<std::string> stood;
            for (const TextLineBox& line : drawn) {
                Value::List held = cut(whole, line, stood);
                if (held.empty()) continue;  // an empty line holds nothing to find
                lines.push_back(Value{}
                                    .set("box", Value(Value::List{Value(quarter(line.bounds.x - n.bounds->x)), Value(quarter(line.bounds.y - n.bounds->y)),
                                                                  Value(quarter(line.bounds.width)), Value(quarter(line.bounds.height))}))
                                    .set("c", Value(std::move(held))));
            }
            if (lines.empty())
                for (const semantic::Inline& from : made.content) pieces.push_back(piece(from));
            const bool plain = lines.empty() && (pieces.empty() || (pieces.size() == 1 && pieces.front().type() == Value::Type::text));
            std::string text = !lines.empty() ? Value(lines).json() : pieces.empty() ? std::string{} : Value(pieces).json();
            if (fresh ? !text.empty() : text != e.text) {
                if (!lines.empty()) entry(n.id).set("lines", Value(std::move(lines)));
                else if (plain) entry(n.id).set("text", pieces.empty() ? std::string{} : std::string(pieces.front().as_string()));
                else entry(n.id).set("content", Value(std::move(pieces)));
                e.text = std::move(text);
            }
        }
        // The children its text places stand there; the others are the element's children.
        if (made.placed != e.placed) {
            for (const std::string& id : e.placed)
                if (!std::ranges::contains(made.placed, id))
                    if (auto child = elements_.find(id); child != elements_.end()) {
                        child->second.inlined = false;
                        child->second.sent.reset();
                        entry(id).set("inline", false);
                        shown_again.push_back(id);
                    }
            for (const std::string& id : made.placed)
                if (!std::ranges::contains(e.placed, id)) {
                    Element& child = elements_[id];
                    child.inlined = true;
                    child.sent.reset();
                    entry(id).set("inline", true);
                }
            e.placed = std::move(made.placed);
        }
        std::vector<std::string> block;
        for (const std::string& child : n.children)
            if (!std::ranges::contains(e.placed, child)) block.push_back(child);
        if (fresh ? !block.empty() : block != e.block) {
            Value::List kids;
            for (const std::string& c : block) kids.emplace_back(c);
            entry(n.id).set("children", Value(std::move(kids)));
            e.block = std::move(block);
        }
        if (added ? !n.children.empty() : n.children != e.children) {
            e.children = n.children;
            regrouped.push_back(n.id);
        }
        // A view that scrolls: where it is and how far it goes. Its children's places are places in what it scrolls.
        // How far is the node's to say; where it is is read once the batch's nodes are taken (below): the offset its
        // children's boxes were laid out with, which a glide moves frame by frame while the node states where it ends.
        if (n.role == Role::scroll_view && n.value.map()) {
            const auto number = [&](std::string_view key) {
                const Value* stated = n.value.get(key);
                return stated && stated->is_number() ? round(static_cast<float>(stated->as_double())) : std::int64_t{0};
            };
            e.extent = {number("max_x"), number("max_y")};
            if (!std::ranges::contains(scrolls_, n.id)) scrolls_.push_back(n.id);
        } else if (e.scroll) {
            entry(n.id).set("scroll", Value{});
            e.scroll.reset();
            std::erase(scrolls_, n.id);
            for (const std::string& child : n.children) moved.push_back(child);
        }
        std::optional<Box> bounds;
        if (n.bounds) bounds = Box{round(n.bounds->x), round(n.bounds->y), round(n.bounds->width), round(n.bounds->height)};
        if (added || bounds != e.bounds) {
            e.bounds = bounds;
            moved.push_back(n.id);
        }
        if (n.states.has(State::focused)) focus = n.id, focus_name = n.name;
        else if (focus == n.id) focus.clear();
        if (focus == n.id) focus_name = n.name;
        std::string says = spoken(n);
        if (!first && n.role == Role::status && !says.empty() && (added || says != e.spoken)) live = says;
        if (!first && added && n.role == Role::alert_dialog && !n.name.empty()) alert = n.name;
        e.spoken = std::move(says);
    };
    for (const spec::SemanticNode& n : delta.added) take(n, true);
    for (const spec::SemanticNode& n : delta.updated) take(n, false);
    // What a text placed and places no more is an element of its own again: its tag, its text, its box.
    for (const std::string& id : shown_again) {
        const auto known = elements_.find(id);
        if (known == elements_.end() || known->second.inlined) continue;
        known->second.tag.clear();
        if (const spec::SemanticNode* node = find(id)) take(*node, false);
        moved.push_back(id);
    }

    // Where each view that scrolls is: said when it changed; its children's boxes stay what they were.
    std::erase_if(scrolls_, [&](const std::string& id) { return !elements_.contains(id); });
    for (const std::string& id : scrolls_) {
        Element& e = elements_.find(id)->second;
        const Point shown = tree_.runtime().scroll_offset(tree_.runtime().find(id)).value_or(Point{});
        const Box now{round(shown.x), round(shown.y), e.extent[0], e.extent[1]};
        if (e.scroll == now) continue;
        e.scroll = now;
        entry(id).set("scroll", Value(Value::List{Value(now[0]), Value(now[1]), Value(now[2]), Value(now[3])}));
        for (const std::string& child : e.children) moved.push_back(child);
    }

    // Children know their parents again; a child that changed parent moves.
    for (const std::string& id : regrouped) {
        const auto it = elements_.find(id);
        if (it == elements_.end()) continue;
        for (const std::string& c : it->second.children)
            if (auto child = elements_.find(c); child != elements_.end() && child->second.parent != id) {
                child->second.parent = id;
                moved.push_back(c);
            }
    }
    // Boxes, parents first: a box is relative to the parent's.
    auto depth = [](const std::string& id) { return std::ranges::count(id, '/'); };
    std::ranges::sort(moved, [&](const std::string& a, const std::string& b) {
        const auto da = depth(a), db = depth(b);
        return da != db ? da < db : a < b;
    });
    moved.erase(std::unique(moved.begin(), moved.end()), moved.end());
    for (const std::string& id : moved) place(id, out, at);

    if (!out.empty()) {
        Value::List nodes;
        for (Value& v : out) {
            if (v.map() && v.map()->size() > 1) nodes.push_back(std::move(v));
        }
        if (!nodes.empty()) batch.set("nodes", Value(std::move(nodes)));
    }
    if (focus != focused_) batch.set("focus", focus.empty() ? Value{} : Value(focus));
    focused_ = std::move(focus);
    focused_name_ = focused_.empty() ? std::string{} : std::move(focus_name);
    // The skip link: past what comes before the main landmark, to the first
    // thing in it a person can focus.
    {
        const std::string to = main_.empty() ? std::string{} : first_focusable(main_);
        std::string name;
        if (!to.empty())
            for (const auto& [k, v] : elements_.at(main_).attrs)
                if (k == "aria-label") name = v;
        if (std::string key = to.empty() ? std::string{} : to + '\x1f' + name; key != skip_) {
            batch.set("skip", to.empty() ? Value{} : Value{}.set("to", to).set("name", name));
            skip_ = std::move(key);
        }
    }
    if (!live.empty()) batch.set("live", live);
    if (!alert.empty()) batch.set("alert", alert);
    if (!batch.map() || batch.map()->empty()) return std::nullopt;
    batch.set("revision", static_cast<std::int64_t>(delta.revision));
    return batch;
}

// The first node in the subtree a person can focus (tree order), enabled.
std::string DomMirror::first_focusable(const std::string& id) const {
    const auto it = elements_.find(id);
    if (it == elements_.end()) return {};
    const auto& attrs = it->second.attrs;
    const auto has = [&](std::string_view k) { return std::ranges::find(attrs, k, &std::pair<std::string, std::string>::first) != attrs.end(); };
    if (has("tabindex") && !has("aria-disabled")) return id;
    for (const std::string& c : it->second.children)
        if (std::string found = first_focusable(c); !found.empty()) return found;
    return {};
}

// A node's box over the canvas, relative to its parent's (each element is
// placed in its parent's); where it moved, its children's move with it.
void DomMirror::place(const std::string& id, std::vector<Value>& out, std::unordered_map<std::string, std::size_t>& at) {
    const auto it = elements_.find(id);
    if (it == elements_.end()) return;
    Element& e = it->second;
    const auto parent = elements_.find(e.parent);
    const Box base = parent != elements_.end() ? parent->second.box : Box{};
    const Box before = e.box;
    e.box = e.bounds.value_or(base);
    // In a view that scrolls, a place in what it scrolls: where it is in the window, and how far the view is scrolled.
    const Box scrolled = parent != elements_.end() && parent->second.scroll ? *parent->second.scroll : Box{};
    const Box rel{e.box[0] - base[0] + scrolled[0], e.box[1] - base[1] + scrolled[1], e.box[2], e.box[3]};
    // A box and an offset are each rounded to a pixel, so their sum wavers by one as the view glides: a box that
    // stands within a pixel of the one sent is the one sent.
    const bool wavers = parent != elements_.end() && parent->second.scroll && e.sent && (*e.sent)[2] == rel[2] && (*e.sent)[3] == rel[3] &&
                        std::abs((*e.sent)[0] - rel[0]) <= 1 && std::abs((*e.sent)[1] - rel[1]) <= 1;
    // What stands in its parent's text is placed by the text: no box of its own.
    if (!e.inlined && e.sent != rel && !wavers) {
        e.sent = rel;
        auto [slot, made] = at.try_emplace(id, out.size());
        if (made) out.push_back(Value{}.set("id", id));
        out[slot->second].set("box", Value(Value::List{Value(rel[0]), Value(rel[1]), Value(rel[2]), Value(rel[3])}));
    }
    if (e.box != before)
        for (const std::string& c : e.children) place(c, out, at);
}

}  // namespace omui
