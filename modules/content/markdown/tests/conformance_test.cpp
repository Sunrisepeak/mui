// Conformance (implementation plan §19.2 M2 ②) and fidelity of the document.
//
// Every example of md4c 0.5.3's suites (tests/data: the CommonMark 0.31.2
// spec, GitHub's extensions, md4c's regressions and coverage cases):
//
//   fidelity     to_html(parse(md)) is byte for byte md_html(md) — the
//                document keeps everything the HTML needs, for every example;
//   conformance  the HTML matches the suite's expected HTML after the
//                suites' own normalisation (test/normalize.py, ported below):
//                ≥ 95 % of the CommonMark spec, extensions counted apart.
import std;
import openmui.markdown;
import openmui.testing;
import mcpplibs.mwrap.md4c;

using namespace omui::testing;
namespace md = omui::md;
namespace mm = mcpplibs::mwrap::md4c::macros;

namespace {

std::filesystem::path data_dir() { return std::filesystem::path(std::source_location::current().file_name()).parent_path() / "data"; }

struct Example {
    std::string markdown, html, options;
    bool        no_normalize = false;
    int         number = 0;
    int         line = 0;
};

// test/run-testsuite.py's reader.
std::vector<Example> examples(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<Example> out;
    Example cur;
    int state = 0, line_no = 0, number = 0;
    const std::string fence(32, '`');
    auto tabs = [](std::string s) {
        for (std::size_t p; (p = s.find("\xE2\x86\x92")) != std::string::npos;) s.replace(p, 3, "\t");
        return s;
    };
    for (std::string raw; std::getline(in, raw);) {
        ++line_no;
        const std::string line = raw + "\n";
        std::string_view l = raw;
        while (!l.empty() && (l.front() == ' ' || l.front() == '\t' || l.front() == '\r')) l.remove_prefix(1);
        while (!l.empty() && (l.back() == ' ' || l.back() == '\t' || l.back() == '\r')) l.remove_suffix(1);
        if (l.starts_with(fence + " example")) {
            state = 1;
            cur = {};
            cur.no_normalize = l.find("[no-normalize]") != std::string_view::npos;
            cur.line = line_no;
        } else if (state >= 2 && l == fence) {
            state = 0;
            cur.number = ++number;
            cur.markdown = tabs(cur.markdown);
            cur.html = tabs(cur.html);
            out.push_back(cur);
        } else if (l == ".") {
            ++state;
        } else if (state == 1) {
            cur.markdown += line;
        } else if (state == 2) {
            cur.html += line;
        } else if (state == 3) {
            cur.options += line;
        }
    }
    return out;
}

// The suites' command-line options: this package's extensions, and md4c's
// flags for the oracle (mapped here independently of the library).
struct Dialect {
    md::Extension ext = md::commonmark;
    unsigned      flags = 0;
    bool          known = true;
};
Dialect dialect(std::string_view options) {
    Dialect d;
    struct Opt {
        std::string_view name;
        md::Extension    ext;
        unsigned         flags;
    };
    const std::array<Opt, 14> table{{
        {"--ftables", md::Extension::tables, mm::MD_FLAG_TABLES},
        {"--fstrikethrough", md::Extension::strikethrough, mm::MD_FLAG_STRIKETHROUGH},
        {"--ftasklists", md::Extension::tasklists, mm::MD_FLAG_TASKLISTS},
        {"--fpermissive-autolinks", md::autolinks, mm::MD_FLAG_PERMISSIVEAUTOLINKS},
        {"--fpermissive-url-autolinks", md::Extension::url_autolinks, mm::MD_FLAG_PERMISSIVEURLAUTOLINKS},
        {"--fpermissive-www-autolinks", md::Extension::www_autolinks, mm::MD_FLAG_PERMISSIVEWWWAUTOLINKS},
        {"--fpermissive-email-autolinks", md::Extension::email_autolinks, mm::MD_FLAG_PERMISSIVEEMAILAUTOLINKS},
        {"--funderline", md::Extension::underline, mm::MD_FLAG_UNDERLINE},
        {"--flatex-math", md::Extension::math, mm::MD_FLAG_LATEXMATHSPANS},
        {"--fwiki-links", md::Extension::wikilinks, mm::MD_FLAG_WIKILINKS},
        {"--fhard-soft-breaks", md::Extension::hard_breaks, mm::MD_FLAG_HARD_SOFT_BREAKS},
        {"--fcollapse-whitespace", md::Extension::collapse_whitespace, mm::MD_FLAG_COLLAPSEWHITESPACE},
        {"--fpermissive-atx-headers", md::Extension::permissive_atx_headers, mm::MD_FLAG_PERMISSIVEATXHEADERS},
        {"--fno-indented-code", md::Extension::no_indented_code, mm::MD_FLAG_NOINDENTEDCODEBLOCKS},
    }};
    std::size_t i = 0;
    while (i < options.size()) {
        while (i < options.size() && std::isspace(static_cast<unsigned char>(options[i]))) ++i;
        std::size_t j = i;
        while (j < options.size() && !std::isspace(static_cast<unsigned char>(options[j]))) ++j;
        if (j > i) {
            const std::string_view o = options.substr(i, j - i);
            auto it = std::ranges::find(table, o, &Opt::name);
            if (it == table.end()) d.known = false;
            else {
                d.ext = d.ext | it->ext;
                d.flags |= it->flags;
            }
        }
        i = j;
    }
    return d;
}

std::string oracle(std::string_view source, unsigned flags) {
    std::string out;
    (void)md_html(
        source.data(), static_cast<MD_SIZE>(source.size()),
        [](const MD_CHAR* s, MD_SIZE n, void* user) { static_cast<std::string*>(user)->append(s, n); }, &out, flags, 0);
    return out;
}

// ── test/normalize.py, ported ──

bool block_tag(std::string_view t) {
    static constexpr std::array<std::string_view, 50> tags{
        "article", "header", "aside",   "hgroup",   "blockquote", "hr",     "iframe",   "body",       "li",     "map",
        "button",  "object", "canvas",  "ol",       "caption",    "output", "col",      "p",          "colgroup", "pre",
        "dd",      "progress", "div",   "section",  "dl",         "table",  "td",       "dt",         "tbody",  "embed",
        "textarea", "fieldset", "tfoot", "figcaption", "th",      "figure", "thead",    "footer",     "tr",     "form",
        "ul",      "h1",     "h2",      "h3",       "h4",         "h5",     "h6",       "video",      "script", "style"};
    return !t.empty() && std::ranges::find(tags, t) != tags.end();
}

// Python's whitespace (str.isspace, re's \s on str): ASCII and Unicode
// spaces, read from UTF-8.
bool py_space(char32_t c) {
    return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}
bool space(char c) { return py_space(static_cast<unsigned char>(c)) && static_cast<unsigned char>(c) < 0x80; }

// The code point at s[i] and its length in bytes (invalid bytes: one each).
std::pair<char32_t, std::size_t> decode(std::string_view s, std::size_t i) {
    const auto b = static_cast<unsigned char>(s[i]);
    const std::size_t n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
    if (n == 1 || i + n > s.size()) return {b, 1};
    char32_t c = b & (0x7F >> n);
    for (std::size_t k = 1; k < n; ++k) c = c << 6 | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    return {c, n};
}

std::string collapse(std::string_view s) {
    std::string out;
    bool in_space = false;
    for (std::size_t i = 0; i < s.size();) {
        const auto [c, n] = decode(s, i);
        if (py_space(c)) {
            if (!in_space) out += ' ';
            in_space = true;
        } else {
            out += s.substr(i, n);
            in_space = false;
        }
        i += n;
    }
    return out;
}
std::string_view lstrip(std::string_view s, bool newlines_only = false) {
    while (!s.empty()) {
        if (newlines_only) {
            if (s.front() != '\n') break;
            s.remove_prefix(1);
            continue;
        }
        const auto [c, n] = decode(s, 0);
        if (!py_space(c)) break;
        s.remove_prefix(n);
    }
    return s;
}
std::string_view rstrip(std::string_view s) {
    while (!s.empty()) {
        std::size_t start = s.size() - 1;  // the last code point's first byte
        while (start > 0 && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) --start;
        if (!py_space(decode(s, start).first)) break;
        s.remove_suffix(s.size() - start);
    }
    return s;
}
std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
std::string escape_attr(std::string_view s) {  // html.escape(quote=True)
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#x27;"; break;
            default: out += c;
        }
    }
    return out;
}

