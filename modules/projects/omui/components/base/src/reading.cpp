// openmui.ui.base:reading — kbd, separator, link, description list, spinner.
module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

namespace {

constexpr float cap_pad = 6, cap_gap = 3, chord_gap = 8, cap_radius = 5;  // a key cap
constexpr float rule = 1;                                                // a separator's line
constexpr float row_gap = 8, column_gap = 16;                            // a description list
constexpr double turn = 0.9;                                             // seconds a spinner takes to turn once

// A key as drawn, and as said: the platform's command key for `Mod`, symbols for the arrows.
std::string drawn_key(std::string_view key, bool command) {
    if (key == "Mod") return command ? "⌘" : "Ctrl";
    if (key == "Cmd" || key == "Meta") return "⌘";
    if (key == "Up") return "↑";
    if (key == "Down") return "↓";
    if (key == "Left") return "←";
    if (key == "Right") return "→";
    return std::string(key);
}

std::string said_key(std::string_view key, bool command) {
    if (key == "Mod") return command ? "Command" : "Control";
    if (key == "Ctrl") return "Control";
    if (key == "Cmd" || key == "Meta") return "Command";
    if (key == "Alt") return "Option";
    return std::string(key);
}

std::vector<std::string_view> split(std::string_view text, char by) {
    std::vector<std::string_view> out;
    for (const auto part : std::views::split(text, by))
        if (const std::string_view piece(part.begin(), part.end()); !piece.empty()) out.push_back(piece);
    return out;
}

spec::Rect in_window(const Rect& node, const Rect& part) { return {node.x + part.x, node.y + part.y, part.width, part.height}; }

bool command_is_meta() { return primary_modifier() == Modifier::meta; }

}  // namespace

// ── kbd ──

std::vector<std::vector<std::string>> Kbd::chords(std::string_view keys, bool command) {
    std::vector<std::vector<std::string>> out;
    for (const std::string_view chord : split(keys, ' ')) {
        std::vector<std::string> pressed;
        for (const std::string_view key : split(chord, '+')) pressed.push_back(drawn_key(key, command));
        if (!pressed.empty()) out.push_back(std::move(pressed));
    }
    return out;
}

std::string Kbd::spoken(std::string_view keys, bool command) {
    std::string out;
    for (const std::string_view chord : split(keys, ' ')) {
        if (!out.empty()) out += ", then ";
        bool first = true;
        for (const std::string_view key : split(chord, '+')) {
            if (!first) out += '+';
            out += said_key(key, command);
            first = false;
        }
    }
    return out;
}

void Kbd::rebuild() {
    caps_.clear();
    for (const auto& chord : chords(props.keys, command_is_meta())) {
        auto& labels = caps_.emplace_back();
        for (const std::string& key : chord) {
            parts::Label& label = labels.emplace_back();
            label.set_style("code");
            label.set_text(key);
        }
    }
}

void Kbd::mounted() { rebuild(); }

void Kbd::prop_changed(KbdProp) {
    rebuild();
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

Size Kbd::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    const Color ink = m.services.theme().color("text");
    float across = 0, tall = 0;
    for (std::size_t chord = 0; chord < caps_.size(); ++chord) {
        if (chord) across += chord_gap;
        for (std::size_t key = 0; key < caps_[chord].size(); ++key) {
            const Size text = caps_[chord][key].measure(m.services, ink);
            const float cap_height = std::ceil(text.height) + 4;
            across += (key ? cap_gap : 0) + std::max(cap_height, std::ceil(text.width) + 2 * cap_pad);
            tall = std::max(tall, cap_height + 2);  // its lower edge
        }
    }
    return {width.value_or(across), height.value_or(tall)};
}

float Kbd::baseline(MeasureContext& m, Size content) {
    if (caps_.empty() || caps_.front().empty()) return content.height;
    const Color ink = m.services.theme().color("text");
    return 2 + caps_.front().front().baseline(m.services, ink);
}

