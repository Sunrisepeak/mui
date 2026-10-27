module openmui.ui.markdown;

import std;
import openmui.core;
import openmui.view;
import openmui.markdown;
import :markdown_contract;
import :parts;

namespace omui::mdview {

namespace {

// ── the document's look, from the theme ──

constexpr float block_gap = 12;   // between the blocks of the document, a quote, a loose item
constexpr float tight_gap = 4;    // between the items of a tight list
constexpr float bullet_w = 22;    // an item's marker column
constexpr float quote_pad = 14;
constexpr float code_pad = 12;
constexpr float cell_pad_x = 10, cell_pad_y = 6;

typeset::TextStyle from_token(const Theme& theme, std::string_view token) {
    const TextToken& t = theme.text_style(token);
    typeset::TextStyle s;
    s.families = t.families;
    s.size = t.size;
    s.weight = t.weight;
    s.line_height = t.line_height;
    s.letter_spacing = t.letter_spacing;
    s.color = theme.color("text");
    return s;
}

// Paths from and to UTF-8 text, never through the code page (lint `paths`).
std::filesystem::path utf8_path(std::string_view s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string generic_utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

bool linky(md::Span s) { return s == md::Span::link || s == md::Span::image || s == md::Span::wikilink; }

// An inline's styled ranges: each run's spans applied over the block's style.
std::vector<typeset::StyledRange> ranges_of(const Theme& theme, const md::Inline& in, const typeset::TextStyle& base) {
    std::vector<typeset::StyledRange> out;
    const typeset::TextStyle code = from_token(theme, "code");
    for (const md::Run& r : md::runs(in)) {
        if (r.spans == 0) continue;
        auto has = [&](md::Span s) { return (r.spans & md::bit(s)) != 0; };
        typeset::TextStyle s = base;
        if (has(md::Span::strong)) s.weight = std::min(900.0f, std::max(base.weight + 300, 700.0f));
        if (has(md::Span::emphasis)) s.slant = typeset::Slant::italic;
        if (has(md::Span::code) || has(md::Span::math) || has(md::Span::display_math) || has(md::Span::html)) {
            s.families = code.families;
            s.size = base.size * 0.9f;
        }
        if (has(md::Span::html)) s.color = theme.color("text_muted");
        if (has(md::Span::strikethrough)) {
            s.strikethrough = true;
            s.color = theme.color("text_muted");
        }
        if (has(md::Span::underline)) s.underline = true;
        if (has(md::Span::link) || has(md::Span::wikilink)) {
            s.color = theme.color("accent");
            s.underline = true;
        }
        if (has(md::Span::image)) {
            s.slant = typeset::Slant::italic;
            s.color = theme.color("text_muted");
        }
        out.push_back({r.begin, r.end, std::move(s)});
    }
    return out;
}

// The link-like spans of an inline, in order: [begin, end) of their text and
// the link they refer to.
struct Inl {
    std::uint32_t begin, end, ref;
    bool          image;
};
std::vector<Inl> link_spans(const md::Inline& in) {
    std::vector<Inl> out;
    std::vector<std::size_t> open;
    for (const md::Mark& m : in.marks) {
        if (!linky(m.span)) continue;
        if (m.op == md::Op::open) {
            open.push_back(out.size());
            out.push_back({m.at, m.at, m.ref, m.span == md::Span::image});
        } else if (m.op == md::Op::close && !open.empty()) {
            out[open.back()].end = m.at;
            open.pop_back();
        }
    }
    return out;
}

// A paragraph that is one image and nothing else, with a source on disk.
std::optional<std::pair<std::string, std::string>> lone_image(const Data& d) {
    const md::Block& b = d.block;
    if (b.kind != md::Kind::paragraph) return std::nullopt;
    const auto& m = b.content.marks;
    if (m.size() < 2 || m.front().op != md::Op::open || m.front().span != md::Span::image || m.front().at != 0) return std::nullopt;
    if (m.back().op != md::Op::close || m.back().span != md::Span::image || m.back().at != b.content.text.size()) return std::nullopt;
    if (m.front().ref >= d.links.size()) return std::nullopt;
    std::string src = d.links[m.front().ref].href;
    if (src.find("://") != std::string::npos || src.starts_with("data:")) return std::nullopt;  // not a file the runtime can read
    if (!d.hub->base_url.empty() && !std::filesystem::path(src).is_absolute())
        src = generic_utf8((utf8_path(d.hub->base_url) / utf8_path(src)).lexically_normal());
    return std::pair{std::move(src), b.content.text};
}

Role role_of(const md::Block& b) {
    switch (b.kind) {
        case md::Kind::heading: return Role::heading;
        case md::Kind::table_cell: return b.header ? Role::column_header : Role::cell;
        case md::Kind::quote: return Role::blockquote;
        case md::Kind::bullet_list:
        case md::Kind::ordered_list: return Role::list;
        case md::Kind::item: return Role::list_item;
        case md::Kind::table: return Role::table;
        case md::Kind::table_row: return Role::row;
        case md::Kind::html: return Role::code;
        case md::Kind::table_head:
        case md::Kind::table_body: return Role::group;
        default: return Role::paragraph;
    }
}

// The origin of what the document shows (remote text is marked so on every node).
spec::Origin origin_of(const Hub& h) {
    switch (h.origin) {
        case MarkdownOrigin::app: return spec::Origin::app;
        case MarkdownOrigin::remote: return spec::Origin::remote;
        default: return spec::Origin::user_content;
    }
}

template <class C>
Element make(std::shared_ptr<Data> data, std::string id, std::string key) {
    Element e;
    e.type = &component_type<C>();
    e.id = std::move(id);
    e.key = std::move(key);
    e.configure = [data = std::move(data)](void* p) mutable { static_cast<C*>(p)->data = std::move(data); };
    return e;
}

std::string key_of(const Data& d, std::string_view id) {
    // The content and what around it changes how it looks or what it does.
    std::uint64_t h = d.block.hash;
    auto mix = [&](std::uint64_t v) { h = (h ^ v) * 0x100000001b3ull; };
    mix(d.tight);
    mix(d.number);
    mix(static_cast<std::uint64_t>(static_cast<unsigned char>(d.bullet)));
    mix(static_cast<std::uint64_t>(d.task + 1));
    for (char c : d.block.anchor) mix(static_cast<unsigned char>(c));
    return std::format("{}:{:016x}", id, h);
}

void localize_into(md::Block& b, std::span<const md::Link> links, std::vector<md::Link>& out, std::map<std::uint32_t, std::uint32_t>& seen) {
    for (md::Mark& m : b.content.marks) {
        if (m.ref == md::no_ref || m.ref >= links.size()) continue;
        auto [it, fresh] = seen.try_emplace(m.ref, static_cast<std::uint32_t>(out.size()));
        if (fresh) out.push_back(links[m.ref]);
        m.ref = it->second;
    }
    for (md::Block& c : b.children) localize_into(c, links, out, seen);
}

}  // namespace

typeset::TextStyle text_style(const Theme& theme, const md::Block& b) {
    switch (b.kind) {
        case md::Kind::heading: {
            typeset::TextStyle s = from_token(theme, b.level == 1 ? "title" : b.level == 2 ? "heading" : "body");
            if (b.level >= 3) {
                s.weight = 600;
                s.size = b.level == 3 ? 17 : b.level == 4 ? 15 : 14;
                if (b.level == 6) s.color = theme.color("text_muted");
            }
            return s;
        }
        case md::Kind::code:
        case md::Kind::html: {
            typeset::TextStyle s = from_token(theme, "code");
            if (b.kind == md::Kind::html) s.color = theme.color("text_muted");
            return s;
        }
        case md::Kind::table_cell: {
            typeset::TextStyle s = from_token(theme, "body");
            if (b.header) s.weight = 600;
            return s;
        }
        default: return from_token(theme, "body");
    }
}

std::shared_ptr<Data> localize(const md::Block& block, std::span<const md::Link> links, std::shared_ptr<Hub> hub) {
    auto d = std::make_shared<Data>();
    d->block = block;
    std::map<std::uint32_t, std::uint32_t> seen;
    localize_into(d->block, links, d->links, seen);
    d->hub = std::move(hub);
    return d;
}

std::int32_t count_tasks(const md::Block& b) {
    std::int32_t n = b.kind == md::Kind::item && b.task != md::Task::none ? 1 : 0;
    for (const md::Block& c : b.children) n += count_tasks(c);
    return n;
}

Element element(std::shared_ptr<Data> data, std::string id) {
    const std::string key = key_of(*data, id);
    switch (data->block.kind) {
        case md::Kind::paragraph:
            if (auto img = lone_image(*data)) {
                Element e = image({.id = id, .source = img->first, .alt = img->second});
                e.key = key;
                e.prop("max_width", Source{Value("100%"), {}, {}});
                return e;
            }
            [[fallthrough]];
        case md::Kind::heading:
        case md::Kind::table_cell:
        case md::Kind::html: return make<Text>(std::move(data), id, key);
        case md::Kind::code: return make<Code>(std::move(data), id, key);
        case md::Kind::rule: return make<Rule>(std::move(data), id, key);
        default: return make<Group>(std::move(data), id, key);
    }
}

void focus_ring(PaintContext& ctx, Rect r, float radius) {
    ctx.list.stroke_round_rect({r.x - 2, r.y - 2, r.width + 4, r.height + 4}, CornerRadii::all(radius + 2), 2,
                               render::Paint::solid(ctx.theme.color("focus")));
}

// ── Text ──

void Text::mount(Services& s, NodeId n) {
    services_ = &s;
    node_ = n;
    if (data && data->block.kind == md::Kind::heading && !data->block.anchor.empty()) data->hub->headings[data->block.anchor] = n;
}

Text::~Text() {
    if (data && data->block.kind == md::Kind::heading && data->hub) {
        auto it = data->hub->headings.find(data->block.anchor);
        if (it != data->hub->headings.end() && it->second == node_) data->hub->headings.erase(it);
    }
}

void Text::style(const Theme&, layout::Style& s) const {
    if (!data) return;
    const md::Block& b = data->block;
    if (b.kind == md::Kind::heading) s.margin.top = b.level <= 2 ? 8.0f : 4.0f;
    if (b.kind == md::Kind::table_cell) {
        s.padding = Insets{cell_pad_y, cell_pad_x, cell_pad_y, cell_pad_x};
        s.border.bottom = 1;
    }
}

typeset::Paragraph& Text::shaped(Services& s) const {
    if (!paragraph_) {
        const Theme& theme = s.theme();
        typeset::ParagraphStyle ps;
        ps.style = text_style(theme, data->block);
        if (data->block.kind == md::Kind::table_cell) {
            if (data->block.align == md::Align::center) ps.align = typeset::TextAlign::center;
            else if (data->block.align == md::Align::right) ps.align = typeset::TextAlign::end;
        }
        std::string text = data->block.content.text;
        if (data->block.kind == md::Kind::html)
            while (text.ends_with('\n')) text.pop_back();
        paragraph_.emplace(s.fonts(), std::move(text), ps, ranges_of(theme, data->block.content, ps.style));
        laid_ = -1;
    }
    return *paragraph_;
}

void Text::lay(float width) const {
    if (width != laid_) {
        paragraph_->layout(width);
        laid_ = width;
    }
}

Size Text::measure(MeasureContext& m, std::optional<float> w, std::optional<float>, layout::Available aw, layout::Available) {
    typeset::Paragraph& p = shaped(m.services);
    float width = std::numeric_limits<float>::infinity();
    if (w) width = *w + 1.0f / 1024;
    else if (aw.kind == layout::Available::Kind::min_content) width = p.min_intrinsic_width() + 1.0f / 1024;
    else if (aw.is_definite()) width = aw.value + 1.0f / 1024;
    lay(width);
    const Size s = p.size();
    return {w ? *w : std::ceil(s.width), std::ceil(s.height)};
}

float Text::baseline(MeasureContext& m, Size content) {
    typeset::Paragraph& p = shaped(m.services);
    lay(content.width + 1.0f / 1024);
    return p.first_baseline();
}

std::vector<Rect> Text::rects(std::uint32_t begin, std::uint32_t end) const {
    if (!paragraph_) return {};
    std::vector<Rect> out = paragraph_->selection(begin, end);
    for (Rect& r : out) r = r.offset({origin_.x, origin_.y});
    return out;
}

void Text::paint(PaintContext& ctx) const {
    const md::Block& b = data->block;
    const Theme& th = ctx.theme;
    if (b.kind == md::Kind::table_cell) {
        if (b.header) ctx.list.fill_rect({0, 0, ctx.size.width, ctx.size.height}, th.color("surface_variant"));
        ctx.list.fill_rect({0, ctx.size.height - 1, ctx.size.width, 1}, th.color("border"));
    }
    typeset::Paragraph& p = shaped(ctx.services);
    lay(ctx.content.width + 1.0f / 1024);
    origin_ = ctx.content.origin();
    // Inline code sits on a tinted, rounded background.
    for (const md::Run& r : md::runs(b.content)) {
        if ((r.spans & md::bit(md::Span::code)) == 0) continue;
        for (const Rect& rr : p.selection(r.begin, r.end))
            ctx.list.fill_round_rect({origin_.x + rr.x - 2, origin_.y + rr.y + 1, rr.width + 4, rr.height - 2}, CornerRadii::all(4),
                                     render::Paint::solid(th.color("surface_variant")));
    }
    p.paint(ctx.list, origin_);
}

void Text::semantics(SemanticsContext&, spec::SemanticNode& n) const {
    const md::Block& b = data->block;
    n.role = role_of(b);
    if (n.name.empty()) n.name = md::plain_text(b);
    if (b.kind == md::Kind::heading) {
        n.value = Value(static_cast<std::int64_t>(b.level));
        n.description = b.anchor;  // the anchor scroll_to_anchor and links (#anchor) use
    }
    n.origin = origin_of(*data->hub);
}

std::vector<Element> Text::compose() {
    std::vector<Element> out;
    link_ranges_.clear();
    placed_.clear();
    const auto spans = link_spans(data->block.content);
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const Inl& s = spans[i];
        if (s.ref >= data->links.size()) continue;
        Element e;
        e.type = &component_type<Link>();
        e.id = std::format("{}{}", s.image ? "img" : "link", i);
        e.key = e.id;
        auto d = data;
        const std::size_t ref = s.ref;
        const bool image = s.image;
        e.configure = [d, ref, image](void* p) {
            auto* l = static_cast<Link*>(p);
            l->data = d;
            l->index = ref;
            l->image = image;
        };
        e.prop("position", Source{Value("absolute"), {}, {}});
        e.prop("left", Source{Value(0.0), {}, {}});
        e.prop("top", Source{Value(0.0), {}, {}});
        e.prop("width", Source{Value(0.0), {}, {}});
        e.prop("height", Source{Value(0.0), {}, {}});
        if (!image) {
            // Following a link reaches outside the application (S-1); a person's
            // own click is the answer (S-2a), an agent states it.
            e.prop("effect", Source{Value("external"), {}, {}});
            e.prop("answer", Source{Value("yes"), {}, {}});
            const md::Link link = data->links[ref];
            auto hub = data->hub;
            e.on("activate", [hub, link](const Value&) {
                if (hub->activate_link) hub->activate_link(link);
            });
        }
        link_ranges_.emplace_back(s.begin, s.end);
        out.push_back(std::move(e));
    }
    return out;
}

void Text::laid_out() {
    if (!services_ || link_ranges_.empty() || !paragraph_) return;
    const auto children = services_->children(node_);
    // The content box's origin: the node's padding.
    const Insets in = services_->content_insets(node_);
    origin_ = {in.left, in.top};
    std::vector<Rect> boxes;
    for (const auto& [b, e] : link_ranges_) {
        const auto rs = rects(b, e);
        Rect u = rs.empty() ? Rect{} : rs.front();
        for (const Rect& r : rs) u = u.united(r);
        boxes.push_back(u);
    }
    if (boxes == placed_) return;
    placed_ = boxes;
    for (std::size_t i = 0; i < children.size() && i < boxes.size(); ++i) {
        const Rect& r = boxes[i];
        services_->set(children[i], "left", Value(static_cast<double>(r.x)));
        services_->set(children[i], "top", Value(static_cast<double>(r.y)));
        services_->set(children[i], "width", Value(static_cast<double>(r.width)));
        services_->set(children[i], "height", Value(static_cast<double>(r.height)));
    }
}

// ── Link ──

void Link::paint(PaintContext& ctx) const {
    // The text is the paragraph's; a link only lights up while hovered or pressed.
    if (ctx.state.hovered || ctx.state.pressed)
        ctx.list.fill_round_rect({0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(3),
                                 render::Paint::solid(ctx.theme.color("accent_subtle").with_alpha(ctx.state.pressed ? 0.8f : 0.5f)));
}

void Link::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused) focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, 3);
}

