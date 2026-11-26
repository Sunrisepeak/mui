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
    if (const Value* word = piece.get("t")) {  // a word of a line, where the canvas draws it
        text += word->as_string();
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
        expect(documents == 8u && links > 10u && marked > 10u) << documents << " " << links << " " << marked;
        std::println("    one mapping: {} documents, {} nodes each the same element before and over; {} links, {} texts with marks", documents, nodes, links, marked);
    };

    "a drawn text stands line by line where the canvas draws it: each line's words in its box, a link going on into the next line"_test = [] {
        const auto pages = package() / "templates" / "docs" / "pages";
        std::size_t texts = 0, lines_seen = 0, words_seen = 0, wrapped = 0, parts = 0, anchors = 0, in_tree = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(pages)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
            const std::string name = entry.path().lexically_relative(pages).generic_string();
            // Drawn (the lines are those of the frame painted), in a narrow window so that sentences wrap.
            Headless host({.level = HeadlessLevel::pixels, .size = {420, 4000}});
            host.mount("page", column({.id = "page"}, {markdown({.id = "doc", .origin = MarkdownOrigin::app, .source = read(entry.path())})}));
            host.settle();
            const semantic::Snapshot snapshot = host.tree().snapshot({});
            DomMirror mirror(host.runtime());
            const auto batch = mirror.sync();
            expect(batch.has_value() && batch->get("nodes")) << name;
            if (!batch || !batch->get("nodes")) continue;
            std::unordered_map<std::string, const Value*> entries;
            for (const Value& one : *batch->get("nodes")->list()) entries.emplace(std::string(one.get("id")->as_string()), &one);
            const auto count = [&](this auto const& self, const Value& piece) -> void {
                if (piece.type() == Value::Type::text || piece.get("t")) return;
                if (piece.get("tag")->as_string() == "a") {
                    if (piece.get("node")) ++anchors;
                    else if (piece.get("set") && piece.get("set")->get("data-omui-part")) ++parts;
                }
                if (const Value* inner = piece.get("c"))
                    for (const Value& child : *inner->list()) self(child);
            };
            for (const spec::SemanticNode& node : snapshot.nodes) {
                in_tree += node.role == Role::link;
                const auto made = entries.find(node.id);
                if (made == entries.end()) continue;
                if (made->second->get("tag") && made->second->get("tag")->as_string() == "a" && !made->second->get("inline")) ++anchors;  // a link of its own box
                const Value* lines = made->second->get("lines");
                if (!lines) {
                    if (const Value* content = made->second->get("content"))
                        for (const Value& piece : *content->list()) count(piece);
                    continue;
                }
                ++texts;
                std::string held;
                std::unordered_map<std::string, std::string> placed;
                double below = -1;
                for (const Value& line : *lines->list()) {
                    ++lines_seen;
                    const auto& box = *line.get("box")->list();
                    // Its words go along the line, each inside it (a block of code's long line runs on, as drawn).
                    double along = -1;
                    const auto placed_words = [&](this auto const& self, const Value& piece) -> void {
                        if (piece.type() == Value::Type::text) return;
                        if (piece.get("t")) {
                            ++words_seen;
                            const double x = piece.get("x")->as_double(), w = piece.get("w")->as_double();
                            // The space a line broke at is its last word's and hangs past the line's end (a line's
                            // width is its words'): a third of the line's height is more than any space.
                            const double hangs = piece.get("t")->as_string().ends_with(' ') ? box[3].as_double() / 3 : 0;
                            expect(x >= along - 0.5 && w >= 0 && x >= -0.5 && (node.role == Role::code || x + w <= box[2].as_double() + 0.75 + hangs))
                                << name << ": " << node.id << " a word off its line: " << piece.json() << " of " << line.get("box")->json();
                            along = x;
                            return;
                        }
                        if (const Value* inner = piece.get("c"))
                            for (const Value& child : *inner->list()) self(child);
                    };
                    for (const Value& piece : *line.get("c")->list()) {
                        words(piece, held, placed);
                        count(piece);
                        placed_words(piece);
                    }
                    // A block of code does not wrap: a long line runs past its box and is cut there, on the canvas and here.
                    const bool unwrapped = node.role == Role::code;
                    expect(box[2].as_double() > 0 && box[3].as_double() > 0 && box[0].as_double() >= -1 && box[1].as_double() >= -1 && node.bounds &&
                           (unwrapped || box[0].as_double() + box[2].as_double() <= node.bounds->width + 1.5) && box[1].as_double() + box[3].as_double() <= node.bounds->height + 1.5)
                        << name << ": " << node.id << " a line outside its element: " << line.json();
                    expect(box[1].as_double() > below) << name << ": " << node.id << " its lines go down the page";
                    below = box[1].as_double();
                }
                wrapped += lines->list()->size() > 1;
                // Every word of the text, in order: a line's break is the line's end, not a word.
                std::string said = node.name;
                std::erase(said, '\n');
                std::erase(held, '\n');
                expect(held == said) << name << ": " << node.id << " holds \"" << held << "\", the tree \"" << said << "\"";
            }
        }
        expect(texts > 60u && wrapped > 10u && words_seen > 600u && anchors == in_tree) << texts << " " << wrapped << " " << words_seen << " " << anchors << " " << in_tree;
        std::println("    by lines: {} texts in {} lines ({} of more than one line), {} words each at its place; {} links each one element", texts, lines_seen, wrapped, words_seen, anchors);

        // A link too long for its line: the link itself on its first line, an element like it — the same address,
        // naming the node — where it goes on.
        Headless host({.level = HeadlessLevel::pixels, .size = {260, 600}});
        host.mount("page", column({.id = "page"}, {markdown({.id = "doc", .origin = MarkdownOrigin::app,
                                                           .source = "Read [the guide to every operation of the command line and what each one answers](guide/ops.md) first.\n"})}));
        host.settle();
        DomMirror mirror(host.runtime());
        const auto batch = mirror.sync();
        expect(batch.has_value());
        if (!batch) return;
        std::size_t itself = 0, going_on = 0;
        std::string said;
        for (const Value& one : *batch->get("nodes")->list()) {
            const Value* lines = one.get("lines");
            if (!lines) continue;
            expect(lines->list()->size() >= 3u) << "wrapped: " << one.json();
            for (const Value& line : *lines->list())
                for (const Value& piece : *line.get("c")->list()) {
                    if (piece.type() == Value::Type::text || piece.get("t")) {
                        said += piece.type() == Value::Type::text ? std::string(piece.as_string()) : std::string(piece.get("t")->as_string());
                        continue;
                    }
                    expect(piece.get("tag")->as_string() == "a") << piece.json();
                    if (piece.get("node")) {
                        ++itself;
                        expect(going_on == 0u && !piece.get("set")) << "the link itself first; its attributes are its own entry's";
                    } else {
                        ++going_on;
                        const Value* set = piece.get("set");
                        expect(set && set->get("href") && set->get("href")->as_string() == "guide/ops.md" && set->get("data-omui-part") &&
                               set->get("data-omui-part")->as_string().ends_with("link0"))
                            << piece.json();
                    }
                    for (const Value& inner : *piece.get("c")->list()) said += inner.type() == Value::Type::text ? std::string(inner.as_string()) : std::string(inner.get("t")->as_string());
                }
        }
        expect(itself == 1u && going_on >= 1u) << itself << " " << going_on;
        expect(said == "Read the guide to every operation of the command line and what each one answers first.") << said;

        // A block of code with an empty line: the line that is only its break has no words and no width, and the
        // lines around it stand where they are drawn all the same.
        Headless coded({.level = HeadlessLevel::pixels, .size = {420, 400}});
        coded.mount("page", column({.id = "page"}, {markdown({.id = "doc", .origin = MarkdownOrigin::app, .source = "```cpp\nimport omui;\n\nint main() {}\n```\n"})}));
        coded.settle();
        DomMirror code_layer(coded.runtime());
        const auto code_batch = code_layer.sync();
        std::size_t code_lines = 0;
        if (code_batch && code_batch->get("nodes"))
            for (const Value& one : *code_batch->get("nodes")->list())
                if (one.get("tag") && one.get("tag")->as_string() == "pre" && one.get("lines")) code_lines = one.get("lines")->list()->size();
        expect(code_lines >= 2u) << "code with an empty line, by its lines: " << code_lines;
    };
}};
