module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

namespace {

constexpr float choice_row = 32, choice_pad = 4;
constexpr std::size_t choice_rows = 8;

std::string folded(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return out;
}

bool holds(std::string_view text, std::string_view typed) { return typed.empty() || folded(text).find(folded(typed)) != std::string::npos; }

std::string trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return std::string(s);
}

Value list_of(const std::vector<std::string>& v) {
    Value::List l;
    for (const std::string& s : v) l.emplace_back(s);
    return Value(std::move(l));
}

NodeId child_named(const Services& s, NodeId node, std::string_view name) {
    for (NodeId c : s.children(node))
        if (parts::segment(s, c) == name) return c;
    return {};
}

}  // namespace

// ── the popup they share ──

bool ChoicePopup::set_prop(std::string_view k, const Value& v) {
    if (k == "open") open_ = v.as_bool();
    else if (k == "choices") choices_ = v;
    else if (k == "highlight") highlight_ = static_cast<int>(v.as_int());
    else if (k == "action") action_ = std::string(v.as_string());
    else if (k == "arg") arg_ = std::string(v.as_string());
    else if (k == "chosen") chosen_ = std::string(v.as_string());
    else return false;
    if (services_) {
        services_->restyle(node_);
        services_->invalidate_layout(node_);
    }
    return true;
}

Value ChoicePopup::get_prop(std::string_view k) const {
    if (k == "open") return Value(open_);
    if (k == "highlight") return Value(static_cast<std::int64_t>(highlight_));
    // Where a choice is in the window, for its holder's items.
    if (k.starts_with("at:") && services_) {
        std::size_t i = 0;
        const std::string_view n = k.substr(3);
        if (std::from_chars(n.data(), n.data() + n.size(), i).ec != std::errc{}) return {};
        const Rect me = services_->bounds(node_);
        return Value(Value::List{Value(static_cast<double>(me.x + choice_pad)), Value(static_cast<double>(me.y + choice_pad + static_cast<float>(i) * choice_row)),
                                 Value(static_cast<double>(me.width - 2 * choice_pad)), Value(static_cast<double>(choice_row))});
    }
    return {};
}

void ChoicePopup::style(const Theme&, mlibs::layout::Style& s) const {
    const std::size_t n = choices_.list() ? choices_.list()->size() : 0;
    s.display = open_ && n > 0 ? mlibs::layout::Display::flex : mlibs::layout::Display::none;
}

Size ChoicePopup::measure(MeasureContext&, std::optional<float> width, std::optional<float>, mlibs::layout::Available, mlibs::layout::Available) {
    const std::size_t n = choices_.list() ? std::min(choices_.list()->size(), choice_rows) : 0;
    return {width.value_or(200), 2 * choice_pad + static_cast<float>(n) * choice_row};
}

void ChoicePopup::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 8)).fitted(ctx.size);
    parts::shadow(ctx, "lg", box, r);
    ctx.list.fill_round_rect(box, r, xrender::Paint::solid(th.color("surface")));
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, xrender::Paint::solid(th.color("border")));
    const Value::List* l = choices_.list();
    if (!l) return;
    const std::size_t n = std::min(l->size(), choice_rows);
    if (labels_.size() < n) labels_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Rect row{choice_pad, choice_pad + static_cast<float>(i) * choice_row, ctx.size.width - 2 * choice_pad, choice_row};
        if (static_cast<int>(i) == highlight_) ctx.list.fill_round_rect(row, CornerRadii::all(th.corner("small", 4)), xrender::Paint::solid(th.color("surface_variant")));
        parts::Label& lab = labels_[i];
        lab.set_style("body");
        lab.set_max_lines(1);
        const Value* text = (*l)[i].get("label");
        const Value* value = (*l)[i].get("value");
        const bool chosen = value && !chosen_.empty() && value->as_string() == chosen_;
        lab.set_text(std::string(text ? text->as_string() : ""));
        const Color c = chosen ? th.color("accent") : th.color("text");
        const Size t = lab.measure(ctx.services, c, row.width - 20);
        lab.paint(ctx, {row.x + 10, row.center().y - t.height / 2}, c);
    }
}