void Link::semantics(SemanticsContext&, spec::SemanticNode& n) const {
    if (!data || index >= data->links.size()) return;
    const md::Link& l = data->links[index];
    const auto spans = link_spans(data->block.content);
    for (const Inl& s : spans)
        if (s.ref == index) {
            n.name = data->block.content.text.substr(s.begin, s.end - s.begin);
            break;
        }
    if (image) {
        n.role = Role::image;
        n.description = l.href;
    } else {
        n.description = l.title.empty() ? l.href : std::format("{} — {}", l.title, l.href);
        n.value = Value(l.href);
    }
    n.origin = origin_of(*data->hub);
}

// ── Group ──

void Group::style(const Theme&, layout::Style& s) const {
    if (!data) return;
    const md::Block& b = data->block;
    s.direction = layout::Direction::column;
    switch (b.kind) {
        case md::Kind::quote:
            s.padding.left = quote_pad;
            s.row_gap = block_gap;
            break;
        case md::Kind::bullet_list:
        case md::Kind::ordered_list: s.row_gap = b.tight ? tight_gap : block_gap; break;
        case md::Kind::item: {
            const float marker = b.task != md::Task::none ? 24.0f
                                 : data->number ? 14.0f + 8.0f * static_cast<float>(std::to_string(data->number).size())
                                                : bullet_w;
            s.padding.left = marker;
            s.row_gap = data->tight ? tight_gap : block_gap;
            break;
        }
        case md::Kind::table:
            s.align_self = layout::Align::start;  // as wide as its columns, not the document
            s.border = Insets{1, 1, 1, 1};
            s.overflow_x = layout::Overflow::clip;
            s.max_width = layout::Length::percent(100);
            break;
        case md::Kind::table_head:
        case md::Kind::table_body: break;
        case md::Kind::table_row: s.direction = layout::Direction::row; break;
        default: break;
    }
}

