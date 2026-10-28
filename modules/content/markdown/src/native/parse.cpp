// md4c's callbacks to the document: a stack of open blocks, the inline text
// and marks of the current leaf, links collected on the side.
module openmui.markdown;

import std;
import mcpplibs.mwrap.md4c;
import openmui.trace;

namespace omui::md {

namespace {

namespace mm = mcpplibs::mwrap::md4c::macros;

unsigned flags_of(Extension e) {
    constexpr std::array<std::pair<Extension, unsigned>, 15> map{{
        {Extension::tables, mm::MD_FLAG_TABLES},
        {Extension::strikethrough, mm::MD_FLAG_STRIKETHROUGH},
        {Extension::tasklists, mm::MD_FLAG_TASKLISTS},
        {Extension::url_autolinks, mm::MD_FLAG_PERMISSIVEURLAUTOLINKS},
        {Extension::www_autolinks, mm::MD_FLAG_PERMISSIVEWWWAUTOLINKS},
        {Extension::email_autolinks, mm::MD_FLAG_PERMISSIVEEMAILAUTOLINKS},
        {Extension::underline, mm::MD_FLAG_UNDERLINE},
        {Extension::math, mm::MD_FLAG_LATEXMATHSPANS},
        {Extension::wikilinks, mm::MD_FLAG_WIKILINKS},
        {Extension::hard_breaks, mm::MD_FLAG_HARD_SOFT_BREAKS},
        {Extension::no_html_blocks, mm::MD_FLAG_NOHTMLBLOCKS},
        {Extension::no_html_spans, mm::MD_FLAG_NOHTMLSPANS},
        {Extension::no_indented_code, mm::MD_FLAG_NOINDENTEDCODEBLOCKS},
        {Extension::permissive_atx_headers, mm::MD_FLAG_PERMISSIVEATXHEADERS},
        {Extension::collapse_whitespace, mm::MD_FLAG_COLLAPSEWHITESPACE},
    }};
    unsigned f = mm::MD_DIALECT_COMMONMARK;
    for (const auto& [x, flag] : map)
        if (has(e, x)) f |= flag;
    return f;
}

constexpr std::string_view replacement = "\xEF\xBF\xBD";  // U+FFFD for NUL (CommonMark §2.3)

// An entity's text as md4c-html decodes it — upstream's own table and rules
// (named, decimal, hexadecimal; unknown names stay as written), so the
// document and md4c-html cannot disagree about one. Decoding renders the
// entity alone as a paragraph and reads it back; each distinct entity is
// decoded once per thread.
std::string decode_entity(std::string_view entity) {
    thread_local std::unordered_map<std::string, std::string> cache;
    if (auto it = cache.find(std::string(entity)); it != cache.end()) return it->second;
    std::string html;
    (void)md_html(
        entity.data(), static_cast<MD_SIZE>(entity.size()),
        [](const MD_CHAR* s, MD_SIZE n, void* out) { static_cast<std::string*>(out)->append(s, n); }, &html, 0, 0);
    std::string_view v = html;
    if (v.starts_with("<p>")) v.remove_prefix(3);
    if (v.ends_with("</p>\n")) v.remove_suffix(5);
    std::string out;
    for (std::size_t i = 0; i < v.size();) {
        if (v[i] == '&') {
            if (v.substr(i).starts_with("&amp;")) { out += '&'; i += 5; continue; }
            if (v.substr(i).starts_with("&lt;")) { out += '<'; i += 4; continue; }
            if (v.substr(i).starts_with("&gt;")) { out += '>'; i += 4; continue; }
            if (v.substr(i).starts_with("&quot;")) { out += '"'; i += 6; continue; }
        }
        out += v[i++];
    }
    cache.emplace(std::string(entity), out);
    return out;
}

std::string attribute(const MD_ATTRIBUTE& a) {
    std::string out;
    if (!a.text) return out;
    for (unsigned i = 0; a.substr_offsets[i] < a.size; ++i) {
        const MD_OFFSET off = a.substr_offsets[i];
        const std::string_view s(a.text + off, a.substr_offsets[i + 1] - off);
        switch (a.substr_types[i]) {
            case MD_TEXT_NULLCHAR: out += replacement; break;
            case MD_TEXT_ENTITY: out += decode_entity(s); break;
            default: out += s; break;
        }
    }
    return out;
}

struct Builder {
    Document*           doc = nullptr;
    std::size_t         base = 0;          // the parsed text's offset in the whole source
    std::vector<Block*> open;              // open blocks; only the innermost gains children
    bool                synthetic = false; // the innermost is a paragraph md4c does not report (tight list)
    std::vector<std::uint32_t> refs;       // the open spans' link indices
    std::vector<Span>   spans;             // the open spans

