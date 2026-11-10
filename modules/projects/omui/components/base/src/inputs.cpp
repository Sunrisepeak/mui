// openmui.ui.base:inputs — rating, otp input, form field, search box, toggle group.
module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

namespace {

constexpr float star = 22, star_gap = 4;                          // a rating
constexpr float cell_w = 40, cell_h = 48, cell_gap = 8;           // an otp input
constexpr float note_gap = 4;                                     // a form field's label and note
constexpr float search_h = 36, search_icon = 32, search_clear = 28;  // a search box
constexpr float option_h = 32, option_pad = 12, option_inset = 3; // a toggle group

bool primary(const PointerEvent& p) { return p.type != PointerType::mouse || p.button == MouseButton::left; }

spec::Rect in_window(const Rect& node, const Rect& part) { return {node.x + part.x, node.y + part.y, part.width, part.height}; }

// A five-pointed star filling `box`.
xrender::Path star_path(const Rect& box) {
    xrender::Path path;
    const Point centre{box.x + box.width / 2, box.y + box.height * 0.53f};
    const float outer = box.width / 2, inner = outer * 0.42f;
    constexpr double pi = 3.14159265358979323846;
    for (int point = 0; point < 10; ++point) {
        const double angle = -pi / 2 + point * pi / 5;
        const float radius = point % 2 == 0 ? outer : inner;
        const Point at{centre.x + radius * static_cast<float>(std::cos(angle)), centre.y + radius * static_cast<float>(std::sin(angle))};
        if (point == 0) path.move_to(at);
        else path.line_to(at);
    }
    path.close();
    return path;
}

}  // namespace

// ── rating ──

Size Rating::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    const auto stars = static_cast<float>(std::max<std::int64_t>(1, props.max));
    return {width.value_or(stars * star + (stars - 1) * star_gap), height.value_or(star)};
}

double Rating::value_at(float x) const {
    const double index = std::floor(std::max(0.0f, x) / (star + star_gap));
    const double within = (std::max(0.0f, x) - index * (star + star_gap)) / star;
    const double value = index + (props.half && within < 0.5 ? 0.5 : 1.0);
    return std::clamp(value, step(), static_cast<double>(props.max));
}

void Rating::paint(PaintContext& ctx) const {
    const bool off = props.disabled || ctx.state.disabled;
    const double shown = preview_.value_or(props.value);
    const Color filled = ctx.theme.color("rating", Color::hex(0xf59e0b)), empty = ctx.theme.color("border");
    for (std::int64_t index = 0; index < props.max; ++index) {
        const Rect box{static_cast<float>(index) * (star + star_gap), 0, star, star};
        const xrender::Path shape = star_path(box);
        ctx.list.fill_path(shape, xrender::FillRule::nonzero, xrender::Paint::solid(off ? empty.with_alpha(0.5f) : empty));
        const double part = std::clamp(shown - static_cast<double>(index), 0.0, 1.0);
        if (part <= 0) continue;
        // Filled as far as the value goes (half a star: its left half), lighter while only shown.
        ctx.list.push_clip(Rect{box.x, box.y, box.width * static_cast<float>(part), box.height});
        ctx.list.fill_path(shape, xrender::FillRule::nonzero, xrender::Paint::solid(preview_ ? filled.with_alpha(0.6f) : filled));
        ctx.list.pop_clip();
    }
    if (ctx.state.focused && ctx.state.focus_visible) parts::focus_ring(ctx, {-3, -3, ctx.size.width + 6, ctx.size.height + 6}, CornerRadii::all(6));
}

bool Rating::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || props.disabled || props.readonly) return false;
    if (p->kind == PointerKind::leave) {
        if (preview_) preview_.reset(), ctx.repaint();
        return false;
    }
    const std::optional<double> over = ctx.local.x >= 0 && ctx.local.x <= ctx.services.bounds(ctx.node).width ? std::optional(value_at(ctx.local.x)) : std::nullopt;
    if (over != preview_) preview_ = over, ctx.repaint();
    if (p->kind == PointerKind::up && over && primary(*p)) {
        // Pressing the value it has clears it; as the person's action (journaled, taken back).
        (void)services().perform(node(), "set_value", Value{}.set("value", *over == props.value ? 0.0 : *over));
        return true;
    }
    return p->kind == PointerKind::down && over.has_value();
}

