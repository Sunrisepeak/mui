// Markdown written from the document: CommonMark with the document's extensions, such that parsing the text gives
// the document back — to_html(parse(to_markdown(doc), doc.extensions)) == to_html(doc), which the tests hold on
// md4c's suites. The writer chooses one spelling of each construct (ATX headings, fenced code, `*` emphasis, inline
// links) and escapes what would otherwise begin another.
module mlibs.markdown.api;

import std;

namespace mlibs::markdown {

namespace {

using Lines = std::vector<std::string>;

bool digit(char c) { return c >= '0' && c <= '9'; }
bool word(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || (static_cast<unsigned char>(c) & 0x80) != 0; }

struct Where {
    bool cell = false;     // in a table's cell: a `|` ends it
    bool heading = false;  // in an ATX heading: a closing run of `#` is not text
};

class MarkdownWriter {
public:
    explicit MarkdownWriter(const Document& doc)
        : doc_(doc),
          tables_(has(doc.extensions, Extension::tables)),
          strike_(has(doc.extensions, Extension::strikethrough)),
          math_(has(doc.extensions, Extension::math)),
          underline_(has(doc.extensions, Extension::underline)),
          urls_(has(doc.extensions, Extension::url_autolinks)),
          www_(has(doc.extensions, Extension::www_autolinks)),
          emails_(has(doc.extensions, Extension::email_autolinks)) {}

    [[nodiscard]] std::string document() {
        std::string out;
        for (const std::string& line : blocks(doc_.root.children, false)) {
            out += line;
            out += '\n';
        }
        return out;
    }

private:
    const Document& doc_;
    bool            tables_, strike_, math_, underline_, urls_, www_, emails_;

    // Text between marks, escaped: nothing in it begins a construct. `start`: it begins a line.
    void text(std::string& out, std::string_view piece, Where where, bool& start) const {
        for (std::size_t at = 0; at < piece.size(); ++at) {
            const char c = piece[at];
            const char next = at + 1 < piece.size() ? piece[at + 1] : '\0';
            if (start) {
                start = false;
                // A blank that begins a line was written as a reference (a paragraph's own are not kept): so again.
                if (c == ' ' || c == '\t') {
                    out += c == ' ' ? "&#32;" : "&#9;";
                    continue;
                }
                // What begins a block at the start of a line: a heading, a quote, a list item, a rule or a setext
                // underline, a fence; a number before `.` or `)` begins an ordered item.
                if (c == '#' || c == '>' || c == '+' || c == '-' || c == '=' || c == '~') {
                    out += '\\';
                    out += c;
                    continue;
                }
                if (digit(c)) {
                    std::size_t end = at;
                    while (end < piece.size() && digit(piece[end])) ++end;
                    if (end < piece.size() && (piece[end] == '.' || piece[end] == ')') && end - at <= 9) {
                        out += piece.substr(at, end - at);
                        out += '\\';
                        out += piece[end];
                        at = end;
                        continue;
                    }
                }
            }
            switch (c) {
                case '\\':
                case '`':
                case '*':
                case '_':
                case '[':
                case ']':
                case '<':
                case '&':
                    out += '\\';
                    out += c;
                    break;
                case '#':
                    if (where.heading) out += '\\';
                    out += c;
                    break;
                case '~':
                    if (strike_) out += '\\';
                    out += c;
                    break;
                case '$':
                    if (math_) out += '\\';
                    out += c;
                    break;
                case '|':
                    if (where.cell || tables_) out += '\\';
                    out += c;
                    break;
                case '@':
                    if (emails_) out += '\\';
                    out += c;
                    break;
                case ':':
                    // `scheme://`: a permissive autolink begins at its colon.
                    if (urls_ && next == '/') out += '\\';
                    out += c;
                    break;
                case '.':
                    // `www.`: a permissive autolink; without its first dot it is text.
                    if (www_ && at >= 3 && (piece.substr(at - 3, 3) == "www" || piece.substr(at - 3, 3) == "WWW")) out += '\\';
                    out += c;
                    break;
                case '\n': out += "&#10;"; break;  // a line feed no mark stands for: a reference made it
                default: out += c;
            }
        }
    }