    Block& top() { return *open.back(); }

    Block& push(Kind k) {
        Block& parent = top();
        parent.children.push_back(Block{.kind = k});
        open.push_back(&parent.children.back());
        return *open.back();
    }
    void pop();

    void end_synthetic() {
        if (synthetic) {
            synthetic = false;
            pop();
        }
    }
    // Text arrives directly inside an item of a tight list: md4c reports no
    // paragraph there, the document has one (whose list says it is tight).
    Inline& leaf() {
        if (top().kind == Kind::item) {
            push(Kind::paragraph);
            synthetic = true;
        }
        return top().content;
    }
};

std::uint64_t fnv(std::uint64_t h, std::string_view s) {
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}
template <class T>
std::uint64_t fnv_value(std::uint64_t h, T v) {
    return fnv(h, std::string_view(reinterpret_cast<const char*>(&v), sizeof v));
}

}  // namespace

std::uint64_t hash_fields(const Block& b, std::span<const Link> links) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    h = fnv_value(h, static_cast<std::uint8_t>(b.kind));
    h = fnv_value(h, b.level);
    h = fnv_value(h, b.tight);
    h = fnv_value(h, b.mark);
    h = fnv_value(h, b.start);
    h = fnv_value(h, static_cast<std::uint8_t>(b.task));
    h = fnv_value(h, static_cast<std::uint8_t>(b.align));
    h = fnv_value(h, b.header);
    h = fnv_value(h, b.fence);
    h = fnv(fnv(h, b.info), b.language);
    h = fnv_value(h, b.content.text.size());
    h = fnv(h, b.content.text);
    for (const Mark& m : b.content.marks) {
        h = fnv_value(h, m.at);
        h = fnv_value(h, static_cast<std::uint16_t>(static_cast<unsigned>(m.op) << 8 | static_cast<unsigned>(m.span)));
        // A link's destination, not its index: equal blocks at different
        // places of a document hash equally.
        if (m.ref != no_ref && m.ref < links.size()) {
            h = fnv(fnv(h, links[m.ref].href), links[m.ref].title);
            h = fnv_value(h, links[m.ref].autolink);
        }
    }
    return h;
}

std::uint64_t hash_child(std::uint64_t h, std::uint64_t child) { return fnv_value(h, child); }

std::uint64_t hash_of(const Block& b, std::span<const Link> links) {
    std::uint64_t h = hash_fields(b, links);
    for (const Block& c : b.children) h = hash_child(h, c.hash);
    return h;
}

void Builder::pop() {
    Block& b = top();
    b.hash = hash_of(b, doc->links);
    open.pop_back();
}

namespace {

int enter_block(MD_BLOCKTYPE type, void* detail, void* user) {
    auto& B = *static_cast<Builder*>(user);
    if (type == MD_BLOCK_DOC) {
        B.open.assign(1, &B.doc->root);
        return 0;
    }
    B.end_synthetic();
    switch (type) {
        case MD_BLOCK_QUOTE: B.push(Kind::quote); break;
        case MD_BLOCK_UL: {
            auto* d = static_cast<MD_BLOCK_UL_DETAIL*>(detail);
            Block& b = B.push(Kind::bullet_list);
            b.tight = d->is_tight != 0;
            b.mark = d->mark;
            break;
        }
        case MD_BLOCK_OL: {
            auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
            Block& b = B.push(Kind::ordered_list);
            b.tight = d->is_tight != 0;
            b.mark = d->mark_delimiter;
            b.start = d->start;
            break;
        }
        case MD_BLOCK_LI: {
            auto* d = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
            Block& b = B.push(Kind::item);
            if (d->is_task) {
                b.task = d->task_mark == ' ' ? Task::open : Task::done;
                b.task_offset = B.base + d->task_mark_offset;
            }
            break;
        }
        case MD_BLOCK_HR: B.push(Kind::rule); break;
        case MD_BLOCK_H: B.push(Kind::heading).level = static_cast<std::uint8_t>(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level); break;
        case MD_BLOCK_CODE: {
            auto* d = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
            Block& b = B.push(Kind::code);
            b.fence = d->fence_char;
            b.info = attribute(d->info);
            b.language = attribute(d->lang);
            break;
        }
        case MD_BLOCK_HTML: B.push(Kind::html); break;
        case MD_BLOCK_P: B.push(Kind::paragraph); break;
        case MD_BLOCK_TABLE: B.push(Kind::table); break;
        case MD_BLOCK_THEAD: B.push(Kind::table_head); break;
        case MD_BLOCK_TBODY: B.push(Kind::table_body); break;
        case MD_BLOCK_TR: B.push(Kind::table_row); break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            Block& b = B.push(Kind::table_cell);
            b.header = type == MD_BLOCK_TH;
            switch (static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align) {
                case MD_ALIGN_LEFT: b.align = Align::left; break;
                case MD_ALIGN_CENTER: b.align = Align::center; break;
                case MD_ALIGN_RIGHT: b.align = Align::right; break;
                default: b.align = Align::none; break;
            }
            break;
        }
        default: break;
    }
    return 0;
}

