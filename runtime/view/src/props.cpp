// Properties: what the runtime itself interprets (layout, look, semantics),
// bindings to reactive sources, and transitions of the look.
module openmui.view;

import std;
import openmui.core;
import openmui.layout;
import :impl;

namespace omui {

namespace {

constexpr std::array<std::string_view, 44> layout_props{
    "width", "height", "min_width", "min_height", "max_width", "max_height", "padding", "padding_top",
    "padding_right", "padding_bottom", "padding_left", "padding_x", "padding_y", "margin", "gap", "row_gap",
    "column_gap", "direction", "wrap", "justify", "align", "align_items", "align_content", "align_self",
    "justify_self", "grow", "shrink", "basis", "position", "left", "top", "right", "bottom", "aspect_ratio",
    "display", "overflow", "columns", "rows", "auto_rows", "auto_columns", "column", "row", "grid_flow",
    "border_width",
};

constexpr std::array<std::string_view, 15> visual_props{
    "background", "border_color", "border_width", "radius", "shadow", "opacity", "translate_x", "translate_y",
    "scale", "rotation", "clip", "visible", "pointer_events", "overflow", "radii",
};

constexpr std::array<std::string_view, 9> semantic_props{
    "label", "description", "role", "origin", "focusable", "disabled", "verb", "effect", "answer",
};

template <std::size_t N>
constexpr bool in(const std::array<std::string_view, N>& set, std::string_view name) noexcept {
    return std::ranges::find(set, name) != set.end();
}

layout::Length to_length(const Value& v, const Theme& theme) {
    if (v.is_number()) return layout::pt(static_cast<float>(v.as_double()));
    std::string_view s = v.as_string();
    if (s.empty() || s == "auto") return layout::Length::automatic();
    if (s.ends_with('%')) {
        float x = 0;
        std::from_chars(s.data(), s.data() + s.size() - 1, x);
        return layout::pct(x);
    }
    float x = 0;
    if (auto r = std::from_chars(s.data(), s.data() + s.size(), x); r.ec == std::errc{} && r.ptr == s.data() + s.size())
        return layout::pt(x);
    return layout::pt(theme.space(s, 0));
}

float to_space(const Value& v, const Theme& theme) {
    if (v.is_number()) return static_cast<float>(v.as_double());
    std::string_view s = v.as_string();
    float x = 0;
    if (auto r = std::from_chars(s.data(), s.data() + s.size(), x); r.ec == std::errc{} && r.ptr == s.data() + s.size())
        return x;
    return theme.space(s, 0);
}

Insets to_insets(const Value& v, const Theme& theme) {
    if (const Value::List* l = v.list()) {
        auto at = [&](std::size_t i) { return to_space((*l)[i], theme); };
        switch (l->size()) {
            case 1: return Insets::all(at(0));
            case 2: return Insets::symmetric(at(0), at(1));
            case 3: return {at(0), at(1), at(2), at(1)};
            case 4: return {at(0), at(1), at(2), at(3)};
            default: return {};
        }
    }
    if (v.map()) {
        auto side = [&](std::string_view k, std::string_view axis) {
            if (const Value* x = v.get(k)) return to_space(*x, theme);
            if (const Value* x = v.get(axis)) return to_space(*x, theme);
            return 0.0f;
        };
        return {side("top", "vertical"), side("right", "horizontal"), side("bottom", "vertical"), side("left", "horizontal")};
    }
    return Insets::all(to_space(v, theme));
}

template <class E, std::size_t N>
std::optional<E> choose(std::string_view s, const std::array<std::pair<std::string_view, E>, N>& table) {
    for (const auto& [k, e] : table)
        if (k == s) return e;
    return std::nullopt;
}

constexpr std::array<std::pair<std::string_view, layout::Align>, 7> aligns{{
    {"auto", layout::Align::auto_}, {"start", layout::Align::start}, {"end", layout::Align::end},
    {"center", layout::Align::center}, {"stretch", layout::Align::stretch}, {"baseline", layout::Align::baseline},
    {"flex_start", layout::Align::start},
}};

layout::GridLine to_grid_line(const Value& v, layout::GridLine current) {
    if (v.is_number()) return layout::GridLine::at(static_cast<std::int16_t>(v.as_int()), current.span);
    if (v.map()) {
        const Value* l = v.get("line");
        const Value* s = v.get("span");
        return {static_cast<std::int16_t>(l ? l->as_int() : 0), static_cast<std::int16_t>(s ? std::max<std::int64_t>(1, s->as_int(1)) : 1)};
    }
    // "2", "span 3", "2 / span 3"
    std::string_view s = v.as_string();
    layout::GridLine g = current;
    auto number = [](std::string_view t) {
        while (!t.empty() && t.front() == ' ') t.remove_prefix(1);
        int x = 0;
        std::from_chars(t.data(), t.data() + t.size(), x);
        return static_cast<std::int16_t>(x);
    };
    auto slash = s.find('/');
    std::string_view first = s.substr(0, slash);
    while (!first.empty() && first.back() == ' ') first.remove_suffix(1);
    if (first.starts_with("span")) g = layout::GridLine::spanning(std::max<std::int16_t>(1, number(first.substr(4))));
    else g.line = number(first);
    if (slash != std::string_view::npos) {
        std::string_view second = s.substr(slash + 1);
        while (!second.empty() && second.front() == ' ') second.remove_prefix(1);
        if (second.starts_with("span")) g.span = std::max<std::int16_t>(1, number(second.substr(4)));
    }
    return g;
}

std::vector<layout::Track> to_tracks(const Value& v) {
    if (const Value::List* l = v.list()) {
        std::vector<layout::Track> out;
        for (const Value& t : *l) {
            if (t.is_number()) out.push_back(layout::Track::points(static_cast<float>(t.as_double())));
            else for (const layout::Track& x : parse_tracks(t.as_string())) out.push_back(x);
        }
        return out;
    }
    if (v.is_number()) {
        // A count of equal columns.
        return std::vector<layout::Track>(static_cast<std::size_t>(std::max<std::int64_t>(0, v.as_int())), layout::Track::fr(1));
    }
    return parse_tracks(v.as_string());
}

void apply_layout(layout::Style& s, std::string_view k, const Value& v, const Theme& theme) {
    using namespace layout;
    if (k == "width") s.width = to_length(v, theme);
    else if (k == "height") s.height = to_length(v, theme);
    else if (k == "min_width") s.min_width = to_length(v, theme);
    else if (k == "min_height") s.min_height = to_length(v, theme);
    else if (k == "max_width") s.max_width = to_length(v, theme);
    else if (k == "max_height") s.max_height = to_length(v, theme);
    else if (k == "padding") s.padding = to_insets(v, theme);
    else if (k == "padding_top") s.padding.top = to_space(v, theme);
    else if (k == "padding_right") s.padding.right = to_space(v, theme);
    else if (k == "padding_bottom") s.padding.bottom = to_space(v, theme);
    else if (k == "padding_left") s.padding.left = to_space(v, theme);
    else if (k == "padding_x") s.padding.left = s.padding.right = to_space(v, theme);
    else if (k == "padding_y") s.padding.top = s.padding.bottom = to_space(v, theme);
    else if (k == "margin") s.margin = to_insets(v, theme);
    else if (k == "gap") s.row_gap = s.column_gap = to_space(v, theme);
    else if (k == "row_gap") s.row_gap = to_space(v, theme);
    else if (k == "column_gap") s.column_gap = to_space(v, theme);
    else if (k == "direction") {
        constexpr std::array<std::pair<std::string_view, Direction>, 4> t{
            {{"row", Direction::row}, {"column", Direction::column}, {"row_reverse", Direction::row_reverse},
             {"column_reverse", Direction::column_reverse}}};
        s.direction = choose(v.as_string(), t).value_or(Direction::row);
    } else if (k == "wrap") {
        if (v.type() == Value::Type::boolean) s.wrap = v.as_bool() ? Wrap::wrap : Wrap::no_wrap;
        else {
            constexpr std::array<std::pair<std::string_view, Wrap>, 3> t{
                {{"no_wrap", Wrap::no_wrap}, {"wrap", Wrap::wrap}, {"wrap_reverse", Wrap::wrap_reverse}}};
            s.wrap = choose(v.as_string(), t).value_or(Wrap::no_wrap);
        }
    } else if (k == "justify") {
        constexpr std::array<std::pair<std::string_view, Justify>, 6> t{
            {{"start", Justify::start}, {"end", Justify::end}, {"center", Justify::center},
             {"space_between", Justify::space_between}, {"space_around", Justify::space_around},
             {"space_evenly", Justify::space_evenly}}};
        s.justify = choose(v.as_string(), t).value_or(Justify::start);
    } else if (k == "align" || k == "align_items") s.align_items = choose(v.as_string(), aligns).value_or(Align::stretch);
    else if (k == "align_content") s.align_content = choose(v.as_string(), aligns).value_or(Align::start);
    else if (k == "align_self") s.align_self = choose(v.as_string(), aligns).value_or(Align::auto_);
    else if (k == "justify_self") s.justify_self = choose(v.as_string(), aligns).value_or(Align::auto_);
    else if (k == "grow") s.grow = static_cast<float>(v.as_double());
    else if (k == "shrink") s.shrink = static_cast<float>(v.as_double(1));
    else if (k == "basis") s.basis = to_length(v, theme);
    else if (k == "position") s.position = v.as_string() == "absolute" ? Position::absolute : Position::relative;
    else if (k == "left") s.left = to_length(v, theme);
    else if (k == "top") s.top = to_length(v, theme);
    else if (k == "right") s.right = to_length(v, theme);
    else if (k == "bottom") s.bottom = to_length(v, theme);
    else if (k == "aspect_ratio") s.aspect_ratio = static_cast<float>(v.as_double());
    else if (k == "display") {
        constexpr std::array<std::pair<std::string_view, Display>, 4> t{
            {{"flex", Display::flex}, {"grid", Display::grid}, {"stack", Display::stack}, {"none", Display::none}}};
        s.display = choose(v.as_string(), t).value_or(Display::flex);
    } else if (k == "overflow") {
        constexpr std::array<std::pair<std::string_view, Overflow>, 3> t{
            {{"visible", Overflow::visible}, {"clip", Overflow::clip}, {"scroll", Overflow::scroll}}};
        s.overflow_x = s.overflow_y = choose(v.as_string(), t).value_or(Overflow::visible);
    } else if (k == "columns") s.columns = to_tracks(v);
    else if (k == "rows") s.rows = to_tracks(v);
    else if (k == "auto_rows") {
        auto t = to_tracks(v);
        s.auto_rows = t.empty() ? Track::automatic() : t.front();
    } else if (k == "auto_columns") {
        auto t = to_tracks(v);
        s.auto_columns = t.empty() ? Track::automatic() : t.front();
    } else if (k == "column") s.column_start = to_grid_line(v, s.column_start);
    else if (k == "row") s.row_start = to_grid_line(v, s.row_start);
    else if (k == "grid_flow") s.grid_flow = v.as_string() == "column" ? GridFlow::column : GridFlow::row;
    else if (k == "border_width") s.border = Insets::all(static_cast<float>(v.as_double()));
}

std::optional<Field> scalar_field(std::string_view k) {
    if (k == "opacity") return Field::opacity;
    if (k == "translate_x") return Field::translate_x;
    if (k == "translate_y") return Field::translate_y;
    if (k == "scale") return Field::scale;
    if (k == "rotation") return Field::rotation;
    if (k == "border_width") return Field::border_width;
    return std::nullopt;
}

float scalar_default(Field f) { return f == Field::opacity || f == Field::scale ? 1.0f : 0.0f; }

}  // namespace

bool is_layout_prop(std::string_view name) noexcept { return in(layout_props, name); }
bool is_visual_prop(std::string_view name) noexcept { return in(visual_props, name); }

void Runtime::Impl::set_source(NodeId id, const std::string& name, const Source& source) {
    Node& n = at(id);
    if (source.write) n.sources.insert_or_assign(name, source);
    else n.sources.erase(name);
    if (!source.reactive()) {
        n.bindings.erase(name);
        apply(id, name, source.constant.value_or(Value{}));
        return;
    }
    // The binding: re-read when what the source reads changes; applying the
    // value is not part of what it reads.
    Watch w([this, id, name, read = source.read] {
        Value v = read();
        untracked([&] {
            if (!alive(id)) return;
            ++stats.bindings;
            apply(id, name, v);
        });
    });
    at(id).bindings.insert_or_assign(name, std::move(w));
}

void Runtime::Impl::clear_prop(NodeId id, std::string_view name) {
    Node& n = at(id);
    if (auto it = n.bindings.find(name); it != n.bindings.end()) n.bindings.erase(it);
    if (auto it = n.sources.find(name); it != n.sources.end()) n.sources.erase(it);
    apply(id, name, Value{});
}

void Runtime::Impl::apply(NodeId id, std::string_view name, const Value& value) {
    Node& n = at(id);
    if (value.is_null()) {
        if (auto it = n.props.find(name); it != n.props.end()) n.props.erase(it);
    } else {
        n.props.insert_or_assign(std::string(name), value);
    }
    std::uint8_t what = dirty_paint;
    if (is_layout_prop(name)) sync_style(id);
    if (is_visual_prop(name)) {
        Node& m = at(id);
        const Theme& t = theme;
        if (auto f = scalar_field(name)) {
            float target = value.is_null() ? scalar_default(*f) : static_cast<float>(value.as_double(scalar_default(*f)));
            animate_field(id, name, *f, target);
        } else if (name == "background" || name == "border_color") {
            Color c = value.is_null() ? Color::transparent()
                                      : (value.type() == Value::Type::text ? t.color(value.as_string(), Color::transparent())
                                                                           : ValueOf<Color>::from(value));
            bool bg = name == "background";
            Field base = bg ? Field::background_r : Field::border_r;
            const std::array<float, 4> parts{c.r, c.g, c.b, c.a};
            for (std::size_t i = 0; i < 4; ++i)
                animate_field(id, name, static_cast<Field>(static_cast<std::size_t>(base) + i), parts[i]);
        } else if (name == "radius") {
            float r = value.is_number() ? static_cast<float>(value.as_double()) : t.corner(value.as_string(), 0);
            animate_field(id, name, Field::radius, r);
        } else if (name == "radii") {
            if (const Value::List* l = value.list(); l && l->size() == 4)
                m.visual.radii = CornerRadii{static_cast<float>((*l)[0].as_double()), static_cast<float>((*l)[1].as_double()),
                                             static_cast<float>((*l)[2].as_double()), static_cast<float>((*l)[3].as_double())};
            else m.visual.radii.reset();
        } else if (name == "shadow") {
            m.visual.shadow = std::string(value.as_string());
        } else if (name == "clip") {
            m.visual.clip = value.as_bool();
        } else if (name == "overflow") {
            m.visual.clip = value.as_string() == "clip" || value.as_string() == "scroll";
        } else if (name == "visible") {
            m.visual.visible = value.is_null() || value.as_bool(true);
            what |= dirty_semantics;
        } else if (name == "pointer_events") {
            m.visual.hit = value.as_string() != "none";
        }
    }
    if (in(semantic_props, name)) {
        Node& m = at(id);
        if (name == "disabled") {
            m.state.disabled = value.as_bool();
            if (m.state.disabled && focus == id) set_focus({}, false);
        } else if (name == "focusable") {
            if (value.is_null()) m.focusable.reset();
            else m.focusable = value.as_bool();
        }
        what |= dirty_semantics;
    }
    Node& m = at(id);
    if (!is_layout_prop(name) || name == "border_width") {
        if (m.type->set_prop && m.type->set_prop(m.instance, name, value)) {
            what |= dirty_semantics;
            if (m.type->style) sync_style(id);  // its default style may follow the property
            if (m.type->measure) {
                layout.mark_dirty(m.layout);
                what |= dirty_layout;
            }
        }
    }
    touch(what);
}

void Runtime::Impl::sync_style(NodeId id) {
    Node& n = at(id);
    if (n.state.removing) return;
    layout::Style s;
    if (n.type->style) n.type->style(n.instance, theme, s);
    for (const auto& [k, v] : n.props)
        if (is_layout_prop(k)) apply_layout(s, k, v, theme);
    if (n.type->overlay) {
        // Laid out at the root's corner, then placed against its anchor.
        s.position = layout::Position::absolute;
        s.left = s.top = layout::pt(0);
        s.right = s.bottom = layout::Length::automatic();
        if (n.type->placement && n.type->placement(n.instance).match_width && alive(n.parent))
            s.min_width = layout::pt(std::max(s.min_width.resolve(0).value_or(0), at(n.parent).bounds.width));
    }
    if (!(layout.style(n.layout) == s)) {
        layout.set_style(n.layout, std::move(s));
        touch(dirty_layout | dirty_paint);
    }
}

void Runtime::Impl::resolve_visual(NodeId id) {
    // Theme tokens resolve again (a new theme); values jump, they do not animate.
    Node& n = at(id);
    const bool shown = n.shown;
    n.shown = false;
    std::vector<std::pair<std::string, Value>> visual;
    for (const auto& [k, v] : n.props)
        if (is_visual_prop(k)) visual.emplace_back(k, v);
    for (const auto& [k, v] : visual) apply(id, k, v);
    at(id).shown = shown;
}

void Runtime::Impl::animate_field(NodeId id, std::string_view prop, Field field, float target) {
    Node& n = at(id);
    const auto i = static_cast<std::size_t>(field);
    const std::uint32_t bit = 1u << i;
    anim::Set::Channel& c = n.channels[i];
    const Transition* t = nullptr;
    if (n.shown) {
        if (auto it = n.transitions.find(prop); it != n.transitions.end()) t = &it->second;
        else if (auto all = n.transitions.find("all"); all != n.transitions.end() && prop != "enter" && prop != "exit")
            t = &all->second;
    }
    if (!t) {
        n.visual.f[i] = target;
        if (anims.valid(c)) anims.jump(c, target);
        n.moving &= ~bit;
        touch(dirty_paint);
        return;
    }
    if ((n.moving & bit) && anims.valid(c) && anims.target(c) == static_cast<double>(target)) return;
    if (!(n.moving & bit) && n.visual.f[i] == target) return;
    if (!anims.valid(c)) c = anims.hold(n.visual.f[i]);
    const seconds start = now() + t->delay;
    if (t->kind == Transition::Kind::tween) anims.tween_to(c, start, target, t->duration, t->ease);
    else anims.spring_to(c, start, target, t->spring);
    n.moving |= bit;
    if (!n.state.animating) {
        n.state.animating = true;
        touch(dirty_semantics);
    }
    if (std::ranges::find(animating, id) == animating.end()) animating.push_back(id);
    touch(dirty_paint);
}

}  // namespace omui