ActResult Rating::rate(double value) {
    if (props.disabled || props.readonly) return ActResult::fail(props.readonly ? "it only shows" : "it is disabled");
    value = std::clamp(std::round(value / step()) * step(), 0.0, static_cast<double>(props.max));
    const double old = props.value;
    if (value != props.value) {
        write_value(value);
        emit_changed(value);
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", old));
}

ActResult Rating::increment(ActContext&) { return rate(props.value + step()); }
ActResult Rating::decrement(ActContext&) { return rate(props.value - step()); }

ActResult Rating::set_value(ActContext&, const Value& value) {
    if (value.as_string() == "max") return rate(static_cast<double>(props.max));
    if (!value.is_number()) return ActResult::fail("a rating is a number, 0 … max, or `max`");
    return rate(value.as_double());
}

void Rating::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value{}.set("value", props.value).set("min", 0.0).set("max", static_cast<double>(props.max)).set("step", step());
    if (props.readonly) node.states.set(State::readonly, true);
    if (props.disabled) node.states.set(State::disabled, true);
}

// ── otp input ──

std::string OtpInput::accepted(std::string_view text, bool numeric) {
    std::string out;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isdigit(byte)) out.push_back(c);
        else if (!numeric && std::isalpha(byte)) out.push_back(static_cast<char>(std::toupper(byte)));
    }
    return out;
}

void OtpInput::rebuild() {
    const auto length = static_cast<std::size_t>(std::clamp<std::int64_t>(props.length, 1, 12));
    if (cells_.size() != length) cells_ = std::vector<parts::Label>(length);
    for (std::size_t index = 0; index < cells_.size(); ++index) {
        cells_[index].set_style("heading");
        cells_[index].set_text(index < props.value.size() ? std::string(1, props.value[index]) : std::string());
    }
    caret_ = std::min(caret_, props.value.size());
}

void OtpInput::mounted() {
    caret_ = props.value.size();
    rebuild();
}

void OtpInput::prop_changed(OtpInputProp) {
    rebuild();
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

Size OtpInput::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    const auto count = static_cast<float>(cells_.size());
    return {width.value_or(count * cell_w + (count - 1) * cell_gap), height.value_or(cell_h)};
}

void OtpInput::paint(PaintContext& ctx) const {
    const Theme& theme = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    const bool typing = ctx.state.focused && !off;
    const std::size_t at = std::min(caret_, cells_.size() - 1);
    for (std::size_t index = 0; index < cells_.size(); ++index) {
        const Rect cell{static_cast<float>(index) * (cell_w + cell_gap), 0, cell_w, cell_h};
        const CornerRadii corners = CornerRadii::all(theme.corner("medium", 8));
        const bool current = typing && index == at;
        ctx.list.fill_round_rect(cell, corners, xrender::Paint::solid(theme.color(off ? "surface_variant" : "surface")));
        ctx.list.stroke_round_rect({cell.x + 0.5f, cell.y + 0.5f, cell.width - 1, cell.height - 1}, corners, current ? 2.0f : 1.0f,
                                   xrender::Paint::solid(theme.color(current ? "accent" : "border")));
        const Color ink = theme.color(off ? "text_muted" : "text");
        if (const Size text = cells_[index].measure(ctx.services, ink); !cells_[index].text().empty())
            cells_[index].paint(ctx, {cell.x + (cell.width - text.width) / 2, (cell.height - text.height) / 2}, ink);
        else if (current)
            ctx.list.fill_rect({cell.x + cell.width / 2 - 1, cell.height * 0.28f, 2, cell.height * 0.44f}, theme.color("accent"));  // where it goes
    }
}