    // A run of code: fenced by more backticks than it holds, padded where it begins or ends with one.
    static void code(std::string& out, std::string_view held) {
        std::size_t longest = 0, run = 0;
        for (const char c : held) {
            run = c == '`' ? run + 1 : 0;
            longest = std::max(longest, run);
        }
        const std::string fence(longest + 1, '`');
        const bool blank = std::ranges::all_of(held, [](char c) { return c == ' '; });
        const bool pad = !held.empty() && !blank && (held.front() == '`' || held.back() == '`' || (held.front() == ' ' && held.back() == ' '));
        out += fence;
        if (pad) out += ' ';
        for (const char c : held) out += c == '\n' ? ' ' : c;  // a bar inside is the code's: md4c reads the run first
        if (pad) out += ' ';
        out += fence;
    }

    static std::string destination(std::string_view href) {
        const bool pointed = href.empty() || std::ranges::any_of(href, [](char c) {
            const auto byte = static_cast<unsigned char>(c);
            return byte <= 0x20 || byte == 0x7f || c == '<' || c == '>';
        });
        std::string out;
        if (pointed) out += '<';
        for (const char c : href) {
            if (c == '\n') {
                out += ' ';
                continue;
            }
            if (c == '\\' || c == '&' || c == '`' || (pointed ? (c == '<' || c == '>') : (c == '(' || c == ')'))) out += '\\';
            out += c;
        }
        if (pointed) out += '>';
        return out;
    }

    static std::string title(std::string_view said) {
        if (said.empty()) return {};
        std::string out = " \"";
        for (const char c : said) {
            if (c == '\\' || c == '"' || c == '&' || c == '`' || c == '<') out += '\\';
            out += c;
        }
        out += '"';
        return out;
    }