void Kbd::paint(PaintContext& ctx) const {
    const Color ink = ctx.theme.color("text"), face = ctx.theme.color("surface_variant"), edge = ctx.theme.color("border");
    float x = 0;
    for (std::size_t chord = 0; chord < caps_.size(); ++chord) {
        if (chord) x += chord_gap;
        for (std::size_t key = 0; key < caps_[chord].size(); ++key) {
            parts::Label& label = caps_[chord][key];
            const Size text = label.measure(ctx.services, ink);
            const float cap_height = std::ceil(text.height) + 4, cap_width = std::max(cap_height, std::ceil(text.width) + 2 * cap_pad);
            if (key) x += cap_gap;
            // The cap: its edge one point lower than its face, as a key stands.
            const Rect cap{x, 0, cap_width, cap_height + 2};
            ctx.list.fill_round_rect(cap, CornerRadii::all(cap_radius), xrender::Paint::solid(edge));
            ctx.list.fill_round_rect({x, 0, cap_width, cap_height}, CornerRadii::all(cap_radius), xrender::Paint::solid(face));
            label.paint(ctx, {x + (cap_width - text.width) / 2, 2}, ink);
            x += cap_width;
        }
    }
}

void Kbd::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = spoken(props.keys, command_is_meta());
    node.value = Value(props.keys);
}

// ── separator ──

void Separator::mounted() {
    label_.set_style("caption");
    label_.set_text(props.label);
}

void Separator::prop_changed(SeparatorProp) {
    label_.set_text(props.label);
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

Size Separator::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    // Across: as wide as it is given, as tall as its word; down: the other way.
    const Size text = props.label.empty() ? Size{} : label_.measure(m.services, m.services.theme().color("text_muted"));
    if (props.vertical) return {width.value_or(std::max(rule, std::ceil(text.width))), height.value_or(std::max(16.0f, std::ceil(text.height) + 24))};
    return {width.value_or(std::max(16.0f, std::ceil(text.width) + 24)), height.value_or(std::max(rule, std::ceil(text.height)))};
}

void Separator::paint(PaintContext& ctx) const {
    const Color line = ctx.theme.color("border"), muted = ctx.theme.color("text_muted");
    const Size size = ctx.size;
    const Size text = props.label.empty() ? Size{} : label_.measure(ctx.services, muted);
    constexpr float space = 8;  // between the word and the line
    if (props.vertical) {
        const float x = std::floor((size.width - rule) / 2);
        if (props.label.empty()) {
            ctx.list.fill_rect({x, 0, rule, size.height}, line);
            return;
        }
        const float word_top = (size.height - text.height) / 2;
        ctx.list.fill_rect({x, 0, rule, std::max(0.0f, word_top - space)}, line);
        ctx.list.fill_rect({x, word_top + text.height + space, rule, std::max(0.0f, size.height - word_top - text.height - space)}, line);
        label_.paint(ctx, {(size.width - text.width) / 2, word_top}, muted);
        return;
    }
    const float y = std::floor((size.height - rule) / 2);
    if (props.label.empty()) {
        ctx.list.fill_rect({0, y, size.width, rule}, line);
        return;
    }
    const float word_left = (size.width - text.width) / 2;
    ctx.list.fill_rect({0, y, std::max(0.0f, word_left - space), rule}, line);
    ctx.list.fill_rect({word_left + text.width + space, y, std::max(0.0f, size.width - word_left - text.width - space), rule}, line);
    label_.paint(ctx, {word_left, (size.height - text.height) / 2}, muted);
}

void Separator::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value(props.vertical ? "vertical" : "horizontal");
}

// ── link ──

bool Link::opens(std::string_view href) { return href.starts_with("https://") || href.starts_with("http://") || href.starts_with("mailto:"); }

void Link::mounted() {
    label_.set_style("body");
    label_.set_text(props.text);
    classify();
}

// An address leaves the application: the node's activation stated external, answered "yes"
// (a node's effect only strengthens its contract's, S-1); a verb's link is its verb's.
void Link::classify() {
    if (!attached()) return;
    const bool leaves = props.verb.empty() && !props.href.empty();
    services().set(node(), "effect", leaves ? Value("external") : Value{});
    services().set(node(), "answer", leaves ? Value("yes") : Value{});
}

