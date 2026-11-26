// The page before the program and the layer over the canvas are the same elements (0.1.0 plan UI3, D-286: one
// mapping, three moments). For every page of the minimal documents: each node's element in the static projection
// and in the Web host's first batch, where each link leads, and the words each element holds.
import std;
import omui.core;
import omui.view;
import omui.semantic;
import omui.host.headless;
import omui.host.web;
import omui.ui.markdown;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

std::filesystem::path package() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path(); }

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// The tag the projection writes for a node: `<tag data-omui-target="id"`.
std::string projected(std::string_view html, std::string_view id) {
    const std::size_t at = html.find(std::format(" data-omui-target=\"{}\"", id));
    if (at == std::string_view::npos) return {};
    const std::size_t open = html.rfind('<', at);
    return std::string(html.substr(open + 1, at - open - 1));
}

// The words a batch's pieces hold, a line's break a line feed; and the child nodes they place, each with its tag.
void words(const Value& piece, std::string& text, std::unordered_map<std::string, std::string>& placed) {
    if (piece.type() == Value::Type::text) {
        text += piece.as_string();
        return;
    }
    const std::string tag(piece.get("tag")->as_string());
    if (tag == "br") text += '\n';
    if (const Value* node = piece.get("node")) placed.emplace(std::string(node->as_string()), tag);
    if (const Value* inner = piece.get("c"))
        for (const Value& child : *inner->list()) words(child, text, placed);
}

}  // namespace

static UTest cases{"oweb.layer", [] {
    "one mapping: a node is the same element in the page before the program and in the layer over the canvas"_test = [] {
        const auto pages = package() / "templates" / "docs" / "pages";
        std::size_t documents = 0, nodes = 0, links = 0, marked = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(pages)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
            ++documents;
            const std::string name = entry.path().lexically_relative(pages).generic_string();
            Headless host({.level = HeadlessLevel::layout, .size = {960, 1200}});
            host.mount("page", column({.id = "page"}, {markdown({.id = "doc", .origin = MarkdownOrigin::app, .source = read(entry.path())})}));
            host.settle();
            const semantic::Snapshot snapshot = host.tree().snapshot({});
            const std::string html = semantic::static_html(snapshot, {});

            DomMirror mirror(host.runtime());
            const auto batch = mirror.sync();
            expect(batch.has_value() && batch->get("nodes")) << name;
            if (!batch || !batch->get("nodes")) continue;
            std::unordered_map<std::string, const Value*> entries;
            std::unordered_map<std::string, std::string> placed;  // a node standing in a text: the tag its text gives it
            std::unordered_map<std::string, std::string> held;    // a node's own words in the layer
            for (const Value& one : *batch->get("nodes")->list()) {
                const std::string id(one.get("id")->as_string());
                entries.emplace(id, &one);
                std::string text;
                if (const Value* plain = one.get("text")) text = plain->as_string();
                if (const Value* content = one.get("content"))
                    for (const Value& piece : *content->list()) words(piece, text, placed);
                held.emplace(id, std::move(text));
            }
            expect(!mirror.sync().has_value()) << name << ": nothing changed, nothing sent";

            for (const spec::SemanticNode& node : snapshot.nodes) {
                if (node.properties.get("comment")) continue;  // the source's, shown by nothing
                ++nodes;
                const auto entry_of = entries.find(node.id);
                expect(entry_of != entries.end()) << name << ": " << node.id << " has no element in the layer";
                if (entry_of == entries.end()) continue;
                const Value& made = *entry_of->second;
                const bool inlined = placed.contains(node.id);
                const std::string live = inlined ? placed.at(node.id) : made.get("tag") ? std::string(made.get("tag")->as_string()) : std::string{};
                std::string before = projected(html, node.id);
                // What differs by design: what a person operates and what the canvas shows keep their ARIA form over the
                // canvas; a paragraph of a list set close is no block of its own in a document.
                if (before == "button" || before == "input") before = "div";
                if (before == "img") before = inlined ? "span" : "div";
                if (before == "span" && node.role == Role::paragraph) before = "p";
                expect(live == before) << name << ": " << node.id << " is <" << before << "> in the page and <" << live << "> in the layer";
                expect(inlined == (made.get("inline") != nullptr) && inlined != (made.get("box") != nullptr || !node.bounds)) << name << ": " << node.id << " stands in a text or has its box";
                if (node.role == Role::link) {
                    ++links;
                    const Value* href = node.properties.get("href");
                    const Value* set = made.get("set");
                    expect(live == "a" && href && set && set->get("href") && set->get("href")->as_string() == href->as_string()) << name << ": " << node.id << " leads where the tree says";
                }
                if (!node.marks.empty() && !inlined && live != "pre") {
                    ++marked;
                    expect(held.at(node.id) == node.name) << name << ": " << node.id << " holds \"" << held.at(node.id) << "\", the tree \"" << node.name << "\"";
                }
                if ((live == "p" || live.starts_with('h')) && node.marks.empty() && !inlined) expect(held.at(node.id) == node.name) << name << ": " << node.id;
            }
            // As many links in the layer as the page had.
            std::size_t anchors = 0;
            for (std::size_t at = html.find("<a "); at != std::string::npos; at = html.find("<a ", at + 1)) ++anchors;
            std::size_t in_tree = 0;
            for (const spec::SemanticNode& node : snapshot.nodes) in_tree += node.role == Role::link;
            expect(anchors == in_tree) << name << ": " << anchors << " in the page, " << in_tree << " in the tree";
        }
        expect(documents == 7u && links > 10u && marked > 10u) << documents << " " << links << " " << marked;
        std::println("    one mapping: {} documents, {} nodes each the same element before and over; {} links, {} texts with marks", documents, nodes, links, marked);
    };
}};