// A reference as normalize.py outputs it: the character, but & < > " as
// entities, and the reference itself when it names nothing.
std::string reference(std::string_view ref) {
    const std::string decoded = [&] {
        std::string html = oracle(ref, 0);  // md4c-html's decoding, one source of truth with the library
        std::string_view v = html;
        if (v.starts_with("<p>")) v.remove_prefix(3);
        if (v.ends_with("</p>\n")) v.remove_suffix(5);
        return std::string(v);
    }();
    if (decoded == "&amp;" || decoded == "&lt;" || decoded == "&gt;" || decoded == "&quot;") return decoded;
    if (decoded.find('&') != std::string::npos) return std::string(ref);  // unknown name: as written
    return decoded;
}

std::string unescape_attr(std::string_view v) {
    std::string out;
    for (std::size_t i = 0; i < v.size();) {
        if (v[i] == '&') {
            const std::size_t semi = v.find(';', i);
            if (semi != std::string_view::npos && semi - i < 40) {
                const std::string r = reference(v.substr(i, semi - i + 1));
                if (r == "&amp;") out += '&';
                else if (r == "&lt;") out += '<';
                else if (r == "&gt;") out += '>';
                else if (r == "&quot;") out += '"';
                else out += r;
                i = semi + 1;
                continue;
            }
        }
        out += v[i++];
    }
    return out;
}

