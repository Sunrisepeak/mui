// openmui.ui.base:navigating — context menu, hover card, command palette, sidebar, toolbar.
module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

namespace {

constexpr float palette_w = 480, command_h = 34, palette_rows = 8;  // a command palette
constexpr float place_h = 36, rail_w = 56, side_w = 220, side_pad = 8;  // a sidebar

bool primary(const PointerEvent& p) { return p.type != PointerType::mouse || p.button == MouseButton::left; }

spec::Rect in_window(const Rect& node, const Rect& part) { return {node.x + part.x, node.y + part.y, part.width, part.height}; }

// The element an attached overlay is for: the one before it, else its parent.
NodeId element_before(Services& services, NodeId node) {
    const NodeId parent = services.parent(node);
    NodeId target = parent;
    for (NodeId child : services.children(parent)) {
        if (child == node) break;
        target = child;
    }
    return target;
}

// A raised surface: an overlay's.
void raised(PaintContext& ctx, std::string_view corner, float fallback) {
    const Theme& theme = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii corners = CornerRadii::all(theme.corner(corner, fallback)).fitted(ctx.size);
    parts::shadow(ctx, "lg", box, corners);
    ctx.list.fill_round_rect(box, corners, xrender::Paint::solid(theme.color("surface")));
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, corners, 1, xrender::Paint::solid(theme.color("border")));
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

// ── context menu ──

ContextMenu::~ContextMenu() {
    if (!attached()) return;
    if (timer_) services().cancel(timer_);
    for (const std::uint64_t token : listening_) services().off(token);
}

Placement ContextMenu::placement() const {
    Placement where{Placement::Side::below, Placement::Anchor::previous, 0, false, false, true, false};
    where.point = at_;
    return where;
}

void ContextMenu::mounted() {
    was_open_ = props.open;
    // The tree around it is complete at the next frame: its element then, and what asks for its choices.
    timer_ = services().after(seconds(0), [this] {
        timer_ = 0;
        target_ = element_before(services(), node());
        listening_.push_back(services().on(target_, "pointer_down", [this](const Value& args) {
            if (!args.get("button") || args.get("button")->as_string() != "right") return;
            const Rect bounds = services().bounds(target_);
            open_at({bounds.x + static_cast<float>(args.get("x")->as_double()), bounds.y + static_cast<float>(args.get("y")->as_double())});
        }));
        listening_.push_back(services().on(target_, "key_down", [this](const Value& args) {
            if (!args.get("key") || args.get("key")->as_string() != "ContextMenu") return;
            const Rect bounds = services().bounds(target_);
            open_at({bounds.x + 8, bounds.bottom()});
        }));
    });
}

void ContextMenu::open_at(Point at) {
    at_ = at;
    services().set(node(), "open", Value(true));
    if (!props.open) {
        props.open = true;  // unbound: open all the same
        prop_changed(ContextMenuProp::open);
    }
    services().invalidate_layout(node());
}

void ContextMenu::close() {
    if (!props.open) return;
    services().set(node(), "open", Value(false));
    if (props.open) {
        props.open = false;
        prop_changed(ContextMenuProp::open);
    }
}

void ContextMenu::prop_changed(ContextMenuProp p) {
    if (p != ContextMenuProp::open || !attached()) return;
    services().restyle(node());
    services().invalidate_semantics(node());
    if (props.open == was_open_) return;
    was_open_ = props.open;
    if (props.open) {
        // The first choice that can take focus takes it (menus are keyboard-first).
        for (NodeId child : services().children(node()))
            if (!services().get(child, "disabled").as_bool()) {
                services().focus(child);
                break;
            }
    } else {
        if (target_) services().focus(target_);
        emit_closed();
    }
}

void ContextMenu::style(const Theme&, mlibs::layout::Style& s) const {
    s.display = props.open ? mlibs::layout::Display::flex : mlibs::layout::Display::none;
    s.direction = mlibs::layout::Direction::column;
    s.padding = Insets::all(4);
    s.min_width = mlibs::layout::pt(180);
    s.border = Insets::all(1);
}

void ContextMenu::paint(PaintContext& ctx) const { raised(ctx, "medium", 8); }

void ContextMenu::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.states.set(State::expanded, props.open);
}

