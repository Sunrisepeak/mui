// A block of code that runs (omui.ui.markdown:live): where a provider runs a block's language, a document of the
// application's own shows the example above its source; the example's nodes are in the tree, marked as what the
// block gave (S-16's `output`), and the document the tree states — as Markdown, as HTML read back — is the block
// alone. A document that is not the application's own shows the source, and runs nothing.
import std;

import omui.ui.markdown;
import omui.ui.base;
import omui.semantic;
import omui.host.headless;
import mlibs.compose;
import mlibs.markdown;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

constexpr std::string_view document = "# An example\n\nBefore it.\n\n```demo\nHello\n```\n\nAfter it.\n\n```demo\n\n```\n";

// Runs blocks of the language `demo`: a button that says the block's text; an empty block is wrong.
const MarkdownLive demo{.language = "demo", .make = [](Services&, std::string_view text) -> std::expected<Element, std::string> {
                            while (text.ends_with('\n')) text.remove_suffix(1);
                            if (text.empty()) return std::unexpected(std::string("1:1: nothing to run"));
                            return button({.id = "go", .text = std::format("Say {}", text)});
                        }};

const spec::SemanticNode* by_id(const semantic::Snapshot& snapshot, std::string_view id) {
    const auto found = std::ranges::find(snapshot.nodes, id, &spec::SemanticNode::id);
    return found == snapshot.nodes.end() ? nullptr : &*found;
}

}  // namespace

static UTest cases{"markdown.live", [] {
    "a block that runs: the example above its source in the application's own document, the document still the block alone"_test = [] {
        expect(markdown_live("demo") == nullptr) << "nothing runs a language until a provider is installed";
        expect(mlibs::compose::process().install(markdown_live_slot, demo, "markdown_live_test").has_value());
        expect(markdown_live("demo") == &demo && markdown_live("cpp") == nullptr && markdown_live("") == nullptr);

        Headless host({.level = HeadlessLevel::layout, .size = {640, 800}});
        host.mount("main", markdown({.id = "doc", .origin = MarkdownOrigin::app, .source = std::string(document)}));
        host.settle();
        const semantic::Snapshot own = host.tree().snapshot({});
        const spec::SemanticNode* figure = by_id(own, "main/b2");
        const spec::SemanticNode* example = by_id(own, "main/b2/live");
        const spec::SemanticNode* run = by_id(own, "main/b2/live/go");
        const spec::SemanticNode* source = by_id(own, "main/b2/code");
        expect(figure && figure->role == Role::figure && figure->children == std::vector<std::string>{"main/b2/live", "main/b2/code"}) << "the example, then its source";
        expect(example && example->role == Role::group && example->name == "Example" && example->properties.get("output") && example->properties.get("output")->as_bool());
        expect(run && run->role == Role::button && run->name == "Say Hello") << "the example's own nodes, operated like any";
        expect(source && source->role == Role::code && source->name == "Hello" && source->properties.get("language")->as_string() == "demo");
        // What is wrong with a block's text stands where its example would.
        const spec::SemanticNode* problem = by_id(own, "main/b4/live/problem");
        expect(problem && problem->role == Role::status && problem->name == "1:1: nothing to run");

        // The document the tree states is the source's: an output is no part of it.
        const std::string written = markdown_of(own.nodes);
        expect(written.contains("```demo\nHello\n```") && !written.contains("Say Hello") && !written.contains("nothing to run")) << written;
        expect(mlibs::markdown::to_html(mlibs::markdown::parse(written)) == mlibs::markdown::to_html(mlibs::markdown::parse(document))) << written;
        // And so is its HTML read back: the example is in the page, marked, and left out by a reading of the document.
        // (As a page is written: without the button that copies a block, which works only with the program.)
        semantic::Snapshot page = own;
        for (spec::SemanticNode& node : page.nodes) std::erase_if(node.children, [](const std::string& id) { return id.ends_with("/code/copy"); });
        std::erase_if(page.nodes, [](const spec::SemanticNode& node) { return node.id.ends_with("/code/copy"); });
        const std::string html = semantic::static_html(page);
        expect(html.contains("<figure data-omui-target=\"main/b2\">") && html.contains("data-omui-target=\"main/b2/live\" data-output=\"\" aria-label=\"Example\" role=\"group\"") &&
               html.contains(">Say Hello</button>"))
            << html;
        expect(mlibs::markdown::to_html(mlibs::markdown::from_html(html)) == mlibs::markdown::to_html(mlibs::markdown::parse(document))) << html;

        // What a person typed, or a remote text says, is shown and not run.
        Headless other({.level = HeadlessLevel::layout, .size = {640, 800}});
        other.mount("main", markdown({.id = "doc", .origin = MarkdownOrigin::user_content, .source = std::string(document)}));
        other.settle();
        const semantic::Snapshot typed = other.tree().snapshot({});
        const spec::SemanticNode* plain = by_id(typed, "main/b2");
        expect(plain && plain->role == Role::code && plain->name == "Hello" && !by_id(typed, "main/b2/live")) << "a block of code, as before";
    };
}};
