// The Markdown component as a person, an agent and a renderer meet it: the
// semantic tree is the document's structure; links reach outside only with an
// answer (a person's click is one, S-2a); tasks rewrite their mark in the
// source; streaming keeps the nodes of unchanged blocks; the pixels are pinned.
import std;

import openmui.ui.markdown;
import openmui.host.headless;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

constexpr std::string_view readme = R"(# openmui

A UI framework where **pixels, accessibility, agents and tests** are renderings
of one semantic tree. See [the spec](https://example.com/spec "Spec") or jump to [usage](#usage).

> Quoted *wisdom*.

## Usage

1. Install with `xlings`
2. Build with `mcpp`

- [x] parse
- [ ] render

| Feature | Status |
|:--------|-------:|
| tables  | done   |
| math    | later  |

```cpp
import openmui;
```

---
)";

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}, std::string answer = {}) {
    Invocation call;
    call.args = std::move(args);
    call.answer = std::move(answer);
    call.actor = {"agent", "test", {}};
    auto e = h.tree().act(id, action, call);
    h.step();
    return e.exit_code().value_or(-1);
}

std::optional<spec::SemanticNode> find_role(Headless& h, Role r, std::string_view name = {}) {
    for (const auto& n : h.tree().snapshot({}).nodes)
        if (n.role == r && (name.empty() || n.name == name)) return n;
    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"ui.markdown"};

    s.test("the semantic tree is the document's structure", [] {
        Headless h({.size = {720, 900}});
        auto src = signal(std::string(readme));
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = src})}));
        h.settle();
        auto doc = h.tree().node("main/doc");
        check(doc && doc->role == Role::document && doc->name == "openmui", "a document named by its first heading");
        const auto h1 = h.tree().node("main/doc/b0");
        check(h1 && h1->role == Role::heading && h1->value == Value(std::int64_t{1}) && h1->name == "openmui");
        const auto usage = find_role(h, Role::heading, "Usage");
        check(usage && usage->description == "usage" && usage->value == Value(std::int64_t{2}), "level and anchor");
        check(find_role(h, Role::blockquote).has_value(), "quotes are blockquotes");
        check(find_role(h, Role::list) && find_role(h, Role::list_item), "lists and items");
        check(find_role(h, Role::table) && find_role(h, Role::row) && find_role(h, Role::column_header, "Feature") &&
                  find_role(h, Role::cell, "later"),
              "tables, rows, headers, cells");
        const auto code = find_role(h, Role::code);
        check(code && code->name == "import openmui;" && code->description == "cpp", "code with its language");
        check(find_role(h, Role::button, "Copy code").has_value(), "and a copy button");
        check(find_role(h, Role::separator).has_value(), "rules are separators");
        const auto spec_link = find_role(h, Role::link, "the spec");
        check(spec_link && spec_link->value == Value("https://example.com/spec"), "links are link nodes with their address");
        check(spec_link && spec_link->action("activate") && spec_link->action("activate")->effect == Effect::external,
              "following a link reaches outside (S-1)");
        const auto jump = find_role(h, Role::link, "usage");
        check(jump && jump->action("activate") && jump->action("activate")->effect == Effect::reversible && jump->action("activate")->answer.empty(),
              "a heading of the document is inside: no answer asked");
        check(h.tree().violations().empty(), std::format("{} violations", h.tree().violations().size()));
    });

    s.test("links: an agent needs the answer, a person's click is it (S-2, S-2a)", [] {
        Headless h({.size = {720, 900}});
        std::vector<std::string> opened;
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = std::string(readme),
                                                           .on_link_activated = [&](std::string href) { opened.push_back(href); }})}));
        h.settle();
        const auto link = find_role(h, Role::link, "the spec");
        check(link.has_value());
        if (!link) return;
        const std::string id = link->id;
        check_eq(act(h, id, "activate"), std::int64_t{spec::exit_code::needs_answer});
        check(opened.empty(), "nothing opened without the answer");
        check_eq(act(h, id, "activate", {}, "yes"), std::int64_t{0});
        check(opened == std::vector<std::string>{"https://example.com/spec"});
        // A person clicks the link itself: no question asked.
        check(h.click(id));
        h.settle();
        check_eq(opened.size(), std::size_t{2});
        // The document's own action only follows the document's links.
        check_eq(act(h, "main/doc", "open_link", Value{}.set("href", "https://evil.example"), "yes"), std::int64_t{spec::exit_code::failure});
        check_eq(act(h, "main/doc", "open_link", Value{}.set("href", "https://example.com/spec"), "yes"), std::int64_t{0});
        check_eq(opened.size(), std::size_t{3});
    });

    s.test("an in-document link and scroll_to_anchor bring the heading into view", [] {
        Headless h({.size = {720, 260}});
        std::string long_doc = std::string(readme);
        for (int i = 0; i < 40; ++i) long_doc += std::format("\nParagraph {} of filler text to scroll past.\n", i);
        long_doc += "\n## Appendix\n\nThe end.\n";
        std::vector<std::string> seen;
        h.mount("main", scroll({.id = "view", .grow = 1}, {markdown({.id = "doc", .source = long_doc,
                                                                     .on_heading_visible = [&](std::string a) { seen.push_back(a); }})}));
        h.settle();
        const auto appendix = find_role(h, Role::heading, "Appendix");
        check(appendix && appendix->states.has(State::offscreen), "the appendix starts below the view");
        {
            Invocation call;
            call.args = Value{}.set("anchor", "appendix");
            call.actor = {"agent", "test", {}};
            auto e = h.tree().act(find_role(h, Role::document)->id, "scroll_to_anchor", call);
            check(e.exit_code() == 0, e.ndjson());
        }
        h.settle();
        const auto after = find_role(h, Role::heading, "Appendix");
        check(after && !after->states.has(State::offscreen), "and is in view after scroll_to_anchor");
        check(!seen.empty() && seen.front() == "openmui", "heading_visible reports the first heading in view");
        check_eq(act(h, find_role(h, Role::document)->id, "scroll_to_anchor", Value{}.set("anchor", "nope")),
                 std::int64_t{spec::exit_code::failure});
    });

    s.test("selecting text: pressed and dragged across a paragraph, the text between is selected and on the clipboard; an agent selects too", [] {
        Headless h({.size = {720, 900}});
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = std::string("A paragraph to select some words from.\n")})}));
        h.settle();
        const auto para = find_role(h, Role::paragraph, "A paragraph to select some words from.");
        check(para.has_value(), "the paragraph");
        if (!para) return;
        const Rect r = h.runtime().screen_bounds(h.runtime().find(para->id));
        const Point from{r.x + 2, r.y + r.height / 2}, to{r.x + 70, r.y + r.height / 2};  // a few words in
        h.step({PointerEvent{.kind = PointerKind::down, .position = from, .button = MouseButton::left, .buttons = 1}});
        h.step({PointerEvent{.kind = PointerKind::move, .position = to, .buttons = 1}});
        h.step({PointerEvent{.kind = PointerKind::up, .position = to, .button = MouseButton::left}});
        h.settle();
        const std::string copied = h.runtime().clipboard_text();
        check(copied.starts_with("A para") && copied.size() < std::string("A paragraph to select some words from.").size(), copied);
        check(h.tree().node(para->id)->states.has(State::selected), "selected");
        check_eq(act(h, para->id, "set_selection", Value{}.set("anchor", 2).set("focus", 11)), std::int64_t{0});
    });

    s.test("tasks: toggling rewrites the mark in the source and tells the binding", [] {
        Headless h({.size = {720, 900}});
        auto src = signal(std::string(readme));
        std::vector<std::pair<std::int32_t, bool>> toggled;
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = src,
                                                           .on_task_toggled = [&](bool checked, std::int32_t i) { toggled.emplace_back(i, checked); }})}));
        h.settle();
        const auto render_task = find_role(h, Role::checkbox, "render");
        check(render_task && !render_task->states.has(State::checked));
        if (!render_task) return;
        check_eq(act(h, render_task->id, "toggle"), std::int64_t{0});
        h.settle();
        check(src.peek().contains("- [x] render"), "the source has the new mark");
        check((toggled == std::vector<std::pair<std::int32_t, bool>>{{1, true}}));
        const auto now = find_role(h, Role::checkbox, "render");
        check(now && now->states.has(State::checked));
        check_eq(act(h, "main/doc", "toggle_task", Value{}.set("index", std::int64_t{0})), std::int64_t{0});
        h.settle();
        check(src.peek().contains("- [ ] parse"));        // A person's click toggles it as well (activate), and undo takes it back.
        const auto again = find_role(h, Role::checkbox, "render");
        check(again && h.click(again->id), "clicked");
        h.settle();
        check(src.peek().contains("- [ ] render"), "a click unchecked it");
        check_eq(h.runtime().undo({}).exit_code().value_or(-1), std::int64_t{0});
        h.settle();
        check(src.peek().contains("- [x] render"), "undone");
    });

    s.test("streaming: unchanged blocks keep their nodes, the stream re-parses the tail", [] {
        Headless h({.size = {720, 900}});
        auto src = signal(std::string());
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = src})}));
        h.settle();
        const std::string full(readme);
        NodeId first;
        for (std::size_t pos = 0; pos < full.size(); pos += 7) {
            src.set(full.substr(0, std::min(full.size(), pos + 7)));
            h.step();
            if (!first && pos > 20) first = h.runtime().find("main/doc/b0");  // the heading is complete
        }
        h.settle();
        check(first && h.runtime().find("main/doc/b0") == first, "the first heading's node survived every append");
        Markdown* m = nullptr;
        (void)m;
        const auto doc = h.tree().node("main/doc");
        check(doc && doc->name == "openmui");
        check(find_role(h, Role::table) && find_role(h, Role::code), "the whole document arrived");
        // The `append` action streams too.
        check_eq(act(h, "main/doc", "append", Value{}.set("chunk", "\n## More\n\nappended\n")), std::int64_t{0});
        h.settle();
        check(find_role(h, Role::heading, "More").has_value());
        check(src.peek().ends_with("appended\n"), "the binding learns the appended source");
    });

    s.test("copy, find and the code block's copy button", [] {
        Headless h({.size = {720, 900}});
        std::string clipboard;
        h.runtime().set_clipboard([&] { return clipboard; }, [&](std::string_view t) { clipboard = std::string(t); });
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = std::string(readme)})}));
        h.settle();
        check_eq(act(h, "main/doc", "copy"), std::int64_t{0});
        check(clipboard.starts_with("openmui\n") && clipboard.contains("Quoted wisdom."), clipboard.substr(0, 80));
        Invocation call;
        call.args = Value{}.set("query", "MCPP");
        call.actor = {"agent", "test", {}};
        auto e = h.tree().act("main/doc", "find", call);
        const Value* hits = e.data_value();
        check(hits && hits->list() && hits->list()->size() == 1, "one block mentions mcpp");
        const auto copy_button = find_role(h, Role::button, "Copy code");
        check(copy_button.has_value());
        if (!copy_button) return;
        check(h.click(copy_button->id));
        h.step();
        check_eq(clipboard, std::string("import openmui;"));
        check(find_role(h, Role::button, "Copied").has_value(), "the button says so");
    });

    s.test("remote text is marked so on every node", [] {
        Headless h({.size = {720, 600}});
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .origin = MarkdownOrigin::remote, .source = std::string(readme)})}));
        h.settle();
        for (const auto& n : h.tree().snapshot({.root = "main/doc"}).nodes)
            check(n.origin == spec::Origin::remote, std::format("{} is not marked remote", n.id));
    });

    s.test("glyph motion reaches the document's text; the component does nothing for it (D-68)", [] {
        Headless h({.size = {720, 600}});
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = std::string(readme)})}));
        h.settle();
        auto hash = [&] {
            Hash x;
            const auto& px = h.pixels();
            for (int y = 0; y < px.height(); ++y) x.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
            return x.value();
        };
        const auto rest = hash();
        // A paragraph's words rise and the heading's letters gather: set on the
        // document's own nodes, as an agent or a script would.
        h.runtime().set(h.runtime().find("main/doc/b1"), "glyph_motion", to_value(GlyphMotion::rise()));
        h.runtime().set(h.runtime().find("main/doc/b0"), "glyph_motion", to_value(GlyphMotion::gather(5)));
        h.step();
        h.step();
        check(hash() != rest, "on their way");
        check(h.tree().node("main/doc/b1")->states.has(State::animating));
        h.settle();
        check(hash() == rest, "and back to the document's own pixels");
        check(!h.tree().node("main/doc/b1")->states.has(State::animating));
    });

    s.test("the same pixels on every platform", [] {
        HeadlessOptions o;
        o.size = {640, 820};
        o.scale = 2;
        Headless h(o);
        h.mount("main", column({.padding = 16, .background = "background"}, {markdown({.id = "doc", .source = std::string(readme)})}));
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        check(h.save_png(scratch_dir("ui-markdown") / "readme@2x.png").has_value());
        std::println("    pixels {}", hash.hex());
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        check_eq(hash.hex(), std::string("5f07eca688e51220"));
    });

    return s.run(argc, argv);
}
