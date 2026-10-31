// openmui.ui.markdown:document — the Markdown component.
//
// Its source is parsed by openmui.markdown into a document of blocks, and
// every block becomes a node (openmui.ui.markdown:parts): the semantic tree
// is the document's structure, links are link nodes whose activation reaches
// outside the application (an agent states its answer), tasks are checkboxes
// that rewrite their mark in the source. A source that extends the current
// one, or `append`, streams: only the blocks after the last stable boundary
// are parsed again, and every unchanged block keeps its node, shaped text
// and layout (blocks are keyed by their content).
//
//     markdown({.id = "answer", .source = text, .origin = MarkdownOrigin::remote,
//               .on_link_activated = [](std::string href) { open(href); }});
export module openmui.ui.markdown:document;

import std;
import openmui.core;
import openmui.view;
import openmui.markdown;
import :markdown_contract;
import :parts;

export namespace omui {

class Markdown : public MarkdownContract<Markdown> {
public:
    Markdown();
    ~Markdown();

    void mounted();
    void prop_changed(MarkdownProp p);
    void style(const Theme& theme, layout::Style& s) const;
    void restyle();
    std::vector<Element> compose();
    void laid_out();
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult append(ActContext& ctx, const Value& chunk);
    ActResult scroll_to_anchor(ActContext& ctx, const Value& anchor);
    ActResult open_link(ActContext& ctx, const Value& href);
    ActResult toggle_task(ActContext& ctx, const Value& index);
    ActResult copy(ActContext& ctx);
    ActResult find(ActContext& ctx, const Value& query);

    // The document as parsed, and its source.
    [[nodiscard]] const md::Document& document() const noexcept { return stream_.document(); }
    [[nodiscard]] std::string_view source() const noexcept { return stream_.source(); }
    // Bytes the last change parsed (streaming re-parses only the tail).
    [[nodiscard]] std::size_t last_parsed() const noexcept { return stream_.last_parsed(); }

private:
    void reset_stream();
    void take(std::string_view source);  // a new value of `source`: appended when it extends the current one
    void changed();                      // after the document changed: rebuild the blocks, tell the binding
    bool toggle(std::int32_t index);

    std::shared_ptr<mdview::Hub> hub_;
    md::Stream                   stream_;
    md::Extension                ext_ = md::gfm;
    std::uint64_t                generation_ = 0;  // bumped when every block must be built again (theme, base, origin)
    std::map<std::string, Element, std::less<>> built_;  // the last blocks' elements, by key
    std::string                  heading_seen_;
};

// Registers the component by name, for view documents and immediate mode.
void register_markdown_component();

}  // namespace omui
