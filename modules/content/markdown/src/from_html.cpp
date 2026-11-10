// HTML as content input (D-141): a subset read into the document Markdown
// makes — the same blocks and spans, read as a browser reads them (a
// whitespace run with a line break is a soft break, entities decoded,
// unclosed tags closed, containers passed through, scripts dropped).
// Markdown's own HTML (to_html) is read back as it was written.
module mlibs.markdown.api;

import std;

namespace mlibs::markdown {

namespace {

bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Entities decoded as md4c-html decodes them; an ampersand that begins none is itself.
std::string decoded(std::string_view text) {
    std::string out;
    for (std::size_t at = 0; at < text.size();) {
        if (text[at] == '&') {
            const std::size_t end = text.find(';', at);
            if (end != std::string_view::npos && end - at <= 32) {
                const std::string_view entity = text.substr(at, end - at + 1);
                const bool named = std::ranges::all_of(entity.substr(1, entity.size() - 2), [](char c) {
                    return std::isalnum(static_cast<unsigned char>(c)) || c == '#';
                });
                if (named && entity.size() > 2) {
                    out += decode_entity(entity);
                    at = end + 1;
                    continue;
                }
            }
        }
        out += text[at++];
    }
    return out;
}

struct Tag {
    std::string                                      name;  // lower case
    std::vector<std::pair<std::string, std::string>> attributes;  // names lower case, values decoded
    bool                                             closing = false, self_closing = false;

    [[nodiscard]] std::optional<std::string_view> attribute(std::string_view wanted) const {
        for (const auto& [key, value] : attributes)
            if (key == wanted) return value;
        return std::nullopt;
    }
};

// One tag from `<`; none when what follows is not a tag (then the `<` is text).
std::optional<Tag> read_tag(std::string_view html, std::size_t& at) {
    std::size_t cursor = at + 1;
    Tag tag;
    if (cursor < html.size() && html[cursor] == '/') {
        tag.closing = true;
        ++cursor;
    }
    const std::size_t name_begin = cursor;
    while (cursor < html.size() && (std::isalnum(static_cast<unsigned char>(html[cursor])) || html[cursor] == '-')) ++cursor;
    if (cursor == name_begin || !std::isalpha(static_cast<unsigned char>(html[name_begin]))) return std::nullopt;
    tag.name = lower(html.substr(name_begin, cursor - name_begin));
    while (cursor < html.size()) {
        while (cursor < html.size() && space(html[cursor])) ++cursor;
        if (cursor >= html.size()) return std::nullopt;
        if (html[cursor] == '>') {
            at = cursor + 1;
            return tag;
        }
        if (html.substr(cursor).starts_with("/>")) {
            tag.self_closing = true;
            at = cursor + 2;
            return tag;
        }
        const std::size_t key_begin = cursor;
        while (cursor < html.size() && !space(html[cursor]) && html[cursor] != '=' && html[cursor] != '>' && html[cursor] != '/') ++cursor;
        if (cursor == key_begin) {
            ++cursor;  // a stray character: skipped
            continue;
        }
        std::string key = lower(html.substr(key_begin, cursor - key_begin));
        std::string value;
        while (cursor < html.size() && space(html[cursor])) ++cursor;
        if (cursor < html.size() && html[cursor] == '=') {
            ++cursor;
            while (cursor < html.size() && space(html[cursor])) ++cursor;
            if (cursor < html.size() && (html[cursor] == '"' || html[cursor] == '\'')) {
                const char quote = html[cursor];
                const std::size_t end = html.find(quote, cursor + 1);
                if (end == std::string_view::npos) return std::nullopt;
                value = decoded(html.substr(cursor + 1, end - cursor - 1));
                cursor = end + 1;
            } else {
                const std::size_t begin = cursor;
                while (cursor < html.size() && !space(html[cursor]) && html[cursor] != '>') ++cursor;
                value = decoded(html.substr(begin, cursor - begin));
            }
        }
        tag.attributes.emplace_back(std::move(key), std::move(value));
    }
    return std::nullopt;
}

constexpr std::array<std::string_view, 10> dropped_whole{"script", "style", "template", "iframe", "object", "embed", "noscript", "svg", "math", "head"};
constexpr std::array<std::string_view, 21> containers{"div", "section", "article", "main", "header", "footer", "nav", "aside", "figure", "body", "html",
                                                      "details", "center", "dl", "form", "fieldset", "address", "hgroup", "search", "menu", "picture"};
constexpr std::array<std::string_view, 6> paragraphs{"p", "summary", "figcaption", "dt", "dd", "legend"};

std::optional<Span> span_of(std::string_view name) {
    if (name == "em" || name == "i" || name == "cite" || name == "dfn" || name == "var") return Span::emphasis;
    if (name == "strong" || name == "b") return Span::strong;
    if (name == "code" || name == "kbd" || name == "samp" || name == "tt") return Span::code;
    if (name == "del" || name == "s" || name == "strike") return Span::strikethrough;
    if (name == "u" || name == "ins") return Span::underline;
    if (name == "a") return Span::link;
    if (name == "x-equation") return Span::math;
    if (name == "x-wikilink") return Span::wikilink;
    return std::nullopt;
}

bool is_leaf(Kind kind) { return kind == Kind::paragraph || kind == Kind::heading || kind == Kind::table_cell || kind == Kind::code; }

class Reader {
public:
    explicit Reader(Document& doc) : doc_(doc) { frames_.push_back({&doc.root}); }

