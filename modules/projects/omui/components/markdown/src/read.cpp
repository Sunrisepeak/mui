module omui.ui.markdown;

import std;
import omui.core;
import mlibs.markdown;
import :read;

namespace omui {

namespace {

namespace md = mlibs::markdown;
using Node = spec::SemanticNode;

struct Reader {
    std::unordered_map<std::string_view, const Node*> by_id;
    md::Document                                      document;

    [[nodiscard]] const Node* find(std::string_view id) const {
        const auto found = by_id.find(id);
        return found == by_id.end() ? nullptr : found->second;
    }

    static std::string_view text_of(const Node& n, std::string_view key) {
        const Value* stated = n.properties.get(key);
        return stated && stated->type() == Value::Type::text ? stated->as_string() : std::string_view{};
    }

    std::uint32_t link(std::string href, std::string title) {
        document.links.push_back({std::move(href), std::move(title), false});
        return static_cast<std::uint32_t>(document.links.size() - 1);
    }

    // A node's text and marks as a leaf's inline content: each range an opening and a closing at its bytes, an outer
    // one opened first and closed last; a line feed in the text is a hard break (S-17).
    md::Inline inline_of(const Node& n) {
        md::Inline out;
        out.text = n.name;
        struct Event {
            std::size_t   at;
            bool          open;
            std::size_t   mark;  // its index: opens in the list's order, closes against it
            md::Span      span;
            std::uint32_t ref;
        };
        std::vector<Event> events;
        for (std::size_t index = 0; index < n.marks.size(); ++index) {
            const spec::Mark& mark = n.marks[index];
            md::Span span = md::Span::emphasis;
            std::uint32_t ref = md::no_ref;
            switch (mark.kind) {
                case spec::MarkKind::emphasis: span = md::Span::emphasis; break;
                case spec::MarkKind::strong: span = md::Span::strong; break;
                case spec::MarkKind::code: span = md::Span::code; break;
                case spec::MarkKind::deletion: span = md::Span::strikethrough; break;
                case spec::MarkKind::underline: span = md::Span::underline; break;
                case spec::MarkKind::raw: span = md::Span::html; break;
                case spec::MarkKind::math: span = md::Span::math; break;
                case spec::MarkKind::math_display: span = md::Span::display_math; break;
                case spec::MarkKind::link:
                case spec::MarkKind::image: {
                    const Node* child = find(mark.node);
                    if (!child) continue;
                    const bool image = mark.kind == spec::MarkKind::image;
                    span = image ? md::Span::image : md::Span::link;
                    ref = link(std::string(text_of(*child, image ? "source" : "href")), std::string(text_of(*child, "title")));
                    break;
                }
                default: continue;  // a meaning Markdown has no spelling for: its text stays
            }
            const std::size_t from = spec::byte_of_code_point(n.name, mark.start), to = spec::byte_of_code_point(n.name, mark.end);
            events.push_back({from, true, index, span, ref});
            events.push_back({to, false, index, span, ref});
        }
        // At one place: what closes before what opens; the innermost closes first, the outermost opens first.
        std::ranges::stable_sort(events, [](const Event& a, const Event& b) {
            if (a.at != b.at) return a.at < b.at;
            if (a.open != b.open) return !a.open;
            return a.open ? a.mark < b.mark : a.mark > b.mark;
        });
        std::size_t event = 0;
        const auto marks_until = [&](std::size_t at) {
            for (; event < events.size() && events[event].at <= at; ++event)
                out.marks.push_back({static_cast<std::uint32_t>(events[event].at), events[event].open ? md::Op::open : md::Op::close, events[event].span, events[event].ref});
        };
        for (std::size_t at = n.name.find('\n'); at != std::string::npos; at = n.name.find('\n', at + 1)) {
            marks_until(at);
            out.marks.push_back({static_cast<std::uint32_t>(at), md::Op::hard_break, md::Span::emphasis, md::no_ref});
        }
        marks_until(n.name.size());
        return out;
    }

    void children_into(const Node& n, md::Block& into) {
        for (const std::string& id : n.children)
            if (const Node* child = find(id)) block(*child, into);
    }