bool ChoicePopup::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    const Value::List* l = choices_.list();
    if (!p || !l) return false;
    const int i = static_cast<int>((ctx.local.y - choice_pad) / choice_row);
    if (i < 0 || static_cast<std::size_t>(i) >= std::min(l->size(), choice_rows)) return true;
    if (p->kind == PointerKind::move && i != highlight_) {
        highlight_ = i;  // its holder reads it when Enter chooses
        ctx.repaint();
    }
    if (p->kind == PointerKind::up && (p->type != PointerType::mouse || p->button == MouseButton::left)) {
        const Value* v = (*l)[static_cast<std::size_t>(i)].get("value");
        // The holder's own action, as the person: journaled, taken back.
        (void)services_->perform(services_->parent(node_), action_, Value{}.set(arg_, v ? *v : Value{}));
    }
    return true;
}

void ChoicePopup::semantics(SemanticsContext&, spec::SemanticNode& node) const { node.actions.push_back({"dismiss", Effect::reversible, {}, {}, {}}); }

ActResult ChoicePopup::act(ActContext&, std::string_view action) {
    if (action != "dismiss") return ActResult::fail(std::format("the popup has no action '{}'", action));
    services_->set(node_, "open", Value(false));
    return ActResult::done();
}

// ── combobox ──

std::string Combobox::label_of(std::string_view value) const {
    for (std::size_t i = 0; i < props.options.size(); ++i)
        if (props.options[i] == value) return i < props.labels.size() ? props.labels[i] : props.options[i];
    return std::string(value);
}

std::vector<std::size_t> Combobox::matches(std::string_view typed) const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < props.options.size(); ++i)
        if (holds(i < props.labels.size() ? props.labels[i] : props.options[i], typed)) out.push_back(i);
    return out;
}

void Combobox::mounted() { query_.set(label_of(props.value)); }

void Combobox::prop_changed(ComboboxProp p) {
    if (p == ComboboxProp::value && filter_.empty()) query_.set(label_of(props.value));
    if (!attached()) return;
    if (p == ComboboxProp::options || p == ComboboxProp::labels) show(expanded());
    services().invalidate_semantics(node());
}

void Combobox::style(const Theme&, mlibs::layout::Style& s) const {
    s.direction = mlibs::layout::Direction::column;
    s.min_width = mlibs::layout::pt(200);
}

std::vector<Element> Combobox::compose() {
    Element field = text_box({.id = "field",
                              .disabled = props.disabled,
                              .label = props.label,
                              .placeholder = props.placeholder,
                              .text = query_,
                              .on_changed = [this](std::string t) { typed(std::move(t)); },
                              .on_submitted = [this](std::string) { commit(); },
                              .width = Dim(mlibs::layout::pct(100))});
    Element pop;
    pop.type = &component_type<ChoicePopup>();
    pop.id = "popup";
    pop.prop("action", Source{Value("set_value"), {}, {}});
    pop.prop("arg", Source{Value("value"), {}, {}});
    return {std::move(field), std::move(pop)};
}

NodeId Combobox::popup() const { return child_named(services(), node(), "popup"); }

bool Combobox::expanded() const {
    const NodeId p = attached() ? popup() : NodeId{};
    return p && services().get(p, "open").as_bool() && !shown_.empty();
}

void Combobox::show(bool open) {
    const NodeId p = popup();
    if (!p) return;
    shown_ = matches(filter_);
    highlight_ = std::clamp(highlight_, 0, std::max(0, static_cast<int>(shown_.size()) - 1));
    Value::List choices;
    for (const std::size_t i : shown_) choices.push_back(Value{}.set("value", props.options[i]).set("label", label_of(props.options[i])));
    services().set(p, "choices", Value(std::move(choices)));
    services().set(p, "chosen", Value(props.value));
    services().set(p, "highlight", Value(static_cast<std::int64_t>(highlight_)));
    services().set(p, "open", Value(open));
    services().invalidate_semantics(node());
}

