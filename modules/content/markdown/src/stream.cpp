// Streaming append: keep the blocks before the last stable boundary, re-parse
// only the rest.
//
// A boundary is the start of a line that follows a blank line where nothing is
// open: no fenced code or raw HTML block, no list or quote. Blocks on either
// side of such a point are parsed independently (CommonMark has one exception,
// link reference definitions, which make the stream re-parse whole).
//
// A cheap line scanner proposes boundaries: a line after a blank line that
// starts in column 0 with a character that cannot continue a list (not a
// space, a tab, a bullet or a digit), outside what it believes to be fenced
// code or a raw HTML block. md4c then decides: the segment up to the boundary
// is parsed followed by a sentinel line, and the boundary holds only if the
// sentinel comes back as a top-level paragraph of its own — had a fence, an
// HTML block or a list still been open, the sentinel would be inside it. The
// scanner may be wrong in either direction; the result never is.
module openmui.markdown;

import std;
import openmui.trace;

namespace omui::md {

namespace {

constexpr std::string_view sentinel = "omui\x01stream\x01sentinel";

bool blank(std::string_view line) {
    return std::ranges::all_of(line, [](char c) { return c == ' ' || c == '\t'; });
}

// The first character of a line that can only start a new top-level block
// after a blank line (a space, a tab, a bullet or a digit could continue a
// list item).
bool starts_new(std::string_view line) {
    if (line.empty()) return false;
    const auto c = static_cast<unsigned char>(line.front());
    return c > ' ' && c != '-' && c != '+' && c != '*' && !(c >= '0' && c <= '9');
}

// Skips container prefixes (up to three spaces, then '>' or a list marker),
// so a fence or an HTML block opened inside a quote or an item is seen too.
std::string_view strip_containers(std::string_view line) {
    for (;;) {
        std::size_t i = 0;
        while (i < line.size() && i < 3 && line[i] == ' ') ++i;
        std::string_view rest = line.substr(i);
        if (rest.starts_with('>')) {
            rest.remove_prefix(1);
            if (rest.starts_with(' ')) rest.remove_prefix(1);
            line = rest;
            continue;
        }
        if (rest.size() >= 2 && (rest[0] == '-' || rest[0] == '+' || rest[0] == '*') && (rest[1] == ' ' || rest[1] == '\t')) {
            line = rest.substr(2);
            continue;
        }
        std::size_t d = 0;
        while (d < rest.size() && d < 9 && rest[d] >= '0' && rest[d] <= '9') ++d;
        if (d > 0 && d + 1 < rest.size() && (rest[d] == '.' || rest[d] == ')') && (rest[d + 1] == ' ' || rest[d + 1] == '\t')) {
            line = rest.substr(d + 2);
            continue;
        }
        return line;
    }
}

std::size_t indent_of(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return i;
}

bool iequals_prefix(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
    return true;
}

bool icontains(std::string_view s, std::string_view needle) {
    for (std::size_t i = 0; i + needle.size() <= s.size(); ++i)
        if (iequals_prefix(s.substr(i), needle)) return true;
    return false;
}

// CommonMark's raw HTML block kinds 1–5, the ones a blank line does not end.
int html_start(std::string_view s) {
    if (!s.starts_with('<')) return 0;
    for (std::string_view tag : {"<script", "<pre", "<style", "<textarea"}) {
        if (iequals_prefix(s, tag)) {
            const std::string_view after = s.substr(tag.size());
            if (after.empty() || after[0] == ' ' || after[0] == '\t' || after[0] == '>') return 1;
        }
    }
    if (s.starts_with("<!--")) return 2;
    if (s.starts_with("<?")) return 3;
    if (s.starts_with("<![CDATA[")) return 5;
    if (s.size() > 2 && s[1] == '!' && std::isalpha(static_cast<unsigned char>(s[2]))) return 4;
    return 0;
}

bool html_ends(int kind, std::string_view line) {
    switch (kind) {
        case 1: return icontains(line, "</script>") || icontains(line, "</pre>") || icontains(line, "</style>") || icontains(line, "</textarea>");
        case 2: return line.find("-->") != std::string_view::npos;
        case 3: return line.find("?>") != std::string_view::npos;
        case 4: return line.find('>') != std::string_view::npos;
        case 5: return line.find("]]>") != std::string_view::npos;
        default: return true;
    }
}

void shift_refs(Block& b, std::uint32_t by) {
    for (Mark& m : b.content.marks)
        if (m.ref != no_ref) m.ref += by;
    for (Block& c : b.children) shift_refs(c, by);
}

}  // namespace

Stream::Stream(Extension extensions) : ext_(extensions) { clear(); }

void Stream::clear() {
    source_.clear();
    doc_ = Document{};
    doc_.extensions = ext_;
    doc_.root.hash = hash_of(doc_.root, doc_.links);
    frozen_hash_ = hash_fields(doc_.root, doc_.links);
    frozen_ = frozen_blocks_ = frozen_links_ = last_parsed_ = 0;
    frozen_anchors_.clear();
    whole_ = false;
    scanned_ = 0;
    blank_before_ = true;
    fence_char_ = 0;
    fence_length_ = 0;
    html_kind_ = 0;
    candidates_.clear();
    ++revision_;
}

void Stream::assign(std::string_view source) {
    clear();
    append(source);
}

void Stream::append(std::string_view chunk) {
    trace::Span span("markdown.append", "markdown");
    ++revision_;
    last_parsed_ = 0;
    const std::size_t old = source_.size();
    source_ += chunk;
    if (!whole_ && std::string_view(source_).substr(old ? old - 1 : 0).find("]:") != std::string_view::npos) {
        // Possibly a link reference definition: it may define a link used
        // before it, so nothing stays frozen any more.
        whole_ = true;
        frozen_ = frozen_blocks_ = frozen_links_ = 0;
        frozen_anchors_.clear();
        frozen_hash_ = hash_fields(doc_.root, {});
        candidates_.clear();
    }
    if (!whole_) {
        scan();
        // The newest boundary first; a refused one is not tried again.
        for (int attempts = 0; attempts < 2 && !candidates_.empty(); ++attempts) {
            const std::size_t b = candidates_.back();
            candidates_.pop_back();
            if (freeze(b)) {
                candidates_.clear();
                break;
            }
        }
    }
    reparse_tail();
}

void Stream::scan() {
    const std::string_view s = source_;
    while (scanned_ < s.size()) {
        const std::size_t eol = s.find_first_of("\r\n", scanned_);
        if (eol == std::string_view::npos) break;           // the last line is not complete
        if (s[eol] == '\r' && eol + 1 == s.size()) break;   // a '\r' that may be half of "\r\n"
        const std::size_t next = eol + (s[eol] == '\r' && s[eol + 1] == '\n' ? 2 : 1);
        const std::string_view line = s.substr(scanned_, eol - scanned_);
        const bool is_blank = blank(line);
        if (blank_before_ && !is_blank && fence_char_ == 0 && html_kind_ == 0 && scanned_ > frozen_ && starts_new(line))
            candidates_.push_back(scanned_);
        const std::string_view inner = strip_containers(line);
        const std::size_t ind = indent_of(inner);
        const std::string_view body = inner.substr(ind);
        if (fence_char_) {
            std::size_t n = 0;
            while (n < body.size() && body[n] == fence_char_) ++n;
            if (ind <= 3 && n >= fence_length_ && blank(body.substr(n))) fence_char_ = 0;
        } else if (html_kind_) {
            if (html_ends(html_kind_, line)) html_kind_ = 0;
        } else if (ind <= 3) {
            std::size_t n = 0;
            const char c = body.empty() ? 0 : body[0];
            while (n < body.size() && body[n] == c) ++n;
            if ((c == '`' || c == '~') && n >= 3 && !(c == '`' && body.substr(n).find('`') != std::string_view::npos)) {
                fence_char_ = c;
                fence_length_ = n;
            } else if (const int k = html_start(body); k && !html_ends(k, body.substr(1))) {
                html_kind_ = k;
            }
        }
        blank_before_ = is_blank;
        scanned_ = next;
    }
}

bool Stream::freeze(std::size_t boundary) {
    std::string segment = source_.substr(frozen_, boundary - frozen_);
    segment += sentinel;
    segment += '\n';
    Document part;
    part.extensions = ext_;
    const bool ok = parse_into(part, segment, frozen_);
    last_parsed_ += segment.size();
    auto& blocks = part.root.children;
    if (!ok || blocks.empty() || blocks.back().kind != Kind::paragraph || blocks.back().content.text != sentinel ||
        !blocks.back().content.marks.empty())
        return false;
    blocks.pop_back();
    doc_.root.children.resize(frozen_blocks_);
    doc_.links.resize(frozen_links_);
    const Anchors none;
    for (Block& b : blocks) {
        shift_refs(b, static_cast<std::uint32_t>(frozen_links_));
        assign_anchors(b, none, frozen_anchors_);
        frozen_hash_ = hash_child(frozen_hash_, b.hash);
        doc_.root.children.push_back(std::move(b));
    }
    frozen_blocks_ = doc_.root.children.size();
    std::ranges::move(part.links, std::back_inserter(doc_.links));
    frozen_links_ = doc_.links.size();
    frozen_ = boundary;
    return true;
}

void Stream::reparse_tail() {
    doc_.root.children.resize(frozen_blocks_);
    doc_.links.resize(frozen_links_);
    Document tail;
    tail.extensions = ext_;
    const std::string_view rest = std::string_view(source_).substr(frozen_);
    doc_.complete = parse_into(tail, rest, frozen_);
    last_parsed_ += rest.size();
    Anchors used;  // the tail's, on top of the frozen counts
    std::uint64_t h = frozen_hash_;
    for (Block& b : tail.root.children) {
        shift_refs(b, static_cast<std::uint32_t>(frozen_links_));
        assign_anchors(b, frozen_anchors_, used);
        h = hash_child(h, b.hash);
        doc_.root.children.push_back(std::move(b));
    }
    std::ranges::move(tail.links, std::back_inserter(doc_.links));
    doc_.root.hash = h;
}

}  // namespace omui::md
