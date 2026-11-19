// The semantic tree as HTML (D-236, G2): the static projection a reader without JavaScript reads, its text the tree's,
// its elements what HTML has for each role, the same attributes the Web host's live mirror gives them.
import std;
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
        mlibs::text::FontCollection c;
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

    "a running application's tree projected: every node there by its id, every text the tree's"_test = [] {
        RuntimeOptions o;
        o.fonts = &fonts();
        o.size = {320, 200};
        Runtime rt(o);
        rt.mount("main", column({.padding = 8, .gap = 4}, {
                                    text({.id = "title", .text = "Notes", .style = "title"}),
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
    };
}};