void Combobox::typed(std::string text) {
    if (text == label_of(props.value) && filter_.empty()) return;  // the chosen value's label, set from here
    filter_ = std::move(text);
    highlight_ = 0;
    show(true);
}

void Combobox::commit() {
    // Enter: the match the arrows are on; else what is typed, when it may stand or names an option.
    std::string to;
    if (expanded()) highlight_ = static_cast<int>(services().get(popup(), "highlight").as_int(highlight_));  // the pointer may have moved it
    if (expanded() && highlight_ < static_cast<int>(shown_.size())) to = props.options[shown_[static_cast<std::size_t>(highlight_)]];
    else
        for (std::size_t i = 0; i < props.options.size() && to.empty(); ++i)
            if (folded(label_of(props.options[i])) == folded(query_.peek())) to = props.options[i];
    if (to.empty() && props.free) to = trimmed(query_.peek());
    if (!to.empty()) (void)services().perform(node(), "set_value", Value{}.set("value", to));
}

bool Combobox::on_event(EventContext&, const InputEvent& e) {
    const auto* k = std::get_if<KeyEvent>(&e);
    if (!k || k->kind != KeyKind::down || props.disabled) return false;
    if (k->key == Key::down || k->key == Key::up) {
        if (!expanded()) {
            const auto all = matches(filter_);
            const auto at = std::ranges::find_if(all, [&](std::size_t i) { return props.options[i] == props.value; });
            highlight_ = at == all.end() ? 0 : static_cast<int>(at - all.begin());
            show(true);
        } else {
            const int n = static_cast<int>(shown_.size());
            highlight_ = n ? (highlight_ + (k->key == Key::down ? 1 : n - 1)) % n : 0;
            show(true);
        }
        return true;
    }
    if (k->key == Key::escape && expanded()) {
        if (!props.free) {
            filter_.clear();
            query_.set(label_of(props.value));
        }
        show(false);
        return true;
    }
    return false;
}

ActResult Combobox::choose(std::string value, bool restoring) {
    if (!restoring && !value.empty() && !props.free && std::ranges::find(props.options, value) == props.options.end())
        return ActResult::fail(std::format("`{}` is not an option", value));
    const std::string old = props.value;
    filter_.clear();
    if (value != props.value) {
        write_value(value);
        emit_changed(value);
    }
    query_.set(label_of(props.value));
    show(false);
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", old));
}

ActResult Combobox::set_value(ActContext& ctx, const Value& value) { return choose(std::string(value.as_string()), ctx.restoring); }

ActResult Combobox::expand(ActContext&) {
    if (props.disabled) return ActResult::fail("it is disabled");
    filter_.clear();
    // Lit on the chosen one, as the arrows go on from it.
    const auto all = matches("");
    const auto at = std::ranges::find_if(all, [&](std::size_t i) { return props.options[i] == props.value; });
    highlight_ = at == all.end() ? 0 : static_cast<int>(at - all.begin());
    show(true);
    return ActResult::done();
}

ActResult Combobox::collapse(ActContext&) {
    show(false);
    return ActResult::done();
}

void Combobox::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    Value::List options;
    for (const std::string& o : props.options) options.emplace_back(o);
    node.value = Value{}.set("value", props.value).set("options", Value(std::move(options)));
    node.states.set(State::expanded, expanded()).set(State::collapsible);
}

std::vector<spec::SemanticNode> Combobox::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (const std::string& o : props.options) out.push_back(*virtual_item(path, o));
    return out;
}

