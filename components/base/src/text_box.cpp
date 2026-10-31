module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;
import openmui.unicode;

namespace omui {

using parts::focus_ring;
using parts::keep_frames;
using parts::mix;

namespace {

constexpr float default_w = 240;
constexpr float pad_y = 8, pad_x = 10;
constexpr seconds blink_period{0.53};
constexpr std::string_view bullet = "•";

// The modifier that moves by words: Alt where the primary is Command (macOS), else Ctrl.
Modifier word_modifier() { return primary_modifier() == Modifier::meta ? Modifier::alt : Modifier::ctrl; }

bool only(Modifiers m, Modifiers wanted) { return (m.bits & ~static_cast<std::uint8_t>(Modifier::shift)) == wanted.bits; }

}  // namespace

TextBox::~TextBox() {
    if (blink_timer_ && attached()) services().cancel(blink_timer_);
}

void TextBox::mounted() {
    editor_ = typeset::TextEditor(props.text, {props.multiline, static_cast<std::size_t>(std::max(0, props.max_length))});
    editor_.move_to(editor_.text().size());
}

void TextBox::prop_changed(TextBoxProp p) {
    if (!attached()) return;
    switch (p) {
        case TextBoxProp::text:
            // From outside (a binding, a script): the field shows it; the caret goes to its end.
            if (props.text != editor_.text()) {
                editor_.set_text(props.text);
                editor_.move_to(editor_.text().size());
                paragraph_dirty_ = true;
                services().invalidate_layout(node());
            }
            break;
        case TextBoxProp::multiline:
        case TextBoxProp::max_length: {
            const std::string text = editor_.text();
            editor_ = typeset::TextEditor(text, {props.multiline, static_cast<std::size_t>(std::max(0, props.max_length))});
            editor_.move_to(text.size());
            paragraph_dirty_ = true;
            services().invalidate_layout(node());
            break;
        }
        case TextBoxProp::password:
        case TextBoxProp::placeholder:
            paragraph_dirty_ = true;
            services().invalidate_layout(node());
            break;
        default:
            services().invalidate_paint(node());
            break;
    }
}

void TextBox::restyle() { paragraph_dirty_ = true; }

void TextBox::style(const Theme& theme, layout::Style& s) const {
    s.padding = {pad_y, pad_x, pad_y, pad_x};
    s.border = Insets::all(1);
    s.min_height = layout::pt(36);
}

std::string TextBox::shown() const {
    std::string composed = editor_.display_text();
    if (!props.password) return composed;
    std::string masked;
    for (std::size_t i = 0, n = unicode::grapheme_count(composed); i < n; ++i) masked += bullet;
    return masked;
}

std::size_t TextBox::to_shown(std::size_t o) const {
    // The composition replaces the selection where it is shown.
    if (editor_.composing()) {
        const typeset::Selection s = editor_.selection();
        if (o >= s.end()) o = o - (s.end() - s.begin()) + editor_.composition().size();
        else if (o > s.begin()) o = s.begin();
    }
    if (!props.password) return o;
    const std::string composed = editor_.display_text();
    return unicode::grapheme_count(std::string_view(composed).substr(0, std::min(o, composed.size()))) * bullet.size();
}

std::size_t TextBox::from_shown(std::size_t o) const {
    std::size_t composed_offset = o;
    if (props.password) {
        const std::string composed = editor_.display_text();
        const std::vector<std::size_t> b = unicode::grapheme_boundaries(composed);
        const std::size_t i = std::min(o / bullet.size(), b.empty() ? std::size_t{0} : b.size() - 1);
        composed_offset = b.empty() ? 0 : b[i];
    }
    if (editor_.composing()) {
        const typeset::Selection s = editor_.selection();
        const std::size_t comp = editor_.composition().size();
        if (composed_offset >= s.begin() + comp) return composed_offset - comp + (s.end() - s.begin());
        if (composed_offset > s.begin()) return s.begin();
    }
    return composed_offset;
}

typeset::Paragraph& TextBox::paragraph(Services& s, float width) const {
    const std::string text = shown();
    if (paragraph_dirty_ || !paragraph_ || text != paragraph_text_) {
        const Theme& theme = s.theme();
        const TextToken& tok = theme.text_style("body");
        typeset::ParagraphStyle ps;
        ps.style.families = tok.families;
        ps.style.size = tok.size;
        ps.style.weight = tok.weight;
        ps.style.line_height = tok.line_height;
        ps.style.color = theme.color("text");
        ps.wrap = props.multiline;
        line_height_ = tok.size * tok.line_height;
        paragraph_.emplace(s.fonts(), text, std::move(ps));
        paragraph_text_ = text;
        paragraph_width_ = -1;
        paragraph_dirty_ = false;
    }
    const float w = props.multiline ? width : std::numeric_limits<float>::infinity();
    if (w != paragraph_width_) {
        paragraph_->layout(w);
        paragraph_width_ = w;
    }
    return *paragraph_;
}

Size TextBox::measure(MeasureContext& m, std::optional<float> width, std::optional<float>, layout::Available aw, layout::Available) {
    const float w = width.value_or(aw.is_definite() ? std::min(aw.value, default_w) : default_w);
    typeset::Paragraph& p = paragraph(m.services, w);
    if (!props.multiline) return {w, line_height_};
    return {w, std::max(p.size().height, 3 * line_height_)};
}

float TextBox::baseline(MeasureContext& m, Size content) {
    typeset::Paragraph& p = paragraph(m.services, content.width);
    return p.lines().empty() ? line_height_ * 0.8f : p.first_baseline();
}

Point TextBox::text_origin(const Rect& content) const {
    if (props.multiline) return {content.x, content.y};
    return {content.x - scroll_x_, content.y + (content.height - line_height_) / 2};
}

void TextBox::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii radii = CornerRadii::all(th.corner("medium", 8)).fitted(ctx.size);
    content_ = ctx.content;