ActResult OtpInput::take(std::string code) {
    if (props.disabled) return ActResult::fail("it is disabled");
    const std::string kept = accepted(code, props.numeric);
    if (kept.size() != code.size()) return ActResult::fail(props.numeric ? "a code of digits" : "a code of letters and digits");
    if (code.size() > cells_.size()) return ActResult::fail(std::format("a code of at most {} characters", cells_.size()));
    const std::string old = props.value;
    if (code != props.value) {
        write_value(code);
        caret_ = code.size();
        rebuild();
        emit_changed(code);
        if (code.size() == cells_.size()) emit_completed(code);
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", old));
}

ActResult OtpInput::set_value(ActContext&, const Value& value) { return take(std::string(value.as_string())); }

void OtpInput::typed(std::string_view text) {
    // Each character into the box the caret is at (replacing what was there), the caret moving on; a paste fills on.
    std::string code = props.value;
    for (const char c : accepted(text, props.numeric)) {
        if (caret_ >= cells_.size()) break;
        if (caret_ < code.size()) code[caret_] = c;
        else code.push_back(c);
        ++caret_;
    }
    if (code != props.value) (void)services().perform(node(), "set_value", Value{}.set("value", code));
}

bool OtpInput::on_event(EventContext& ctx, const InputEvent& e) {
    if (props.disabled) return false;
    if (const auto* text = std::get_if<TextEvent>(&e)) {
        typed(text->text);
        ctx.repaint();
        return true;
    }
    if (const auto* key = std::get_if<KeyEvent>(&e); key && key->kind == KeyKind::down) {
        if (key->key == Key::backspace) {
            std::string code = props.value;
            if (caret_ < code.size()) code.erase(caret_, 1);
            else if (!code.empty()) code.pop_back(), caret_ = code.size();
            if (code != props.value) (void)services().perform(node(), "set_value", Value{}.set("value", code));
            caret_ = std::min(caret_, code.size());
            ctx.repaint();
            return true;
        }
        if (key->key == Key::left || key->key == Key::right) {
            caret_ = key->key == Key::left ? (caret_ > 0 ? caret_ - 1 : 0) : std::min(caret_ + 1, props.value.size());
            ctx.repaint();
            return true;
        }
    }
    if (const auto* p = std::get_if<PointerEvent>(&e); p && p->kind == PointerKind::down) {
        // The box pressed, up to the one after what is typed.
        const auto index = static_cast<std::size_t>(std::max(0.0f, ctx.local.x) / (cell_w + cell_gap));
        caret_ = std::min({index, props.value.size(), cells_.size() - 1});
        services().focus(node());
        ctx.repaint();
        return true;
    }
    return false;
}

void OtpInput::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value(props.value);
    node.states.set(State::editable, !props.disabled);
    if (props.disabled) node.states.set(State::disabled, true);
}

// ── form field ──

float FormField::label_height(const Theme& theme) const {
    if (props.label.empty()) return 0;
    const TextToken& token = theme.text_style("label");
    return std::ceil(token.size * token.line_height) + note_gap;
}

float FormField::note_height(const Theme& theme) const {
    if (props.error.empty() && props.help.empty()) return 0;
    const TextToken& token = theme.text_style("caption");
    return std::ceil(token.size * token.line_height) + note_gap;
}

void FormField::mounted() {
    label_.set_style("label");
    note_.set_style("caption");
    prop_changed(FormFieldProp::label);
}

void FormField::prop_changed(FormFieldProp) {
    label_.set_text(props.required && !props.label.empty() ? props.label + " *" : props.label);
    note_.set_text(props.error.empty() ? props.help : props.error);
    if (!attached()) return;
    services().restyle(node());
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

void FormField::style(const Theme& theme, mlibs::layout::Style& s) const {
    // The control between the label's line and the note's.
    s.direction = mlibs::layout::Direction::column;
    s.padding = {label_height(theme), 0, note_height(theme), 0};
}

void FormField::paint(PaintContext& ctx) const {
    if (!props.label.empty()) label_.paint(ctx, {0, 0}, ctx.theme.color("text"));
    if (props.error.empty() && props.help.empty()) return;
    const Color ink = props.error.empty() ? ctx.theme.color("text_muted") : ctx.theme.color("danger");
    const Size note = note_.measure(ctx.services, ink, ctx.size.width);
    note_.paint(ctx, {0, ctx.size.height - note.height}, ink);
}

void FormField::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.description = props.error.empty() ? props.help : props.error;
    if (props.required) node.states.set(State::required, true);
    if (!props.error.empty()) node.states.set(State::invalid, true);
}

// ── search box ──

void SearchBox::style(const Theme&, mlibs::layout::Style& s) const {
    // The words between the magnifier and the × (room kept for it).
    s.direction = mlibs::layout::Direction::row;
    s.padding = {0, 4, 0, search_icon};
    s.align_items = mlibs::layout::Align::center;
    s.min_height = mlibs::layout::Length::points(search_h);
}

