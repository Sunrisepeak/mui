// The document to HTML, byte for byte what md4c-html writes for the source:
// the same tags and newlines, the same escaping of text (& < > ") and of URLs
// (percent-encoding but for the characters md4c-html keeps), alt text without
// tags inside images, paragraphs of tight lists without <p>.
module openmui.markdown;

import std;

namespace omui::md {

namespace {

void escape(std::string& out, std::string_view s) {
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c; break;
        }
    }
}

bool url_safe(unsigned char c) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    return std::string_view("~-_.+!*(),%#@?=;:/,+$").find(static_cast<char>(c)) != std::string_view::npos;
}

void escape_url(std::string& out, std::string_view s) {
    constexpr std::string_view hex = "0123456789ABCDEF";
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (url_safe(c)) out += ch;
        else if (c == '&') out += "&amp;";
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
}

struct Writer {
    const Document& doc;
    std::string     out;

    void inline_content(const Inline& in);
    void block(const Block& b, bool tight);
};

std::string_view open_tag(Span s) {
    switch (s) {
        case Span::emphasis: return "<em>";
        case Span::strong: return "<strong>";
        case Span::code: return "<code>";
        case Span::strikethrough: return "<del>";
        case Span::underline: return "<u>";
        case Span::math: return "<x-equation>";
        case Span::display_math: return "<x-equation type=\"display\">";
        default: return {};
    }
}

std::string_view close_tag(Span s) {
    switch (s) {
        case Span::emphasis: return "</em>";
        case Span::strong: return "</strong>";
        case Span::code: return "</code>";
        case Span::strikethrough: return "</del>";
        case Span::underline: return "</u>";
        case Span::link: return "</a>";
        case Span::math:
        case Span::display_math: return "</x-equation>";
        case Span::wikilink: return "</x-wikilink>";
        default: return {};
    }
}

void Writer::inline_content(const Inline& in) {
    const std::string_view text = in.text;
    std::size_t pos = 0;
    int image = 0;  // inside an image: its alt attribute, no tags
    int html = 0;   // inside raw inline HTML: verbatim
    auto flush = [&](std::size_t to) {
        if (to <= pos) return;
        if (html) out += text.substr(pos, to - pos);
        else escape(out, text.substr(pos, to - pos));
        pos = to;
    };
    for (const Mark& m : in.marks) {
        flush(m.at);
        switch (m.op) {
            case Op::soft_break:
                out += image ? " " : "\n";
                pos = m.at + 1;
                break;
            case Op::hard_break:
                out += image ? " " : "<br>\n";
                pos = m.at + 1;
                break;
            case Op::open:
                if (m.span == Span::html) {
                    ++html;
                    break;
                }
                if (image) {
                    if (m.span == Span::image) ++image;
                    break;
                }
                if (m.span == Span::link) {
                    const Link& l = doc.links[m.ref];
                    out += "<a href=\"";
                    escape_url(out, l.href);
                    if (!l.title.empty()) {
                        out += "\" title=\"";
                        escape(out, l.title);
                    }
                    out += "\">";
                } else if (m.span == Span::image) {
                    ++image;
                    out += "<img src=\"";
                    escape_url(out, doc.links[m.ref].href);
                    out += "\" alt=\"";
                } else if (m.span == Span::wikilink) {
                    out += "<x-wikilink data-target=\"";
                    escape(out, doc.links[m.ref].href);
                    out += "\">";
                } else {
                    out += open_tag(m.span);
                }
                break;
            case Op::close:
                if (m.span == Span::html) {
                    --html;
                    break;
                }
                if (m.span == Span::image) {
                    if (--image > 0) break;
                    const Link& l = doc.links[m.ref];
                    if (!l.title.empty()) {
                        out += "\" title=\"";
                        escape(out, l.title);
                    }
                    out += "\">";
                    break;
                }
                if (!image) out += close_tag(m.span);
                break;
        }
    }
    flush(text.size());
}

void Writer::block(const Block& b, bool tight) {
    auto children = [&](bool t) {
        for (const Block& c : b.children) block(c, t);
    };
    switch (b.kind) {
        case Kind::document: children(false); break;
        case Kind::quote:
            out += "<blockquote>\n";
            children(false);
            out += "</blockquote>\n";
            break;
        case Kind::bullet_list:
            out += "<ul>\n";
            children(b.tight);
            out += "</ul>\n";
            break;
        case Kind::ordered_list:
            out += b.start == 1 ? std::string("<ol>\n") : std::format("<ol start=\"{}\">\n", b.start);
            children(b.tight);
            out += "</ol>\n";
            break;
        case Kind::item:
            if (b.task != Task::none) {
                out += "<li class=\"task-list-item\"><input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled";
                if (b.task == Task::done) out += " checked";
                out += ">";
            } else {
                out += "<li>";
            }
            // An item's own paragraphs are bare in a tight list; deeper ones are not.
            for (const Block& c : b.children) block(c, tight && c.kind == Kind::paragraph);
            out += "</li>\n";
            break;
        case Kind::rule: out += "<hr>\n"; break;
        case Kind::heading:
            out += std::format("<h{}>", b.level);
            inline_content(b.content);
            out += std::format("</h{}>\n", b.level);
            break;
        case Kind::code:
            out += "<pre><code";
            if (!b.language.empty()) {
                out += " class=\"";
                if (!b.language.starts_with("language-")) out += "language-";
                escape(out, b.language);
                out += "\"";
            }
            out += ">";
            escape(out, b.content.text);
            out += "</code></pre>\n";
            break;
        case Kind::html: out += b.content.text; break;
        case Kind::paragraph:
            if (tight) {
                inline_content(b.content);
            } else {
                out += "<p>";
                inline_content(b.content);
                out += "</p>\n";
            }
            break;
        case Kind::table:
            out += "<table>\n";
            children(false);
            out += "</table>\n";
            break;
        case Kind::table_head:
            out += "<thead>\n";
            children(false);
            out += "</thead>\n";
            break;
        case Kind::table_body:
            out += "<tbody>\n";
            children(false);
            out += "</tbody>\n";
            break;
        case Kind::table_row:
            out += "<tr>\n";
            children(false);
            out += "</tr>\n";
            break;
        case Kind::table_cell: {
            const std::string_view tag = b.header ? "th" : "td";
            out += std::format("<{}", tag);
            switch (b.align) {
                case Align::left: out += " align=\"left\">"; break;
                case Align::center: out += " align=\"center\">"; break;
                case Align::right: out += " align=\"right\">"; break;
                default: out += ">"; break;
            }
            inline_content(b.content);
            out += std::format("</{}>\n", tag);
            break;
        }
    }
}

}  // namespace

std::string to_html(const Document& doc) {
    Writer w{doc, {}};
    w.block(doc.root, false);
    return std::move(w.out);
}

}  // namespace omui::md