ActResult ContextMenu::show(ActContext&, const ShowArgs& args) {
    if (args.x.is_number() && args.y.is_number()) open_at({static_cast<float>(args.x.as_double()), static_cast<float>(args.y.as_double())});
    else {
        const Rect bounds = target_ ? services().bounds(target_) : services().bounds(node());
        open_at({bounds.x + 8, bounds.bottom()});
    }
    return ActResult::done().undo_by("dismiss");
}

ActResult ContextMenu::dismiss(ActContext&) {
    close();
    return ActResult::done();
}

// ── hover card ──

HoverCard::~HoverCard() {
    if (!attached()) return;
    if (timer_) services().cancel(timer_);
    for (const std::uint64_t token : listening_) services().off(token);
}

void HoverCard::mounted() {
    timer_ = services().after(seconds(0), [this] {
        timer_ = 0;
        target_ = element_before(services(), node());
        listening_.push_back(services().on(target_, "pointer_enter", [this](const Value&) { pointed(true); }));
        listening_.push_back(services().on(target_, "focus", [this](const Value&) { pointed(true); }));
        listening_.push_back(services().on(target_, "pointer_leave", [this](const Value&) { pointed(false); }));
        listening_.push_back(services().on(target_, "blur", [this](const Value&) { pointed(false); }));
        // The pointer on the card keeps it, as on its element.
        listening_.push_back(services().on(node(), "pointer_enter", [this](const Value&) {
            over_card_ = true;
            pointed(over_target_);
        }));
        listening_.push_back(services().on(node(), "pointer_leave", [this](const Value&) {
            over_card_ = false;
            pointed(over_target_);
        }));
    });
}

void HoverCard::pointed(bool at) {
    over_target_ = at;
    if (timer_) services().cancel(timer_), timer_ = 0;
    // Pointed at (the element or the card): shown after `delay`; neither: gone after `linger`.
    const bool wanted = over_target_ || over_card_;
    if (wanted == props.open) return;
    timer_ = services().after(seconds(wanted ? props.delay : props.linger), [this, wanted] {
        timer_ = 0;
        set_open(wanted);
    });
}

void HoverCard::set_open(bool open) {
    services().set(node(), "open", Value(open));
    if (props.open != open) {
        props.open = open;
        prop_changed(HoverCardProp::open);
    }
}

void HoverCard::prop_changed(HoverCardProp p) {
    if (p != HoverCardProp::open || !attached()) return;
    services().restyle(node());
    services().invalidate_semantics(node());
}

void HoverCard::style(const Theme&, mlibs::layout::Style& s) const {
    s.display = props.open ? mlibs::layout::Display::flex : mlibs::layout::Display::none;
    s.direction = mlibs::layout::Direction::column;
    s.padding = Insets::all(12);
    s.row_gap = 6;
    s.max_width = mlibs::layout::pt(320);
    s.border = Insets::all(1);
}

void HoverCard::paint(PaintContext& ctx) const { raised(ctx, "large", 12); }

void HoverCard::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.states.set(State::expanded, props.open);
}

ActResult HoverCard::show(ActContext&) {
    set_open(true);
    return ActResult::done().undo_by("dismiss");
}

ActResult HoverCard::dismiss(ActContext&) {
    set_open(false);
    return ActResult::done().undo_by("show");
}

// ── command palette ──

std::vector<const VerbDesc*> CommandPalette::matching(std::string_view query) const {
    std::vector<const VerbDesc*> offered;
    if (!attached()) return offered;
    Verbs& verbs = services().verbs();
    if (props.verbs.empty()) {
        for (const VerbDesc* desc : verbs.list())
            if (desc->args.empty() && !desc->person) offered.push_back(desc);
    } else {
        for (const std::string& name : props.verbs)
            if (const VerbDesc* desc = verbs.find(name)) offered.push_back(desc);
    }
    std::vector<std::string> words;
    for (const auto part : std::views::split(std::string_view(query), ' '))
        if (const std::string_view word(part.begin(), part.end()); !word.empty()) words.push_back(lower(word));
    std::erase_if(offered, [&](const VerbDesc* desc) {
        const std::string said = lower(desc->name + " " + desc->summary);
        return !std::ranges::all_of(words, [&](const std::string& word) { return said.contains(word); });
    });
    // Those whose words begin with the first word first, then by name.
    std::ranges::stable_sort(offered, [&](const VerbDesc* a, const VerbDesc* b) {
        const auto starts = [&](const VerbDesc* d) {
            return !words.empty() && (lower(d->summary).starts_with(words.front()) || lower(d->name).starts_with(words.front()));
        };
        if (starts(a) != starts(b)) return starts(a);
        return a->name < b->name;
    });
    return offered;
}

