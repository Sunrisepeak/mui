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

// Animation the runtime applies to whatever the node draws (D-68).
constexpr std::array<std::string_view, 2> motion_props{"glyph_motion", "pixel_effect"};

// `undo_scope`: the node keeps its own history of what is done inside it (S-12).
constexpr std::array<std::string_view, 13> semantic_props{
    "label", "description", "role", "origin", "focusable", "disabled", "readonly", "verb", "verb_args", "effect", "answer", "person", "undo_scope",
};

// The theme scope a node and its subtree are drawn in (Runtime::set_scope).
constexpr std::array<std::string_view, 1> scope_props{"theme_scope"};

// What a node is to a framework-drawn window (D-75): `window_area: "drag"`
// moves the window (window_area_at).
constexpr std::array<std::string_view, 1> window_props{"window_area"};

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

std::span<const std::string_view> runtime_properties() noexcept {
    static const auto all = [] {
        std::vector<std::string_view> v;
        for (auto set : {std::span<const std::string_view>(layout_props), std::span<const std::string_view>(visual_props),
                         std::span<const std::string_view>(motion_props), std::span<const std::string_view>(semantic_props),
                         std::span<const std::string_view>(window_props), std::span<const std::string_view>(scope_props)})
            v.insert(v.end(), set.begin(), set.end());
        std::ranges::sort(v);
        v.erase(std::ranges::unique(v).begin(), v.end());
        return v;
    }();
    return all;
}

bool is_runtime_property(std::string_view name) noexcept { return std::ranges::binary_search(runtime_properties(), name); }
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

namespace {

using PropAnimation = Node::PropAnimation;

struct Lanes {
    PropAnimation::Kind   kind = PropAnimation::Kind::number;
    std::uint8_t          count = 1;
    std::array<double, 4> v{};
};

// What a property's value interpolates as, when it does: a number (integers
// stay integers), a percentage, a colour (hex or a theme token: OKLab lanes),
// or a list of up to four numbers (edges, radii).
std::optional<Lanes> lanes_of(const Value& v, const Theme& theme) {
    using K = PropAnimation::Kind;
    if (v.type() == Value::Type::integer) return Lanes{K::integer, 1, {static_cast<double>(v.as_int())}};
    if (v.type() == Value::Type::real) return Lanes{K::number, 1, {v.as_double()}};
    if (v.type() == Value::Type::text) {
        const std::string_view s = v.as_string();
        if (s.size() > 1 && s.back() == '%') {
            double d = 0;
            if (std::from_chars(s.data(), s.data() + s.size() - 1, d).ec == std::errc{}) return Lanes{K::percent, 1, {d}};
            return std::nullopt;
        }
        const bool color = (!s.empty() && s.front() == '#') || theme.colors.contains(s);
        if (!color) return std::nullopt;
        const Color c = theme.color(s, Color::transparent());
        const auto l = hanim::lanes<hanim::Rgba>::to(hanim::Rgba{c.r, c.g, c.b, c.a});
        return Lanes{K::color, 4, {l[0], l[1], l[2], l[3]}};
    }
    if (const Value::List* l = v.list(); l && !l->empty() && l->size() <= 4) {
        Lanes out{K::list, static_cast<std::uint8_t>(l->size()), {}};
        for (std::size_t i = 0; i < l->size(); ++i) {
            if (!(*l)[i].is_number()) return std::nullopt;
            out.v[i] = (*l)[i].as_double();
        }
        return out;
    }
    return std::nullopt;
}

Value value_of(PropAnimation::Kind kind, std::uint8_t count, const std::array<double, 4>& v) {
    using K = PropAnimation::Kind;
    switch (kind) {
        case K::integer: return Value(static_cast<std::int64_t>(std::llround(v[0])));
        case K::number: return Value(v[0]);
        case K::percent: return Value(std::format("{}%", v[0]));
        case K::color: {
            const hanim::Rgba c = hanim::lanes<hanim::Rgba>::from(v);
            return ValueOf<Color>::to(Color{c.r, c.g, c.b, c.a});
        }
        case K::list: {
            Value::List l;
            for (std::uint8_t i = 0; i < count; ++i) l.emplace_back(v[i]);
            return Value(std::move(l));
        }
    }
    return {};
}

}  // namespace