void Group::paint(PaintContext& ctx) const {
    const md::Block& b = data->block;
    const Theme& th = ctx.theme;
    if (b.kind == md::Kind::quote) {
        ctx.list.fill_round_rect({0, 0, 3, ctx.size.height}, CornerRadii::all(1.5f), render::Paint::solid(th.color("border_strong")));
        return;
    }
    if (b.kind == md::Kind::table) {
        ctx.list.stroke_round_rect({0.5f, 0.5f, ctx.size.width - 1, ctx.size.height - 1}, {}, 1, render::Paint::solid(th.color("border")));
        return;
    }
    if (b.kind != md::Kind::item || b.task != md::Task::none) return;
    // The marker, on the first line of the item's text.
    const typeset::TextStyle body = text_style(th, md::Block{.kind = md::Kind::paragraph});
    const float line = body.size * body.line_height;
    if (data->number) {
        if (!marker_) {
            typeset::ParagraphStyle ps;
            ps.style = body;
            ps.style.color = th.color("text_muted");
            ps.style.features.push_back(typeset::Feature::on("tnum"));
            ps.align = typeset::TextAlign::end;
            const char delim = data->bullet ? data->bullet : '.';
            marker_.emplace(ctx.services.fonts(), std::format("{}{}", data->number, delim), ps);
            marker_->layout(ctx.content.x - 6);
        }
        marker_->paint(ctx.list, {0, 0});
        return;
    }
    const float r = 2.75f;
    const Point c{bullet_w / 2 - 2, line / 2};
    ctx.list.fill_round_rect({c.x - r, c.y - r, 2 * r, 2 * r}, CornerRadii::all(r), render::Paint::solid(th.color("text_muted")));
}