    ctx.list.fill_round_rect(box, radii, render::Paint::solid(off ? th.color("surface_variant") : th.color("surface")));
    const bool focus = ctx.state.focused && !off;
    const float bw = focus ? 2.0f : 1.0f;
    const Color border = focus ? th.color("accent") : (ctx.state.hovered && !off ? th.color("border_strong") : th.color("border"));
    CornerRadii inner = radii;
    for (float* r : {&inner.top_left, &inner.top_right, &inner.bottom_right, &inner.bottom_left}) *r = std::max(0.0f, *r - bw / 2);
    ctx.list.stroke_round_rect({bw / 2, bw / 2, box.width - bw, box.height - bw}, inner, bw, render::Paint::solid(border));

    typeset::Paragraph& p = paragraph(ctx.services, ctx.content.width);
    // Keep the caret in view on a single line.
    // While composing, the caret follows the preedit (which stands where the selection was).
    const std::size_t caret_shown = editor_.composing() ? to_shown(editor_.selection().end()) : to_shown(editor_.selection().focus);
    const typeset::Caret caret = p.caret(caret_shown);
    if (!props.multiline) {
        const float limit = std::max(0.0f, p.size().width - ctx.content.width + 2);
        if (caret.x - scroll_x_ > ctx.content.width - 2) scroll_x_ = caret.x - ctx.content.width + 2;
        if (caret.x - scroll_x_ < 0) scroll_x_ = caret.x;
        scroll_x_ = std::clamp(scroll_x_, 0.0f, limit);
    }
    const Point origin = text_origin(ctx.content);