class Normalizer {
public:
    std::string output;

    void feed_data(std::string_view data) {
        // Split at character and entity references, as HTMLParser does.
        std::size_t i = 0;
        while (i < data.size()) {
            const std::size_t amp = data.find('&', i);
            if (amp == std::string_view::npos) {
                data_(data.substr(i));
                return;
            }
            if (amp > i) data_(data.substr(i, amp - i));
            std::size_t j = amp + 1;
            const bool numeric = j < data.size() && data[j] == '#';
            if (numeric) ++j;
            if (numeric && j < data.size() && (data[j] == 'x' || data[j] == 'X')) ++j;
            const std::size_t name = j;
            while (j < data.size() && std::isalnum(static_cast<unsigned char>(data[j]))) ++j;
            if (j > name && j < data.size() && data[j] == ';') {
                output += reference(data.substr(amp, j - amp + 1));
                last_ = "ref";
                i = j + 1;
            } else {
                data_(data.substr(amp, 1));
                i = amp + 1;
            }
        }
    }

    void feed_tag(std::string_view tag) {
        if (tag.starts_with("<!--")) {
            output += tag;
            last_ = "comment";
            return;
        }
        if (tag.starts_with("<!")) {
            output += tag;
            last_ = "decl";
            return;
        }
        if (tag.starts_with("<?")) {
            output += tag;
            last_ = "pi";
            return;
        }
        std::string_view body = tag.substr(1, tag.size() - 2);
        const bool end = body.starts_with('/');
        if (end) body.remove_prefix(1);
        std::size_t n = 0;
        while (n < body.size() && (std::isalnum(static_cast<unsigned char>(body[n])) || body[n] == '-' || body[n] == ':')) ++n;
        if (n == 0 || !std::isalpha(static_cast<unsigned char>(body[0]))) {
            data_(tag);
            return;
        }
        const std::string name = lower(body.substr(0, n));
        if (end) {
            if (name == "pre") in_pre_ = false;
            else if (block_tag(name)) output = std::string(rstrip(output));
            output += "</" + name + ">";
            last_tag_ = name;
            last_ = "endtag";
            return;
        }
        std::string_view rest = body.substr(n);
        const bool self_closing = rstrip(rest).ends_with('/');
        if (self_closing) {
            rest = rstrip(rest);
            rest.remove_suffix(1);
        }
        std::vector<std::pair<std::string, std::optional<std::string>>> attrs;
        std::size_t i = 0;
        while (i < rest.size()) {
            while (i < rest.size() && space(rest[i])) ++i;
            std::size_t k = i;
            while (k < rest.size() && !space(rest[k]) && rest[k] != '=') ++k;
            if (k == i) {
                ++i;
                continue;
            }
            std::string key = lower(rest.substr(i, k - i));
            i = k;
            while (i < rest.size() && space(rest[i])) ++i;
            std::optional<std::string> value;
            if (i < rest.size() && rest[i] == '=') {
                ++i;
                while (i < rest.size() && space(rest[i])) ++i;
                if (i < rest.size() && (rest[i] == '"' || rest[i] == '\'')) {
                    const char q = rest[i];
                    const std::size_t close = rest.find(q, i + 1);
                    const std::size_t e = close == std::string_view::npos ? rest.size() : close;
                    value = unescape_attr(rest.substr(i + 1, e - i - 1));
                    i = e + 1;
                } else {
                    std::size_t e = i;
                    while (e < rest.size() && !space(rest[e])) ++e;
                    value = unescape_attr(rest.substr(i, e - i));
                    i = e;
                }
            }
            attrs.emplace_back(std::move(key), std::move(value));
        }
        std::ranges::sort(attrs);
        if (name == "pre") in_pre_ = true;
        if (block_tag(name)) output = std::string(rstrip(output));
        output += "<" + name;
        for (const auto& [k, v] : attrs) {
            output += " " + k;
            if (v) output += "=\"" + escape_attr(*v) + "\"";
        }
        output += ">";
        last_tag_ = name;
        last_ = self_closing ? "endtag" : "starttag";
    }

private:
    void data_(std::string_view data) {
        const bool after_tag = last_ == "endtag" || last_ == "starttag";
        const bool after_block = after_tag && block_tag(last_tag_);
        std::string d(data);
        if (after_tag && last_tag_ == "br") d = std::string(lstrip(d, true));
        if (!in_pre_) d = collapse(d);
        if (after_block && !in_pre_) {
            if (last_ == "starttag") d = std::string(lstrip(d));
            else if (last_ == "endtag") d = std::string(rstrip(lstrip(d)));
        }
        output += d;
        last_ = "data";
    }

