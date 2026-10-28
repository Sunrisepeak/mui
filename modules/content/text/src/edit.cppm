// openmui.text:edit — the editing model of a text field.
//
// Text, a selection (anchor and focus on grapheme boundaries), an input
// method's composition, and undo/redo. Movement and deletion go by grapheme
// cluster or by word (UAX #29), so a caret never lands inside a family emoji
// or between a base and its accent. Vertical movement needs layout and is the
// text field's (Paragraph::offset_above / offset_below).
export module openmui.text:edit;

import std;

export namespace omui::typeset {

struct Selection {
    std::size_t anchor = 0, focus = 0;
    [[nodiscard]] std::size_t begin() const noexcept { return std::min(anchor, focus); }
    [[nodiscard]] std::size_t end() const noexcept { return std::max(anchor, focus); }
    [[nodiscard]] bool empty() const noexcept { return anchor == focus; }
    friend bool operator==(const Selection& a, const Selection& b) noexcept { return a.anchor == b.anchor && a.focus == b.focus; }
};

enum class Unit : std::uint8_t { grapheme, word, line, document };

struct EditorOptions {
    bool        multiline = false;   // newlines may be inserted (else they become spaces)
    std::size_t max_length = 0;      // in grapheme clusters; 0: unlimited
};

class TextEditor {
public:
    explicit TextEditor(std::string text = {}, EditorOptions options = {});

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] Selection selection() const noexcept { return selection_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    // Replaces the whole text (not undoable: a new document).
    void set_text(std::string text);
    // Offsets snap to grapheme boundaries.
    void select(std::size_t anchor, std::size_t focus);
    void select_all();
    void select_word_at(std::size_t offset);

    // Types or pastes: replaces the selection. Answers false when the
    // length limit refused all of it.
    bool insert(std::string_view text);
    void delete_backward(Unit unit = Unit::grapheme);
    void delete_forward(Unit unit = Unit::grapheme);

    // Moves the focus (and the anchor too unless `extend`); a non-empty
    // selection collapses to its side first when not extending.
    void move_left(Unit unit = Unit::grapheme, bool extend = false);
    void move_right(Unit unit = Unit::grapheme, bool extend = false);
    void move_to(std::size_t offset, bool extend = false);

    // Input method composition: pre-edit text shown at the selection, not
    // yet part of the text.
    void set_composition(std::string_view preedit, int cursor = -1);
    void commit_composition(std::string_view text);
    void cancel_composition();
    [[nodiscard]] bool composing() const noexcept { return !composition_.empty(); }
    [[nodiscard]] const std::string& composition() const noexcept { return composition_; }
    // The text as displayed: with the pre-edit inserted at the selection.
    [[nodiscard]] std::string display_text() const;

    bool undo();
    bool redo();
    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }

private:
    struct Step {
        std::string text;
        Selection   before;
        Selection   after;
        std::string after_text;
        bool        typing = false;
    };
    void replace(std::size_t begin, std::size_t end, std::string_view with, bool typing);
    [[nodiscard]] std::size_t snap(std::size_t offset) const;

    std::string       text_;
    Selection         selection_;
    EditorOptions     options_;
    std::string       composition_;
    int               composition_cursor_ = -1;
    std::vector<Step> undo_, redo_;
    std::uint64_t     revision_ = 0;
};

}  // namespace omui::typeset