const Transition* Runtime::Impl::transition_for(const Node& n, std::string_view prop) const {
    if (auto it = n.transitions.find(prop); it != n.transitions.end()) return &it->second;
    if (prop == "enter" || prop == "exit") return nullptr;
    if (auto all = n.transitions.find("all"); all != n.transitions.end()) return &all->second;
    return nullptr;
}

void Runtime::Impl::stop_prop(NodeId id, std::string_view name) {
    Node& n = at(id);
    auto it = std::ranges::find(n.prop_animations, name, &PropAnimation::name);
    if (it == n.prop_animations.end()) return;
    for (std::uint8_t i = 0; i < it->lanes; ++i)
        if (anims.valid(it->channels[i])) anims.remove(it->channels[i]);
    n.prop_animations.erase(it);
}

bool Runtime::Impl::animate_prop(NodeId id, std::string_view name, const Value& value) {
    // The runtime's own channels animate these (animate_field).
    if (scalar_field(name) || name == "background" || name == "border_color" || name == "radius") return false;
    Node& n = at(id);
    const Transition* t = n.shown && !value.is_null() ? transition_for(n, name) : nullptr;
    const auto to = t ? lanes_of(value, theme_of(id)) : std::nullopt;
    auto running = std::ranges::find(n.prop_animations, name, &PropAnimation::name);
    if (!to) {
        if (running != n.prop_animations.end()) stop_prop(id, name);
        return false;
    }
    std::array<double, 4> from{};
    const seconds start = now() + t->delay;
    if (running != n.prop_animations.end() && running->kind == to->kind && running->lanes == to->count) {
        if (running->target == value && !running->resting) return true;  // already on its way there
        if (running->target == value) return false;                       // already there
        for (std::uint8_t i = 0; i < to->count; ++i) from[i] = anims.value(running->channels[i], now());
    } else {
        if (running != n.prop_animations.end()) stop_prop(id, name);
        auto shown = n.props.find(name);
        if (shown == n.props.end()) return false;
        const auto f = lanes_of(shown->second, theme_of(id));
        if (!f || f->kind != to->kind || f->count != to->count) return false;
        if (shown->second == value) return false;
        from = f->v;
        PropAnimation a;
        a.name = std::string(name);
        a.kind = to->kind;
        a.lanes = to->count;
        for (std::uint8_t i = 0; i < to->count; ++i) a.channels[i] = anims.hold(from[i]);
        n.prop_animations.push_back(std::move(a));
        running = n.prop_animations.end() - 1;
    }
    running->target = value;
    running->resting = false;
    for (std::uint8_t i = 0; i < to->count; ++i) {
        if (t->kind == Transition::Kind::tween) anims.tween_to(running->channels[i], start, to->v[i], t->duration, t->ease);
        else anims.spring_to(running->channels[i], start, to->v[i], t->spring);
    }
    if (!n.state.animating) {
        n.state.animating = true;
        touch(dirty_semantics);
    }
    if (std::ranges::find(animating, id) == animating.end()) animating.push_back(id);
    frame_requested = true;
    return true;
}

void Runtime::Impl::step_props(NodeId id, seconds t, const hanim::Policy& policy) {
    // Copies: applying a value may reach the component, which may change the node.
    std::vector<std::pair<std::string, Value>> values;
    {
        Node& n = at(id);
        for (PropAnimation& a : n.prop_animations) {
            if (a.resting) continue;
            std::array<double, 4> v{};
            bool settled = true;
            for (std::uint8_t i = 0; i < a.lanes; ++i) {
                v[i] = anims.value(a.channels[i], t, policy);
                settled = settled && anims.settled(a.channels[i], t);
            }
            if (policy.reduce_motion || settled) {
                values.emplace_back(a.name, a.target);  // exactly what was set
                a.resting = true;
            } else {
                values.emplace_back(a.name, value_of(a.kind, a.lanes, v));
            }
        }
    }
    for (const auto& [k, v] : values)
        if (alive(id)) apply_now(id, k, v);
}

void Runtime::Impl::apply(NodeId id, std::string_view name, const Value& value) {
    if (animate_prop(id, name, value)) return;
    apply_now(id, name, value);
}