std::vector<Element> SearchBox::compose() {
    Element field;
    field.type = &component_type<TextBox>();
    field.id = "field";
    field.prop("text", Source{Value(props.value), {}, {}});
    field.prop("placeholder", Source{Value(props.placeholder), {}, {}});
    field.prop("label", Source{Value(props.label), {}, {}});
    field.prop("disabled", Source{Value(props.disabled), {}, {}});
    field.prop("grow", Source{Value(1.0), {}, {}});
    field.prop("bare", Source{Value(true), {}, {}});  // its surface and border are the search box's
    // What the person types is its words; Enter searches with them.
    field.on("changed", [this](const Value& args) {
        const std::string text(args.get("text") ? args.get("text")->as_string() : std::string_view{});
        if (text == props.value) return;
        const bool had = !props.value.empty();
        write_value(text);
        emit_changed(text);
        if (had != !text.empty()) services().recompose(node());  // its × comes or goes
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    });
    field.on("submitted", [this](const Value&) { emit_submitted(props.value); });
    std::vector<Element> out;
    out.push_back(std::move(field));
    // While it holds words: the × that clears them, a button of its own (drawn by the box itself).
    if (!props.value.empty() && !props.disabled)
        out.push_back(box({.id = "clear", .width = Dim(search_clear), .height = Dim(search_clear), .label = "Clear",
                           .on = {{"press", [this](const Value&) { (void)services().perform(node(), "clear", Value{}); }}}}));
    return out;
}

Rect SearchBox::clear_box(Size size) const { return {size.width - 4 - search_clear, (size.height - search_clear) / 2, search_clear, search_clear}; }

void SearchBox::paint(PaintContext& ctx) const {
    const Theme& theme = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii corners = CornerRadii::all(ctx.size.height / 2);
    ctx.list.fill_round_rect(box, corners, xrender::Paint::solid(theme.color("surface_variant")));
    // Its field focused: the pill's edge in the accent, as a text box's is.
    const auto children = services().children(node());
    if (!children.empty() && services().state(children.front()).focused)
        ctx.list.stroke_round_rect({0.75f, 0.75f, box.width - 1.5f, box.height - 1.5f}, corners, 1.5f, xrender::Paint::solid(theme.color("accent")));
    // The magnifier: a ring and its handle.
    const Color muted = theme.color("text_muted");
    const Point centre{search_icon / 2 + 1, ctx.size.height / 2 - 1};
    xrender::Path lens;
    lens.add_circle(centre, 5.5f);
    ctx.list.stroke_path(lens, {.width = 1.75f}, xrender::Paint::solid(muted));
    xrender::Path handle;
    handle.move_to({centre.x + 4, centre.y + 4}).line_to({centre.x + 8, centre.y + 8});
    ctx.list.stroke_path(handle, {.width = 1.75f, .cap = xrender::LineCap::round}, xrender::Paint::solid(muted));
    // While it holds words: the × that clears them.
    if (!props.value.empty() && !props.disabled) {
        const Rect clear = clear_box(ctx.size);
        const Point mid{clear.x + clear.width / 2, clear.y + clear.height / 2};
        constexpr float arm = 4;
        xrender::Path cross;
        cross.move_to({mid.x - arm, mid.y - arm}).line_to({mid.x + arm, mid.y + arm}).move_to({mid.x + arm, mid.y - arm}).line_to({mid.x - arm, mid.y + arm});
        ctx.list.stroke_path(cross, {.width = 1.75f, .cap = xrender::LineCap::round}, xrender::Paint::solid(muted));
    }
}

bool SearchBox::on_event(EventContext&, const InputEvent& e) {
    if (props.disabled) return false;
    if (const auto* key = std::get_if<KeyEvent>(&e); key && key->kind == KeyKind::down && key->key == Key::escape && !props.value.empty()) {
        (void)services().perform(node(), "clear", Value{});
        return true;
    }
    return false;
}