    void read(std::string_view html) {
        for (std::size_t at = 0; at < html.size();) {
            if (html[at] == '<') {
                if (html.substr(at).starts_with("<!--")) {
                    const std::size_t end = html.find("-->", at + 4);
                    at = end == std::string_view::npos ? html.size() : end + 3;
                    continue;
                }
                if (html.substr(at).starts_with("<!") || html.substr(at).starts_with("<?")) {
                    const std::size_t end = html.find('>', at);
                    at = end == std::string_view::npos ? html.size() : end + 1;
                    continue;
                }
                std::size_t after = at;
                if (auto tag = read_tag(html, after)) {
                    at = after;
                    if (!tag->closing && std::ranges::contains(dropped_whole, tag->name) && !tag->self_closing) {
                        const std::string end_tag = "</" + tag->name;
                        std::size_t end = at;
                        while (true) {
                            end = html.find('<', end);
                            if (end == std::string_view::npos || lower(html.substr(end, end_tag.size())) == end_tag) break;
                            ++end;
                        }
                        if (end == std::string_view::npos) return finish();
                        const std::size_t close = html.find('>', end);
                        at = close == std::string_view::npos ? html.size() : close + 1;
                        continue;
                    }
                    element(*tag);
                    continue;
                }
            }
            const std::size_t end = std::min(html.find('<', at + 1), html.size());
            text(html.substr(at, end - at));
            at = end;
        }
        finish();
    }

private:
    struct Open {
        Span          span;
        std::uint32_t ref = no_ref;
    };
    struct Frame {
        Block*            block;
        bool              implicit = false;      // a paragraph no tag opened (a tight item's)
        bool              any_implicit = false;  // an item: a paragraph of its was bare; a list: an item's was
        std::vector<Open> spans;                 // a leaf's spans open now
        bool              after_break = false;   // a leaf: a <br> just written (its line break is not a soft break)
    };

    Document&          doc_;
    std::vector<Frame> frames_;

    Frame& top() { return frames_.back(); }
    Block& open_block(Kind kind) {
        Block& parent = *top().block;
        parent.children.push_back(Block{.kind = kind});
        frames_.push_back({&parent.children.back()});
        return parent.children.back();
    }

    // The leaf inline content goes into: the open one, or a paragraph no tag opened.
    Frame* leaf() {
        if (is_leaf(top().block->kind)) return &top();
        const Kind kind = top().block->kind;
        if (kind == Kind::table || kind == Kind::table_head || kind == Kind::table_body || kind == Kind::table_row || kind == Kind::bullet_list ||
            kind == Kind::ordered_list)
            return nullptr;  // no text there
        open_block(Kind::paragraph);
        top().implicit = true;
        if (frames_.size() >= 2 && frames_[frames_.size() - 2].block->kind == Kind::item) frames_[frames_.size() - 2].any_implicit = true;
        return &top();
    }

    void close_spans(Frame& frame, std::size_t keep = 0) {
        Inline& in = frame.block->content;
        while (frame.spans.size() > keep) {
            in.marks.push_back({static_cast<std::uint32_t>(in.text.size()), Op::close, frame.spans.back().span, frame.spans.back().ref});
            frame.spans.pop_back();
        }
    }

    // Closes the top frame: a leaf's spans closed, its trailing whitespace trimmed; an
    // empty implicit paragraph dropped; a list tight when its items' paragraphs were implicit.
    void pop() {
        Frame frame = std::move(top());
        frames_.pop_back();
        Block& b = *frame.block;
        if (is_leaf(b.kind) && b.kind != Kind::code) {
            close_spans(frame);
            Inline& in = b.content;
            // Whitespace after the last span's edge: a code span's or an equation's own spaces stay.
            std::uint32_t kept = 0;
            for (const Mark& m : in.marks)
                if (m.op == Op::open || m.op == Op::close) kept = std::max(kept, m.at);
            std::size_t end = in.text.size();
            while (end > kept && space(in.text[end - 1])) --end;
            if (end < in.text.size()) {
                in.text.resize(end);
                std::erase_if(in.marks, [&](const Mark& m) { return (m.op == Op::soft_break || m.op == Op::hard_break) && m.at >= end; });
                for (Mark& m : in.marks) m.at = std::min(m.at, static_cast<std::uint32_t>(end));
            }
            if (frame.implicit && in.text.empty() && std::ranges::none_of(in.marks, [](const Mark& m) { return m.span == Span::image; }))
                top().block->children.pop_back();
        }
        // A list is tight when an item's paragraph was bare (no <p>): an item tells its list as it closes.
        if (b.kind == Kind::item && frame.any_implicit) top().any_implicit = true;
        if (b.kind == Kind::bullet_list || b.kind == Kind::ordered_list) b.tight = frame.any_implicit;
    }

