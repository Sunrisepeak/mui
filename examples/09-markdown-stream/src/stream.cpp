module stream;

import std;
import openmui;
import openmui.ui.markdown;
import mcpplibs.os;

namespace stream {

namespace {

namespace md = omui::md;

std::string pieces(std::size_t n) { return std::format("{} {}", n, n == 1 ? "piece" : "pieces"); }

// The newest leaf of the document — where appended text lands — and the path
// of its node: the ids openmui.ui.markdown gives its blocks.
struct Leaf {
    std::string path;
    std::size_t length = 0;  // its text, in bytes (as its node draws it)
    bool        found = false;
};

Leaf newest(const md::Document& doc, std::string_view root) {
    Leaf out;
    const md::Block* b = &doc.root;
    std::string path(root);
    while (!b->children.empty()) {
        const std::size_t i = b->children.size() - 1;
        const md::Kind k = b->kind;
        const char* tag = k == md::Kind::bullet_list || k == md::Kind::ordered_list ? "i"
                          : k == md::Kind::table || k == md::Kind::table_head || k == md::Kind::table_body ? "r"
                          : k == md::Kind::table_row                                                     ? "c"
                                                                                                         : "b";
        path += std::format("/{}{}", tag, i);
        b = &b->children[i];
    }
    if (b == &doc.root) return out;
    std::string_view text = b->content.text;
    if (b->kind == md::Kind::code || b->kind == md::Kind::html)
        while (text.ends_with('\n')) text.remove_suffix(1);
    out.path = std::move(path);
    out.length = text.size();
    out.found = true;
    return out;
}

// New words rising into place, letter by letter (only `begin` onwards).
omui::Value fade_from(std::size_t begin) {
    omui::GlyphMotion m;
    m.domain = omui::GlyphDomain::grapheme;
    m.begin = begin;
    m.from = {.y = 4, .opacity = 0};
    m.to = {};
    m.transition = omui::Transition::tween(omui::seconds(0.28), omui::anim::ease::out_cubic);
    m.stagger = omui::seconds(0.012);
    return omui::to_value(m);
}

void fade_when_built(omui::Runtime* rt, std::string path, omui::NodeId old, std::size_t from, int tries) {
    rt->after(omui::seconds(0), [rt, path = std::move(path), old, from, tries] {
        const omui::NodeId n = rt->find(path);
        if (n && n != old) rt->set(n, "glyph_motion", fade_from(from));
        else if (tries > 0) fade_when_built(rt, path, old, from, tries - 1);
    });
}

}  // namespace

void Model::append(std::string_view chunk) {
    if (chunk.empty()) return;
    const omui::seconds now = runtime ? runtime->now() : omui::seconds(0);
    if (first.count() < 0) first = now;
    last = now;
    ++chunks;
    // Where the newest block ended before this piece.
    const Leaf before = newest(mirror.document(), "main/view/doc");
    mirror.append(chunk);
    parsed += mirror.last_parsed();
    source.set(std::string(mirror.source()));
    const Leaf after = newest(mirror.document(), "main/view/doc");
    const double secs = (last - first).count();
    status.set(std::format("{} · {} bytes · {:.0f} pieces/s · {} bytes parsed per piece", pieces(chunks), mirror.source().size(),
                           secs > 0 ? static_cast<double>(chunks) / secs : 0.0, parsed / std::max<std::size_t>(chunks, 1)));
    if (!fade || !runtime || !after.found) return;
    // The same block grew: only its new bytes move; a new block: all of it.
    const std::size_t from = before.found && before.path == after.path && after.length >= before.length ? before.length : 0;
    // The block's new node exists once a frame has rebuilt the blocks: the
    // node at its path is then another than now (a changed block is a new node).
    fade_when_built(runtime, after.path, runtime->find(after.path), from, 8);
}

void Model::end() {
    ended = true;
    status.set(std::format("done · {} · {} bytes · {} bytes parsed per piece", pieces(chunks), mirror.source().size(),
                           parsed / std::max<std::size_t>(chunks, 1)));
}

std::vector<std::string> demo_tokens() {
    constexpr std::string_view answer = R"(# Streaming, done right

A model writes its answer **token by token**. openmui renders each piece as it
arrives: the document re-parses only what follows its last *stable* block, and
every finished block keeps its node, its shaped text and its layout.

## What you see

1. New words fade in — a glyph motion over just the new bytes.
2. Old words stay still: nothing above the tail is laid out again.
3. Code, tables and lists form as their closing lines arrive.

```cpp
for (auto token : model.stream(prompt))
    answer.append(token);   // a few microseconds each
```

| Piece of work | Cost per token |
|:--------------|---------------:|
| parse the tail | about 1 µs |
| lay out the tail | one block |

> The status line below counts the pieces and the bytes parsed for each.
)";
    // Tokens as a model sends them: a word with the space before it, or a mark.
    std::vector<std::string> out;
    std::string cur;
    for (char c : answer) {
        if ((c == ' ' || c == '\n') && !cur.empty() && cur.back() != ' ' && cur.back() != '\n') {
            out.push_back(std::move(cur));
            cur.clear();
        }
        cur += c;
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

void play(Model& m, std::vector<std::string> tokens, double per_second) {
    auto shared = std::make_shared<std::vector<std::string>>(std::move(tokens));
    auto next = std::make_shared<std::function<void(std::size_t)>>();
    *next = [&m, shared, next, per_second](std::size_t i) {
        if (i >= shared->size()) {
            m.end();
            return;
        }
        m.append((*shared)[i]);
        m.runtime->after(omui::seconds(1.0 / per_second), [next, i] { (*next)(i + 1); });
    };
    m.runtime->after(omui::seconds(0.2), [next] { (*next)(0); });
}

void read_stdin(Model& m) {
    // The reader blocks on the input; what it reads is appended on the UI thread.
    std::thread([&m, rt = m.runtime] {
        namespace os = mcpplibs::os;
        for (;;) {
            auto chunk = os::stdio::read_input(4096);
            if (!chunk || chunk->empty()) break;
            rt->queue().post([&m, text = std::move(*chunk)] { m.append(text); });
        }
        rt->queue().post([&m] { m.end(); });
    }).detach();
}

omui::AppSpec app(Model& m) {
    using namespace omui;
    AppSpec spec;
    spec.id = "org.openmui.examples.markdown-stream";
    spec.title = "Markdown stream";
    spec.size = {860, 720};
    spec.view = [&m] {
        return column({.id = "app", .grow = 1, .background = "background"},
                      {
                          scroll({.id = "view", .grow = 1, .padding = Edges(24.0f, 32.0f)},
                                 {markdown({.id = "doc", .origin = MarkdownOrigin::remote, .source = m.source})}),
                          box({.id = "status-bar", .padding = Edges(6.0f, 16.0f), .background = "surface"},
                              {text({.id = "status", .text = derive([&m] { return m.status.get(); }), .style = "caption"})}),
                      });
    };
    return spec;
}

}  // namespace stream