ActResult SearchBox::words(std::string value) {
    if (props.disabled) return ActResult::fail("it is disabled");
    const std::string old = props.value;
    if (value != props.value) {
        write_value(value);
        emit_changed(value);
        services().recompose(node());  // its field shows them
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", old));
}

ActResult SearchBox::set_value(ActContext&, const Value& value) { return words(std::string(value.as_string())); }
ActResult SearchBox::clear(ActContext&) { return words({}); }

ActResult SearchBox::submit(ActContext&) {
    if (props.disabled) return ActResult::fail("it is disabled");
    emit_submitted(props.value);
    return ActResult::done(Value(props.value));
}

void SearchBox::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value(props.value);
    if (props.disabled) node.states.set(State::disabled, true);
}

// ── toggle group ──

void ToggleGroup::mounted() { prop_changed(ToggleGroupProp::options); }

void ToggleGroup::prop_changed(ToggleGroupProp) {
    if (labels_.size() != props.options.size()) labels_ = std::vector<parts::Label>(props.options.size());
    for (std::size_t index = 0; index < labels_.size(); ++index) {
        labels_[index].set_style("label");
        labels_[index].set_text(label_of(index));
    }
    focus_ = std::min(focus_, props.options.empty() ? 0 : props.options.size() - 1);
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

std::string ToggleGroup::label_of(std::size_t index) const { return index < props.labels.size() ? props.labels[index] : props.options[index]; }

std::optional<std::size_t> ToggleGroup::index_of(std::string_view option) const {
    for (std::size_t index = 0; index < props.options.size(); ++index)
        if (props.options[index] == option) return index;
    return std::nullopt;
}

bool ToggleGroup::on(std::string_view option) const { return std::ranges::find(props.selected, option) != props.selected.end(); }

Size ToggleGroup::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    float across = 2 * option_inset;
    for (parts::Label& label : labels_) across += std::ceil(label.measure(m.services, m.services.theme().color("text")).width) + 2 * option_pad;
    return {width.value_or(across), height.value_or(option_h)};
}

void ToggleGroup::paint(PaintContext& ctx) const {
    const Theme& theme = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const float radius = theme.corner("medium", 8);
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, box);
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, CornerRadii::all(radius).fitted(ctx.size), 1, xrender::Paint::solid(theme.color("border")));
    at_.clear();
    float x = option_inset;
    for (std::size_t index = 0; index < labels_.size(); ++index) {
        const float width = std::ceil(labels_[index].measure(ctx.services, theme.color("text")).width) + 2 * option_pad;
        at_.push_back({x, option_inset, width, ctx.size.height - 2 * option_inset});
        x += width;
    }
    for (std::size_t index = 0; index < at_.size(); ++index) {
        const Rect& r = at_[index];
        const CornerRadii corners = CornerRadii::all(radius - option_inset).fitted(r.size());
        const bool pressed = on(props.options[index]);
        if (pressed)
            ctx.part({.role = "selection", .reference = props.options[index], .allocation = r, .kind = xrender::PartKind::box},
                     [&] { ctx.list.fill_round_rect(r, corners, xrender::Paint::solid(theme.color("accent").with_alpha(0.14f))); });
        else if (hover_ == static_cast<int>(index))
            ctx.list.fill_round_rect(r, corners, xrender::Paint::solid(theme.color("surface_pressed").with_alpha(0.5f)));
        const Color ink = pressed ? theme.color("accent") : theme.color("text");
        const Size text = labels_[index].measure(ctx.services, ink);
        labels_[index].paint(ctx, {r.x + (r.width - text.width) / 2, r.y + (r.height - text.height) / 2}, ink);
        if (ctx.state.focused && ctx.state.focus_visible && index == focus_) parts::focus_ring(ctx, r, corners);
    }
    if (off) ctx.list.pop_layer();
}

