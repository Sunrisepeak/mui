module openmui.site;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.base;

namespace omui::site {

namespace {

// Whether a person or an agent can change something through the node: an
// action that is not a read (scrolling into view is everyone's).
bool operable(const Value& n) {
    if (const Value* actions = n.get("actions"); actions && actions->list())
        for (const Value& a : *actions->list())
            if (const Value* e = a.get("effect"); !e || e->as_string() != "read") return true;
    return false;
}

// A role's colour: what a person reads (text, headings) blue, what a person
// or an agent operates (buttons, fields) amber, structure grey.
Color role_colour(const Theme& theme, std::string_view role, bool operable) {
    if (operable) return theme.color("#f59e0b");
    if (role == "heading" || role == "text" || role == "paragraph" || role == "link" || role == "list_item" || role == "list") return theme.color("#60a5fa");
    return theme.color("#94a3b8");
}

}  // namespace

void Lens::prop_changed(LensProp) {
    if (attached()) services().invalidate_paint(node());
}

Size Lens::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, layout::Available, layout::Available) {
    return {width.value_or(0), height.value_or(0)};
}

void Lens::paint(PaintContext& ctx) const {
    auto& out = ctx.list;
    out.fill_rect({0, 0, ctx.size.width, ctx.size.height}, Color{0.02f, 0.03f, 0.05f, 0.45f});  // the pixels dimmed under the tree (still readable)
    const Value::List* nodes = props.nodes.list();
    if (!nodes) return;
    const Rect self = services().bounds(node());
    std::vector<Rect> taken;  // the captions drawn: one never covers another
    std::size_t label = 0;
    for (const Value& n : *nodes) {
        const Value::List* b = n.get("bounds") ? n.get("bounds")->list() : nullptr;
        if (!b || b->size() != 4) continue;
        const Rect r{static_cast<float>((*b)[0].as_double(0)) - self.x, static_cast<float>((*b)[1].as_double(0)) - self.y, static_cast<float>((*b)[2].as_double(0)),
                     static_cast<float>((*b)[3].as_double(0))};
        if (r.width <= 0 || r.height <= 0 || r.x > ctx.size.width || r.y > ctx.size.height || r.x + r.width < 0 || r.y + r.height < 0) continue;
        const std::string role = n.get("role") ? std::string(n.get("role")->as_string()) : "generic";
        const bool acts = operable(n);
        const Color c = role_colour(ctx.theme, role, acts);
        out.stroke_round_rect(r, CornerRadii::all(3), acts ? 1.6f : 1.0f, render::Paint::solid(c.with_alpha(acts ? 0.95f : 0.55f)));
        // Its role, small, on a chip at its top left (where there is room, and no caption is yet).
        if (role == "generic" || r.width < 48 || r.height < 14) continue;
        if (label >= roles_.size()) roles_.emplace_back(), roles_.back().set_style("caption");
        parts::Label& l = roles_[label];
        l.set_text(role);
        const Size size = l.measure(services(), c);
        const Rect chip{r.x + 2, std::max(0.0f, r.y - 7), size.width + 8, size.height + 2};
        if (std::ranges::any_of(taken, [&](const Rect& t) { return chip.x < t.x + t.width && t.x < chip.x + chip.width && chip.y < t.y + t.height && t.y < chip.y + chip.height; }))
            continue;
        taken.push_back(chip);
        out.fill_round_rect(chip, CornerRadii::all(3), render::Paint::solid(Color{0.04f, 0.06f, 0.09f, 0.92f}));
        l.paint(ctx, {chip.x + 4, chip.y + 1}, c);
        ++label;
    }
}

void Lens::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    node.name = "The semantic tree's wireframe";
    const Value::List* nodes = props.nodes.list();
    node.value = Value{}.set("nodes", static_cast<std::int64_t>(nodes ? nodes->size() : 0));
}

}  // namespace omui::site
