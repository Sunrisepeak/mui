module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

using parts::keep_frames;
using parts::mix;

namespace {

constexpr float field_h = 36, pad_x = 12, chevron_room = 24, option_h = 32;

}  // namespace

// ── select ──

std::string Select::label_of(std::string_view value) const {
    for (std::size_t i = 0; i < props.options.size(); ++i)
        if (props.options[i] == value) return i < props.labels.size() ? props.labels[i] : props.options[i];
    return std::string(value);
}

void Select::mounted() {
    hover_.attach(services(), 0);
    label_.set_style("body");
    label_.set_text(props.value.empty() ? std::string("Choose…") : label_of(props.value));
}

void Select::prop_changed(SelectProp p) {
    label_.set_text(props.value.empty() ? std::string("Choose…") : label_of(props.value));
    if (!attached()) return;
    if (p == SelectProp::options || p == SelectProp::labels) services().recompose(node());
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

std::vector<Element> Select::compose() {
    Element pop;
    pop.type = &component_type<SelectPopup>();
    pop.id = "popup";
    for (std::size_t i = 0; i < props.options.size(); ++i) {
        const std::string value = props.options[i];
        Element o;
        o.type = &component_type<SelectOption>();
        o.key = value;
        o.prop("value", Source{Value(value), {}, {}});
        o.prop("label", Source{Value(i < props.labels.size() ? props.labels[i] : value), {}, {}});
        o.on("activate", [this, value](const Value&) { choose(value); });
        pop.children.push_back(std::move(o));
    }
    return {std::move(pop)};
}

NodeId Select::popup() const {
    for (NodeId c : services().children(node()))
        if (parts::segment(services(), c) == "popup") return c;
    return {};
}

bool Select::expanded() const {
    const NodeId p = attached() ? popup() : NodeId{};
    return p && services().get(p, "open").as_bool();
}

void Select::open(bool on) {
    const NodeId p = popup();
    if (!p) return;
    services().set(p, "open", Value(on));
    if (on) {
        // Focus the chosen option (or the first) so arrows and Enter work at once.
        NodeId first, chosen;
        for (NodeId o : services().children(p)) {
            if (!first) first = o;
            if (services().get(o, "value").as_string() == props.value) chosen = o;
        }
        if (chosen || first) services().focus(chosen ? chosen : first);
    } else {
        services().focus(node());
    }
    services().invalidate_semantics(node());
}

void Select::choose(const std::string& value) {
    if (value != props.value) {
        write_value(value);
        label_.set_text(label_of(value));
        emit_changed(value);
        services().invalidate_layout(node());
    }
    open(false);
}

bool Select::on_event(EventContext& ctx, const InputEvent& e) {
    // A click opens the popup, or closes it.
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || props.disabled || p->kind != PointerKind::up || (p->type == PointerType::mouse && p->button != MouseButton::left))
        return false;
    // Only presses on the field itself: its popup's options are its children
    // too, and their events bubble through it.
    const Rect b = services().bounds(node());
    if (ctx.local.x < 0 || ctx.local.y < 0 || ctx.local.x >= b.width || ctx.local.y >= b.height) return false;
    open(!expanded());
    return true;
}

ActResult Select::expand(ActContext&) {
    open(true);
    return ActResult::done();
}

ActResult Select::collapse(ActContext&) {
    open(false);
    return ActResult::done();
}

ActResult Select::set_value(ActContext&, const Value& value) {
    const std::string v(value.as_string());
    if (std::ranges::find(props.options, v) == props.options.end()) {
        std::string known;
        for (const std::string& o : props.options) known += known.empty() ? o : ", " + o;
        return ActResult::fail(std::format("`{}` is not an option ({})", v, known));
    }
    choose(v);
    return ActResult::done(Value(v));
}

void Select::style(const Theme&, layout::Style& s) const {
    s.padding = {0, pad_x + chevron_room, 0, pad_x};
    s.min_height = layout::pt(field_h);
    s.min_width = layout::pt(160);
    s.border = Insets::all(1);
}

Size Select::measure(MeasureContext& m, std::optional<float> width, std::optional<float>, layout::Available, layout::Available) {
    float w = 0;
    for (std::size_t i = 0; i < props.options.size(); ++i) {
        parts::Label probe;
        probe.set_style("body");
        probe.set_text(i < props.labels.size() ? props.labels[i] : props.options[i]);
        w = std::max(w, probe.measure(m.services, m.services.theme().color("text")).width);
    }
    const Size t = label_.measure(m.services, m.services.theme().color("text"));
    return {width.value_or(std::ceil(std::max(w, t.width))), std::ceil(t.height)};
}

