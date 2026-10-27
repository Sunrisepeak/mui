module openmui.ui.markdown;

import std;
import openmui.core;
import openmui.view;
import openmui.markdown;
import :markdown_contract;
import :parts;
import :document;

namespace omui {

namespace {

md::Extension extensions_of(const std::vector<std::string>& names) {
    md::Extension e = md::extensions(names);
    return names.empty() ? md::gfm : e;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The task items of the document in order, with their marks' byte offsets.
void tasks(const md::Block& b, std::vector<const md::Block*>& out) {
    if (b.kind == md::Kind::item && b.task != md::Task::none) out.push_back(&b);
    for (const md::Block& c : b.children) tasks(c, out);
}

}  // namespace

Markdown::Markdown() : hub_(std::make_shared<mdview::Hub>()), stream_(md::gfm) {}

Markdown::~Markdown() {
    // Parts outlive nothing of ours but the hub; they must not call back.
    hub_->activate_link = nullptr;
    hub_->toggle_task = nullptr;
}

void Markdown::mounted() {
    hub_->services = &services();
    hub_->document = node();
    hub_->origin = props.origin;
    hub_->base_url = props.base_url;
    hub_->activate_link = [this](const md::Link& l) {
        // A link to a heading of this document scrolls to it as well.
        if (l.href.starts_with('#'))
            if (auto it = hub_->headings.find(std::string_view(l.href).substr(1)); it != hub_->headings.end())
                services().scroll_into_view(it->second);
        emit_link_activated(l.href);
    };
    hub_->toggle_task = [this](std::int32_t i) { (void)toggle(i); };
    reset_stream();
}

void Markdown::reset_stream() {
    ext_ = extensions_of(props.extensions);
    stream_ = md::Stream(ext_);
    stream_.assign(props.source);
}

void Markdown::take(std::string_view source) {
    if (source.size() >= stream_.source().size() && source.starts_with(stream_.source())) {
        if (source.size() == stream_.source().size()) return;
        stream_.append(source.substr(stream_.source().size()));
    } else {
        stream_.assign(source);
    }
}

void Markdown::prop_changed(MarkdownProp p) {
    if (!attached()) return;
    switch (p) {
        case MarkdownProp::source: take(props.source); break;
        case MarkdownProp::extensions:
            if (extensions_of(props.extensions) == ext_) return;
            reset_stream();
            break;
        case MarkdownProp::base_url:
        case MarkdownProp::origin:
            hub_->base_url = props.base_url;
            hub_->origin = props.origin;
            ++generation_;  // every block, built again
            break;
        case MarkdownProp::label: services().invalidate_semantics(node()); return;
    }
    services().recompose(node());
    services().invalidate_semantics(node());
}

void Markdown::changed() {
    write_source(std::string(stream_.source()));
    emit_changed(std::string(stream_.source()));
    services().recompose(node());
    services().invalidate_semantics(node());
}

void Markdown::style(const Theme&, layout::Style& s) const {
    s.direction = layout::Direction::column;
    s.row_gap = 12;
}

void Markdown::restyle() {
    // Shaped text and table columns depend on the theme: every block anew.
    ++generation_;
    if (attached()) services().recompose(node());
}

std::vector<Element> Markdown::compose() {
    const md::Document& doc = stream_.document();
    std::vector<Element> out;
    std::map<std::string, Element, std::less<>> built;
    std::int32_t task = 0;
    for (std::size_t i = 0; i < doc.root.children.size(); ++i) {
        const md::Block& b = doc.root.children[i];
        const std::string id = std::format("b{}", i);
        // A block that did not change is the element it was (no copy of the
        // block, no new node): what streaming leaves alone stays as it is.
        const std::string probe = std::format("{}:{:016x}:{}:{}:{}", id, b.hash, task, generation_, b.anchor);
        auto it = built_.find(probe);
        if (it != built_.end()) {
            out.push_back(it->second);
            built.emplace(probe, it->second);
        } else {
            auto data = mdview::localize(b, doc.links, hub_);
            data->task = task;
            Element e = mdview::element(std::move(data), id);
            e.key = std::format("{}:{}", e.key, generation_);
            built.emplace(probe, e);
            out.push_back(std::move(e));
        }
        task += mdview::count_tasks(b);
    }
    built_ = std::move(built);
    return out;
}

void Markdown::laid_out() {
    // heading_visible: the first heading whose bottom is below the top of the
    // view (the nearest scroll view around the document, else the window).
    if (hub_->headings.empty()) return;
    float top = 0;
    for (NodeId p = services().parent(node()); p; p = services().parent(p))
        if (services().get(p, "vertical").type() == Value::Type::boolean && services().get(p, "y").is_number()) {
            top = services().bounds(p).y;
            break;
        }
    std::string best;
    float best_y = std::numeric_limits<float>::infinity();
    for (const auto& [anchor, n] : hub_->headings) {
        const Rect r = services().bounds(n);
        if (r.bottom() > top && r.y < best_y) {
            best_y = r.y;
            best = anchor;
        }
    }
    if (!best.empty() && best != heading_seen_) {
        heading_seen_ = best;
        emit_heading_visible(best);
    }
}

void Markdown::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (!props.label.empty()) node.name = props.label;
    else
        for (const md::Block& b : stream_.document().root.children)
            if (b.kind == md::Kind::heading) {
                node.name = md::plain_text(b);
                break;
            }
    node.origin = props.origin == MarkdownOrigin::remote ? spec::Origin::remote
                  : props.origin == MarkdownOrigin::app  ? spec::Origin::app
                                                         : spec::Origin::user_content;
}

ActResult Markdown::append(ActContext&, const Value& chunk) {
    stream_.append(chunk.as_string());
    changed();
    return ActResult::done(Value(static_cast<std::int64_t>(stream_.source().size())));
}

ActResult Markdown::scroll_to_anchor(ActContext&, const Value& anchor) {
    std::string_view a = anchor.as_string();
    if (a.starts_with('#')) a.remove_prefix(1);
    auto it = hub_->headings.find(a);
    if (it == hub_->headings.end()) return ActResult::fail(std::format("no heading has the anchor '{}'", a));
    services().scroll_into_view(it->second);
    return ActResult::done(Value(services().path(it->second)));
}

ActResult Markdown::open_link(ActContext&, const Value& href) {
    // Only what the document links to: an agent cannot use the document to
    // reach an address of its own choosing.
    const std::string_view h = href.as_string();
    for (const md::Link& l : stream_.document().links)
        if (l.href == h) {
            hub_->activate_link(l);
            return ActResult::done(Value(l.href));
        }
    return ActResult::fail(std::format("the document has no link to '{}'", h));
}

ActResult Markdown::toggle_task(ActContext&, const Value& index) {
    const auto i = static_cast<std::int32_t>(index.as_int(-1));
    if (!toggle(i)) return ActResult::fail(std::format("the document has no task {}", i));
    std::vector<const md::Block*> all;
    tasks(stream_.document().root, all);
    return ActResult::done(Value(all[static_cast<std::size_t>(i)]->task == md::Task::done));
}

bool Markdown::toggle(std::int32_t index) {
    std::vector<const md::Block*> all;
    tasks(stream_.document().root, all);
    if (index < 0 || static_cast<std::size_t>(index) >= all.size()) return false;
    const std::size_t at = all[static_cast<std::size_t>(index)]->task_offset;
    std::string source(stream_.source());
    if (at >= source.size()) return false;
    const bool checked = source[at] == ' ';
    source[at] = checked ? 'x' : ' ';
    stream_.assign(source);
    changed();
    emit_task_toggled(checked, index);
    return true;
}

ActResult Markdown::copy(ActContext&) {
    const std::string text = md::plain_text(stream_.document().root);
    services().set_clipboard_text(text);
    return ActResult::done(Value(static_cast<std::int64_t>(text.size())));
}

ActResult Markdown::find(ActContext&, const Value& query) {
    const std::string q = lower(query.as_string());
    Value::List hits;
    if (q.empty()) return ActResult::done(Value(std::move(hits)));
    const auto kids = services().children(node());
    const auto& blocks = stream_.document().root.children;
    for (std::size_t i = 0; i < blocks.size() && i < kids.size(); ++i)
        if (lower(md::plain_text(blocks[i])).contains(q)) hits.emplace_back(services().path(kids[i]));
    return ActResult::done(Value(std::move(hits)));
}

}  // namespace omui