    ctx.list.push_clip(ctx.content.outset({2, 2, 2, 2}));
    const typeset::Selection sel = editor_.selection();
    if (!sel.empty() && !editor_.composing()) {
        const Color sc = th.color("selection");
        for (const Rect& r : p.selection(to_shown(sel.begin()), to_shown(sel.end())))
            ctx.list.fill_rect(r.offset(origin), sc);
    }
    if (editor_.text().empty() && !editor_.composing() && !props.placeholder.empty()) {
        const TextToken& tok = th.text_style("body");
        typeset::ParagraphStyle ps;
        ps.style.families = tok.families;
        ps.style.size = tok.size;
        ps.style.line_height = tok.line_height;
        ps.style.color = th.color("text_muted");
        ps.wrap = props.multiline;
        typeset::Paragraph ph(ctx.services.fonts(), props.placeholder, std::move(ps));
        ph.layout(props.multiline ? ctx.content.width : std::numeric_limits<float>::infinity());
        ph.paint(ctx.list, {ctx.content.x, origin.y});
    } else {
        if (off) ctx.list.push_layer(0.55f, render::BlendMode::normal, {}, {}, box);
        p.paint(ctx.list, origin);
        if (off) ctx.list.pop_layer();
    }
    if (editor_.composing()) {
        // The preedit is underlined where it stands.
        const std::size_t sa = to_shown(sel.begin()), sb = to_shown(sel.end());
        for (const Rect& r : p.selection(sa, std::max(sa, sb)))
            ctx.list.fill_rect({r.x + origin.x, r.bottom() + origin.y - 2, r.width, 1.5f}, th.color("accent"));
    }
    if (focus && caret_on_ && (sel.empty() || editor_.composing())) {
        const Rect cr{origin.x + caret.x - 0.75f, origin.y + caret.top, 1.5f, caret.height};
        ctx.list.fill_rect(cr, th.color("accent"));
    }
    ctx.list.pop_clip();

    // Input methods put their candidates at the caret.
    if (focus) ctx.services.text_input(ctx.node, Rect{origin.x + caret.x, origin.y + caret.top, 1.5f, caret.height});
}

void TextBox::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused && ctx.state.focus_visible)
        focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.theme.corner("medium", 8)).fitted(ctx.size));
}

