// openmui.ui.base:list — a list of a model's rows, virtualised (spec S-7):
// only the rows in view (and a few beyond) are nodes; the rest are stated by
// the semantic tree all the same, offscreen, and an action on one brings it
// into view first. Rows keep their measured heights; Tab enters at one item
// and arrows walk the rest (a roving focus); selection is none, single or
// multiple, with `selected` two-way.
//
//     omui::list_view({.id = "notes", .model = "notes", .selectable = omui::ListSelectable::single,
//                      .height = 300},
//                     [](const omui::Row& r, std::size_t) { return omui::text({.text = omui::Model::label_of(r)}); });
export module openmui.ui.base:list;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :list_contract;

export namespace omui {

class List : public ListContract<List> {
public:
    static constexpr bool clips = true;

    using ItemFn = std::function<Element(const Row&, std::size_t)>;
    ItemFn item;  // how a row is shown (list_view sets it); else its label

    void mounted();
    void prop_changed(ListProp p);
    void style(const Theme& theme, layout::Style& s) const;
    [[nodiscard]] Point scroll_offset() const { return {0, offset_}; }
    [[nodiscard]] std::vector<Element> compose();
    bool on_event(EventContext& ctx, const InputEvent& e);
    void laid_out();
    void paint_over(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    bool reveal(std::string_view key);

    ActResult scroll_to(ActContext& ctx, const ScrollToArgs& args);
    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);

    void activate_row(const std::string& key);
    [[nodiscard]] std::pair<std::size_t, std::size_t> window() const noexcept { return {first_, last_}; }

private:
    [[nodiscard]] std::size_t rows() const noexcept { return model_ ? model_->size() : 0; }
    [[nodiscard]] float height_of(std::size_t i) const;
    [[nodiscard]] float top_of(std::size_t i) const;
    [[nodiscard]] float total() const { return top_of(rows()); }
    [[nodiscard]] float viewport() const;
    [[nodiscard]] std::size_t index_at(float y) const;
    bool update_window();  // the window follows the offset and the viewport
    void scroll(float to);
    // Scrolls the least that shows row i; true when it moved.
    bool show(std::size_t i);
    ActResult step(int by);
    void bind_model();

    Model*                                 model_ = nullptr;
    Watch                                  watch_;
    float                                  offset_ = 0;
    std::size_t                            first_ = 0, last_ = 0;  // the materialised rows [first, last)
    std::string                            current_;  // the item Tab enters at
    std::string                            revealing_;  // kept in view while measured heights settle
    std::unordered_map<std::string, float> heights_;
    mutable std::vector<float>             prefix_;
    mutable bool                           prefix_dirty_ = true;
};

// An item of a list: the row's view inside, selection and focus on it.
class ListItem {
public:
    static constexpr std::string_view name = "list_item";
    static constexpr Role role = Role::list_item;

    void mount(Services& s, NodeId n) { services_ = &s, node_ = n; }
    bool set_prop(std::string_view k, const Value& v);
    [[nodiscard]] Value get_prop(std::string_view k) const;
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

private:
    Services*   services_ = nullptr;
    NodeId      node_;
    std::string key_;
    bool        selected_ = false;
};

// A list whose items `item` makes from its model's rows.
[[nodiscard]] Element list_view(ListOptions options, List::ItemFn item, std::source_location where = std::source_location::current());

}  // namespace omui
