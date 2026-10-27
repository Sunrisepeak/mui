// openmui.markdown — Markdown to a document of blocks and inline marks.
//
// CommonMark 0.31 with GitHub's extensions, parsed by md4c
// (mcpplibs.mwrap.md4c). The document is what a renderer, an accessibility
// tree and an agent need: blocks nested as in the source (quotes, lists,
// items, tables), each leaf's text with entities decoded, and its inline
// structure as ordered marks over that text, so emphasis inside a link inside
// strong text keeps its nesting. `to_html` writes, from the document alone,
// the HTML md4c-html writes from the source: the tests hold the two equal on
// every upstream example, which is the proof that parsing lost nothing.
//
// `Stream` is for text that arrives in pieces (an LLM's answer): appending
// re-parses only what follows the last stable block boundary; the blocks
// before it are kept, with their hashes, so a renderer re-lays out only the
// tail.
//
//     auto doc = omui::md::parse("# Title\n\nSome *text*.\n");
//     doc.root.children[0].kind == Kind::heading;  // .anchor == "title"
//
// Depends on the standard library, md4c and openmui.trace only (D-55); parse
// and append are traced as spans of category "markdown".
export module openmui.markdown;

import std;

export namespace omui::md {

// ── extensions ──

enum class Extension : std::uint32_t {
    none                   = 0,
    tables                 = 1u << 0,   // GFM tables
    strikethrough          = 1u << 1,   // ~~gone~~
    tasklists              = 1u << 2,   // - [x] item
    url_autolinks          = 1u << 3,   // https://example.com without <>
    www_autolinks          = 1u << 4,   // www.example.com
    email_autolinks        = 1u << 5,   // a@b.example without <> and mailto:
    underline              = 1u << 6,   // _underline_ (then `_` no longer emphasises)
    math                   = 1u << 7,   // $inline$ and $$display$$
    wikilinks              = 1u << 8,   // [[target|label]]
    hard_breaks            = 1u << 9,   // every line break is a hard break
    no_html_blocks         = 1u << 10,  // raw HTML blocks are text
    no_html_spans          = 1u << 11,  // raw inline HTML is text
    no_indented_code       = 1u << 12,  // only fenced code blocks
    permissive_atx_headers = 1u << 13,  // ###heading without the space
    collapse_whitespace    = 1u << 14,  // runs of whitespace in text become one space
};
[[nodiscard]] constexpr Extension operator|(Extension a, Extension b) noexcept {
    return static_cast<Extension>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr Extension operator&(Extension a, Extension b) noexcept {
    return static_cast<Extension>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr bool has(Extension set, Extension e) noexcept { return (set & e) == e && e != Extension::none; }

inline constexpr Extension commonmark = Extension::none;
inline constexpr Extension autolinks = Extension::url_autolinks | Extension::www_autolinks | Extension::email_autolinks;
inline constexpr Extension no_html = Extension::no_html_blocks | Extension::no_html_spans;
inline constexpr Extension gfm = Extension::tables | Extension::strikethrough | Extension::tasklists | autolinks;

// "commonmark", "gfm", "autolinks", "no_html", or one extension's name
// ("tables", "math" …).
[[nodiscard]] std::optional<Extension> extension(std::string_view name) noexcept;
[[nodiscard]] Extension extensions(std::span<const std::string> names) noexcept;  // unknown names are ignored

// ── the document ──

enum class Kind : std::uint8_t {
    document,
    quote,
    bullet_list,
    ordered_list,
    item,
    rule,
    heading,
    code,       // fenced or indented; `content.text` is verbatim
    html,       // a raw HTML block; `content.text` is verbatim
    paragraph,
    table,
    table_head,
    table_body,
    table_row,
    table_cell,
};
[[nodiscard]] std::string_view name(Kind k) noexcept;

enum class Align : std::uint8_t { none, left, center, right };
enum class Task : std::uint8_t { none, open, done };

// What an inline mark opens or closes.
enum class Span : std::uint8_t {
    emphasis,
    strong,
    code,
    strikethrough,
    underline,
    link,
    image,         // the marks inside are its text alternative
    html,          // raw inline HTML: the text inside is verbatim
    math,
    display_math,
    wikilink,
};
inline constexpr std::size_t span_count = 11;
[[nodiscard]] constexpr std::uint16_t bit(Span s) noexcept { return static_cast<std::uint16_t>(1u << static_cast<unsigned>(s)); }

enum class Op : std::uint8_t { open, close, soft_break, hard_break };

inline constexpr std::uint32_t no_ref = 0xffffffffu;

struct Mark {
    std::uint32_t at = 0;            // byte offset in Inline::text
    Op            op = Op::open;
    Span          span = Span::emphasis;  // of an open or close
    std::uint32_t ref = no_ref;      // link, image, wikilink: index in Document::links
};

// A leaf's text and its inline structure. Entities are decoded; a soft line
// break is the space at its mark, a hard one the '\n' at its mark.
struct Inline {
    std::string       text;
    std::vector<Mark> marks;  // in document order; opens and closes nest
};

struct Link {
    std::string href;          // entities decoded; an image's source, a wikilink's target
    std::string title;         // empty: none
    bool        autolink = false;
};

struct Block {
    Kind          kind = Kind::paragraph;
    std::uint8_t  level = 0;          // heading: 1–6
    bool          tight = false;      // list: its items' paragraphs are not set apart
    char          mark = 0;           // bullet list: '-', '+' or '*'; ordered list: '.' or ')'
    std::uint32_t start = 1;          // ordered list: its first number
    Task          task = Task::none;  // item of a task list
    std::size_t   task_offset = 0;    // the task mark's byte in the source (what toggling rewrites)
    Align         align = Align::none;  // table cell
    bool          header = false;     // table cell of the head row
    char          fence = 0;          // code: '`' or '~'; 0 when indented
    std::string   info;               // code: the info string
    std::string   language;           // code: the info string's first word
    std::string   anchor;             // heading: a slug unique in the document
    Inline        content;            // paragraph, heading, table cell, code, html
    std::vector<Block> children;
    std::uint64_t hash = 0;           // of everything above but the anchor: equal blocks, equal hashes
};

struct Document {
    Block             root{.kind = Kind::document};
    std::vector<Link> links;
    Extension         extensions = gfm;
    bool              complete = true;  // false: md4c ran out of memory, the document is a prefix
};

// Parses a whole source. Sources must be shorter than 4 GiB (md4c's offsets).
[[nodiscard]] Document parse(std::string_view source, Extension extensions = gfm);

// The HTML md4c-html writes for the same source and extensions (no renderer
// flags), from the document alone.
[[nodiscard]] std::string to_html(const Document& doc);

// The visible text of a block: inline text as it reads, blocks on their own
// lines, table cells separated by tabs.
[[nodiscard]] std::string plain_text(const Block& block);

// GitHub's heading anchor for a heading's text: lower case, punctuation
// dropped, spaces to hyphens ("Hello, World!" → "hello-world").
[[nodiscard]] std::string slug(std::string_view text);

// Every block of the tree in document order, the root first.
template <class F>
void walk(const Block& b, F&& f) {
    f(b);
    for (const Block& c : b.children) walk(c, f);
}

// A stretch of an inline under one set of spans: what a text engine styles.
struct Run {
    std::uint32_t begin = 0, end = 0;
    std::uint16_t spans = 0;        // bit(Span) of every span in force
    std::uint32_t link = no_ref;    // the innermost link, image or wikilink
};
[[nodiscard]] std::vector<Run> runs(const Inline& in);

// Equality of every field (defined in the module: GCC 16.1 crashes on the
// defaulted comparisons of these recursive types when they are imported).
[[nodiscard]] bool operator==(const Mark& a, const Mark& b) noexcept;
[[nodiscard]] bool operator==(const Inline& a, const Inline& b) noexcept;
[[nodiscard]] bool operator==(const Link& a, const Link& b) noexcept;
[[nodiscard]] bool operator==(const Block& a, const Block& b) noexcept;
[[nodiscard]] bool operator==(const Document& a, const Document& b) noexcept;
[[nodiscard]] bool operator==(const Run& a, const Run& b) noexcept;

// ── streaming ──

class Stream {
public:
    explicit Stream(Extension extensions = gfm);

    void assign(std::string_view source);   // the whole source anew
    void append(std::string_view chunk);    // more of it
    void clear();

    [[nodiscard]] const Document& document() const noexcept { return doc_; }
    [[nodiscard]] std::string_view source() const noexcept { return source_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    // The first byte the next append may re-parse; the top-level blocks before
    // it (`stable_blocks`) no longer change.
    [[nodiscard]] std::size_t frozen() const noexcept { return frozen_; }
    [[nodiscard]] std::size_t stable_blocks() const noexcept { return frozen_blocks_; }
    // Bytes md4c read in the last assign / append (the cost streaming saves).
    [[nodiscard]] std::size_t last_parsed() const noexcept { return last_parsed_; }
    // A link reference definition makes every append re-parse the whole
    // source: a definition anywhere changes links everywhere.
    [[nodiscard]] bool whole() const noexcept { return whole_; }

private:
    void scan();
    bool freeze(std::size_t boundary);
    void reparse_tail();

    Extension     ext_;
    std::string   source_;
    Document      doc_;
    std::uint64_t revision_ = 0;
    std::size_t   frozen_ = 0, frozen_blocks_ = 0, frozen_links_ = 0, last_parsed_ = 0;
    std::map<std::string, int, std::less<>> frozen_anchors_;  // slug → uses before frozen_
    std::uint64_t frozen_hash_ = 0;  // the root's hash state after its frozen children
    bool          whole_ = false;
    // The line scanner: where it stopped, whether the line before was blank,
    // the fence or raw HTML block it believes open, and the boundaries it saw.
    std::size_t   scanned_ = 0;
    bool          blank_before_ = true;
    char          fence_char_ = 0;
    std::size_t   fence_length_ = 0;
    int           html_kind_ = 0;  // 1–5: CommonMark's raw HTML block kinds that may span blank lines
    std::vector<std::size_t> candidates_;
};

}  // namespace omui::md

// Between the module's own units (module linkage, not exported).
namespace omui::md {

// md4c over `source`, appending its top-level blocks and links to `doc`; task
// offsets are shifted by `base`. False when md4c failed (out of memory).
bool parse_into(Document& doc, std::string_view source, std::size_t base);
std::uint64_t hash_of(const Block& b, std::span<const Link> links);
// hash_of in two steps: a block's own fields, then each child's hash folded in.
std::uint64_t hash_fields(const Block& b, std::span<const Link> links);
std::uint64_t hash_child(std::uint64_t h, std::uint64_t child);
using Anchors = std::map<std::string, int, std::less<>>;  // slug → times used
// Heading anchors unique across `base` and `used` together; only `used` is
// updated (a stream's tail counts on top of its frozen part, without a copy).
void assign_anchors(Block& b, const Anchors& base, Anchors& used);

}  // namespace omui::md