bool ToggleGroup::on_event(EventContext& ctx, const InputEvent& e) {
    if (props.disabled) return false;
    if (const auto* key = std::get_if<KeyEvent>(&e); key && key->kind == KeyKind::down && (key->key == Key::space || key->key == Key::enter)) {
        if (focus_ < props.options.size())
            (void)services().perform(node(), "set_selected", Value{}.set("selected", [&] {
                std::vector<std::string> now = props.selected;
                if (on(props.options[focus_])) std::erase(now, props.options[focus_]);
                else now.push_back(props.options[focus_]);
                return ValueOf<std::vector<std::string>>::to(now);
            }()));
        return true;
    }
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    int over = -1;
    for (std::size_t index = 0; index < at_.size(); ++index)
        if (at_[index].contains(ctx.local)) over = static_cast<int>(index);
    if (p->kind == PointerKind::leave) over = -1;
    if (over != hover_) hover_ = over, ctx.repaint();
    if (p->kind == PointerKind::up && over >= 0 && primary(*p)) {
        focus_ = static_cast<std::size_t>(over);
        std::vector<std::string> now = props.selected;
        const std::string& option = props.options[focus_];
        if (on(option)) std::erase(now, option);
        else now.push_back(option);
        (void)services().perform(node(), "set_selected", Value{}.set("selected", ValueOf<std::vector<std::string>>::to(now)));
        return true;
    }
    return p->kind == PointerKind::down && over >= 0;
}

ActResult ToggleGroup::choose(std::vector<std::string> selected) {
    if (props.disabled) return ActResult::fail("it is disabled");
    for (const std::string& option : selected)
        if (!index_of(option)) return ActResult::fail(std::format("`{}` is not one of its options", option));
    // In the options' order, each once.
    std::vector<std::string> ordered;
    for (const std::string& option : props.options)
        if (std::ranges::find(selected, option) != selected.end()) ordered.push_back(option);
    const std::vector<std::string> old = props.selected;
    if (ordered != props.selected) {
        write_selected(ordered);
        emit_changed(ordered);
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
    return ActResult::done(ValueOf<std::vector<std::string>>::to(props.selected))
        .undo_by("set_selected", Value{}.set("selected", ValueOf<std::vector<std::string>>::to(old)));
}

ActResult ToggleGroup::set_selected(ActContext&, const Value& selected) { return choose(ValueOf<std::vector<std::string>>::from(selected)); }

ActResult ToggleGroup::next(ActContext&) {
    if (!props.options.empty()) focus_ = std::min(focus_ + 1, props.options.size() - 1);
    services().invalidate_paint(node());
    return ActResult::done();
}

ActResult ToggleGroup::previous(ActContext&) {
    focus_ = focus_ > 0 ? focus_ - 1 : 0;
    services().invalidate_paint(node());
    return ActResult::done();
}

spec::SemanticNode ToggleGroup::item(std::string_view path, std::size_t index) const {
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, props.options[index]);
    n.role = Role::toggle_button;
    n.name = label_of(index);
    n.states.set(State::checked, on(props.options[index]));
    if (props.disabled) n.states.set(State::disabled, true);
    if (index < at_.size() && attached()) n.bounds = in_window(services().bounds(node()), at_[index]);
    if (!props.disabled) n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    return n;
}

void ToggleGroup::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = ValueOf<std::vector<std::string>>::to(props.selected);
}

std::vector<spec::SemanticNode> ToggleGroup::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (std::size_t index = 0; index < props.options.size(); ++index) out.push_back(item(path, index));
    return out;
}

std::optional<spec::SemanticNode> ToggleGroup::virtual_item(std::string_view path, std::string_view key) const {
    const auto index = index_of(key);
    return index ? std::optional(item(path, *index)) : std::nullopt;
}

ActResult ToggleGroup::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    // Its inverse taken back on the item (S-12) is the group's own action.
    if (action != "activate") return act(ctx, action);
    if (!index_of(key)) return ActResult::fail(std::format("no option `{}`", key));
    std::vector<std::string> now = props.selected;
    if (on(key)) std::erase(now, std::string(key));
    else now.emplace_back(key);
    return choose(std::move(now));
}

// ── file drop ──

void FileDrop::mounted() {
    label_.set_style("label");
    note_.set_style("caption");
    prop_changed(FileDropProp::label);
}

void FileDrop::prop_changed(FileDropProp) {
    label_.set_text(props.label);
    std::string kinds;
    for (const std::string& extension : props.accept) kinds += kinds.empty() ? extension : ", " + extension;
    note_.set_text(!refused_.empty() ? refused_ : kinds.empty() ? std::string() : std::format("Takes {}", kinds));
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

bool FileDrop::accepts(std::string_view file) const {
    if (props.accept.empty()) return true;
    const auto dot = file.rfind('.');
    if (dot == std::string_view::npos) return false;
    const std::string extension = [&] {
        std::string out(file.substr(dot + 1));
        for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }();
    return std::ranges::any_of(props.accept, [&](const std::string& one) { return one == extension; });
}

Size FileDrop::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available) {
    const Size text = label_.measure(m.services, m.services.theme().color("text"));
    const Size note = note_.text().empty() ? Size{} : note_.measure(m.services, m.services.theme().color("text_muted"));
    const float wide = aw.kind == mlibs::layout::Available::Kind::definite ? aw.value : std::max(text.width, note.width) + 48;
    return {width.value_or(wide), height.value_or(std::max(96.0f, text.height + note.height + 48))};
}

