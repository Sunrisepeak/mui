module openmui.text;

import std;
import openmui.unicode;

namespace omui::typeset {

TextEditor::TextEditor(std::string text, EditorOptions options) : options_(options) { set_text(std::move(text)); }

void TextEditor::set_text(std::string text) {
    text_ = std::move(text);
    if (!options_.multiline) std::ranges::replace(text_, '\n', ' ');
    selection_ = {text_.size(), text_.size()};
    undo_.clear();
    redo_.clear();
    composition_.clear();
    ++revision_;
}

std::size_t TextEditor::snap(std::size_t offset) const {
    if (offset >= text_.size()) return text_.size();
    if (offset == 0) return 0;
    // The grapheme boundary at or before the offset.
    std::size_t next = unicode::next_grapheme(text_, offset);
    std::size_t prev = unicode::previous_grapheme(text_, next);
    return prev <= offset ? prev : unicode::previous_grapheme(text_, offset);
}

void TextEditor::select(std::size_t anchor, std::size_t focus) { selection_ = {snap(anchor), snap(focus)}; }

void TextEditor::select_all() { selection_ = {0, text_.size()}; }

void TextEditor::select_word_at(std::size_t offset) {
    auto [b, e] = unicode::word_at(text_, std::min(offset, text_.empty() ? 0 : text_.size() - 1));
    selection_ = {b, e};
}

void TextEditor::replace(std::size_t begin, std::size_t end, std::string_view with, bool typing) {
    Step step;
    step.before = selection_;
    step.text = text_.substr(begin, end - begin);
    text_.replace(begin, end - begin, with);
    selection_ = {begin + with.size(), begin + with.size()};
    step.after = selection_;
    step.after_text = std::string(with);
    step.typing = typing;
    // Consecutive typing at the caret merges into one undo step.
    if (typing && !undo_.empty() && undo_.back().typing && step.text.empty() && undo_.back().after == step.before &&
        with.find_first_of(" \n") == std::string_view::npos) {
        Step& last = undo_.back();
        last.after_text += step.after_text;
        last.after = step.after;
    } else {
        undo_.push_back(std::move(step));
    }
    redo_.clear();
    ++revision_;
}

bool TextEditor::insert(std::string_view input) {
    std::string s(input);
    if (!options_.multiline) {
        std::ranges::replace(s, '\n', ' ');
        std::ranges::replace(s, '\r', ' ');
    } else {
        // Normalise line ends to \n.
        std::string t;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\r') {
                t += '\n';
                if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
            } else {
                t += s[i];
            }
        }
        s = std::move(t);
    }
    if (options_.max_length > 0) {
        std::size_t current = unicode::grapheme_count(text_) -
                              unicode::grapheme_count(std::string_view(text_).substr(selection_.begin(), selection_.end() - selection_.begin()));
        if (current >= options_.max_length) return false;
        std::size_t room = options_.max_length - current;
        auto b = unicode::grapheme_boundaries(s);
        if (b.size() - 1 > room) s.resize(b[room]);
    }
    replace(selection_.begin(), selection_.end(), s, true);
    return true;
}

namespace {

std::size_t word_left(std::string_view t, std::size_t o) {
    auto b = unicode::word_boundaries(t);
    // The start of the word before o, skipping white space and punctuation.
    std::size_t best = 0;
    for (std::size_t i = 0; i + 1 < b.size(); ++i) {
        if (b[i] >= o) break;
        auto seg = t.substr(b[i], b[i + 1] - b[i]);
        bool wordlike = std::ranges::any_of(seg, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || (c & 0x80); });
        if (wordlike) best = b[i];
    }
    return best;
}

std::size_t word_right(std::string_view t, std::size_t o) {
    auto b = unicode::word_boundaries(t);
    for (std::size_t i = 0; i + 1 < b.size(); ++i) {
        if (b[i + 1] <= o) continue;
        auto seg = t.substr(b[i], b[i + 1] - b[i]);
        bool wordlike = std::ranges::any_of(seg, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || (c & 0x80); });
        if (wordlike) return b[i + 1];
    }
    return t.size();
}