void Link::prop_changed(LinkProp p) {
    label_.set_text(props.text);
    if (p == LinkProp::href || p == LinkProp::verb) classify();
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

Size Link::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    const Size text = label_.measure(m.services, m.services.theme().color("accent"));
    return {width.value_or(std::ceil(text.width)), height.value_or(std::ceil(text.height) + 2)};
}

float Link::baseline(MeasureContext& m, Size) { return label_.baseline(m.services, m.services.theme().color("accent")); }

void Link::paint(PaintContext& ctx) const {
    const bool off = props.disabled;
    // Hovered or pressed: the accent's darker steps, as a button's are.
    const Color ink = off ? ctx.theme.color("text_muted")
                      : ctx.state.pressed ? ctx.theme.color("accent_pressed", ctx.theme.color("accent"))
                      : ctx.state.hovered ? ctx.theme.color("accent_hover", ctx.theme.color("accent"))
                                          : ctx.theme.color("accent");
    const Size text = label_.measure(ctx.services, ink);
    label_.paint(ctx, {0, 0}, ink);
    // The underline: under the baseline, thicker while it is hovered.
    const float under = label_.baseline(ctx.services, ink) + 2;
    ctx.list.fill_rect({0, under, text.width, ctx.state.hovered && !off ? 1.5f : 1.0f}, ink.with_alpha(ctx.state.hovered ? 1.0f : 0.55f));
    if (ctx.state.focused && ctx.state.focus_visible) parts::focus_ring(ctx, {-2, -1, text.width + 4, ctx.size.height + 2}, CornerRadii::all(4));
}

void Link::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.text;
    if (!props.href.empty()) node.value = Value(props.href);
    if (props.disabled) node.states.set(State::disabled, true);
}

ActResult Link::activate(ActContext&) {
    if (props.disabled) return ActResult::fail("the link is disabled");
    // A verb is the runtime's to invoke (the node's `verb`); an address is the system's to open.
    if (props.verb.empty()) {
        if (!opens(props.href)) return ActResult::fail(std::format("`{}` is not an address the system opens (http, https, mailto)", props.href));
        if (attached() && !services().open_url(props.href)) return ActResult::fail(std::format("nothing here opens {}", props.href));
    }
    emit_activated(props.href);
    return ActResult::done();
}

// ── description list ──

void DescriptionList::rebuild() {
    terms_ = std::vector<parts::Label>(props.terms.size());
    details_ = std::vector<parts::Label>(props.terms.size());
    for (std::size_t index = 0; index < props.terms.size(); ++index) {
        terms_[index].set_style("label");
        terms_[index].set_text(props.terms[index]);
        details_[index].set_style("body");
        details_[index].set_text(index < props.details.size() ? props.details[index] : std::string());
    }
}

void DescriptionList::mounted() { rebuild(); }

void DescriptionList::prop_changed(DescriptionListProp) {
    rebuild();
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

Size DescriptionList::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available) {
    const Color muted = m.services.theme().color("text_muted"), ink = m.services.theme().color("text");
    const float available = width.value_or(aw.kind == mlibs::layout::Available::Kind::definite ? aw.value : 360.0f);
    term_width_ = 0;
    if (props.aligned)
        for (parts::Label& term : terms_) term_width_ = std::max(term_width_, std::ceil(term.measure(m.services, muted).width));
    term_width_ = std::min(term_width_, available * 0.4f);
    rows_.clear();
    float y = 0, widest = 0;
    for (std::size_t index = 0; index < terms_.size(); ++index) {
        if (index) y += row_gap;
        const Size term = terms_[index].measure(m.services, muted, props.aligned ? term_width_ : available);
        const float detail_width = props.aligned ? std::max(0.0f, available - term_width_ - column_gap) : available;
        const Size detail = details_[index].measure(m.services, ink, detail_width);
        // Aligned: the term on its description's first baseline.
        const float lift = props.aligned ? details_[index].baseline(m.services, ink) - terms_[index].baseline(m.services, muted) : 0;
        const float tall = props.aligned ? std::max(term.height + std::max(0.0f, lift), detail.height + std::max(0.0f, -lift)) : term.height + 2 + detail.height;
        rows_.push_back({0, y, available, tall});
        widest = std::max(widest, props.aligned ? term_width_ + column_gap + detail.width : std::max(term.width, detail.width));
        y += tall;
    }
    return {width.value_or(std::min(available, std::ceil(widest))), height.value_or(std::ceil(y))};
}