void Group::semantics(SemanticsContext&, spec::SemanticNode& n) const {
    const md::Block& b = data->block;
    n.role = role_of(b);
    if (b.kind == md::Kind::item && b.task != md::Task::none) n.states.set(State::checked, b.task == md::Task::done);
    if (b.kind == md::Kind::ordered_list) n.value = Value(static_cast<std::int64_t>(b.start));
    n.origin = origin_of(*data->hub);
}

std::vector<Element> Group::compose() {
    std::vector<Element> out;
    const md::Block& b = data->block;
    if (b.kind == md::Kind::item && b.task != md::Task::none) {
        // The checkbox, in the marker column.
        Element e;
        e.type = &component_type<Task>();
        e.id = "task";
        e.key = std::format("task:{}:{}", static_cast<int>(b.task), data->task);
        auto d = data;
        e.configure = [d](void* p) { static_cast<Task*>(p)->data = d; };
        e.prop("position", Source{Value("absolute"), {}, {}});
        e.prop("left", Source{Value(0.0), {}, {}});
        e.prop("top", Source{Value(3.0), {}, {}});
        auto hub = data->hub;
        const std::int32_t index = data->task;
        e.on("activate", [hub, index](const Value&) {
            if (hub->toggle_task) hub->toggle_task(index);
        });
        out.push_back(std::move(e));
    }
    // A table's columns: every cell of a column gets the column's widest
    // content as its flex basis and the column's longest word as its minimum,
    // shrinking by what lies between, so every row lays out to the same
    // widths and a narrow table keeps its natural size (CSS's automatic table
    // layout, by flex).
    auto columns = data->columns;
    if (b.kind == md::Kind::table) {
        const Theme& theme = services_->theme();
        auto cols = std::make_shared<std::vector<std::pair<float, float>>>();
        for (const md::Block& section : b.children)
            for (const md::Block& row : section.children)
                for (std::size_t c = 0; c < row.children.size(); ++c) {
                    const md::Block& cell = row.children[c];
                    typeset::ParagraphStyle ps;
                    ps.style = text_style(theme, cell);
                    typeset::Paragraph p(services_->fonts(), cell.content.text, ps, ranges_of(theme, cell.content, ps.style));
                    if (cols->size() <= c) cols->resize(c + 1, {0.0f, 0.0f});
                    (*cols)[c].first = std::max((*cols)[c].first, std::ceil(p.min_intrinsic_width()) + 2 * cell_pad_x);
                    (*cols)[c].second = std::max((*cols)[c].second, std::ceil(p.max_intrinsic_width()) + 2 * cell_pad_x);
                }
        columns = std::move(cols);
    }
    std::uint32_t number = b.kind == md::Kind::ordered_list ? b.start : 0;
    std::int32_t task = data->task + (b.kind == md::Kind::item && b.task != md::Task::none ? 1 : 0);
    for (std::size_t i = 0; i < b.children.size(); ++i) {
        const md::Block& c = b.children[i];
        auto d = std::make_shared<Data>();
        d->block = c;
        d->links = data->links;  // the parent's numbering
        d->hub = data->hub;
        d->columns = columns;
        d->task = task;
        task += count_tasks(c);
        std::string id = std::format("b{}", i);
        switch (b.kind) {
            case md::Kind::bullet_list:
            case md::Kind::ordered_list:
                d->tight = b.tight;
                d->number = b.kind == md::Kind::ordered_list ? number++ : 0;
                d->bullet = b.kind == md::Kind::ordered_list ? b.mark : 0;
                id = std::format("i{}", i);
                break;
            case md::Kind::item: d->tight = data->tight; break;
            case md::Kind::table:
            case md::Kind::table_head:
            case md::Kind::table_body: id = std::format("r{}", i); break;
            case md::Kind::table_row: id = std::format("c{}", i); break;
            default: break;
        }
        Element e = element(std::move(d), id);
        if (b.kind == md::Kind::table_row && columns && i < columns->size()) {
            const auto [lo, hi] = (*columns)[i];
            e.prop("basis", Source{Value(static_cast<double>(hi)), {}, {}});
            e.prop("min_width", Source{Value(static_cast<double>(lo)), {}, {}});
            e.prop("grow", Source{Value(0.0), {}, {}});
            // CSS scales shrinking by the basis: (hi - lo) / hi shrinks a column by what it can give.
            e.prop("shrink", Source{Value(hi > 0 ? static_cast<double>((hi - lo) / hi) : 0.0), {}, {}});
        }
        out.push_back(std::move(e));
    }
    return out;
}