    // A node as the blocks it states, added to `into`.
    void block(const Node& n, md::Block& into) {
        // What a block gave when it ran (S-16's `output`: a live example) is shown, and no part of the document's
        // source: the block says it.
        if (const Value* output = n.properties.get("output"); output && output->type() == Value::Type::boolean && output->as_bool()) return;
        if (const Value* comment = n.properties.get("comment")) {
            md::Block made{.kind = md::Kind::html};
            made.content.text = std::format("<!--{}-->\n", comment->as_string());
            into.children.push_back(std::move(made));
            return;
        }
        switch (n.role) {
            case Role::heading: {
                md::Block made{.kind = md::Kind::heading};
                const Value* level = n.properties.get("level");
                made.level = static_cast<std::uint8_t>(std::clamp<std::int64_t>(level && level->is_number() ? level->as_int() : n.value.is_number() ? n.value.as_int() : 2, 1, 6));
                made.anchor = std::string(text_of(n, "anchor"));
                made.content = inline_of(n);
                into.children.push_back(std::move(made));
                return;
            }
            case Role::paragraph:
            case Role::text:
            case Role::label: {
                if (n.name.empty()) return children_into(n, into);
                md::Block made{.kind = md::Kind::paragraph};
                made.content = inline_of(n);
                into.children.push_back(std::move(made));
                return;
            }
            case Role::blockquote: {
                md::Block made{.kind = md::Kind::quote};
                children_into(n, made);
                into.children.push_back(std::move(made));
                return;
            }
            case Role::list: {
                const Value* ordered = n.properties.get("ordered");
                const Value* tight = n.properties.get("tight");
                const Value* start = n.properties.get("start");
                md::Block made{.kind = ordered && ordered->as_bool() ? md::Kind::ordered_list : md::Kind::bullet_list};
                made.tight = tight && tight->as_bool();
                if (start && start->is_number()) made.start = static_cast<std::uint32_t>(std::max<std::int64_t>(0, start->as_int()));
                children_into(n, made);
                into.children.push_back(std::move(made));
                return;
            }
            case Role::list_item: {
                md::Block made{.kind = md::Kind::item};
                // A task's item holds its checkbox: what it says is the item's mark.
                for (const std::string& id : n.children)
                    if (const Node* child = find(id); child && child->role == Role::checkbox)
                        made.task = child->states.has(State::checked) ? md::Task::done : md::Task::open;
                children_into(n, made);
                into.children.push_back(std::move(made));
                return;
            }
            case Role::code: {
                const bool raw = !text_of(n, "raw").empty();
                md::Block made{.kind = raw ? md::Kind::html : md::Kind::code};
                made.content.text = n.name + "\n";
                if (!raw) {
                    made.language = std::string(text_of(n, "language"));
                    made.info = made.language;
                    made.fence = '`';
                }
                into.children.push_back(std::move(made));
                return;
            }
            case Role::separator:
                into.children.push_back(md::Block{.kind = md::Kind::rule});
                return;
            case Role::image: {
                const std::string_view source = text_of(n, "source");
                if (source.empty()) return;
                md::Block made{.kind = md::Kind::paragraph};
                made.content.text = n.name;
                const std::uint32_t ref = link(std::string(source), std::string(text_of(n, "title")));
                made.content.marks = {{0, md::Op::open, md::Span::image, ref}, {static_cast<std::uint32_t>(n.name.size()), md::Op::close, md::Span::image, ref}};
                into.children.push_back(std::move(made));
                return;
            }
            case Role::link: {
                // A link that stands alone (no text's mark places it): a paragraph of it.
                const std::string_view href = text_of(n, "href");
                if (href.empty() || n.name.empty()) return;
                md::Block made{.kind = md::Kind::paragraph};
                made.content.text = n.name;
                const std::uint32_t ref = link(std::string(href), std::string(text_of(n, "title")));
                made.content.marks = {{0, md::Op::open, md::Span::link, ref}, {static_cast<std::uint32_t>(n.name.size()), md::Op::close, md::Span::link, ref}};
                into.children.push_back(std::move(made));
                return;
            }
            case Role::table: {
                md::Block made{.kind = md::Kind::table};
                for (const std::string& id : n.children) {
                    const Node* section = find(id);
                    if (!section) continue;
                    md::Block rows{.kind = md::Kind::table_body};
                    for (const std::string& row_id : section->children) {
                        const Node* row = find(row_id);
                        if (!row || row->role != Role::row) continue;
                        md::Block cells{.kind = md::Kind::table_row};
                        for (const std::string& cell_id : row->children) {
                            const Node* cell = find(cell_id);
                            if (!cell) continue;
                            md::Block one{.kind = md::Kind::table_cell};
                            one.header = cell->role == Role::column_header;
                            const std::string_view align = text_of(*cell, "align");
                            one.align = align == "left" ? md::Align::left : align == "center" ? md::Align::center : align == "right" ? md::Align::right : md::Align::none;
                            one.content = inline_of(*cell);
                            if (one.header) rows.kind = md::Kind::table_head;
                            cells.children.push_back(std::move(one));
                        }
                        rows.children.push_back(std::move(cells));
                    }
                    if (!rows.children.empty()) made.children.push_back(std::move(rows));
                }
                into.children.push_back(std::move(made));
                return;
            }
            // What operates the document is no part of it.
            case Role::button:
            case Role::toggle_button:
            case Role::checkbox:
            case Role::scroll_bar: return;
            default: children_into(n, into);  // a container: what it holds
        }
    }
};

}  // namespace

mlibs::markdown::Document document_of(std::span<const spec::SemanticNode> nodes) {
    Reader reader;
    if (nodes.empty()) return std::move(reader.document);
    // A child a text's mark places is written where the mark is (found by its id), not again as a block: the copy of
    // the nodes read from has it in no node's children.
    std::unordered_set<std::string_view> placed;
    for (const Node& n : nodes)
        for (const spec::Mark& mark : n.marks)
            if (!mark.node.empty()) placed.insert(mark.node);
    std::vector<Node> read(nodes.begin(), nodes.end());
    for (Node& n : read) std::erase_if(n.children, [&](const std::string& id) { return placed.contains(id); });
    for (const Node& n : read) reader.by_id.emplace(n.id, &n);
    reader.block(read.front(), reader.document.root);
    return std::move(reader.document);
}

std::string markdown_of(std::span<const spec::SemanticNode> nodes) { return mlibs::markdown::to_markdown(document_of(nodes)); }

}  // namespace omui