    // A leaf's inline content; its lines are its soft and hard breaks.
    [[nodiscard]] std::string inline_content(const Inline& in, Where where, bool start = true) const {
        std::string out;
        const std::string_view all = in.text;
        std::size_t at = 0;
        int verbatim = 0;  // inside embedded markup or a formula: as written
        std::vector<std::string_view> closers;  // of the spans open, innermost last
        const auto flush = [&](std::size_t to) {
            to = std::min(to, all.size());
            if (to <= at) return;
            if (verbatim) out += all.substr(at, to - at);
            else text(out, all.substr(at, to - at), where, start);
            start = false;
            at = to;
        };
        for (std::size_t index = 0; index < in.marks.size(); ++index) {
            const Mark& mark = in.marks[index];
            flush(mark.at);
            switch (mark.op) {
                case Op::soft_break:
                    // A cell and an ATX heading are one line.
                    out += where.cell || where.heading ? ' ' : '\n';
                    at = mark.at + 1;
                    start = !(where.cell || where.heading);
                    break;
                case Op::hard_break:
                    out += where.cell || where.heading ? " " : "\\\n";
                    at = mark.at + 1;
                    start = !(where.cell || where.heading);
                    break;
                case Op::open:
                    start = false;
                    switch (mark.span) {
                        case Span::code: {
                            // To its close: what it holds is verbatim.
                            std::size_t close = index + 1;
                            while (close < in.marks.size() && !(in.marks[close].op == Op::close && in.marks[close].span == Span::code)) ++close;
                            const std::size_t end = close < in.marks.size() ? in.marks[close].at : all.size();
                            code(out, all.substr(mark.at, end - mark.at));
                            at = end;
                            index = close;
                            break;
                        }
                        case Span::html: ++verbatim; break;
                        case Span::math:
                            out += '$';
                            ++verbatim;
                            break;
                        case Span::display_math:
                            out += "$$";
                            ++verbatim;
                            break;
                        case Span::emphasis: {
                            // Beside a `*` already written (a strong one's) the underscore keeps the two apart.
                            const bool beside = !out.empty() && out.back() == '*';
                            out += beside && !underline_ ? '_' : '*';
                            closers.push_back(beside && !underline_ ? "_" : "*");
                            break;
                        }
                        case Span::strong:
                            out += "**";
                            closers.push_back("**");
                            break;
                        case Span::strikethrough:
                            out += "~~";
                            closers.push_back("~~");
                            break;
                        case Span::underline:
                            out += '_';
                            closers.push_back("_");
                            break;
                        case Span::link:
                            if (!out.empty() && out.back() == '!' && !verbatim) out.insert(out.size() - 1, 1, '\\');  // text, not an image's mark
                            out += '[';
                            break;
                        case Span::image: out += "!["; break;
                        case Span::wikilink:
                            out += "[[";
                            if (mark.ref < doc_.links.size())
                                for (const char c : doc_.links[mark.ref].href) {
                                    if (c == '|' || c == '\\' || c == ']') out += '\\';
                                    out += c;
                                }
                            out += '|';
                            break;
                    }
                    break;
                case Op::close:
                    switch (mark.span) {
                        case Span::code: break;
                        case Span::html: --verbatim; break;
                        case Span::math:
                            --verbatim;
                            out += '$';
                            break;
                        case Span::display_math:
                            --verbatim;
                            out += "$$";
                            break;
                        case Span::link:
                        case Span::image: {
                            const Link link = mark.ref < doc_.links.size() ? doc_.links[mark.ref] : Link{};
                            out += std::format("]({}{})", destination(link.href), title(link.title));
                            break;
                        }
                        case Span::wikilink: out += "]]"; break;
                        default:
                            if (!closers.empty()) {
                                out += closers.back();
                                closers.pop_back();
                            }
                    }
                    break;
            }
        }
        flush(all.size());
        return out;
    }

    static Lines split(std::string_view all) {
        Lines out;
        for (std::size_t at = 0; at <= all.size();) {
            const std::size_t end = std::min(all.find('\n', at), all.size());
            out.emplace_back(all.substr(at, end - at));
            at = end + 1;
        }
        return out;
    }

    // Sibling blocks: a blank line between two, but where a tight list's item holds them.
    [[nodiscard]] Lines blocks(std::span<const Block> list, bool tight) const {
        Lines out;
        char last_mark = 0;  // of the list just before: one beside it of its kind takes another, or the two are one
        Kind last_kind = Kind::document;
        bool first = true;
        for (const Block& block : list) {
            char mark = 0;
            if (block.kind == Kind::bullet_list || block.kind == Kind::ordered_list) {
                const bool ordered = block.kind == Kind::ordered_list;
                mark = block.mark ? block.mark : ordered ? '.' : '-';
                if (ordered ? (mark != '.' && mark != ')') : (mark != '-' && mark != '+' && mark != '*')) mark = ordered ? '.' : '-';
                if (last_kind == block.kind && last_mark == mark) mark = ordered ? (mark == '.' ? ')' : '.') : (mark == '-' ? '*' : '-');
            }
            Lines made = this->block(block, mark);
            if (!first && !tight) out.emplace_back();
            first = false;
            for (std::string& line : made) out.push_back(std::move(line));
            last_kind = block.kind;
            last_mark = mark;
        }
        return out;
    }