// ── Task ──

void Task::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool on = data->block.task == md::Task::done;
    const Rect box{0, 0, 16, 16};
    if (on) {
        ctx.list.fill_round_rect(box, CornerRadii::all(4), render::Paint::solid(th.color("accent")));
        render::Path tick;
        tick.move_to({4, 8.5f}).line_to({7, 11.5f}).line_to({12.5f, 5});
        ctx.list.stroke_path(tick, {.width = 2, .cap = render::LineCap::round, .join = render::LineJoin::round},
                             render::Paint::solid(th.color("on_accent")));
    } else {
        ctx.list.fill_round_rect(box, CornerRadii::all(4), render::Paint::solid(th.color("surface")));
        ctx.list.stroke_round_rect({0.75f, 0.75f, 14.5f, 14.5f}, CornerRadii::all(3.5f), 1.5f,
                                   render::Paint::solid(ctx.state.hovered ? th.color("accent") : th.color("border_strong")));
    }
}

void Task::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused) focus_ring(ctx, {0, 0, 16, 16}, 4);
}

ActResult Task::act(ActContext&, std::string_view action) {
    if (action != "toggle") return ActResult::fail(std::format("md_task has no action '{}'", action));
    if (data->hub->toggle_task) data->hub->toggle_task(data->task);
    return ActResult::done(Value(data->block.task != md::Task::done));
}