void Runtime::Impl::apply_now(NodeId id, std::string_view name, const Value& value) {
    if (options.debug.repaints) flashes.emplace_back(id, now());
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
        const Theme& t = theme_of(id);
        if (auto f = scalar_field(name)) {
            float target = value.is_null() ? scalar_default(*f) : static_cast<float>(value.as_double(scalar_default(*f)));
            animate_field(id, name, *f, target);
        } else if (name == "background" || name == "border_color") {
            Color c = value.is_null() ? Color::transparent()
                                      : (value.type() == Value::Type::text ? t.color(value.as_string(), Color::transparent())
                                                                           : ValueOf<Color>::from(value));
            bool bg = name == "background";
            Field base = bg ? Field::background_r : Field::border_r;
            const std::array<float, 4> parts = color_lanes(c);
            for (std::size_t i = 0; i < 4; ++i)
                animate_field(id, name, static_cast<Field>(static_cast<std::size_t>(base) + i), parts[i]);
            // Shown exactly as set when no transition runs; the frame shows the lanes while one does.
            Node& now = at(id);
            (bg ? now.visual.background_target : now.visual.border_target) = c;
            if (!(now.moving & (bg ? background_bits : border_bits))) (bg ? now.visual.background_color : now.visual.border_color) = c;
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
            m.visual.hit_self = value.as_string() != "children";
        }
    }
    if (name == "role" && !rules_of(id).empty()) apply_rules(id);  // a rule may name its role
    if (name == "theme_scope") {
        // It joins the scope it names (or its parent's again), its subtree with it.
        const NodeId parent = at(id).parent;
        rescope(id, alive(parent) ? at(parent).scope : 0);
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
    if (name == "pixel_effect") {
        Node& g = at(id);
        g.pixel_effect = effect_state(value, "pixel_effect");
        if (g.pixel_effect && g.pixel_effect->duration) {
            if (!g.state.animating) {
                g.state.animating = true;
                what |= dirty_semantics;
            }
            if (std::ranges::find(animating, id) == animating.end()) animating.push_back(id);
            frame_requested = true;
        }
        touch(what | dirty_paint);
        return;
    }
    if (in(motion_props, name)) {
        Node& g = at(id);
        if (auto motion = value.is_null() ? std::nullopt : glyph_motion_from(value)) {
            g.glyph_motion = Node::GlyphMotionState{*motion, now(), seconds(std::numeric_limits<double>::infinity())};
            if (!g.state.animating) {
                g.state.animating = true;
                what |= dirty_semantics;
            }
            if (std::ranges::find(animating, id) == animating.end()) animating.push_back(id);
            frame_requested = true;
        } else {
            g.glyph_motion.reset();
        }
        touch(what);
        return;
    }
    Node& m = at(id);
    // The component too, when its contract has a property of this name: a
    // layout name a component also means (text's `align`, a stage's `left`)
    // reaches both the layout and the component, never only the layout.
    {
        auto use = use_theme(id);
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

std::optional<Node::PixelEffectState> Runtime::Impl::effect_state(const Value& value, std::string_view where, const Theme* theme) {
    const Value* fx = value.get("name");
    if (!fx || !value.map()) return std::nullopt;
    const auto ref = render::effects().find(fx->as_string());
    if (!ref) {
        warn("{}: no effect is named '{}' (render::effects().names())", where, fx->as_string());
        return std::nullopt;
    }
    std::vector<std::pair<std::string_view, float>> given;
    const render::EffectInfo* about = render::effects().info(*ref);
    const auto param = [&](std::string_view n) -> std::string_view {  // its name as the effect keeps it (lives as long as the effect)
        if (about)
            for (const auto& ep : about->params)
                if (ep.name == n) return ep.name;
        return {};
    };
    for (const auto& [k, v] : *value.map()) {
        if (v.is_number()) given.emplace_back(k, static_cast<float>(v.as_double()));
        // A colour by the theme's name: its channels from the node's theme (a pack's accent edited, the effect follows).
        else if (v.type() == Value::Type::text && theme && k != "name" && k != "ease")
            if (const auto r = param(std::format("{}_r", k)), g = param(std::format("{}_g", k)), b = param(std::format("{}_b", k)); !r.empty() && !g.empty() && !b.empty()) {
                const Color c = theme->color(v.as_string());
                given.emplace_back(r, c.r);
                given.emplace_back(g, c.g);
                given.emplace_back(b, c.b);
                if (const auto a = param(std::format("{}_a", k)); !a.empty()) given.emplace_back(a, c.a);
            }
    }
    Node::PixelEffectState st;
    st.effect = *ref;
    st.params = render::effects().params(*ref, given);
    if (const render::EffectInfo* info = render::effects().info(*ref))
        for (std::size_t i = 0; i < info->params.size(); ++i)
            if (info->params[i].name == "progress") st.progress = static_cast<int>(i);
    st.start = now();
    if (const Value* d = value.get("duration"); d && d->is_number() && st.progress >= 0) st.duration = seconds(d->as_double());
    if (const Value* d = value.get("delay"); d && d->is_number()) st.delay = seconds(d->as_double());
    if (const Value* e = value.get("ease"))
        if (auto pe = hanim::parse_ease(e->as_string())) st.ease = *pe;
    if (const Value* r = value.get("reverse")) st.reverse = r->as_bool();
    return st;
}

// The style's rule for a node: the window's for the root (or for the node
// that joins a theme scope: the scope's window), else its type's, else its
// role's (D-69) — from its scope's rules.
void Runtime::Impl::apply_rules(NodeId id) {
    Node& n = at(id);
    n.styled_effect.reset();
    const std::vector<EffectRule>& effect_rules = rules_of(id);
    if (effect_rules.empty()) return;
    const bool window = id == root || scope_root(id);
    Role r = n.type->contract ? n.type->contract->role : n.type->role;
    if (auto it = n.props.find("role"); it != n.props.end())
        if (auto parsed = spec::parse_role(it->second.as_string())) r = *parsed;
    const std::string role = std::format("role:{}", spec::name(r));
    const EffectRule* best = nullptr;
    for (const EffectRule& r : effect_rules) {
        if (window ? r.selector == "window" : r.selector == n.type->name) {
            best = &r;
            break;
        }
        if (!window && !best && r.selector == role) best = &r;
    }
    if (!best) return;
    n.styled_effect = effect_state(best->effect, std::format("the style's `{}`", best->selector), &theme_of(id));
    if (!n.styled_effect) return;
    // Its states' rules: the same selector and a state (`button:pressed`), the same effect, their params.
    auto& fx = *n.styled_effect;
    fx.rest = fx.params;
    for (const auto& [state, bit] : {std::pair{std::string_view(":pressed"), std::uint8_t{1}}, {":hovered", std::uint8_t{2}}, {":focused", std::uint8_t{3}}})
        for (const EffectRule& r : effect_rules) {
            if (r.selector.size() != best->selector.size() + state.size() || !r.selector.starts_with(best->selector) || !r.selector.ends_with(state)) continue;
            const Value* name = r.effect.get("name");
            const Value* base = best->effect.get("name");
            if (!name || !base || name->as_string() != base->as_string()) {
                warn("the style's `{}`: a state's rule draws the same effect as its node's (`{}`)", r.selector, base ? base->as_string() : "");
                continue;
            }
            // Its params: the node's rule's, with the state's own over them.
            Value merged = best->effect;
            if (const Value::Map* m = r.effect.map())
                for (const auto& [k, v] : *m) merged.set(k, v);
            if (auto st = effect_state(merged, std::format("the style's `{}`", r.selector), &theme_of(id))) fx.variants.push_back({bit, std::move(st->params)});
            if (const Value* t = r.effect.get("transition"); t && t->is_number()) fx.transition = seconds(t->as_double());
        }
}

void Runtime::Impl::sync_style(NodeId id) {
    Node& n = at(id);
    if (n.state.removing) return;
    layout::Style s;
    const Theme& theme = theme_of(id);
    if (n.type->style) n.type->style(n.instance, theme, s);
    for (const auto& [k, v] : n.props)
        if (is_layout_prop(k)) apply_layout(s, k, v, theme);
    if (n.parent == root && !slot_insets.empty()) {
        // A slot kept from the window's edges (inside a window's own frame).
        for (const auto& [slot, node] : mounted)
            if (node == id)
                if (auto it = slot_insets.find(slot); it != slot_insets.end()) {
                    s.margin.top += it->second.top;
                    s.margin.right += it->second.right;
                    s.margin.bottom += it->second.bottom;
                    s.margin.left += it->second.left;
                }
    }
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
    hanim::Set::Channel& c = n.channels[i];
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
