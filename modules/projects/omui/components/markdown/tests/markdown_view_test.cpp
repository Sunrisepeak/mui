// The Markdown component as a person, an agent and a renderer meet it: the
// semantic tree is the document's structure; links reach outside only with an
// answer (a person's click is one, S-2a); tasks rewrite their mark in the
// source; streaming keeps the nodes of unchanged blocks; the pixels are pinned.
import std;

import omui.ui.markdown;
import omui.host.headless;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
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
import omui;
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

UTest cases{"ui.markdown", [] {

    "the semantic tree is the document's structure"_test = [] {
        Headless h({.size = {720, 900}});
        auto src = signal(std::string(readme));
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = src})}));
        h.settle();
        auto doc = h.tree().node("main/doc");
        expect(doc && doc->role == Role::document && doc->name == "openmui") << "a document named by its first heading";
        const auto h1 = h.tree().node("main/doc/b0");
        expect(h1 && h1->role == Role::heading && h1->value == Value(std::int64_t{1}) && h1->name == "openmui");
        const auto usage = find_role(h, Role::heading, "Usage");
        expect(usage && usage->description == "usage" && usage->value == Value(std::int64_t{2})) << "level and anchor";
        expect(find_role(h, Role::blockquote).has_value()) << "quotes are blockquotes";
        expect(find_role(h, Role::list) && find_role(h, Role::list_item)) << "lists and items";
        expect(find_role(h, Role::table) && find_role(h, Role::row) && find_role(h, Role::column_header, "Feature") &&
                  find_role(h, Role::cell, "later")) << "tables, rows, headers, cells";
        const auto code = find_role(h, Role::code);
        expect(code && code->name == "import omui;" && code->description == "cpp") << "code with its language";
        expect(find_role(h, Role::button, "Copy code").has_value()) << "and a copy button";
        expect(find_role(h, Role::separator).has_value()) << "rules are separators";
        const auto spec_link = find_role(h, Role::link, "the spec");
        expect(spec_link && spec_link->value == Value("https://example.com/spec")) << "links are link nodes with their address";
        expect(spec_link && spec_link->action("activate") && spec_link->action("activate")->effect == Effect::external) << "following a link reaches outside (S-1)";
        const auto jump = find_role(h, Role::link, "usage");
        expect(jump && jump->action("activate") && jump->action("activate")->effect == Effect::reversible && jump->action("activate")->answer.empty()) << "a heading of the document is inside: no answer asked";
        expect(h.tree().violations().empty()) << std::format("{} violations", h.tree().violations().size());
    };

    "links: an agent needs the answer, a person's click is it (S-2, S-2a)"_test = [] {
        Headless h({.size = {720, 900}});
        std::vector<std::string> opened;
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = std::string(readme),
                                                           .on_link_activated = [&](std::string href) { opened.push_back(href); }})}));
        h.settle();
        const auto link = find_role(h, Role::link, "the spec");
        expect(link.has_value());
        if (!link) return;
        const std::string id = link->id;
        expect(eq(act(h, id, "activate"), std::int64_t{spec::exit_code::needs_answer}));
        expect(opened.empty()) << "nothing opened without the answer";
        expect(eq(act(h, id, "activate", {}, "yes"), std::int64_t{0}));
        expect(opened == std::vector<std::string>{"https://example.com/spec"});
        // A person clicks the link itself: no question asked.
        expect(h.click(id));
        h.settle();
        expect(eq(opened.size(), std::size_t{2}));
        // The document's own action only follows the document's links.
        expect(eq(act(h, "main/doc", "open_link", Value{}.set("href", "https://evil.example"), "yes"), std::int64_t{spec::exit_code::failure}));
        expect(eq(act(h, "main/doc", "open_link", Value{}.set("href", "https://example.com/spec"), "yes"), std::int64_t{0}));
        expect(eq(opened.size(), std::size_t{3}));
    };

    "an in-document link and scroll_to_anchor bring the heading into view"_test = [] {
        Headless h({.size = {720, 260}});
        std::string long_doc = std::string(readme);
        for (int i = 0; i < 40; ++i) {
            long_doc += std::format("\nParagraph {} of filler text to scroll past.\n", i);
            if (i == 15) long_doc += "\n## Middle\n";
        }
        long_doc += "\n## Appendix\n\nThe end.\n";
        std::vector<std::string> seen;
        h.mount("main", scroll({.id = "view", .grow = 1}, {markdown({.id = "doc", .source = long_doc,
                                                                     .on_heading_visible = [&](std::string a) { seen.push_back(a); }})}));
        h.settle();
        const auto appendix = find_role(h, Role::heading, "Appendix");
        expect(appendix && appendix->states.has(State::offscreen)) << "the appendix starts below the view";
        {
            Invocation call;
            call.args = Value{}.set("anchor", "appendix");
            call.actor = {"agent", "test", {}};
            auto e = h.tree().act(find_role(h, Role::document)->id, "scroll_to_anchor", call);
            expect(e.exit_code() == 0) << e.ndjson();
        }
        h.settle();
        const auto after = find_role(h, Role::heading, "Appendix");
        expect(after && !after->states.has(State::offscreen)) << "and is in view after scroll_to_anchor";
        expect(!seen.empty() && seen.front() == "openmui") << "heading_visible reports the first heading in view";
        // A heading the view can scroll to the top of: its section is read from the top, not glimpsed at the bottom.
        expect(eq(act(h, find_role(h, Role::document)->id, "scroll_to_anchor", Value{}.set("anchor", "middle")), std::int64_t{0}));
        h.settle();
        const auto middle = find_role(h, Role::heading, "Middle");
        expect(middle && middle->bounds && middle->bounds->y < 4) << (middle && middle->bounds ? std::format("at y {}", middle->bounds->y) : "no bounds");
        expect(eq(act(h, find_role(h, Role::document)->id, "scroll_to_anchor", Value{}.set("anchor", "nope")),
                 std::int64_t{spec::exit_code::failure}));
    };

    "selecting text: pressed and dragged across a paragraph and into the next, the text between is selected; Ctrl+C copies it; an agent selects too"_test = [] {
        Headless h({.size = {720, 900}});
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = std::string("A paragraph to select some words from.\n\nAnd a second one after it.\n")})}));
        h.settle();
        const auto para = find_role(h, Role::paragraph, "A paragraph to select some words from.");
        expect(para.has_value()) << "the paragraph";
        if (!para) return;
        const Rect r = h.runtime().screen_bounds(h.runtime().find(para->id));
        const Point from{r.x + 2, r.y + r.height / 2}, to{r.x + 70, r.y + r.height / 2};  // a few words in
        h.step({PointerEvent{.kind = PointerKind::down, .position = from, .button = MouseButton::left, .buttons = 1}});
        h.step({PointerEvent{.kind = PointerKind::move, .position = to, .buttons = 1}});
        h.step({PointerEvent{.kind = PointerKind::up, .position = to, .button = MouseButton::left}});
        h.settle();
        h.key(Key::c, Modifier::ctrl);
        h.settle();
        const std::string copied = h.runtime().clipboard_text();
        expect(copied.starts_with("A para") && copied.size() < std::string("A paragraph to select some words from.").size()) << copied;
        expect(h.tree().node(para->id)->states.has(State::selected)) << "selected";
        // Into the next paragraph: one selection over both, a line apart.
        const auto second = find_role(h, Role::paragraph, "And a second one after it.");
        expect(second.has_value()) << "the second paragraph";
        if (second) {
            const Rect next = h.runtime().screen_bounds(h.runtime().find(second->id));
            h.step({PointerEvent{.kind = PointerKind::down, .position = from, .button = MouseButton::left, .buttons = 1}});
            h.step({PointerEvent{.kind = PointerKind::move, .position = {next.x + 40, next.y + next.height / 2}, .buttons = 1}});
            h.step({PointerEvent{.kind = PointerKind::up, .position = {next.x + 40, next.y + next.height / 2}, .button = MouseButton::left}});
            h.settle();
            const std::string both = h.runtime().selected_text();
            expect(both.starts_with("A paragraph to select some words from.\nAnd")) << both;
        }
        expect(eq(act(h, para->id, "set_selection", Value{}.set("anchor", 2).set("focus", 11)), std::int64_t{0}));
    };

    "tasks: toggling rewrites the mark in the source and tells the binding"_test = [] {
        Headless h({.size = {720, 900}});
        auto src = signal(std::string(readme));
        std::vector<std::pair<std::int32_t, bool>> toggled;
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = src,
                                                           .on_task_toggled = [&](bool checked, std::int32_t i) { toggled.emplace_back(i, checked); }})}));
        h.settle();
        const auto render_task = find_role(h, Role::checkbox, "render");
        expect(render_task && !render_task->states.has(State::checked));
        if (!render_task) return;
        expect(eq(act(h, render_task->id, "toggle"), std::int64_t{0}));
        h.settle();
        expect(src.peek().contains("- [x] render")) << "the source has the new mark";
        expect((toggled == std::vector<std::pair<std::int32_t, bool>>{{1, true}}));
        const auto now = find_role(h, Role::checkbox, "render");
        expect(now && now->states.has(State::checked));
        expect(eq(act(h, "main/doc", "toggle_task", Value{}.set("index", std::int64_t{0})), std::int64_t{0}));
        h.settle();
        expect(src.peek().contains("- [ ] parse"));        // A person's click toggles it as well (activate), and undo takes it back.
        const auto again = find_role(h, Role::checkbox, "render");
        expect(again && h.click(again->id)) << "clicked";
        h.settle();
        expect(src.peek().contains("- [ ] render")) << "a click unchecked it";
        expect(eq(h.runtime().undo({}).exit_code().value_or(-1), std::int64_t{0}));
        h.settle();
        expect(src.peek().contains("- [x] render")) << "undone";
    };

    "streaming: unchanged blocks keep their nodes, the stream re-parses the tail"_test = [] {
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
        expect(first && h.runtime().find("main/doc/b0") == first) << "the first heading's node survived every append";
        Markdown* m = nullptr;
        (void)m;
        const auto doc = h.tree().node("main/doc");
        expect(doc && doc->name == "openmui");
        expect(find_role(h, Role::table) && find_role(h, Role::code)) << "the whole document arrived";
        // The `append` action streams too.
        expect(eq(act(h, "main/doc", "append", Value{}.set("chunk", "\n## More\n\nappended\n")), std::int64_t{0}));
        h.settle();
        expect(find_role(h, Role::heading, "More").has_value());
        expect(src.peek().ends_with("appended\n")) << "the binding learns the appended source";
    };

    "copy, find and the code block's copy button"_test = [] {
        Headless h({.size = {720, 900}});
        std::string clipboard;
        h.runtime().set_clipboard([&] { return clipboard; }, [&](std::string_view t) { clipboard = std::string(t); });
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .source = std::string(readme)})}));
        h.settle();
        expect(eq(act(h, "main/doc", "copy"), std::int64_t{0}));
        expect(clipboard.starts_with("openmui\n") && clipboard.contains("Quoted wisdom.")) << clipboard.substr(0, 80);
        Invocation call;
        call.args = Value{}.set("query", "MCPP");
        call.actor = {"agent", "test", {}};
        auto e = h.tree().act("main/doc", "find", call);
        const Value* hits = e.data_value();
        expect(hits && hits->list() && hits->list()->size() == 1) << "one block mentions mcpp";
        const auto copy_button = find_role(h, Role::button, "Copy code");
        expect(copy_button.has_value());
        if (!copy_button) return;
        expect(h.click(copy_button->id));
        h.step();
        expect(eq(clipboard, std::string("import omui;")));
        expect(find_role(h, Role::button, "Copied").has_value()) << "the button says so";
    };

    "remote text is marked so on every node"_test = [] {
        Headless h({.size = {720, 600}});
        h.mount("main", column({.padding = 16}, {markdown({.id = "doc", .origin = MarkdownOrigin::remote, .source = std::string(readme)})}));
        h.settle();
        for (const auto& n : h.tree().snapshot({.root = "main/doc"}).nodes)
            expect(n.origin == spec::Origin::remote) << std::format("{} is not marked remote", n.id);
    };

    "glyph motion reaches the document's text; the component does nothing for it (D-68)"_test = [] {
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
        expect(hash() != rest) << "on their way";
        expect(h.tree().node("main/doc/b1")->states.has(State::animating));
        h.settle();
        expect(hash() == rest) << "and back to the document's own pixels";
        expect(!h.tree().node("main/doc/b1")->states.has(State::animating));
    };

    "the same pixels on every platform"_test = [] {
        HeadlessOptions o;
        o.size = {640, 820};
        o.scale = 2;
        Headless h(o);
        h.mount("main", column({.padding = 16, .background = "background"}, {markdown({.id = "doc", .source = std::string(readme)})}));
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        expect(h.save_png(scratch_dir("ui-markdown") / "readme@2x.png").has_value());
        std::println("    pixels {}", hash.hex());
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        // Re-pinned with M1 ⑤b: the sample's code block reads `import omui;` (was `import openmui;`).
        expect(eq(hash.hex(), std::string("ab03efbf649fa262")));
    };
}};