    void close_leaf() {
        if (is_leaf(top().block->kind)) pop();
    }

    // Closes frames up to and including the nearest of `kinds`; none open: nothing.
    void pop_to(std::initializer_list<Kind> kinds) {
        const auto found = std::find_if(frames_.rbegin(), frames_.rend() - 1, [&](const Frame& f) { return std::ranges::contains(kinds, f.block->kind); });
        if (found == frames_.rend() - 1) return;
        const auto depth = static_cast<std::size_t>(std::distance(found, frames_.rend()));
        while (frames_.size() >= depth) pop();
    }

    void text(std::string_view raw) {
        if (top().block->kind == Kind::code) {
            top().block->content.text += decoded(raw);
            return;
        }
        const std::string words = decoded(raw);
        if (std::ranges::all_of(words, space) && !is_leaf(top().block->kind)) return;  // between blocks
        Frame* frame = leaf();
        if (!frame) return;
        Inline& in = frame->block->content;
        for (std::size_t at = 0; at < words.size();) {
            if (!space(words[at])) {
                std::size_t end = at;
                while (end < words.size() && !space(words[end])) ++end;
                in.text.append(words, at, end - at);
                frame->after_break = false;
                at = end;
                continue;
            }
            std::size_t end = at;
            bool newline = false;
            while (end < words.size() && space(words[end])) newline = newline || words[end] == '\n', ++end;
            const bool start = in.text.empty() && in.marks.empty();  // nothing in the leaf yet
            if (frame->after_break || start) {
                // After a <br>, or at the leaf's start: the whitespace is not text.
            } else if (newline) {
                in.marks.push_back({static_cast<std::uint32_t>(in.text.size()), Op::soft_break});
                in.text += ' ';
            } else {
                in.text.append(words, at, end - at);
            }
            frame->after_break = false;
            at = end;
        }
    }