    [[nodiscard]] Lines block(const Block& b, char mark) const {
        switch (b.kind) {
            case Kind::document: return blocks(b.children, false);
            case Kind::quote: {
                Lines out = blocks(b.children, false);
                if (out.empty()) out.emplace_back();
                for (std::string& line : out) line = line.empty() ? ">" : "> " + line;
                return out;
            }
            case Kind::bullet_list:
            case Kind::ordered_list: {
                Lines out;
                std::uint32_t number = b.start;
                bool first = true;
                for (const Block& item : b.children) {
                    const std::string marker = b.kind == Kind::ordered_list ? std::format("{}{}", number++, mark) : std::string(1, mark);
                    Lines held = blocks(item.children, b.tight);
                    if (item.task != Task::none) {
                        const std::string_view box = item.task == Task::done ? "[x] " : "[ ] ";
                        if (held.empty()) held.emplace_back(box);
                        else held.front().insert(0, box);
                    }
                    if (!first && !b.tight) out.emplace_back();
                    first = false;
                    if (held.empty()) {
                        out.push_back(marker);
                        continue;
                    }
                    const std::string indent(marker.size() + 1, ' ');
                    for (std::size_t line = 0; line < held.size(); ++line) {
                        if (line == 0) out.push_back(held[line].empty() ? marker : marker + " " + held[line]);
                        else out.push_back(held[line].empty() ? std::string() : indent + held[line]);
                    }
                }
                return out;
            }
            case Kind::item: return blocks(b.children, false);  // an item outside a list: its blocks
            case Kind::rule: return {"___"};  // an underscore's is no bullet's and no setext underline's
            case Kind::heading: {
                // Lines in a heading: only one underlined has them (levels 1 and 2).
                const bool lines = std::ranges::any_of(b.content.marks, [](const Mark& m) { return m.op == Op::soft_break || m.op == Op::hard_break; });
                if (lines && b.level <= 2) {
                    Lines out = split(inline_content(b.content, {}));
                    out.emplace_back(b.level == 1 ? "===" : "---");
                    return out;
                }
                const std::string held = inline_content(b.content, {.heading = true}, false);
                return {std::string(std::clamp<std::size_t>(b.level, 1, 6), '#') + (held.empty() ? "" : " " + held)};
            }
            case Kind::code: {
                std::string_view held = b.content.text;
                const std::string_view info = !b.info.empty() ? std::string_view(b.info) : std::string_view(b.language);
                const char fence = info.contains('`') || b.fence == '~' ? '~' : '`';
                std::size_t longest = 0, run = 0;
                for (const char c : held) {
                    run = c == fence ? run + 1 : 0;
                    longest = std::max(longest, run);
                }
                const std::string bar(std::max<std::size_t>(3, longest + 1), fence);
                Lines out;
                std::string opening = bar;
                for (const char c : info) {
                    if (c == '\\' || c == '&') opening += '\\';
                    opening += c;
                }
                out.push_back(std::move(opening));
                if (held.ends_with('\n')) held.remove_suffix(1);
                if (!b.content.text.empty())
                    for (std::string& line : split(held)) out.push_back(std::move(line));
                out.push_back(bar);
                return out;
            }
            case Kind::html: {
                std::string_view held = b.content.text;
                while (held.ends_with('\n')) held.remove_suffix(1);
                return split(held);
            }
            case Kind::paragraph: return split(inline_content(b.content, {}));
            case Kind::table: {
                Lines out;
                const auto row = [&](const Block& cells) {
                    std::string line = "|";
                    for (const Block& cell : cells.children) line += std::format(" {} |", inline_content(cell.content, {.cell = true}, false));
                    return line;
                };
                for (const Block& section : b.children)
                    for (const Block& cells : section.children) {
                        out.push_back(row(cells));
                        if (section.kind != Kind::table_head) continue;
                        std::string rule = "|";
                        for (const Block& cell : cells.children)
                            rule += cell.align == Align::left ? " :-- |" : cell.align == Align::center ? " :-: |" : cell.align == Align::right ? " --: |" : " --- |";
                        out.push_back(std::move(rule));
                    }
                return out;
            }
            default: return blocks(b.children, false);
        }
    }
};

}  // namespace

std::string to_markdown(const Document& doc) { return MarkdownWriter(doc).document(); }

}  // namespace mlibs::markdown
