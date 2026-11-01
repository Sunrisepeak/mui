module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

namespace {

constexpr float code_head = 28;

}  // namespace

std::string_view CodeEditor::language_key() const noexcept {
    static constexpr std::array<std::string_view, 7> keys{"cpp", "typescript", "python", "lua", "json", "toml", "plain"};
    return keys[static_cast<std::size_t>(props.language)];
}

std::string_view CodeEditor::language_name() const noexcept {
    static constexpr std::array<std::string_view, 7> names{"C++", "TypeScript", "Python", "Lua", "JSON", "TOML", "Text"};
    return names[static_cast<std::size_t>(props.language)];
}

void CodeEditor::mounted() {
    tag_.set_style("caption");
    tag_.set_text(std::string(language_name()));
}

NodeId CodeEditor::field() const {
    for (NodeId c : services().children(node()))
        if (parts::segment(services(), c) == "text") return c;
    return {};
}

void CodeEditor::prop_changed(CodeEditorProp p) {
    if (p == CodeEditorProp::language) tag_.set_text(std::string(language_name()));
    if (!attached()) return;
    // From outside: its field shows it (from its field: it has it already).
    if (p == CodeEditorProp::text && !own_edit_)
        if (const NodeId f = field()) services().set(f, "text", Value(props.text));
    if (p == CodeEditorProp::language || p == CodeEditorProp::readonly || p == CodeEditorProp::lines) services().recompose(node());
    services().restyle(node());
    services().invalidate_paint(node());
}

void CodeEditor::style(const Theme&, layout::Style& s) const {
    s.direction = layout::Direction::column;
    s.padding = {code_head, 1, 1, 1};
}

std::vector<Element> CodeEditor::compose() {
    const TextToken code_line{14, 400, 1.55f, 0, {}};
    const float tall = static_cast<float>(std::max<std::int64_t>(1, props.lines)) * code_line.size * code_line.line_height + 16;
    Element f = text_box({.id = "text",
                          .label = props.label.empty() ? std::format("{} code", language_name()) : props.label,
                          .multiline = true,
                          .readonly = props.readonly,
                          .text = props.text,
                          .on_changed = [this](std::string t) {
                              own_edit_ = true;
                              write_text(t);
                              emit_changed(t);
                              own_edit_ = false;
                          },
                          .grow = 1});
    f.prop("min_height", Source{Value(static_cast<double>(tall)), {}, {}});
    f.prop("bare", Source{Value(true), {}, {}});
    f.prop("language", Source{Value(std::string(language_key())), {}, {}});
    return {std::move(f)};
}

void CodeEditor::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 8)).fitted(ctx.size);
    ctx.list.fill_round_rect(box, r, render::Paint::solid(th.color("surface")));
    // Its head: the language it is in.
    ctx.list.fill_round_rect({0, 0, box.width, code_head}, {r.top_left, r.top_right, 0, 0}, render::Paint::solid(th.color("surface_variant")));
    ctx.list.fill_rect({0, code_head - 1, box.width, 1}, th.color("border"));
    const Size t = tag_.measure(ctx.services, th.color("text_muted"));
    tag_.paint(ctx, {12, (code_head - t.height) / 2}, th.color("text_muted"));
    const NodeId f = field();
    const bool focus = f && ctx.services.state(f).focused;  // its field's focus is its own
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, focus ? 1.5f : 1, render::Paint::solid(focus ? th.color("accent") : th.color("border")));
}

void CodeEditor::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label.empty() ? std::format("{} code", language_name()) : props.label;
    node.value = Value{}.set("language", std::string(language_key())).set("lines", static_cast<std::int64_t>(std::ranges::count(props.text, '\n') + 1));
}

ActResult CodeEditor::set_value(ActContext&, const Value& value) {
    if (props.readonly) return ActResult::fail("it is read only");
    const std::string old = props.text, text(value.as_string());
    if (text != props.text) {
        write_text(text);
        prop_changed(CodeEditorProp::text);
        emit_changed(text);
    }
    return ActResult::done().undo_by("set_value", Value{}.set("value", old));
}

}  // namespace omui
