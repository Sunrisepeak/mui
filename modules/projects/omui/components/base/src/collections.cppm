// openmui.ui.base:collections — a model's rows as a hierarchy (a tree view)
// and as a table (a data grid). Both draw only the rows in view, however
// many there are, and state every row to the semantic tree as an item of its
// own (`<id>/<key>`) that a screen reader reads and an agent operates; a
// change of the model draws and states them again.
export module openmui.ui.base:collections;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :tree_view_contract;
import :data_grid_contract;

export namespace omui {

class TreeView : public TreeViewContract<TreeView> {
public:
    static constexpr bool focusable = true;
    static constexpr bool clips = true;

    void mounted();
    void prop_changed(TreeViewProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult set_value(ActContext& ctx, const Value& value);
    ActResult set_expanded(ActContext& ctx, const SetExpandedArgs& args);
    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);
    ActResult open(ActContext& ctx);
    ActResult close(ActContext& ctx);
    ActResult activate(ActContext& ctx);

    // A row as it is shown: its key, its model row, how deep, whether it has children.
    struct Line {
        std::string key;
        std::size_t row = 0;
        int         depth = 0;
        bool        parent = false;
    };
    [[nodiscard]] const std::vector<Line>& lines() const;  // the rows shown, in order (the open ones' children under them)

private:
    void bind_model();
    void dirty();
    [[nodiscard]] std::string label_of(std::size_t row) const;
    [[nodiscard]] std::string parent_of(std::string_view key) const;
    [[nodiscard]] bool has_children(std::string_view key) const;
    [[nodiscard]] bool is_open(std::string_view key) const;
    ActResult choose(std::string key);
    ActResult set_open(std::string key, bool open);
    void show(std::size_t line);  // scrolled the least that shows it
    [[nodiscard]] std::optional<std::size_t> line_of(std::string_view key) const;
    [[nodiscard]] spec::SemanticNode item(std::string_view path, std::string_view key) const;

    Model*                                          model_ = nullptr;
    Watch                                           watch_;
    mutable std::vector<Line>                       lines_;
    mutable std::unordered_map<std::string, std::vector<std::size_t>> kids_;  // a key's children's rows ("" the roots')
    mutable bool                                    dirty_ = true;
    mutable std::vector<parts::Label>               labels_;
    mutable Size                                    size_{};
    float                                           offset_ = 0;
    int                                             hover_ = -1;
};

class DataGrid : public DataGridContract<DataGrid> {
public:
    static constexpr bool focusable = true;
    static constexpr bool clips = true;

    void mounted();
    void prop_changed(DataGridProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult set_sort(ActContext& ctx, const Value& sort);
    ActResult set_selected(ActContext& ctx, const Value& keys);
    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);
    ActResult first(ActContext& ctx);
    ActResult last(ActContext& ctx);
    ActResult activate(ActContext& ctx);
    ActResult toggle(ActContext& ctx);

    // The model's rows in the order shown (sorted as `sort` says).
    [[nodiscard]] const std::vector<std::size_t>& order() const;
    // A cell as it is shown: text, numbers as numbers, yes and no.
    [[nodiscard]] static std::string text_of(const Value& v);
    // The sort after this column's header is pressed: ascending, descending, none.
    [[nodiscard]] std::string sort_after(std::string_view field) const;

private:
    void bind_model();
    void dirty();
    [[nodiscard]] std::vector<float> column_widths(float width) const;
    [[nodiscard]] std::string header_of(std::size_t c) const;
    [[nodiscard]] bool chosen(std::string_view key) const;
    ActResult select(std::vector<std::string> keys);
    ActResult sort_to(std::string sort);
    ActResult go(std::size_t i);  // the row gone to (selection follows it, one at a time)
    void show(std::size_t i);
    [[nodiscard]] spec::SemanticNode row_item(std::string_view path, std::size_t at) const;
    [[nodiscard]] spec::SemanticNode header_item(std::string_view path, std::size_t c) const;
    [[nodiscard]] std::optional<std::size_t> position_of(std::string_view key) const;

    Model*                             model_ = nullptr;
    Watch                              watch_;
    mutable std::vector<std::size_t>   order_;
    mutable bool                       dirty_ = true;
    mutable std::vector<parts::Label>  cells_;    // reused from frame to frame
    mutable std::vector<parts::Label>  headers_;
    mutable Size                       size_{};
    float                              offset_ = 0;
    std::size_t                        focus_ = 0;  // the row gone to (its place in the order)
    int                                hover_ = -1;
};

}  // namespace omui