    std::string last_ = "starttag", last_tag_;
    bool        in_pre_ = false;
};

std::string normalize(std::string_view html) {
    Normalizer n;
    std::size_t i = 0;
    while (i < html.size()) {
        if (html.substr(i).starts_with("<![CDATA[")) {
            const std::size_t e = html.find("]]>", i);
            const std::size_t end = e == std::string_view::npos ? html.size() : e + 3;
            n.output += html.substr(i, end - i);
            i = end;
        } else if (html[i] == '<') {
            const std::size_t e = html.find('>', i);
            if (e == std::string_view::npos) {
                n.feed_data(html.substr(i));
                break;
            }
            n.feed_tag(html.substr(i, e - i + 1));
            i = e + 1;
        } else {
            const std::size_t e = html.find('<', i);
            const std::size_t end = e == std::string_view::npos ? html.size() : e;
            n.feed_data(html.substr(i, end - i));
            i = end;
        }
    }
    return n.output;
}

struct Tally {
    int examples = 0, identical = 0, conforming = 0;
};

Tally run_suite(const std::filesystem::path& file, bool show_failures) {
    Tally t;
    for (const Example& e : examples(file)) {
        const Dialect d = dialect(e.options);
        check(d.known, std::format("{} example {}: an option this test does not map: {}", file.filename().string(), e.number, e.options));
        ++t.examples;
        const std::string mine = md::to_html(md::parse(e.markdown, d.ext));
        const std::string theirs = oracle(e.markdown, d.flags);
        if (mine == theirs) ++t.identical;
        else if (show_failures)
            fail(std::format("{} example {} (line {}): the document's HTML differs from md4c-html\n--- md\n{}--- md4c-html\n{}--- to_html\n{}",
                             file.filename().string(), e.number, e.line, e.markdown, theirs, mine));
        const bool ok = e.no_normalize ? mine == e.html : normalize(mine) == normalize(e.html);
        if (ok) ++t.conforming;
    }
    return t;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"markdown.conformance"};

    s.test("the normaliser agrees with normalize.py's documented cases", [] {
        check_eq(normalize("<p>a  \t b</p>"), std::string("<p>a b</p>"));
        check_eq(normalize("\n\t<p>\n\t\ta  b\t\t</p>\n\t"), std::string("<p>a b</p>"));
        check_eq(normalize("<i>a  b</i> "), std::string("<i>a b</i> "));
        check_eq(normalize("<br />"), std::string("<br>"));
        check_eq(normalize("<a title=\"bar\" HREF=\"foo\">x</a>"), std::string("<a href=\"foo\" title=\"bar\">x</a>"));
        check_eq(normalize("&forall;&amp;&gt;&lt;&quot;"), std::string("\xE2\x88\x80&amp;&gt;&lt;&quot;"));
    });

    s.test("CommonMark 0.31.2: every example, HTML from the document identical to md4c-html; ≥ 95 % conform", [] {
        const Tally t = run_suite(data_dir() / "spec.txt", true);
        std::println("    spec.txt: {} examples, {} identical to md4c-html, {} conform ({:.1f} %)", t.examples, t.identical, t.conforming,
                     100.0 * t.conforming / std::max(1, t.examples));
        check_eq(t.examples, 652);
        check_eq(t.identical, t.examples);
        check(t.conforming * 100 >= t.examples * 95, "the CommonMark pass rate is at least 95 %");
    });

    s.test("GitHub's and md4c's extensions, regressions and coverage cases", [] {
        int examples = 0, identical = 0, conforming = 0;
        for (const auto& entry : std::filesystem::directory_iterator(data_dir())) {
            const auto name = entry.path().filename().string();
            if (entry.path().extension() != ".txt" || name == "spec.txt") continue;
            const Tally t = run_suite(entry.path(), true);
            std::println("    {}: {} examples, {} identical, {} conform", name, t.examples, t.identical, t.conforming);
            examples += t.examples;
            identical += t.identical;
            conforming += t.conforming;
        }
        std::println("    extensions: {} examples, {} identical, {} conform ({:.1f} %)", examples, identical, conforming,
                     100.0 * conforming / std::max(1, examples));
        check_eq(examples, 156);
        check_eq(identical, examples);
        check(conforming * 100 >= examples * 95);
    });

    return s.run(argc, argv);
}