void Task::semantics(SemanticsContext&, spec::SemanticNode& n) const {
    // Named by its item's text, so "check 'write the tests'" finds it.
    std::string name;
    for (const md::Block& c : data->block.children)
        if (c.kind == md::Kind::paragraph) {
            name = c.content.text;
            break;
        }
    n.name = name;
    n.states.set(State::checked, data->block.task == md::Task::done);
    n.value = Value(static_cast<std::int64_t>(data->task));
    if (!n.action("toggle")) n.actions.push_back({"toggle", Effect::reversible, {}, {}, {}});
    n.origin = origin_of(*data->hub);
}

// ── Code ──

void Code::style(const Theme&, layout::Style& s) const {
    s.padding = Insets{code_pad, code_pad, code_pad, code_pad};
    s.overflow_x = layout::Overflow::clip;
}

typeset::Paragraph& Code::shaped(Services& s) const {
    if (!paragraph_) {
        typeset::ParagraphStyle ps;
        ps.style = text_style(s.theme(), data->block);
        ps.wrap = false;
        std::string text = data->block.content.text;
        while (text.ends_with('\n')) text.pop_back();
        paragraph_.emplace(s.fonts(), std::move(text), ps);
        laid_ = -1;
    }
    return *paragraph_;
}

Size Code::measure(MeasureContext& m, std::optional<float> w, std::optional<float>, layout::Available aw, layout::Available) {
    typeset::Paragraph& p = shaped(m.services);
    if (laid_ < 0) {
        p.layout(std::numeric_limits<float>::infinity());
        laid_ = 1;
    }
    const Size s = p.size();
    const float width = w.value_or(aw.is_definite() ? aw.value : std::ceil(s.width));
    return {width, std::ceil(std::max(s.height, 18.0f))};
}

