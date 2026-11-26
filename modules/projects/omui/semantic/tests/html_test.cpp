// The semantic tree as HTML (D-236, G2): the static projection a reader without JavaScript reads, its text the tree's,
// its elements what HTML has for each role, the same attributes the Web host's live mirror gives them.
import std;
import mlibs.text.harfbuzz;
import omui.semantic;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;
namespace sem = omui::semantic;

namespace {

spec::SemanticNode node(std::string id, Role role, std::string name, std::vector<std::string> children = {}) {
    spec::SemanticNode n;
    n.id = std::move(id);
    n.role = role;
    n.name = std::move(name);
    n.children = std::move(children);
    return n;
}

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c{mlibs::text::harfbuzz::shaper()};
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

}  // namespace

static UTest cases{"html", [] {
    "each role as HTML has it, its text and its attributes; the projection names each node for the program"_test = [] {
        sem::Snapshot snapshot;
        spec::SemanticNode heading = node("main/title", Role::heading, "Build <with> understanding");
        heading.value = Value(1.0);
        spec::SemanticNode save = node("main/save", Role::button, "Save & close");
        save.states.set(State::focusable, true);
        spec::SemanticNode note = node("main/note", Role::paragraph, "Start with \"a working interface\".");
        spec::SemanticNode places = node("main/places", Role::navigation, "Places", {"main/places/home"});
        snapshot.nodes = {node("main", Role::main, "", {"main/title", "main/save", "main/note", "main/places"}), heading, save, note, places,
                          node("main/places/home", Role::link, "Home")};
        const std::string html = sem::static_html(snapshot, {.lang = "en"});
        expect(html.starts_with("<main data-omui-target=\"main\" lang=\"en\">")) << html;
        expect(html.contains("<h1 data-omui-target=\"main/title\">Build &lt;with&gt; understanding</h1>")) << "a heading of level 1, named by its text, escaped: " << html;
        expect(html.contains("<button data-omui-target=\"main/save\" type=\"button\">Save &amp; close</button>")) << html;
        expect(!html.contains("tabindex")) << "nothing takes the focus before the program runs";
        expect(html.contains("<p data-omui-target=\"main/note\">Start with \"a working interface\".</p>")) << html;
        expect(html.contains("<nav data-omui-target=\"main/places\" aria-label=\"Places\">") && html.contains("<a data-omui-target=\"main/places/home\">Home</a>"))
            << html;
        expect(!html.contains(" role=\"button\"") && !html.contains(" role=\"heading\"")) << "a tag says its own role";
    };

    "a document's nodes: their properties the elements' attributes, their marks the elements of their text (S-16, S-17)"_test = [] {
        sem::Snapshot snapshot;
        spec::SemanticNode title = node("doc/title", Role::heading, "Install");
        title.properties.set("level", Value(std::int64_t{2}));
        title.properties.set("anchor", Value("install"));
        // "Read the “guide” & run it": a link over “guide” (code points 9 to 16), stress inside it, code at the end.
        spec::SemanticNode text = node("doc/p", Role::paragraph, "Read the “guide” & run it", {"doc/p/link0"});
        text.marks = {{spec::MarkKind::link, 9, 16, "doc/p/link0"}, {spec::MarkKind::emphasis, 10, 15, {}}, {spec::MarkKind::code, 19, 25, {}}};
        spec::SemanticNode link = node("doc/p/link0", Role::link, "“guide”");
        link.properties.set("href", Value("guide.html?a=1&b=2"));
        link.properties.set("title", Value("The guide"));
        link.description = "The guide";
        spec::SemanticNode code = node("doc/code", Role::code, "if (a < b)\n    run();");
        code.properties.set("language", Value("cpp"));
        spec::SemanticNode list = node("doc/list", Role::list, "", {"doc/list/item"});
        list.properties.set("ordered", Value(true));
        list.properties.set("start", Value(std::int64_t{3}));
        list.properties.set("tight", Value(true));
        spec::SemanticNode comment = node("doc/note", Role::none, "");
        comment.properties.set("comment", Value(" code: a.cpp#b "));
        comment.states.set(State::hidden, true);
        spec::SemanticNode cell = node("doc/table/body/row/cell", Role::cell, "1.5");
        cell.properties.set("align", Value("right"));
        spec::SemanticNode markup = node("doc/raw", Role::code, "<details>more</details>");
        markup.properties.set("raw", Value("html"));
        spec::SemanticNode foreign = markup;
        foreign.id = "doc/foreign";
        foreign.origin = spec::Origin::remote;
        snapshot.nodes = {node("doc", Role::document, "", {"doc/title", "doc/note", "doc/p", "doc/code", "doc/list", "doc/table", "doc/raw", "doc/foreign"}),
                          title, comment, text, link, code, list, node("doc/list/item", Role::list_item, "", {"doc/list/item/p"}),
                          node("doc/list/item/p", Role::paragraph, "third"),
                          node("doc/table", Role::table, "", {"doc/table/head", "doc/table/body"}),
                          node("doc/table/head", Role::group, "", {"doc/table/head/row"}), node("doc/table/head/row", Role::row, "", {"doc/table/head/row/cell"}),
                          node("doc/table/head/row/cell", Role::column_header, "Size"),
                          node("doc/table/body", Role::group, "", {"doc/table/body/row"}), node("doc/table/body/row", Role::row, "", {"doc/table/body/row/cell"}), cell,
                          markup, foreign};
        for (const spec::SemanticNode& n : snapshot.nodes) expect(spec::check(n).empty()) << n.id;
        const std::string html = sem::static_html(snapshot, {});
        expect(html.contains("<h2 data-omui-target=\"doc/title\" id=\"install\">Install</h2>")) << html;
        expect(html.contains("<p data-omui-target=\"doc/p\">Read the <a data-omui-target=\"doc/p/link0\" href=\"guide.html?a=1&amp;b=2\" title=\"The guide\">“<em>guide</em>”</a> &amp; <code>run it</code></p>"))
            << "the link stands where its mark is, once: " << html;
        expect(html.find("doc/p/link0") == html.rfind("doc/p/link0")) << "a child a mark places is not written again";
        expect(html.contains("<pre data-omui-target=\"doc/code\"><code class=\"language-cpp\">if (a &lt; b)\n    run();\n</code></pre>")) << html;
        // With a look: the parts of a text drawn otherwise say how, by the bytes they cover (the link's 9 to 20, the
        // code's 23 to 29); code that holds its button has its box around both.
        {
            sem::Snapshot held = snapshot;
            for (spec::SemanticNode& n : held.nodes)
                if (n.id == "doc/code") n.children = {"doc/code/copy"};
            held.nodes.push_back(node("doc/code/copy", Role::button, "Copy code"));
            sem::Look look;
            look.spans["doc/p"] = {{9, 20, "color:#4f46e5;"}, {23, 29, "font-size:13px;"}};
            look.styles["doc/code"] = "display:block;padding:12px 12px 12px 12px;position:relative;";
            look.styles["doc/code/copy"] = "position:absolute;top:6px;right:6px;";
            const std::string looked = sem::static_html(held, {.look = &look});
            expect(looked.contains("title=\"The guide\" style=\"color:#4f46e5;\">“<em style=\"color:#4f46e5;\">guide</em>”</a> &amp; <code style=\"font-size:13px;\">run it</code></p>")) << looked;
            expect(looked.contains("<div style=\"display:block;padding:12px 12px 12px 12px;position:relative;\">\n  <pre data-omui-target=\"doc/code\"><code class=\"language-cpp\">")) << looked;
            expect(looked.contains("</code></pre>\n  <button data-omui-target=\"doc/code/copy\" style=\"position:absolute;top:6px;right:6px;\"")) << looked;
        }
        expect(html.contains("<ol data-omui-target=\"doc/list\" start=\"3\">") && html.contains("<span data-omui-target=\"doc/list/item/p\">third</span>"))
            << "an ordered list from its start; a tight item's paragraph no block of its own: " << html;
        expect(html.contains("<!-- code: a.cpp#b -->")) << html;
        expect(html.contains("<thead data-omui-target=\"doc/table/head\">") && html.contains("<tbody data-omui-target=\"doc/table/body\">") &&
               html.contains("<th data-omui-target=\"doc/table/head/row/cell\">Size</th>") && html.contains("<td data-omui-target=\"doc/table/body/row/cell\" align=\"right\">1.5</td>"))
            << html;
        expect(html.contains("role=\"code\"><details>more</details></div>")) << "markup the application wrote, as markup: " << html;
        expect(html.contains("&lt;details&gt;more&lt;/details&gt;")) << "markup from elsewhere, as text (S-3): " << html;
    };

    "the look of a drawn frame: each node's layout as CSS says the same, its text's face, size and colour, the view's unstated nodes around theirs"_test = [] {
        RuntimeOptions o;
        o.fonts = &fonts();
        o.size = {320, 200};
        Runtime rt(o);
        rt.mount("main", column({.padding = 8, .gap = 4}, {
                                    text({.id = "title", .text = "Notes", .style = "title", .heading = 1}),
                                    row({.id = "bar", .gap = 12}, {
                                        text({.id = "count", .text = "3 notes"}),
                                        box({.id = "add", .width = 60, .height = 24, .label = "Add a note", .on = {{"press", [](const Value&) {}}}}),
                                    }),
                                }));
        expect(rt.frame({seconds(0), {}}).failure.empty());
        sem::Tree tree(rt);
        const sem::Snapshot snapshot = tree.snapshot();
        const sem::Look look = sem::html_look(rt, snapshot);
        const std::string_view title = look.style("main/title"), add = look.style("main/bar/add");
        const Theme theme = Theme::light();
        expect(title.contains("font-family:'omui-inter',system-ui,sans-serif;") && title.contains(std::format("font-size:{}px;", static_cast<int>(theme.text_style("title").size))) &&
               title.contains("font-weight:650;") && title.contains("color:#0f172a;") && title.contains("line-height:") &&
               title.contains(std::format("letter-spacing:{}px;", theme.text_style("title").letter_spacing)) && !look.style("main/count").contains("letter-spacing"))
            << "the title's text as the frame drew it, the space between its letters too: " << title << " | " << look.style("main/count");
        expect(add.contains("width:60px;") && add.contains("height:24px;")) << add;
        expect(look.fonts.size() >= 2 && look.fonts.front().family == "Inter") << "the faces the frame's text is set in";
        // Its containers: stated or not, each says its layout.
        std::string containers;
        for (const auto& [id, around] : look.wrappers)
            for (const sem::Look::Wrapper& w : around) containers += std::format("[{} {}]", id, w.style);
        const std::string html = sem::static_html(snapshot, {.lang = "en", .look = &look});
        expect((containers + html).contains("display:flex;flex-direction:column;gap:4px;") && (containers + html).contains("padding:8px 8px 8px 8px;")) << "the column: " << containers << html;
        expect((containers + html).contains("gap:12px;")) << "the row: " << containers << html;
        expect(html.starts_with("<div data-omui-target=\"app\" lang=\"en\" data-omui-look=\"omui-inter\"")) << html;
        expect(eq(sem::font_key("JetBrains Mono"), std::string("omui-jetbrainsmono")));
        expect(html.contains(std::format("data-omui-target=\"main/title\" style=\"{}\"", title))) << html;
        // The same nodes, the same text: a look adds nothing a reader reads.
        const std::string plain = sem::static_html(snapshot, {.lang = "en"});
        const auto text_of = [](std::string_view markup) {
            std::string out;
            bool tag = false;
            for (const char c : markup) {
                if (c == '<') tag = true;
                else if (c == '>') tag = false;
                else if (!tag && c != ' ' && c != '\n') out += c;
            }
            return out;
        };
        expect(eq(text_of(html), text_of(plain)));
        for (const spec::SemanticNode& n : snapshot.nodes) expect(html.contains(std::format("data-omui-target=\"{}\"", n.id))) << n.id;
        // Wrappers open and close: as many of one as of the other.
        const auto count = [&](std::string_view what) { std::size_t n = 0; for (auto at = html.find(what); at != std::string::npos; at = html.find(what, at + 1)) ++n; return n; };
        expect(eq(count("<div"), count("</div>"))) << html;
    };

    "a running application's tree projected: every node there by its id, every text the tree's"_test = [] {
        RuntimeOptions o;
        o.fonts = &fonts();
        o.size = {320, 200};
        Runtime rt(o);
        rt.mount("main", column({.padding = 8, .gap = 4}, {
                                    text({.id = "title", .text = "Notes", .style = "title", .heading = 1}),
                                    text({.id = "count", .text = "3 notes"}),
                                    box({.id = "add", .width = 60, .height = 24, .label = "Add a note", .on = {{"press", [](const Value&) {}}}}),
                                }));
        expect(rt.frame({seconds(0), {}}).failure.empty());
        sem::Tree tree(rt);
        const sem::Snapshot snapshot = tree.snapshot();
        const std::string html = sem::static_html(snapshot, {.lang = "en"});
        expect(!snapshot.nodes.empty());
        for (const spec::SemanticNode& n : snapshot.nodes) {
            expect(html.contains(std::format("data-omui-target=\"{}\"", n.id))) << n.id;
            if (const std::string text = sem::element_text(n); !text.empty()) expect(html.contains(text)) << std::format("{}: {}", n.id, text);
            if (!n.name.empty()) expect(html.contains(n.name)) << std::format("{}'s name: {}", n.id, n.name);
        }
        expect(html.contains("Notes") && html.contains("3 notes") && html.contains("Add a note")) << html;
        expect(!html.contains("role=\"none\"")) << "a div says no role already";
        expect(html.contains("<h1 data-omui-target=") && html.contains(">Notes</h1>")) << "a text stated a heading (heading = 1) is the document's h1: " << html;
    };
}};