void CommandPalette::refresh() {
    shown_.clear();
    for (const VerbDesc* desc : matching(props.query)) shown_.push_back({desc->name, desc->summary.empty() ? desc->name : desc->summary});
    titles_ = std::vector<parts::Label>(shown_.size());
    for (std::size_t index = 0; index < shown_.size(); ++index) {
        titles_[index].set_style("body");
        titles_[index].set_text(shown_[index].title);
    }
    chosen_ = std::min(chosen_, shown_.empty() ? 0 : shown_.size() - 1);
    if (!attached()) return;
    services().restyle(node());
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

void CommandPalette::mounted() {
    was_open_ = props.open;
    refresh();
}

void CommandPalette::prop_changed(CommandPaletteProp p) {
    if (p == CommandPaletteProp::query || p == CommandPaletteProp::verbs) {
        chosen_ = 0;
        refresh();
    }
    if (p != CommandPaletteProp::open || !attached()) return;
    if (props.open && !was_open_) {
        outcome_.clear();
        refresh();  // the verbs as they are now
        const auto children = services().children(node());
        if (!children.empty()) services().focus(children.front());
    }
    if (!props.open && was_open_) emit_closed();
    was_open_ = props.open;
    services().restyle(node());
    services().invalidate_semantics(node());
}

void CommandPalette::set_open(bool open) {
    services().set(node(), "open", Value(open));
    if (props.open != open) {
        props.open = open;
        prop_changed(CommandPaletteProp::open);
    }
}

void CommandPalette::style(const Theme&, mlibs::layout::Style& s) const {
    // The field on top; the commands listed under it, in the room kept for them.
    const float rows = std::min(palette_rows, static_cast<float>(shown_.size()));
    s.display = props.open ? mlibs::layout::Display::flex : mlibs::layout::Display::none;
    s.direction = mlibs::layout::Direction::column;
    s.width = mlibs::layout::pt(palette_w);
    s.padding = {8, 8, 8 + rows * command_h + (outcome_.empty() ? 0.0f : 22.0f), 8};
    s.border = Insets::all(1);
}

std::vector<Element> CommandPalette::compose() {
    Element field;
    field.type = &component_type<TextBox>();
    field.id = "field";
    field.prop("text", Source{Value(props.query), {}, {}});
    field.prop("placeholder", Source{Value(props.placeholder), {}, {}});
    field.prop("label", Source{Value(props.label), {}, {}});
    field.prop("bare", Source{Value(true), {}, {}});
    field.on("changed", [this](const Value& args) {
        const std::string text(args.get("text") ? args.get("text")->as_string() : std::string_view{});
        if (text == props.query) return;
        write_query(text);
        chosen_ = 0;
        refresh();
    });
    // The field keeps the arrows and Enter: they are the palette's while it is open.
    field.on("key_down", [this](const Value& args) {
        const std::string_view key = args.get("key") ? args.get("key")->as_string() : std::string_view{};
        if (key == "Down") chosen_ = shown_.empty() ? 0 : std::min(chosen_ + 1, shown_.size() - 1);
        else if (key == "Up") chosen_ = chosen_ > 0 ? chosen_ - 1 : 0;
        else if (key == "Escape") set_open(false);
        else return;
        services().invalidate_paint(node());
    });
    field.on("submitted", [this](const Value&) {
        if (chosen_ < shown_.size()) (void)run_verb(shown_[chosen_].verb);
    });
    return {std::move(field)};
}

void CommandPalette::paint(PaintContext& ctx) const {
    raised(ctx, "large", 12);
    const Theme& theme = ctx.theme;
    const float top = ctx.size.height - 8 - std::min(palette_rows, static_cast<float>(shown_.size())) * command_h - (outcome_.empty() ? 0.0f : 22.0f);
    ctx.list.fill_rect({8, top - 4, ctx.size.width - 16, 1}, theme.color("border"));
    for (std::size_t index = 0; index < shown_.size() && index < static_cast<std::size_t>(palette_rows); ++index) {
        const Rect row{8, top + static_cast<float>(index) * command_h, ctx.size.width - 16, command_h};
        if (index == chosen_)
            ctx.part({.role = "selection", .reference = shown_[index].verb, .allocation = row, .kind = xrender::PartKind::box},
                     [&] { ctx.list.fill_round_rect(row, CornerRadii::all(6), xrender::Paint::solid(theme.color("accent").with_alpha(0.12f))); });
        const Color ink = index == chosen_ ? theme.color("accent") : theme.color("text");
        const Size text = titles_[index].measure(ctx.services, ink);
        titles_[index].paint(ctx, {row.x + 10, row.y + (row.height - text.height) / 2}, ink);
    }
    if (!outcome_.empty()) {
        parts::Label said;
        said.set_style("caption");
        said.set_text(outcome_);
        said.paint(ctx, {16, ctx.size.height - 8 - 20}, theme.color("danger"));
    }
}

bool CommandPalette::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || !props.open) return false;
    const float top = ctx.services.bounds(ctx.node).height - 8 - std::min(palette_rows, static_cast<float>(shown_.size())) * command_h - (outcome_.empty() ? 0.0f : 22.0f);
    if (ctx.local.y < top) return false;
    const auto index = static_cast<std::size_t>((ctx.local.y - top) / command_h);
    if (index >= shown_.size()) return false;
    if (p->kind == PointerKind::move && index != chosen_) chosen_ = index, ctx.repaint();
    if (p->kind == PointerKind::up && primary(*p)) {
        chosen_ = index;
        (void)run_verb(shown_[index].verb);
        return true;
    }
    return p->kind == PointerKind::down;
}

