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
        expect(html.contains("<ol data-omui-target=\"doc/list\" start=\"3\">") && html.contains("<span data-omui-target=\"doc/list/item/p\">third</span>"))
            << "an ordered list from its start; a tight item's paragraph no block of its own: " << html;
        expect(html.contains("<!-- code: a.cpp#b -->")) << html;
        expect(html.contains("<thead data-omui-target=\"doc/table/head\">") && html.contains("<tbody data-omui-target=\"doc/table/body\">") &&
               html.contains("<th data-omui-target=\"doc/table/head/row/cell\">Size</th>") && html.contains("<td data-omui-target=\"doc/table/body/row/cell\" align=\"right\">1.5</td>"))
            << html;
        expect(html.contains("role=\"code\"><details>more</details></div>")) << "markup the application wrote, as markup: " << html;
        expect(html.contains("&lt;details&gt;more&lt;/details&gt;")) << "markup from elsewhere, as text (S-3): " << html;
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