void FileDrop::paint(PaintContext& ctx) const {
    const Theme& theme = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    const bool lit = over_ && !off;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii corners = CornerRadii::all(theme.corner("large", 12)).fitted(ctx.size);
    ctx.list.fill_round_rect(box, corners, xrender::Paint::solid(lit ? theme.color("accent").with_alpha(0.08f) : theme.color("surface_variant").with_alpha(0.5f)));
    // A dashed edge: where to let files go; solid in the accent while they are over it.
    static constexpr std::array<float, 2> dashes{6, 4};
    xrender::Path edge;
    edge.add_rounded_rect({0.75f, 0.75f, box.width - 1.5f, box.height - 1.5f}, corners);
    xrender::StrokeStyle line{.width = 1.5f};
    if (!lit) line.dashes = dashes;
    ctx.list.stroke_path(edge, line, xrender::Paint::solid(lit ? theme.color("accent") : theme.color(ctx.state.hovered && !off ? "border_strong" : "border")));
    const Color ink = theme.color(off ? "text_muted" : lit ? "accent" : "text");
    const Size text = label_.measure(ctx.services, ink);
    const Size note = note_.text().empty() ? Size{} : note_.measure(ctx.services, theme.color("text_muted"));
    const float top = (box.height - text.height - (note.height > 0 ? note.height + 4 : 0)) / 2;
    label_.paint(ctx, {(box.width - text.width) / 2, top}, ink);
    if (note.height > 0) note_.paint(ctx, {(box.width - note.width) / 2, top + text.height + 4}, theme.color(refused_.empty() ? "text_muted" : "danger"));
    if (ctx.state.focused && ctx.state.focus_visible) parts::focus_ring(ctx, box, corners);
}

bool FileDrop::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* files = std::get_if<FileDropEvent>(&e);
    if (!files || props.disabled) return false;
    if (files->kind != FileDropEvent::Kind::drop) {
        const bool over = files->kind == FileDropEvent::Kind::over;
        if (over != over_) over_ = over, ctx.repaint();
        return true;
    }
    over_ = false;
    ctx.repaint();
    // As the person's giving (journaled), what it accepts.
    (void)services().perform(node(), "drop", Value{}.set("files", ValueOf<std::vector<std::string>>::to(files->files)));
    return true;
}

ActResult FileDrop::drop(ActContext&, const Value& files) {
    if (props.disabled) return ActResult::fail("it is disabled");
    std::vector<std::string> taken;
    std::vector<std::string> left;
    for (const std::string& file : ValueOf<std::vector<std::string>>::from(files)) (accepts(file) ? taken : left).push_back(file);
    if (!props.multiple && taken.size() > 1) left.insert(left.end(), taken.begin() + 1, taken.end()), taken.resize(1);
    refused_ = left.empty() ? std::string() : std::format("Not taken: {}", [&] {
        std::string names;
        for (const std::string& file : left) {
            const auto slash = file.find_last_of("/\\");
            names += (names.empty() ? "" : ", ") + file.substr(slash == std::string::npos ? 0 : slash + 1);
        }
        return names;
    }());
    prop_changed(FileDropProp::label);
    if (taken.empty()) return ActResult::fail(refused_.empty() ? std::string("no files") : refused_);
    emit_dropped(taken);
    return ActResult::done(ValueOf<std::vector<std::string>>::to(taken));
}

ActResult FileDrop::activate(ActContext&) {
    if (props.disabled) return ActResult::fail("it is disabled");
    emit_requested();
    return ActResult::done();
}

void FileDrop::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    if (!refused_.empty()) node.description = refused_;
    if (props.disabled) node.states.set(State::disabled, true);
}

}  // namespace omui