std::size_t line_start(std::string_view t, std::size_t o) {
    if (o == 0) return 0;
    auto nl = t.rfind('\n', o - 1);
    return nl == std::string_view::npos ? 0 : nl + 1;
}

std::size_t line_end(std::string_view t, std::size_t o) {
    auto nl = t.find('\n', o);
    return nl == std::string_view::npos ? t.size() : nl;
}

std::size_t step_left(std::string_view t, std::size_t o, Unit u) {
    switch (u) {
        case Unit::grapheme: return unicode::previous_grapheme(t, o);
        case Unit::word: return word_left(t, o);
        case Unit::line: return line_start(t, o);
        case Unit::document: return 0;
    }
    return o;
}

std::size_t step_right(std::string_view t, std::size_t o, Unit u) {
    switch (u) {
        case Unit::grapheme: return unicode::next_grapheme(t, o);
        case Unit::word: return word_right(t, o);
        case Unit::line: return line_end(t, o);
        case Unit::document: return t.size();
    }
    return o;
}

}  // namespace

void TextEditor::delete_backward(Unit unit) {
    if (!selection_.empty()) {
        replace(selection_.begin(), selection_.end(), "", false);
        return;
    }
    // Backspace removes a whole cluster: a family emoji or a letter with its
    // accents goes at once, as it came in one caret step.
    std::size_t to = step_left(text_, selection_.focus, unit);
    if (to < selection_.focus) replace(to, selection_.focus, "", false);
}

void TextEditor::delete_forward(Unit unit) {
    if (!selection_.empty()) {
        replace(selection_.begin(), selection_.end(), "", false);
        return;
    }
    std::size_t to = step_right(text_, selection_.focus, unit);
    if (to > selection_.focus) replace(selection_.focus, to, "", false);
}

void TextEditor::move_left(Unit unit, bool extend) {
    if (!extend && !selection_.empty() && unit == Unit::grapheme) {
        selection_ = {selection_.begin(), selection_.begin()};
        return;
    }
    std::size_t f = step_left(text_, selection_.focus, unit);
    selection_ = extend ? Selection{selection_.anchor, f} : Selection{f, f};
}

void TextEditor::move_right(Unit unit, bool extend) {
    if (!extend && !selection_.empty() && unit == Unit::grapheme) {
        selection_ = {selection_.end(), selection_.end()};
        return;
    }
    std::size_t f = step_right(text_, selection_.focus, unit);
    selection_ = extend ? Selection{selection_.anchor, f} : Selection{f, f};
}

void TextEditor::move_to(std::size_t offset, bool extend) {
    std::size_t f = snap(offset);
    selection_ = extend ? Selection{selection_.anchor, f} : Selection{f, f};
}

void TextEditor::set_composition(std::string_view preedit, int cursor) {
    composition_ = std::string(preedit);
    composition_cursor_ = cursor;
    ++revision_;
}

void TextEditor::commit_composition(std::string_view text) {
    composition_.clear();
    composition_cursor_ = -1;
    insert(text);
}

void TextEditor::cancel_composition() {
    composition_.clear();
    composition_cursor_ = -1;
    ++revision_;
}

std::string TextEditor::display_text() const {
    if (composition_.empty()) return text_;
    std::string out = text_;
    out.replace(selection_.begin(), selection_.end() - selection_.begin(), composition_);
    return out;
}

bool TextEditor::undo() {
    if (undo_.empty()) return false;
    Step s = std::move(undo_.back());
    undo_.pop_back();
    std::size_t at = s.after.focus - s.after_text.size();
    text_.replace(at, s.after_text.size(), s.text);
    selection_ = s.before;
    redo_.push_back(std::move(s));
    ++revision_;
    return true;
}

bool TextEditor::redo() {
    if (redo_.empty()) return false;
    Step s = std::move(redo_.back());
    redo_.pop_back();
    std::size_t at = s.before.begin();
    text_.replace(at, s.text.size(), s.after_text);
    selection_ = s.after;
    undo_.push_back(std::move(s));
    ++revision_;
    return true;
}

}  // namespace omui::typeset