void Select::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    hover_.to(!off && ctx.state.hovered ? 1.0 : 0.0, th.transition("motion.fast"));
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 8)).fitted(ctx.size);
    if (off) ctx.list.push_layer(0.5f, render::BlendMode::normal, {}, {}, box);
    ctx.list.fill_round_rect(box, r, render::Paint::solid(mix(th.color("surface"), th.color("surface_variant"), static_cast<float>(hover_.value()))));
    const bool open = expanded();
    const Color border = open ? th.color("accent") : th.color("border");
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, render::Paint::solid(border));
    const Color c = props.value.empty() ? th.color("text_muted") : th.color("text");
    const Size t = label_.measure(ctx.services, c);
    label_.paint(ctx, {ctx.content.x, ctx.content.y + (ctx.content.height - t.height) / 2}, c);
    parts::chevron(ctx, {box.width - pad_x - 6, box.height / 2}, 10, th.color("text_muted"));
    if (off) ctx.list.pop_layer();
    keep_frames(ctx, {&hover_});
}

void Select::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused && ctx.state.focus_visible)
        parts::focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.theme.corner("medium", 8)).fitted(ctx.size));
}

void Select::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    Value::List options;
    for (const std::string& o : props.options) options.emplace_back(o);
    node.value = Value{}.set("value", props.value).set("options", Value(std::move(options)));
    node.states.set(State::expanded, expanded());
    node.states.set(State::collapsible);
}

// ── popup ──

bool SelectPopup::set_prop(std::string_view k, const Value& v) {
    if (k != "open") return false;
    open_ = v.as_bool();
    if (services_) services_->restyle(node_);
    return true;
}

Value SelectPopup::get_prop(std::string_view k) const { return k == "open" ? Value(open_) : Value{}; }

void SelectPopup::style(const Theme&, layout::Style& s) const {
    s.display = open_ ? layout::Display::flex : layout::Display::none;
    s.direction = layout::Direction::column;
    s.padding = Insets::all(4);
    s.border = Insets::all(1);
}

void SelectPopup::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 8)).fitted(ctx.size);
    if (auto it = th.shadows.find("lg"); it != th.shadows.end())
        ctx.list.box_shadow(box, r, it->second.offset, it->second.blur, it->second.spread, it->second.color);
    ctx.list.fill_round_rect(box, r, render::Paint::solid(th.color("surface")));
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, render::Paint::solid(th.color("border")));
}

bool SelectPopup::on_event(EventContext&, const InputEvent& e) {
    const auto* k = std::get_if<KeyEvent>(&e);
    if (!k || k->kind != KeyKind::down || k->key != Key::escape) return false;
    services_->set(node_, "open", Value(false));
    services_->focus(services_->parent(node_));
    return true;
}

void SelectPopup::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    node.actions.push_back({"dismiss", Effect::reversible, {}, {}, {}});
}

ActResult SelectPopup::act(ActContext&, std::string_view action) {
    if (action != "dismiss") return ActResult::fail(std::format("the popup has no action '{}'", action));
    services_->set(node_, "open", Value(false));
    return ActResult::done();
}

// ── option ──

bool SelectOption::set_prop(std::string_view k, const Value& v) {
    if (k == "value") value_ = std::string(v.as_string());
    else if (k == "label") {
        label_.set_style("body");
        label_.set_text(std::string(v.as_string()));
    } else return false;
    return true;
}

Value SelectOption::get_prop(std::string_view k) const {
    if (k == "value") return Value(value_);
    if (k == "label") return Value(label_.text());
    return {};
}

void SelectOption::style(const Theme&, layout::Style& s) const {
    s.padding = {0, 10, 0, 10};
    s.min_height = layout::pt(option_h);
}

Size SelectOption::measure(MeasureContext& m, std::optional<float>, std::optional<float>, layout::Available, layout::Available) {
    const Size t = label_.measure(m.services, m.services.theme().color("text"));
    return {std::ceil(t.width), std::ceil(t.height)};
}

bool SelectOption::selected() const {
    const NodeId select = services_->parent(services_->parent(node_));
    return std::string(services_->get(select, "value").as_string()) == value_;
}

void SelectOption::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool lit = ctx.state.hovered || ctx.state.focused;
    const bool chosen = selected();
    if (lit || chosen)
        ctx.list.fill_round_rect({0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(th.corner("small", 4)),
                                 render::Paint::solid(chosen ? th.color("accent_subtle") : th.color("surface_variant")));
    const Color c = chosen ? th.color("accent") : th.color("text");
    const Size t = label_.measure(ctx.services, c);
    label_.paint(ctx, {ctx.content.x, ctx.content.y + (ctx.content.height - t.height) / 2}, c);
}

bool SelectOption::on_event(EventContext&, const InputEvent& e) {
    const auto* k = std::get_if<KeyEvent>(&e);
    if (!k || k->kind != KeyKind::down || (k->key != Key::down && k->key != Key::up)) return false;
    const auto all = services_->children(services_->parent(node_));
    auto it = std::ranges::find(all, node_);
    if (it == all.end()) return true;
    const std::size_t i = static_cast<std::size_t>(it - all.begin());
    const std::size_t n = all.size();
    services_->focus(all[k->key == Key::down ? (i + 1) % n : (i + n - 1) % n]);
    return true;
}

void SelectOption::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = label_.text();
    node.value = Value(value_);
    node.states.set(State::selected, selected());
}

}  // namespace omui