void Code::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    ctx.list.fill_round_rect({0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(th.corner("medium", 8)),
                             render::Paint::solid(th.color("surface_variant")));
    typeset::Paragraph& p = shaped(ctx.services);
    if (laid_ < 0) {
        p.layout(std::numeric_limits<float>::infinity());
        laid_ = 1;
    }
    ctx.list.push_clip(ctx.content);
    p.paint(ctx.list, ctx.content.origin());
    ctx.list.pop_clip();
}

void Code::semantics(SemanticsContext&, spec::SemanticNode& n) const {
    std::string_view t = data->block.content.text;
    while (t.ends_with('\n')) t.remove_suffix(1);
    n.name = std::string(t);
    if (!data->block.language.empty()) n.description = data->block.language;
    n.origin = origin_of(*data->hub);
}

std::vector<Element> Code::compose() {
    Element e;
    e.type = &component_type<Copy>();
    e.id = "copy";
    e.key = "copy";
    auto d = data;
    e.configure = [d](void* p) { static_cast<Copy*>(p)->data = d; };
    e.prop("position", Source{Value("absolute"), {}, {}});
    e.prop("right", Source{Value(6.0), {}, {}});
    e.prop("top", Source{Value(6.0), {}, {}});
    Services* s = services_;
    const NodeId code = node_;
    e.on("activate", [s, code, d](const Value&) {
        std::string_view t = d->block.content.text;
        while (t.ends_with('\n')) t.remove_suffix(1);
        s->set_clipboard_text(t);
        const auto kids = s->children(code);
        if (!kids.empty()) s->set(kids.front(), "copied", Value(true));
    });
    return {std::move(e)};
}

