// Development aids: the debug overlay (bounds, repaint flashes, the hovered
// node's boxes and source, a frame-time HUD) and node inspection for tools.
module openmui.view;

import std;
import openmui.core;
import openmui.layout;
import openmui.render;
import openmui.text;
import :impl;

namespace omui {

namespace {

constexpr seconds flash_time{0.4};

Value rect_value(const Rect& r) {
    return Value(Value::List{Value(static_cast<double>(r.x)), Value(static_cast<double>(r.y)), Value(static_cast<double>(r.width)),
                             Value(static_cast<double>(r.height))});
}

Value insets_value(const Insets& i) {
    return Value(Value::List{Value(static_cast<double>(i.top)), Value(static_cast<double>(i.right)), Value(static_cast<double>(i.bottom)),
                             Value(static_cast<double>(i.left))});
}

Value length_value(const layout::Length& l) {
    switch (l.unit) {
        case layout::Length::Unit::points: return Value(static_cast<double>(l.value));
        case layout::Length::Unit::percent: return Value(std::format("{}%", l.value));
        case layout::Length::Unit::auto_: return Value("auto");
    }
    return Value("auto");
}

// A label drawn with the code style on a dark plate, its top-left at `at`.
Rect plate(render::DisplayList& list, typeset::FontCollection& fonts, const Theme& theme, std::string text, Point at, Size window) {
    typeset::ParagraphStyle ps;
    const TextToken& code = theme.text_style("code");
    ps.style.families = code.families;
    ps.style.size = 12;
    ps.style.color = Color::white();
    typeset::Paragraph p(fonts, std::move(text), std::move(ps));
    p.layout(std::numeric_limits<float>::infinity());
    const Size s = p.size();
    Rect box{at.x, at.y, s.width + 12, s.height + 8};
    box.x = std::clamp(box.x, 0.0f, std::max(0.0f, window.width - box.width));
    box.y = std::clamp(box.y, 0.0f, std::max(0.0f, window.height - box.height));
    list.fill_round_rect(box, CornerRadii::all(4), render::Paint::solid(Color::hex(0x0f172a, 0.88f)));
    p.paint(list, {box.x + 6, box.y + 4});
    return box;
}

}  // namespace

void Runtime::Impl::paint_debug() {
    const DebugOptions& o = options.debug;
    const Rect window{0, 0, size.width, size.height};
    if (o.bounds) {
        // Every node's border box, a hue per depth.
        constexpr std::array<std::uint32_t, 6> hues{0xef4444, 0xf59e0b, 0x22c55e, 0x06b6d4, 0x6366f1, 0xd946ef};
        auto walk = [&](auto& self, NodeId id, std::size_t depth) -> void {
            if (!displayed(id)) return;
            const Node& n = at(id);
            if (id != root && !n.bounds.empty())
                list.stroke_round_rect(n.bounds, {}, 1, render::Paint::solid(Color::hex(hues[depth % hues.size()], 0.7f)));
            for (NodeId c : n.children) self(self, c, depth + 1);
        };
        walk(walk, root, 0);
    }
    if (o.repaints) {
        // What changed flashes, and fades.
        const seconds t = now();
        std::erase_if(flashes, [&](const auto& f) { return !alive(f.first) || t - f.second > flash_time; });
        for (const auto& [id, when] : flashes) {
            const float k = 1.0f - static_cast<float>((t - when) / flash_time);
            list.fill_rect(at(id).bounds, Color::hex(0xef4444, 0.3f * k));
        }
        if (!flashes.empty()) frame_requested = true;
    }
    if (o.inspector && !hover_chain.empty() && alive(hover_chain.front())) {
        const NodeId id = hover_chain.front();
        const Node& n = at(id);
        const layout::Style& st = layout.style(n.layout);
        const Rect b = n.bounds;
        const Insets m = st.margin;
        list.fill_rect({b.x - m.left, b.y - m.top, b.width + m.horizontal(), b.height + m.vertical()}, Color::hex(0xf59e0b, 0.18f));
        list.fill_rect(b, Color::hex(0x3b82f6, 0.2f));
        const Insets in{st.padding.top + st.border.top, st.padding.right + st.border.right, st.padding.bottom + st.border.bottom,
                        st.padding.left + st.border.left};
        list.fill_rect(b.inset(in), Color::hex(0x22c55e, 0.22f));
        list.stroke_round_rect(b, {}, 1, render::Paint::solid(Color::hex(0x3b82f6)));
        const std::string where = std::format("{}:{}", std::filesystem::path(n.where.file_name()).filename().string(), n.where.line());
        const std::string text = std::format("{}  {}  {:.0f}×{:.0f}  {}", self.path(id).empty() ? std::string("app") : self.path(id),
                                             n.type->name, b.width, b.height, where);
        plate(list, *fonts, theme, text, {pointer.x + 12, pointer.y + 16}, size);
    }
    if (o.hud && !history.empty()) {
        std::vector<double> totals;
        for (const FrameStats& f : history) totals.push_back(f.timings.total);
        std::ranges::sort(totals);
        double mean = 0;
        for (double v : totals) mean += v;
        mean /= static_cast<double>(totals.size());
        const double p95 = totals[std::min(totals.size() - 1, static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(totals.size()))) - 1)];
        const FrameStats& last = history.back();
        const std::string text = std::format("{:.2f} ms  p95 {:.2f}  ·  {} nodes  ·  {} laid out  ·  {} painted", mean, p95, last.nodes,
                                             last.laid_out, last.painted);
        plate(list, *fonts, theme, text, {window.width - 8, 8}, size);
    }
}