ActResult CommandPalette::run_verb(const std::string& verb) {
    // The person's choice: their answer to what reaches outside, as a click is; what cannot be taken back
    // still asks (its confirmation is the application's) — said in the palette, which stays.
    const VerbDesc* desc = services().verbs().find(verb);
    if (!desc) return ActResult::fail(std::format("no verb {}", verb));
    Invocation call;
    call.actor = {"person", "user", {}};
    if (desc->effect == Effect::external) call.answer = desc->answer;
    const Envelope answer = services().verbs().invoke(verb, call);
    if (answer.exit_code().value_or(1) != 0) {
        outcome_ = std::format("{} did not run", verb);
        for (const auto& record : answer.records())
            if (record.kind == spec::RecordKind::error)
                if (const Value* text = record.body.get("text")) outcome_ = std::string(text->as_string());
        services().restyle(node());
        services().invalidate_paint(node());
        return ActResult::fail(outcome_);
    }
    emit_ran(verb);
    set_open(false);
    return ActResult::done(Value(verb));
}

ActResult CommandPalette::show(ActContext&) {
    set_open(true);
    return ActResult::done().undo_by("dismiss");
}

ActResult CommandPalette::dismiss(ActContext&) {
    set_open(false);
    return ActResult::done();
}

ActResult CommandPalette::set_query(ActContext&, const Value& query) {
    const std::string old = props.query;
    write_query(std::string(query.as_string()));
    chosen_ = 0;
    refresh();
    services().recompose(node());  // its field shows the words
    return ActResult::done(Value(props.query)).undo_by("set_query", Value{}.set("query", old));
}

ActResult CommandPalette::next(ActContext&) {
    chosen_ = shown_.empty() ? 0 : std::min(chosen_ + 1, shown_.size() - 1);
    services().invalidate_paint(node());
    return ActResult::done();
}

ActResult CommandPalette::previous(ActContext&) {
    chosen_ = chosen_ > 0 ? chosen_ - 1 : 0;
    services().invalidate_paint(node());
    return ActResult::done();
}

ActResult CommandPalette::run(ActContext&) {
    if (chosen_ >= shown_.size()) return ActResult::fail("no command matches");
    return run_verb(shown_[chosen_].verb);
}

void CommandPalette::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value(props.query);
    node.states.set(State::expanded, props.open);
    if (!outcome_.empty()) node.description = outcome_;
}

std::vector<spec::SemanticNode> CommandPalette::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    if (!props.open) return out;
    for (std::size_t index = 0; index < shown_.size(); ++index)
        if (auto one = virtual_item(path, shown_[index].verb)) out.push_back(std::move(*one));
    return out;
}

std::optional<spec::SemanticNode> CommandPalette::virtual_item(std::string_view path, std::string_view key) const {
    for (std::size_t index = 0; index < shown_.size(); ++index) {
        if (shown_[index].verb != key) continue;
        spec::SemanticNode n;
        n.id = std::format("{}/{}", path, key);
        n.role = Role::option;
        n.name = shown_[index].title;
        n.value = Value(shown_[index].verb);
        n.states.set(State::selected, index == chosen_);
        n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
        return n;
    }
    return std::nullopt;
}

