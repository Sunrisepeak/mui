// openmui.ui.base:entry — what is typed and chosen at once: a combobox (one
// value typed or chosen from the options matching what is typed) and a tag
// input (tags gathered by typing or choosing a suggestion, each a chip
// removed by its ×). Both type into a text box of their own and offer their
// matches in a popup; what is chosen is their own action, performed as the
// person, journaled and taken back as an agent's is.
export module omui.ui.base:entry;

import std;
import omui.core;
import omui.view;
import :parts;
import :combobox_contract;
import :tag_input_contract;

export namespace omui {

class Combobox : public ComboboxContract<Combobox> {
public:
    void mounted();
    void prop_changed(ComboboxProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<Element> compose();
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult expand(ActContext& ctx);
    ActResult collapse(ActContext& ctx);
    ActResult set_value(ActContext& ctx, const Value& value);

    [[nodiscard]] std::string label_of(std::string_view value) const;
    [[nodiscard]] bool expanded() const;
    // The options whose labels hold `typed` (all, when nothing is typed).
    [[nodiscard]] std::vector<std::size_t> matches(std::string_view typed) const;

private:
    void typed(std::string text);
    void commit();
    void show(bool open);
    ActResult choose(std::string value, bool restoring = false);
    [[nodiscard]] NodeId popup() const;

    Signal<std::string>      query_ = signal(std::string());
    std::string              filter_;  // what is typed since the value was chosen
    std::vector<std::size_t> shown_;   // the options the popup offers
    int                      highlight_ = 0;
};

class TagInput : public TagInputContract<TagInput> {
public:
    void mounted();
    void prop_changed(TagInputProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<Element> compose();

    ActResult add(ActContext& ctx, const Value& tag);
    ActResult remove(ActContext& ctx, const Value& tag);
    ActResult set_tags(ActContext& ctx, const Value& tags);

    // The suggestions holding `typed`, not tags already.
    [[nodiscard]] std::vector<std::string> offered(std::string_view typed) const;

private:
    void typed(std::string text);
    void commit();
    void show(bool open);
    ActResult to(std::vector<std::string> tags);
    [[nodiscard]] NodeId popup() const;

    Signal<std::string>      query_ = signal(std::string());
    std::vector<std::string> shown_;
    int                      highlight_ = 0;
};

// What a combobox or a tag input offers as one types: its matches, the one
// the arrows are on lit; a press performs the holder's own action with it.
class ChoicePopup {
public:
    static constexpr std::string_view name = "choice_popup";
    static constexpr Role role = Role::list_box;
    static constexpr bool overlay = true;
    [[nodiscard]] Placement placement() const { return {Placement::Side::below, Placement::Anchor::parent, 4, false, false, true, true}; }

    void mount(Services& s, NodeId n) { services_ = &s, node_ = n; }
    bool set_prop(std::string_view k, const Value& v);
    [[nodiscard]] Value get_prop(std::string_view k) const;
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    ActResult act(ActContext& ctx, std::string_view action);

private:
    Services*                         services_ = nullptr;
    NodeId                            node_;
    bool                              open_ = false;
    Value                             choices_;  // [{value, label}]
    int                               highlight_ = 0;
    std::string                       action_, arg_, chosen_;  // chosen: the holder's value, marked
    mutable std::vector<parts::Label> labels_;
};

}  // namespace omui