Value Runtime::inspect(NodeId node) const {
    const Impl& d = *impl_;
    if (!d.alive(node)) return {};
    const Node& n = d.at(node);
    Value v;
    v.set("id", node == d.root ? std::string("app") : path(node));
    v.set("type", n.type->name);
    if (n.type->contract) v.set("contract", n.type->contract->name);
    v.set("source", std::format("{}:{}:{}", n.where.file_name(), n.where.line(), n.where.column()));
    v.set("function", n.where.function_name());
    Value props = Value(Value::Map{});
    for (const auto& [k, x] : n.props) props.set(k, x);
    v.set("props", std::move(props));
    Value::List bound, handlers, transitions;
    for (const auto& [k, w] : n.bindings) bound.emplace_back(k);
    for (const auto& [k, h] : n.handlers) handlers.emplace_back(k);
    for (const Impl::RuntimeHandler& h : d.runtime_handlers)  // added at run time (scripts, other components)
        if (h.node == node) handlers.emplace_back(std::format("{} (runtime)", h.event));
    for (const auto& [k, t] : n.transitions) transitions.emplace_back(k);
    v.set("bound", Value(std::move(bound))).set("handlers", Value(std::move(handlers))).set("transitions", Value(std::move(transitions)));
    Value state;
    state.set("hovered", n.state.hovered).set("pressed", n.state.pressed).set("focused", n.state.focused);
    state.set("disabled", n.state.disabled).set("removing", n.state.removing).set("animating", n.state.animating);
    state.set("displayed", d.displayed(node));
    v.set("state", std::move(state));
    const layout::Style& s = d.layout.style(n.layout);
    Value style;
    constexpr std::array<std::string_view, 4> displays{"flex", "grid", "stack", "none"};
    constexpr std::array<std::string_view, 4> directions{"row", "column", "row_reverse", "column_reverse"};
    style.set("display", displays[static_cast<std::size_t>(s.display)]);
    style.set("direction", directions[static_cast<std::size_t>(s.direction)]);
    style.set("position", s.position == layout::Position::absolute ? "absolute" : "relative");
    style.set("width", length_value(s.width)).set("height", length_value(s.height));
    style.set("min_width", length_value(s.min_width)).set("max_width", length_value(s.max_width));
    style.set("padding", insets_value(s.padding)).set("margin", insets_value(s.margin)).set("border", insets_value(s.border));
    style.set("grow", static_cast<double>(s.grow)).set("shrink", static_cast<double>(s.shrink));
    style.set("gap", Value(Value::List{Value(static_cast<double>(s.row_gap)), Value(static_cast<double>(s.column_gap))}));
    v.set("layout", std::move(style));
    v.set("frame", rect_value(n.frame)).set("bounds", rect_value(n.bounds));
    v.set("children", static_cast<std::int64_t>(n.children.size()));
    v.set("overlay", n.type->overlay);
    return v;
}

}  // namespace omui