void TextBox::edited() {
    write_text(editor_.text());
    emit_changed(editor_.text());
    paragraph_dirty_ = true;
    caret_moved();
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

void TextBox::caret_moved() {
    blink(true);
    services().invalidate_paint(node());
}

void TextBox::blink(bool restart) {
    if (blink_timer_) {
        services().cancel(blink_timer_);
        blink_timer_ = 0;
    }
    if (!focused_) return;
    if (restart) caret_on_ = true;
    else caret_on_ = !caret_on_;
    services().invalidate_paint(node());
    blink_timer_ = services().after(blink_period, [this] {
        blink_timer_ = 0;
        blink(false);
    });
}

bool TextBox::key(const KeyEvent& e) {
    const bool shift = e.mods.has(Modifier::shift);
    const Modifiers primary = Modifiers(primary_modifier());
    const Modifiers word = Modifiers(word_modifier());
    const bool editable = !props.readonly && !props.disabled;
    const typeset::Unit unit = only(e.mods, word) ? typeset::Unit::word : typeset::Unit::grapheme;
    switch (e.key) {
        case Key::left:
            if (only(e.mods, primary) && primary_modifier() == Modifier::meta) editor_.move_left(typeset::Unit::line, shift);
            else editor_.move_left(unit, shift);
            caret_moved();
            return true;
        case Key::right:
            if (only(e.mods, primary) && primary_modifier() == Modifier::meta) editor_.move_right(typeset::Unit::line, shift);
            else editor_.move_right(unit, shift);
            caret_moved();
            return true;
        case Key::home:
            editor_.move_left(only(e.mods, primary) ? typeset::Unit::document : typeset::Unit::line, shift);
            caret_moved();
            return true;
        case Key::end:
            editor_.move_right(only(e.mods, primary) ? typeset::Unit::document : typeset::Unit::line, shift);
            caret_moved();
            return true;
        case Key::up:
        case Key::down: {
            if (!props.multiline) return false;
            typeset::Paragraph& p = paragraph(services(), content_.width);
            const std::size_t at = to_shown(editor_.selection().focus);
            const float x = p.caret(at).x;
            const std::size_t to = e.key == Key::up ? p.offset_above(at, x) : p.offset_below(at, x);
            editor_.move_to(from_shown(to), shift);
            caret_moved();
            return true;
        }
        case Key::backspace:
            if (!editable) return true;
            editor_.delete_backward(unit);
            edited();
            return true;
        case Key::delete_:
            if (!editable) return true;
            editor_.delete_forward(unit);
            edited();
            return true;
        case Key::enter:
            if (!props.multiline || !editable) return false;  // single line: the contract's key submits
            editor_.insert("\n");
            edited();
            return true;
        case Key::escape:
            if (!editor_.composing()) return false;
            editor_.cancel_composition();
            caret_moved();
            return true;
        default:
            break;
    }
    if (e.mods == primary) {
        switch (e.key) {
            case Key::a:
                editor_.select_all();
                caret_moved();
                return true;
            case Key::c:
            case Key::x: {
                const typeset::Selection s = editor_.selection();
                if (s.empty() || props.password) return true;  // a password does not leave the field
                services().set_clipboard_text(std::string_view(editor_.text()).substr(s.begin(), s.end() - s.begin()));
                if (e.key == Key::x && editable) {
                    editor_.insert("");
                    edited();
                }
                return true;
            }
            case Key::v:
                if (!editable) return true;
                if (editor_.insert(services().clipboard_text())) edited();
                return true;
            case Key::z:
                if (editable && editor_.undo()) edited();
                return true;
            case Key::y:
                if (editable && editor_.redo()) edited();
                return true;
            default:
                return false;
        }
    }
    if (e.mods == (Modifiers(primary_modifier()) | Modifier::shift) && e.key == Key::z) {
        if (editable && editor_.redo()) edited();
        return true;
    }
    return false;
}

bool TextBox::on_event(EventContext& ctx, const InputEvent& e) {
    if (const auto* f = std::get_if<FocusEvent>(&e)) {
        focused_ = f->gained;
        if (!focused_) {
            if (editor_.composing()) editor_.commit_composition(editor_.composition());
            services().text_input(node(), std::nullopt);
            if (editor_.text() != props.text) edited();
        }
        blink(true);
        return false;
    }
    if (props.disabled) return false;
    if (const auto* k = std::get_if<KeyEvent>(&e)) {
        if (k->kind != KeyKind::down) return false;
        return key(*k);
    }
    if (const auto* t = std::get_if<TextEvent>(&e)) {
        if (props.readonly) return true;
        if (editor_.composing()) editor_.commit_composition(t->text);
        else if (!editor_.insert(t->text)) return true;
        edited();
        return true;
    }
    if (const auto* pe = std::get_if<PreeditEvent>(&e)) {
        if (props.readonly) return true;
        if (pe->text.empty()) editor_.cancel_composition();
        else editor_.set_composition(pe->text, pe->cursor_begin);
        paragraph_dirty_ = true;
        caret_moved();
        services().invalidate_layout(node());
        return true;
    }
    if (const auto* p = std::get_if<PointerEvent>(&e)) {
        typeset::Paragraph& para = paragraph(services(), content_.width);
        const Point o = text_origin(content_);
        const std::size_t at = from_shown(para.offset_at({ctx.local.x - o.x, ctx.local.y - o.y}));
        switch (p->kind) {
            case PointerKind::down:
                if (p->button != MouseButton::left && p->type == PointerType::mouse) return false;
                if (p->clicks >= 3) editor_.select_all();
                else if (p->clicks == 2) editor_.select_word_at(at);
                else editor_.move_to(at, p->mods.has(Modifier::shift));
                selecting_ = p->clicks < 2;
                services().capture_pointer(node());
                caret_moved();
                return true;
            case PointerKind::move:
                if (!selecting_) return false;
                editor_.move_to(at, true);
                caret_moved();
                return true;
            case PointerKind::up:
            case PointerKind::cancel:
                if (!selecting_) return true;
                selecting_ = false;
                services().release_pointer();
                return true;
            default:
                return false;
        }
    }
    return false;
}

void TextBox::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = !props.label.empty() ? props.label : props.placeholder;
    node.value = props.password ? Value(shown()) : Value(editor_.text());
    node.origin = props.origin == TextBoxOrigin::app ? Origin::app : props.origin == TextBoxOrigin::remote ? Origin::remote : Origin::user_content;
    node.states.set(State::editable, !props.readonly && !props.disabled);
    node.states.set(State::readonly, props.readonly);
    node.states.set(State::multiline, props.multiline);
    if (props.disabled) node.states.set(State::disabled);
}