std::optional<spec::SemanticNode> Combobox::virtual_item(std::string_view path, std::string_view key) const {
    if (std::ranges::find(props.options, key) == props.options.end()) return std::nullopt;
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, key);
    n.role = Role::option;
    n.name = label_of(key);
    n.states.set(State::selected, key == props.value);
    // Shown while the popup offers it; else read, not seen.
    const auto at = std::ranges::find_if(shown_, [&](std::size_t i) { return props.options[i] == key; });
    if (expanded() && at != shown_.end()) {
        const Value r = services().get(popup(), std::format("at:{}", at - shown_.begin()));
        if (const Value::List* l = r.list(); l && l->size() == 4)
            n.bounds = spec::Rect{static_cast<float>((*l)[0].as_double()), static_cast<float>((*l)[1].as_double()), static_cast<float>((*l)[2].as_double()), static_cast<float>((*l)[3].as_double())};
    } else n.states.set(State::offscreen);
    if (!props.disabled) n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    return n;
}

ActResult Combobox::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    if (action != "activate") return act(ctx, action);  // an inverse taken back on the item (S-12): its own set_value
    if (props.disabled) return ActResult::fail("it is disabled");
    return choose(std::string(key));
}

// ── tag input ──

std::vector<std::string> TagInput::offered(std::string_view typed) const {
    std::vector<std::string> out;
    for (const std::string& s : props.suggestions)
        if (holds(s, typed) && std::ranges::find(props.tags, s) == props.tags.end()) out.push_back(s);
    return out;
}

void TagInput::mounted() {}

void TagInput::prop_changed(TagInputProp p) {
    if (!attached()) return;
    if (p == TagInputProp::tags || p == TagInputProp::disabled) services().recompose(node());
    services().invalidate_semantics(node());
}

void TagInput::style(const Theme& theme, mlibs::layout::Style& s) const {
    s.direction = mlibs::layout::Direction::row;
    s.wrap = mlibs::layout::Wrap::wrap;
    s.align_items = mlibs::layout::Align::center;
    s.row_gap = s.column_gap = 6;
    s.padding = Insets::all(4);
    s.min_height = mlibs::layout::pt(theme.scaled(36));
    s.border = Insets::all(1);
}

void TagInput::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 8)).fitted(ctx.size);
    ctx.list.fill_round_rect(box, r, xrender::Paint::solid(th.color("surface")));
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, xrender::Paint::solid(th.color("border")));
}

std::vector<Element> TagInput::compose() {
    std::vector<Element> out;
    for (std::size_t i = 0; i < props.tags.size(); ++i) {
        const std::string tag = props.tags[i];
        Element c = chip({.id = std::format("tag_{}", i),
                          .disabled = props.disabled,
                          .removable = !props.disabled,
                          .text = tag,
                          .on_removed = [this, tag] { (void)services().perform(node(), "remove", Value{}.set("tag", tag)); }});
        c.key = "tag:" + tag;
        out.push_back(std::move(c));
    }
    // The field takes what is left of the line (a new line when little is).
    Element field = text_box({.id = "field",
                              .disabled = props.disabled,
                              .label = props.label.empty() ? std::string("Add a tag") : props.label,
                              .placeholder = props.placeholder,
                              .text = query_,
                              .on_changed = [this](std::string t) { typed(std::move(t)); },
                              .on_submitted = [this](std::string) { commit(); },
                              .min_width = Dim(120.0f),
                              .grow = 1});
    field.prop("bare", Source{Value(true), {}, {}});  // the tag input's own surface and border around it all
    out.push_back(std::move(field));
    Element pop;
    pop.type = &component_type<ChoicePopup>();
    pop.id = "popup";
    pop.prop("action", Source{Value("add"), {}, {}});
    pop.prop("arg", Source{Value("tag"), {}, {}});
    out.push_back(std::move(pop));
    return out;
}

NodeId TagInput::popup() const { return child_named(services(), node(), "popup"); }