    void element(const Tag& tag) {
        const std::string& name = tag.name;
        Block& here = *top().block;
        if (here.kind == Kind::code) {  // inside <pre>: only its end and its <code> matter
            if (tag.closing && name == "pre") pop();
            else if (!tag.closing && name == "code" && here.language.empty())
                if (const auto kind = tag.attribute("class"); kind && kind->starts_with("language-")) here.info = here.language = std::string(kind->substr(9));
            return;
        }
        if (std::ranges::contains(dropped_whole, name)) return;  // a stray end tag
        if (std::ranges::contains(containers, name)) return close_leaf();
        if (std::ranges::contains(paragraphs, name)) {
            close_leaf();
            if (!tag.closing && !tag.self_closing) open_block(Kind::paragraph);
            return;
        }
        if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
            close_leaf();
            if (!tag.closing) open_block(Kind::heading).level = static_cast<std::uint8_t>(name[1] - '0');
            return;
        }
        if (name == "blockquote") {
            close_leaf();
            if (tag.closing) pop_to({Kind::quote});
            else open_block(Kind::quote);
            return;
        }
        if (name == "ul" || name == "ol") {
            close_leaf();
            if (tag.closing) return pop_to({name == "ul" ? Kind::bullet_list : Kind::ordered_list});
            Block& list = open_block(name == "ul" ? Kind::bullet_list : Kind::ordered_list);
            list.mark = name == "ul" ? '-' : '.';
            if (const auto start = tag.attribute("start")) {
                std::uint32_t number = 1;
                if (std::from_chars(start->data(), start->data() + start->size(), number).ec == std::errc{}) list.start = number;
            }
            return;
        }
        if (name == "li") {
            close_leaf();
            if (tag.closing) return pop_to({Kind::item});
            if (top().block->kind == Kind::item) pop();  // an item left open
            if (top().block->kind != Kind::bullet_list && top().block->kind != Kind::ordered_list) open_block(Kind::bullet_list).mark = '-';
            open_block(Kind::item);
            return;
        }
        if (name == "input") {
            // A task's box: the first thing in its item.
            if (top().block->kind == Kind::item && top().block->children.empty() && tag.attribute("type") == "checkbox")
                top().block->task = tag.attribute("checked") ? Task::done : Task::open;
            return;
        }
        if (name == "pre") {
            close_leaf();
            if (!tag.closing) open_block(Kind::code).fence = '`';
            return;
        }
        if (name == "hr") {
            close_leaf();
            Block& parent = *top().block;
            parent.children.push_back(Block{.kind = Kind::rule});
            return;
        }
        if (name == "table") {
            close_leaf();
            if (tag.closing) pop_to({Kind::table});
            else open_block(Kind::table);
            return;
        }
        if (name == "thead" || name == "tbody" || name == "tfoot") {
            close_leaf();
            if (tag.closing) return pop_to({Kind::table_head, Kind::table_body});
            if (std::ranges::none_of(frames_, [](const Frame& f) { return f.block->kind == Kind::table; })) return;  // outside a table
            while (top().block->kind != Kind::table) pop();
            open_block(name == "thead" ? Kind::table_head : Kind::table_body);
            return;
        }
        if (name == "tr") {
            close_leaf();
            if (tag.closing) return pop_to({Kind::table_row});
            if (top().block->kind == Kind::table_row) pop();
            if (top().block->kind == Kind::table) open_block(Kind::table_body);
            if (top().block->kind != Kind::table_head && top().block->kind != Kind::table_body) return;
            open_block(Kind::table_row);
            return;
        }
        if (name == "th" || name == "td") {
            close_leaf();
            if (tag.closing) return;
            if (top().block->kind != Kind::table_row) return;
            Block& cell = open_block(Kind::table_cell);
            cell.header = name == "th";
            const auto align = tag.attribute("align");
            cell.align = align == "left" ? Align::left : align == "center" ? Align::center : align == "right" ? Align::right : Align::none;
            return;
        }
        if (name == "caption") return close_leaf();
        if (name == "br") {
            Frame* frame = leaf();
            if (!frame) return;
            Inline& in = frame->block->content;
            in.marks.push_back({static_cast<std::uint32_t>(in.text.size()), Op::hard_break});
            in.text += '\n';
            frame->after_break = true;
            return;
        }
        if (name == "img") {
            Frame* frame = leaf();
            if (!frame) return;
            Inline& in = frame->block->content;
            const auto ref = static_cast<std::uint32_t>(doc_.links.size());
            doc_.links.push_back({.href = std::string(tag.attribute("src").value_or("")), .title = std::string(tag.attribute("title").value_or(""))});
            in.marks.push_back({static_cast<std::uint32_t>(in.text.size()), Op::open, Span::image, ref});
            in.text += tag.attribute("alt").value_or("");
            in.marks.push_back({static_cast<std::uint32_t>(in.text.size()), Op::close, Span::image, ref});
            frame->after_break = false;
            return;
        }
        if (const auto span = span_of(name)) {
            if (tag.closing) {
                if (!is_leaf(top().block->kind)) return;
                Frame& frame = top();
                const auto open = std::find_if(frame.spans.rbegin(), frame.spans.rend(), [&](const Open& o) {
                    return o.span == *span || (*span == Span::math && o.span == Span::display_math);
                });
                if (open == frame.spans.rend()) return;
                close_spans(frame, static_cast<std::size_t>(std::distance(open, frame.spans.rend())) - 1);
                return;
            }
            Frame* frame = leaf();
            if (!frame) return;
            Inline& in = frame->block->content;
            Open made{*span};
            if (*span == Span::link || *span == Span::wikilink) {
                made.ref = static_cast<std::uint32_t>(doc_.links.size());
                const auto target = *span == Span::link ? tag.attribute("href") : tag.attribute("data-target");
                doc_.links.push_back({.href = std::string(target.value_or("")), .title = std::string(tag.attribute("title").value_or(""))});
            }
            if (*span == Span::math && tag.attribute("type") == "display") made.span = Span::display_math;
            in.marks.push_back({static_cast<std::uint32_t>(in.text.size()), Op::open, made.span, made.ref});
            frame->spans.push_back(made);
            return;
        }
        // Any other tag: not a block or a span of the document; its text stays.
    }

    void finish() {
        while (frames_.size() > 1) pop();
    }
};

void rehash(Block& b, std::span<const Link> links) {
    for (Block& child : b.children) rehash(child, links);
    b.hash = hash_of(b, links);
}

}  // namespace

Document from_html(std::string_view html, Extension extensions) {
    Document doc;
    doc.extensions = extensions;
    Reader(doc).read(html);
    const Anchors none;
    Anchors used;
    assign_anchors(doc.root, none, used);
    rehash(doc.root, doc.links);
    return doc;
}

}  // namespace mlibs::markdown