ActResult CommandPalette::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    if (action != "activate") return act(ctx, action);
    return run_verb(std::string(key));
}

// ── sidebar ──

void Sidebar::mounted() { prop_changed(SidebarProp::items); }

void Sidebar::prop_changed(SidebarProp) {
    if (labels_.size() != props.items.size()) {
        labels_ = std::vector<parts::Label>(props.items.size());
        initials_ = std::vector<parts::Label>(props.items.size());
    }
    for (std::size_t index = 0; index < labels_.size(); ++index) {
        const std::string label = label_of(index);
        labels_[index].set_style("label");
        labels_[index].set_text(label);
        initials_[index].set_style("label");
        // The first letter (a whole UTF-8 character), as a rail shows it.
        std::size_t first = label.empty() ? 0 : 1;
        while (first < label.size() && (static_cast<unsigned char>(label[first]) & 0xC0) == 0x80) ++first;
        initials_[index].set_text(label.substr(0, first));
    }
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

std::string Sidebar::label_of(std::size_t index) const { return index < props.labels.size() ? props.labels[index] : props.items[index]; }

std::optional<std::size_t> Sidebar::index_of(std::string_view value) const {
    for (std::size_t index = 0; index < props.items.size(); ++index)
        if (props.items[index] == value) return index;
    return std::nullopt;
}

Size Sidebar::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available ah) {
    const float tall = 2 * side_pad + static_cast<float>(props.items.size()) * place_h;
    const float available = ah.kind == mlibs::layout::Available::Kind::definite ? std::max(ah.value, tall) : tall;
    return {width.value_or(props.collapsed ? rail_w : side_w), height.value_or(available)};
}

void Sidebar::paint(PaintContext& ctx) const {
    const Theme& theme = ctx.theme;
    ctx.list.fill_rect({0, 0, ctx.size.width, ctx.size.height}, theme.color("surface_variant"));
    ctx.list.fill_rect({ctx.size.width - 1, 0, 1, ctx.size.height}, theme.color("border"));
    at_.clear();
    const auto current = index_of(props.selected);
    for (std::size_t index = 0; index < labels_.size(); ++index) {
        const Rect row{side_pad, side_pad + static_cast<float>(index) * place_h, ctx.size.width - 2 * side_pad, place_h - 4};
        at_.push_back(row);
        const CornerRadii corners = CornerRadii::all(theme.corner("medium", 8)).fitted(row.size());
        const bool here = current == index;
        if (here)
            ctx.part({.role = "current", .reference = props.items[index], .allocation = row, .kind = xrender::PartKind::box},
                     [&] { ctx.list.fill_round_rect(row, corners, xrender::Paint::solid(theme.color("accent").with_alpha(0.14f))); });
        else if (hover_ == static_cast<int>(index))
            ctx.list.fill_round_rect(row, corners, xrender::Paint::solid(theme.color("surface_pressed").with_alpha(0.6f)));
        const Color ink = here ? theme.color("accent") : theme.color("text");
        parts::Label& shown = props.collapsed ? initials_[index] : labels_[index];
        const Size text = shown.measure(ctx.services, ink);
        const float x = props.collapsed ? row.x + (row.width - text.width) / 2 : row.x + 12;
        shown.paint(ctx, {x, row.y + (row.height - text.height) / 2}, ink);
        if (here && ctx.state.focused && ctx.state.focus_visible) parts::focus_ring(ctx, row, corners);
    }
}

bool Sidebar::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    int over = -1;
    for (std::size_t index = 0; index < at_.size(); ++index)
        if (at_[index].contains(ctx.local)) over = static_cast<int>(index);
    if (p->kind == PointerKind::leave) over = -1;
    if (over != hover_) hover_ = over, ctx.repaint();
    if (p->kind == PointerKind::up && over >= 0 && primary(*p)) {
        (void)services().perform(node(), "set_selected", Value{}.set("selected", props.items[static_cast<std::size_t>(over)]));
        return true;
    }
    return p->kind == PointerKind::down && over >= 0;
}