void DescriptionList::paint(PaintContext& ctx) const {
    const Color muted = ctx.theme.color("text_muted"), ink = ctx.theme.color("text"), divider = ctx.theme.color("border");
    for (std::size_t index = 0; index < terms_.size() && index < rows_.size(); ++index) {
        const Rect& row = rows_[index];
        if (index) ctx.list.fill_rect({0, row.y - row_gap / 2, ctx.size.width, 1}, divider.with_alpha(0.6f));
        if (props.aligned) {
            const float lift = details_[index].baseline(ctx.services, ink) - terms_[index].baseline(ctx.services, muted);
            terms_[index].paint(ctx, {0, row.y + std::max(0.0f, lift)}, muted);
            details_[index].paint(ctx, {term_width_ + column_gap, row.y + std::max(0.0f, -lift)}, ink);
            continue;
        }
        // Stacked: the description under its term.
        terms_[index].paint(ctx, {0, row.y}, muted);
        details_[index].paint(ctx, {0, row.y + terms_[index].measure(ctx.services, muted, ctx.size.width).height + 2}, ink);
    }
}

spec::SemanticNode DescriptionList::item(std::string_view path, std::size_t index) const {
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, index);
    n.role = Role::list_item;
    n.name = props.terms[index];
    n.value = Value(index < props.details.size() ? props.details[index] : std::string());
    if (index < rows_.size() && attached()) n.bounds = in_window(services().bounds(node()), rows_[index]);
    return n;
}

void DescriptionList::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value(static_cast<std::int64_t>(props.terms.size()));
}

std::vector<spec::SemanticNode> DescriptionList::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (std::size_t index = 0; index < props.terms.size(); ++index) out.push_back(item(path, index));
    return out;
}

std::optional<spec::SemanticNode> DescriptionList::virtual_item(std::string_view path, std::string_view key) const {
    std::size_t index = 0;
    if (std::from_chars(key.data(), key.data() + key.size(), index).ec != std::errc{} || index >= props.terms.size()) return std::nullopt;
    return item(path, index);
}

// ── spinner ──

Size Spinner::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    const float diameter = static_cast<float>(std::max(8.0, props.size));
    return {width.value_or(diameter), height.value_or(diameter)};
}

void Spinner::paint(PaintContext& ctx) const {
    const float diameter = std::min(ctx.size.width, ctx.size.height);
    const float stroke = std::max(2.0f, diameter / 9);
    const Point centre{ctx.size.width / 2, ctx.size.height / 2};
    xrender::Path ring;
    ring.add_circle(centre, (diameter - stroke) / 2);
    const Color accent = ctx.theme.color("accent");
    ctx.list.stroke_path(ring, {.width = stroke}, xrender::Paint::solid(accent.with_alpha(0.18f)));
    // An indicator, not a transition: its arc is where the runtime's clock says (the progress bar's way);
    // still, a quarter at rest, when motion is reduced or time is held.
    const bool still = ctx.services.motion_reduced() || ctx.services.time_paused();
    const double phase = still ? 0.0 : std::fmod(ctx.services.now().count() / turn, 1.0);
    xrender::StrokeStyle arc{.width = stroke, .cap = xrender::LineCap::round};
    arc.trim_start = 0;
    arc.trim_end = 0.28f;
    arc.trim_offset = static_cast<float>(phase);
    ctx.list.stroke_path(ring, arc, xrender::Paint::solid(accent));
    if (!still) {
        ctx.services.invalidate_paint(node());
        ctx.services.request_frame();
    }
}

void Spinner::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.states.set(State::busy, true);
}

}  // namespace omui