int leave_block(MD_BLOCKTYPE type, void*, void* user) {
    auto& B = *static_cast<Builder*>(user);
    B.end_synthetic();
    if (type == MD_BLOCK_DOC) return 0;
    B.pop();
    return 0;
}

Span span_of(MD_SPANTYPE t) {
    switch (t) {
        case MD_SPAN_EM: return Span::emphasis;
        case MD_SPAN_STRONG: return Span::strong;
        case MD_SPAN_A: return Span::link;
        case MD_SPAN_IMG: return Span::image;
        case MD_SPAN_CODE: return Span::code;
        case MD_SPAN_DEL: return Span::strikethrough;
        case MD_SPAN_LATEXMATH: return Span::math;
        case MD_SPAN_LATEXMATH_DISPLAY: return Span::display_math;
        case MD_SPAN_WIKILINK: return Span::wikilink;
        case MD_SPAN_U: return Span::underline;
    }
    return Span::emphasis;
}

int enter_span(MD_SPANTYPE type, void* detail, void* user) {
    auto& B = *static_cast<Builder*>(user);
    Inline& in = B.leaf();
    std::uint32_t ref = no_ref;
    if (type == MD_SPAN_A) {
        auto* d = static_cast<MD_SPAN_A_DETAIL*>(detail);
        ref = static_cast<std::uint32_t>(B.doc->links.size());
        B.doc->links.push_back({attribute(d->href), attribute(d->title), d->is_autolink != 0});
    } else if (type == MD_SPAN_IMG) {
        auto* d = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
        ref = static_cast<std::uint32_t>(B.doc->links.size());
        B.doc->links.push_back({attribute(d->src), attribute(d->title), false});
    } else if (type == MD_SPAN_WIKILINK) {
        auto* d = static_cast<MD_SPAN_WIKILINK_DETAIL*>(detail);
        ref = static_cast<std::uint32_t>(B.doc->links.size());
        B.doc->links.push_back({attribute(d->target), {}, false});
    }
    in.marks.push_back({static_cast<std::uint32_t>(in.text.size()), Op::open, span_of(type), ref});
    B.spans.push_back(span_of(type));
    B.refs.push_back(ref);
    return 0;
}

int leave_span(MD_SPANTYPE type, void*, void* user) {
    auto& B = *static_cast<Builder*>(user);
    Inline& in = B.leaf();
    const std::uint32_t ref = B.refs.empty() ? no_ref : B.refs.back();
    if (!B.refs.empty()) {
        B.refs.pop_back();
        B.spans.pop_back();
    }
    in.marks.push_back({static_cast<std::uint32_t>(in.text.size()), Op::close, span_of(type), ref});
    return 0;
}

