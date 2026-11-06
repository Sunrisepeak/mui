// openmui.ui.base:code — a code editor: a text box in its code mode (the
// text coloured by what each stretch is, its lines numbered, Tab indenting,
// Enter keeping the indent) on a surface that says its language.
export module openmui.ui.base:code;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :code_editor_contract;

export namespace omui {

class CodeEditor : public CodeEditorContract<CodeEditor> {
public:
    void mounted();
    void prop_changed(CodeEditorProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<Element> compose();

    ActResult set_value(ActContext& ctx, const Value& value);

    // The language as its field reads it, and as its tag shows it.
    [[nodiscard]] std::string_view language_key() const noexcept;
    [[nodiscard]] std::string_view language_name() const noexcept;

private:
    [[nodiscard]] NodeId field() const;

    mutable parts::Label tag_;
    bool                 own_edit_ = false;  // the text changed in its field: no need to tell the field
};

}  // namespace omui
