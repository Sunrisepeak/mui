// openmui.ui.base:reading — what is read in the page and points on from it:
// a key or chord to press (kbd), a line between groups (separator), text that
// goes somewhere (link), terms and what they mean (description list) and
// something happening for a while (spinner). They draw what they are now;
// how a change of them moves is the slot's (universal motion).
export module omui.ui.base:reading;

import std;
import omui.core;
import omui.view;
import :parts;
import :kbd_contract;
import :separator_contract;
import :link_contract;
import :description_list_contract;
import :spinner_contract;

export namespace omui {

class Kbd : public KbdContract<Kbd> {
public:
    void mounted();
    void prop_changed(KbdProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    float baseline(MeasureContext& m, Size content);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    // The chords to press in turn, each its keys as drawn: "Mod+K G" is {{⌘ or Ctrl, K}, {G}};
    // `command` says whether the system's command key is ⌘.
    [[nodiscard]] static std::vector<std::vector<std::string>> chords(std::string_view keys, bool command);
    // What the keys are called, as a reader says them ("Control+Shift+P").
    [[nodiscard]] static std::string spoken(std::string_view keys, bool command);

private:
    void rebuild();
    mutable std::vector<std::vector<parts::Label>> caps_;  // a label for each key, chord by chord
};

class Separator : public SeparatorContract<Separator> {
public:
    void mounted();
    void prop_changed(SeparatorProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

private:
    mutable parts::Label label_;
};

class Link : public LinkContract<Link> {
public:
    void mounted();
    void prop_changed(LinkProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    float baseline(MeasureContext& m, Size content);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    ActResult activate(ActContext& ctx);

    // Whether an address is one the system is asked to open (http, https, mailto).
    [[nodiscard]] static bool opens(std::string_view href);

private:
    void classify();
    mutable parts::Label label_;
};

class DescriptionList : public DescriptionListContract<DescriptionList> {
public:
    void mounted();
    void prop_changed(DescriptionListProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    // Each pair an item of its own: its name the term, its value the description.
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;

private:
    void rebuild();
    [[nodiscard]] spec::SemanticNode item(std::string_view path, std::size_t index) const;
    mutable std::vector<parts::Label> terms_, details_;
    mutable std::vector<Rect>         rows_;  // each pair's place, as last laid out
    mutable float                     term_width_ = 0;
};

class Spinner : public SpinnerContract<Spinner> {
public:
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
};

}  // namespace omui
