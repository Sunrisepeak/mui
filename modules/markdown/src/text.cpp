// Reading a document as text: a block's visible text, heading slugs, and the
// styled runs a text engine lays out.
module openmui.markdown;

import std;

namespace omui::md {

namespace {

void text_of(const Block& b, std::string& out) {
    switch (b.kind) {
        case Kind::paragraph:
        case Kind::heading:
        case Kind::table_cell:
        case Kind::code:
        case Kind::html: {
            std::string_view t = b.content.text;
            if (b.kind == Kind::code || b.kind == Kind::html)
                while (t.ends_with('\n')) t.remove_suffix(1);
            out += t;
            return;
        }
        case Kind::rule: return;
        case Kind::table_row:
            for (std::size_t i = 0; i < b.children.size(); ++i) {
                if (i) out += '\t';
                text_of(b.children[i], out);
            }
            return;
        default: break;
    }
    bool first = true;
    for (const Block& c : b.children) {
        if (c.kind == Kind::rule) continue;
        if (!first) out += '\n';
        first = false;
        text_of(c, out);
    }
}

}  // namespace

std::string plain_text(const Block& block) {
    std::string out;
    text_of(block, out);
    return out;
}

std::string slug(std::string_view text) {
    // GitHub: lower-case, drop what is neither a letter, a digit, a space, a
    // hyphen nor an underscore; spaces become hyphens. Bytes of non-ASCII
    // characters (CJK, accented letters) are kept as they are.
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c >= 0x80) out += ch;
        else if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += ch;
        else if (c == ' ') out += '-';
    }
    return out;
}

std::vector<Run> runs(const Inline& in) {
    std::vector<Run> out;
    std::array<int, span_count> depth{};
    std::vector<std::uint32_t> links;  // open link-like spans, innermost last
    std::uint32_t pos = 0;
    auto spans_now = [&] {
        std::uint16_t s = 0;
        for (std::size_t i = 0; i < span_count; ++i)
            if (depth[i] > 0) s |= static_cast<std::uint16_t>(1u << i);
        return s;
    };
    auto emit = [&](std::uint32_t to) {
        if (to <= pos) return;
        const std::uint16_t s = spans_now();
        const std::uint32_t link = links.empty() ? no_ref : links.back();
        if (!out.empty() && out.back().end == pos && out.back().spans == s && out.back().link == link) out.back().end = to;
        else out.push_back({pos, to, s, link});
        pos = to;
    };
    for (const Mark& m : in.marks) {
        if (m.op != Op::open && m.op != Op::close) continue;
        emit(m.at);
        const auto i = static_cast<std::size_t>(m.span);
        const bool linky = m.span == Span::link || m.span == Span::image || m.span == Span::wikilink;
        if (m.op == Op::open) {
            ++depth[i];
            if (linky) links.push_back(m.ref);
        } else {
            if (depth[i] > 0) --depth[i];
            if (linky && !links.empty()) links.pop_back();
        }
    }
    emit(static_cast<std::uint32_t>(in.text.size()));
    return out;
}

bool operator==(const Mark& a, const Mark& b) noexcept {
    return a.at == b.at && a.op == b.op && a.span == b.span && a.ref == b.ref;
}
bool operator==(const Inline& a, const Inline& b) noexcept { return a.text == b.text && a.marks == b.marks; }
bool operator==(const Link& a, const Link& b) noexcept { return a.href == b.href && a.title == b.title && a.autolink == b.autolink; }
bool operator==(const Block& a, const Block& b) noexcept {
    return a.kind == b.kind && a.level == b.level && a.tight == b.tight && a.mark == b.mark && a.start == b.start && a.task == b.task &&
           a.task_offset == b.task_offset && a.align == b.align && a.header == b.header && a.fence == b.fence && a.info == b.info &&
           a.language == b.language && a.anchor == b.anchor && a.content == b.content && a.hash == b.hash && a.children == b.children;
}
bool operator==(const Document& a, const Document& b) noexcept {
    return a.extensions == b.extensions && a.complete == b.complete && a.links == b.links && a.root == b.root;
}
bool operator==(const Run& a, const Run& b) noexcept { return a.begin == b.begin && a.end == b.end && a.spans == b.spans && a.link == b.link; }

}  // namespace omui::md
