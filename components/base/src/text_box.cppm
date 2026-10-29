// openmui.ui.base:text_box — a single- or multi-line text field (contract
// `text_box`): the editing model of openmui.text (graphemes, words, undo),
// selection by pointer and keys, the clipboard, input methods (preedit shown
// in place, the candidate window at the caret), a blinking caret on the
// runtime's timers, horizontal scrolling of a long line, masked passwords,
// and actions in grapheme offsets for agents.
export module openmui.ui.base:text_box;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :text_box_contract;

export namespace omui {

class TextBox : public TextBoxContract<TextBox> {
public:
    static constexpr std::string_view cursor = "text";

    TextBox() = default;
    TextBox(const TextBox&) = delete;
    TextBox& operator=(const TextBox&) = delete;
    ~TextBox();

    void mounted();
    void prop_changed(TextBoxProp p);
    void restyle();

    void style(const Theme& theme, layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    float baseline(MeasureContext& m, Size content);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult focus(ActContext& ctx);
    ActResult set_value(ActContext& ctx, const Value& value);
    ActResult set_selection(ActContext& ctx, const SetSelectionArgs& args);
    ActResult replace_selection(ActContext& ctx, const Value& text);
    ActResult submit(ActContext& ctx);
    // A dry run (S-13): the text it would hold, worked out on a copy of the editor.
    ActResult preview(ActContext& ctx, std::string_view action) const;

    [[nodiscard]] const typeset::TextEditor& editor() const noexcept { return editor_; }

private:
    [[nodiscard]] ActResult taken_back_by(ActResult r, const std::string& text, std::size_t anchor, std::size_t focus) const;
    // After the editor's text changed: the property, the event, layout.
    void edited();
    void caret_moved();
    void blink(bool restart);
    bool key(const KeyEvent& e);

    // The text as shown: with the composition in place, masked for passwords.
    [[nodiscard]] std::string shown() const;
    [[nodiscard]] std::size_t to_shown(std::size_t text_offset) const;
    [[nodiscard]] std::size_t from_shown(std::size_t shown_offset) const;
    [[nodiscard]] typeset::Paragraph& paragraph(Services& s, float width) const;
    [[nodiscard]] Point text_origin(const Rect& content) const;

    typeset::TextEditor editor_;
    mutable std::optional<typeset::Paragraph> paragraph_;
    mutable std::string   paragraph_text_;
    mutable float         paragraph_width_ = -1;
    mutable bool          paragraph_dirty_ = true;
    mutable float         scroll_x_ = 0;
    mutable float         line_height_ = 20;
    mutable Rect          content_{};
    bool                  focused_ = false;
    bool                  caret_on_ = true;
    bool                  selecting_ = false;
    std::uint64_t         blink_timer_ = 0;
};

}  // namespace omui