void TagInput::show(bool open) {
    const NodeId p = popup();
    if (!p) return;
    shown_ = offered(query_.peek());
    highlight_ = std::clamp(highlight_, 0, std::max(0, static_cast<int>(shown_.size()) - 1));
    Value::List choices;
    for (const std::string& s : shown_) choices.push_back(Value{}.set("value", s).set("label", s));
    services().set(p, "choices", Value(std::move(choices)));
    services().set(p, "highlight", Value(static_cast<std::int64_t>(highlight_)));
    services().set(p, "open", Value(open && !query_.peek().empty()));
}

void TagInput::typed(std::string text) {
    // A comma keeps what came before it as a tag.
    if (const auto comma = text.find(','); comma != std::string::npos) {
        const std::string tag = trimmed(std::string_view(text).substr(0, comma));
        query_.set(trimmed(std::string_view(text).substr(comma + 1)));
        if (!tag.empty()) (void)services().perform(node(), "add", Value{}.set("tag", tag));
        return;
    }
    highlight_ = 0;
    show(true);
}

void TagInput::commit() {
    const NodeId p = popup();
    const bool open = p && services().get(p, "open").as_bool() && !shown_.empty();
    if (open) highlight_ = static_cast<int>(services().get(p, "highlight").as_int(highlight_));
    std::string tag = open ? shown_[static_cast<std::size_t>(std::clamp(highlight_, 0, static_cast<int>(shown_.size()) - 1))] : trimmed(query_.peek());
    if (!tag.empty()) (void)services().perform(node(), "add", Value{}.set("tag", tag));
}

bool TagInput::on_event(EventContext&, const InputEvent& e) {
    const auto* k = std::get_if<KeyEvent>(&e);
    if (!k || k->kind != KeyKind::down || props.disabled) return false;
    if ((k->key == Key::down || k->key == Key::up) && !shown_.empty()) {
        const int n = static_cast<int>(shown_.size());
        highlight_ = (highlight_ + (k->key == Key::down ? 1 : n - 1)) % n;
        show(true);
        return true;
    }
    if (k->key == Key::escape) {
        if (const NodeId p = popup()) services().set(p, "open", Value(false));
        return true;
    }
    return false;
}

ActResult TagInput::to(std::vector<std::string> tags) {
    const std::vector<std::string> old = props.tags;
    if (tags != props.tags) {
        write_tags(tags);
        services().recompose(node());
        services().invalidate_semantics(node());
    }
    return ActResult::done(list_of(props.tags)).undo_by("set_tags", Value{}.set("tags", list_of(old)));
}

ActResult TagInput::add(ActContext&, const Value& tag) {
    const std::string t = trimmed(tag.as_string());
    if (t.empty()) return ActResult::fail("a tag is a word or a name, not nothing");
    if (std::ranges::find(props.tags, t) != props.tags.end()) return ActResult::fail(std::format("`{}` is a tag already", t));
    if (!props.free && std::ranges::find(props.suggestions, t) == props.suggestions.end()) return ActResult::fail(std::format("`{}` is not one it suggests", t));
    auto tags = props.tags;
    tags.push_back(t);
    ActResult r = to(std::move(tags));
    emit_added(t);
    query_.set(std::string());
    show(false);
    return r;
}

ActResult TagInput::remove(ActContext&, const Value& tag) {
    const std::string t(tag.as_string());
    if (std::ranges::find(props.tags, t) == props.tags.end()) return ActResult::fail(std::format("`{}` is not a tag", t));
    auto tags = props.tags;
    std::erase(tags, t);
    ActResult r = to(std::move(tags));
    emit_removed(t);
    return r;
}

ActResult TagInput::set_tags(ActContext&, const Value& tags) {
    if (!tags.list()) return ActResult::fail("set_tags takes a list of tags");
    std::vector<std::string> v;
    for (const Value& x : *tags.list()) v.emplace_back(x.as_string());
    return to(std::move(v));
}

void TagInput::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value{}.set("tags", list_of(props.tags));
}

}  // namespace omui