ActResult Sidebar::choose(std::string value) {
    if (!index_of(value)) return ActResult::fail(std::format("`{}` is not one of its places", value));
    const std::string old = props.selected;
    if (value != props.selected) {
        write_selected(value);
        emit_changed(value);
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
    return ActResult::done(Value(props.selected)).undo_by("set_selected", Value{}.set("selected", old));
}

ActResult Sidebar::fold(bool collapsed) {
    const bool old = props.collapsed;
    if (collapsed != props.collapsed) {
        write_collapsed(collapsed);
        services().invalidate_layout(node());
        services().invalidate_semantics(node());
    }
    return ActResult::done(Value(props.collapsed)).undo_by(old ? "collapse" : "expand");
}

ActResult Sidebar::set_selected(ActContext&, const Value& selected) { return choose(std::string(selected.as_string())); }
ActResult Sidebar::expand(ActContext&) { return fold(false); }
ActResult Sidebar::collapse(ActContext&) { return fold(true); }

// The arrows: as the person's choice of a place (journaled, taken back), as a press is.
ActResult Sidebar::next(ActContext&) {
    if (props.items.empty()) return ActResult::done();
    const auto at = index_of(props.selected);
    (void)services().perform(node(), "set_selected", Value{}.set("selected", props.items[at ? std::min(*at + 1, props.items.size() - 1) : 0]));
    return ActResult::done(Value(props.selected));
}

ActResult Sidebar::previous(ActContext&) {
    if (props.items.empty()) return ActResult::done();
    const auto at = index_of(props.selected);
    (void)services().perform(node(), "set_selected", Value{}.set("selected", props.items[at && *at > 0 ? *at - 1 : 0]));
    return ActResult::done(Value(props.selected));
}

spec::SemanticNode Sidebar::item(std::string_view path, std::size_t index) const {
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, props.items[index]);
    n.role = Role::link;
    n.name = label_of(index);
    const bool here = props.items[index] == props.selected;
    n.states.set(State::selected, here);
    if (index < at_.size() && attached()) n.bounds = in_window(services().bounds(node()), at_[index]);
    if (!here) n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    return n;
}

void Sidebar::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value(props.selected);
    node.states.set(State::expanded, !props.collapsed);
}

std::vector<spec::SemanticNode> Sidebar::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (std::size_t index = 0; index < props.items.size(); ++index) out.push_back(item(path, index));
    return out;
}

std::optional<spec::SemanticNode> Sidebar::virtual_item(std::string_view path, std::string_view key) const {
    const auto index = index_of(key);
    return index ? std::optional(item(path, *index)) : std::nullopt;
}

ActResult Sidebar::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    if (action != "activate") return act(ctx, action);
    return choose(std::string(key));
}

// ── toolbar ──

void Toolbar::style(const Theme&, mlibs::layout::Style& s) const {
    s.direction = props.vertical ? mlibs::layout::Direction::column : mlibs::layout::Direction::row;
    s.row_gap = s.column_gap = 4;
    s.padding = Insets::all(4);
    s.align_items = mlibs::layout::Align::center;
    s.border = Insets::all(1);
}

void Toolbar::paint(PaintContext& ctx) const {
    const Theme& theme = ctx.theme;
    const CornerRadii corners = CornerRadii::all(theme.corner("medium", 8)).fitted(ctx.size);
    ctx.list.fill_round_rect({0, 0, ctx.size.width, ctx.size.height}, corners, xrender::Paint::solid(theme.color("surface")));
    ctx.list.stroke_round_rect({0.5f, 0.5f, ctx.size.width - 1, ctx.size.height - 1}, corners, 1, xrender::Paint::solid(theme.color("border")));
}

void Toolbar::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
}

std::vector<NodeId> Toolbar::controls() const {
    std::vector<NodeId> out;
    const auto visit = [&](this const auto& self, NodeId at) -> void {
        for (NodeId child : services().children(at)) {
            if (services().focusable(child)) out.push_back(child);
            else self(child);
        }
    };
    visit(node());
    return out;
}

ActResult Toolbar::move(int by, bool to_end) {
    const std::vector<NodeId> all = controls();
    if (all.empty()) return ActResult::done();
    std::size_t at = 0;
    for (std::size_t index = 0; index < all.size(); ++index)
        if (services().state(all[index]).focused) at = index;
    std::size_t to = at;
    if (to_end) to = by < 0 ? 0 : all.size() - 1;
    else if (by > 0) to = std::min(at + 1, all.size() - 1);
    else if (by < 0) to = at > 0 ? at - 1 : 0;
    services().focus(all[to]);
    return ActResult::done();
}

ActResult Toolbar::next(ActContext&) { return move(1); }
ActResult Toolbar::previous(ActContext&) { return move(-1); }
ActResult Toolbar::first(ActContext&) { return move(-1, true); }
ActResult Toolbar::last(ActContext&) { return move(1, true); }

}  // namespace omui