int text(MD_TEXTTYPE type, const MD_CHAR* s, MD_SIZE n, void* user) {
    auto& B = *static_cast<Builder*>(user);
    Inline& in = B.leaf();
    const auto at = static_cast<std::uint32_t>(in.text.size());
    switch (type) {
        case MD_TEXT_NULLCHAR: in.text += replacement; break;
        case MD_TEXT_BR:
            in.marks.push_back({at, Op::hard_break});
            in.text += '\n';
            break;
        case MD_TEXT_SOFTBR:
            in.marks.push_back({at, Op::soft_break});
            in.text += ' ';
            break;
        case MD_TEXT_ENTITY: in.text += decode_entity(std::string_view(s, n)); break;
        case MD_TEXT_HTML:
            // Raw inline HTML is marked, so it stays verbatim (an HTML block's
            // text is all HTML and needs no marks). Adjacent pieces merge.
            if (B.top().kind != Kind::html) {
                if (!in.marks.empty() && in.marks.back().op == Op::close && in.marks.back().span == Span::html && in.marks.back().at == at) {
                    in.text.append(s, n);
                    in.marks.back().at = static_cast<std::uint32_t>(in.text.size());
                    return 0;
                }
                in.marks.push_back({at, Op::open, Span::html});
                in.text.append(s, n);
                in.marks.push_back({static_cast<std::uint32_t>(in.text.size()), Op::close, Span::html});
                return 0;
            }
            in.text.append(s, n);
            break;
        default: in.text.append(s, n); break;
    }
    return 0;
}

}  // namespace

void assign_anchors(Block& b, const Anchors& base, Anchors& used) {
    auto count = [&](const std::string& s) {
        int n = 0;
        if (auto it = base.find(s); it != base.end()) n += it->second;
        if (auto it = used.find(s); it != used.end()) n += it->second;
        return n;
    };
    if (b.kind == Kind::heading) {
        const std::string s = slug(plain_text(b));
        const int n = count(s);
        b.anchor = n == 0 ? s : std::format("{}-{}", s, n);
        ++used[s];
        // The suffixed form is taken too: "a", "a" and "a-1" become a, a-1, a-1-1.
        if (b.anchor != s && count(b.anchor) == 0) used[b.anchor] = 1;
    }
    for (Block& c : b.children) assign_anchors(c, base, used);
}

// Parses `source` into `doc`, appending top-level blocks and links: the
// Stream's tail parse and the whole-source parse share this.
bool parse_into(Document& doc, std::string_view source, std::size_t base) {
    if (source.size() >= 0xffffffffull) return false;
    Builder b;
    b.doc = &doc;
    b.base = base;
    MD_PARSER p{};
    p.abi_version = 0;
    p.flags = flags_of(doc.extensions);
    p.enter_block = enter_block;
    p.leave_block = leave_block;
    p.enter_span = enter_span;
    p.leave_span = leave_span;
    p.text = text;
    const int rc = md_parse(source.data(), static_cast<MD_SIZE>(source.size()), &p, &b);
    return rc == 0;
}

Document parse(std::string_view source, Extension extensions) {
    trace::Span span("markdown.parse", "markdown");
    Document doc;
    doc.extensions = extensions;
    doc.complete = parse_into(doc, source, 0);
    const Anchors none;
    Anchors used;
    assign_anchors(doc.root, none, used);
    doc.root.hash = hash_of(doc.root, doc.links);
    return doc;
}

std::optional<Extension> extension(std::string_view n) noexcept {
    constexpr std::array<std::pair<std::string_view, Extension>, 19> names{{
        {"commonmark", commonmark},
        {"gfm", gfm},
        {"autolinks", autolinks},
        {"no_html", no_html},
        {"tables", Extension::tables},
        {"strikethrough", Extension::strikethrough},
        {"tasklists", Extension::tasklists},
        {"url_autolinks", Extension::url_autolinks},
        {"www_autolinks", Extension::www_autolinks},
        {"email_autolinks", Extension::email_autolinks},
        {"underline", Extension::underline},
        {"math", Extension::math},
        {"wikilinks", Extension::wikilinks},
        {"hard_breaks", Extension::hard_breaks},
        {"no_html_blocks", Extension::no_html_blocks},
        {"no_html_spans", Extension::no_html_spans},
        {"no_indented_code", Extension::no_indented_code},
        {"permissive_atx_headers", Extension::permissive_atx_headers},
        {"collapse_whitespace", Extension::collapse_whitespace},
    }};
    for (const auto& [k, v] : names)
        if (k == n) return v;
    return std::nullopt;
}

Extension extensions(std::span<const std::string> names) noexcept {
    Extension e = Extension::none;
    for (const std::string& n : names)
        if (auto x = extension(n)) e = e | *x;
    return e;
}

std::string_view name(Kind k) noexcept {
    constexpr std::array<std::string_view, 15> names{"document",  "quote", "bullet_list", "ordered_list", "item",
                                                     "rule",      "heading", "code",      "html",         "paragraph",
                                                     "table",     "table_head", "table_body", "table_row", "table_cell"};
    return names[static_cast<std::size_t>(k)];
}

}  // namespace omui::md