// ── Copy ──

Copy::~Copy() {
    if (timer_ && services_) services_->cancel(timer_);
}

bool Copy::set_prop(std::string_view k, const Value& v) {
    if (k != "copied") return false;
    copied_ = v.as_bool();
    label_.reset();
    if (services_) {
        services_->invalidate_layout(node_);
        services_->invalidate_semantics(node_);
        if (timer_) services_->cancel(timer_);
        timer_ = 0;
        if (copied_) {
            Services* s = services_;
            const NodeId n = node_;
            timer_ = services_->after(seconds(1.5), [s, n] { s->set(n, "copied", Value(false)); });
        }
    }
    return true;
}

typeset::Paragraph& Copy::label(Services& s) const {
    if (!label_) {
        typeset::ParagraphStyle ps;
        ps.style = from_token(s.theme(), "caption");
        ps.style.color = s.theme().color(copied_ ? "success" : "text_muted");
        label_.emplace(s.fonts(), copied_ ? "Copied" : "Copy", ps);
        label_->layout(std::numeric_limits<float>::infinity());
    }
    return *label_;
}

Size Copy::measure(MeasureContext& m, std::optional<float>, std::optional<float>, layout::Available, layout::Available) {
    const Size s = label(m.services).size();
    return {std::ceil(s.width) + 16, std::ceil(s.height) + 6};
}

void Copy::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Color bg = ctx.state.pressed ? th.color("surface_pressed") : ctx.state.hovered ? th.color("surface") : th.color("surface_variant");
    const CornerRadii r = CornerRadii::all(th.corner("small", 4));
    ctx.list.fill_round_rect({0, 0, ctx.size.width, ctx.size.height}, r, render::Paint::solid(bg));
    ctx.list.stroke_round_rect({0.5f, 0.5f, ctx.size.width - 1, ctx.size.height - 1}, r, 1, render::Paint::solid(th.color("border")));
    typeset::Paragraph& p = label(ctx.services);
    const Size s = p.size();
    p.paint(ctx.list, {(ctx.size.width - s.width) / 2, (ctx.size.height - s.height) / 2});
}

void Copy::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused) focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, 4);
}

void Copy::semantics(SemanticsContext&, spec::SemanticNode& n) const {
    n.name = copied_ ? "Copied" : "Copy code";
    n.origin = origin_of(*data->hub);
}

// ── Rule ──

void Rule::style(const Theme&, layout::Style& s) const {
    s.margin.top = 4;
    s.margin.bottom = 4;
}

void Rule::semantics(SemanticsContext&, spec::SemanticNode& n) const { n.origin = origin_of(*data->hub); }

void Rule::paint(PaintContext& ctx) const { ctx.list.fill_rect({0, 0, ctx.size.width, 1}, ctx.theme.color("border")); }

}  // namespace omui::mdview