ActResult TextBox::focus(ActContext&) {
    services().focus(node());
    return ActResult::done();
}

namespace {

// The grapheme offset of a byte offset (the unit of set_selection).
std::int64_t grapheme_at(std::string_view text, std::size_t byte) {
    const std::vector<std::size_t> b = unicode::grapheme_boundaries(text);
    return static_cast<std::int64_t>(std::ranges::lower_bound(b, byte) - b.begin());
}

}  // namespace

// What takes an edit back (S-12): the text it replaced, then the selection there was.
ActResult TextBox::taken_back_by(ActResult r, const std::string& text, std::size_t anchor, std::size_t focus) const {
    r.undo_by("set_value", Value{}.set("value", text));
    r.undo_by("set_selection", Value{}.set("anchor", grapheme_at(text, anchor)).set("focus", grapheme_at(text, focus)));
    return r;
}

ActResult TextBox::set_value(ActContext&, const Value& value) {
    if (props.readonly) return ActResult::fail("the field is read-only");
    const std::string before = editor_.text();
    const auto sel = editor_.selection();
    std::string text = value.type() == Value::Type::text ? std::string(value.as_string()) : value.json();
    editor_.select_all();
    editor_.insert(text);
    edited();
    return taken_back_by(ActResult::done(Value(editor_.text())), before, sel.anchor, sel.focus);
}

ActResult TextBox::set_selection(ActContext&, const SetSelectionArgs& args) {
    const Value &anchor = args.anchor, &focus = args.focus;
    // Offsets in graphemes (the action's unit), clamped to the text.
    const std::vector<std::size_t> b = unicode::grapheme_boundaries(editor_.text());
    auto byte_of = [&](const Value& v) -> std::size_t {
        const auto i = static_cast<std::size_t>(std::max<std::int64_t>(0, v.as_int()));
        if (b.empty()) return 0;
        return b[std::min(i, b.size() - 1)];
    };
    editor_.select(byte_of(anchor), byte_of(focus.is_null() ? anchor : focus));
    caret_moved();
    return ActResult::done();
}

ActResult TextBox::replace_selection(ActContext&, const Value& text) {
    if (props.readonly) return ActResult::fail("the field is read-only");
    const std::string before = editor_.text();
    const auto sel = editor_.selection();
    if (!editor_.insert(text.as_string())) return ActResult::fail("the text does not fit (max_length)");
    edited();
    return taken_back_by(ActResult::done(Value(editor_.text())), before, sel.anchor, sel.focus);
}

ActResult TextBox::preview(ActContext& ctx, std::string_view action) const {
    if (action != "set_value" && action != "replace_selection") return ActResult::done();
    if (props.readonly) return ActResult::fail("the field is read-only");
    typeset::TextEditor copy = editor_;
    if (action == "set_value") {
        const Value* v = ctx.args.get("value");
        const Value value = v ? *v : Value{};
        copy.select_all();
        copy.insert(value.type() == Value::Type::text ? std::string(value.as_string()) : value.json());
    } else {
        const Value* t = ctx.args.get("text");
        if (!copy.insert(t ? t->as_string() : std::string_view{})) return ActResult::fail("the text does not fit (max_length)");
    }
    const auto sel = editor_.selection();
    // A password field's text is not shown, nor what it would become.
    ActResult r = props.password ? ActResult::done() : ActResult::done(Value(copy.text()));
    return taken_back_by(std::move(r), editor_.text(), sel.anchor, sel.focus);
}

ActResult TextBox::submit(ActContext&) {
    if (props.multiline) return ActResult::fail("a multi-line field does not submit");
    emit_submitted(editor_.text());
    return ActResult::done(Value(editor_.text()));
}

}  // namespace omui
