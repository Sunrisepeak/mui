// openmui.ui.base:navigation — where one is among places: a breadcrumb (the
// way down to here, each place a link) and a pagination (which page of many).
// Each crumb and each page is a node of the semantic tree an agent finds and
// activates (`<id>/<index>`, `<id>/<page>`, `<id>/previous`, `<id>/next`) —
// items of the component, not children it builds (S-7).
export module omui.ui.base:navigation;

import std;
import omui.core;
import omui.view;
import :parts;
import :breadcrumb_contract;
import :pagination_contract;

export namespace omui {

class Breadcrumb : public BreadcrumbContract<Breadcrumb> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(BreadcrumbProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult go(ActContext& ctx, const Value& index);
    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);

private:
    ActResult go_to(std::int64_t index);
    void relabel();
    [[nodiscard]] spec::SemanticNode item(std::string_view path, std::size_t i) const;

    mutable std::vector<parts::Label> crumbs_;
    mutable parts::Label              separator_;
    mutable std::vector<Rect>         at_;  // each crumb where it was last painted
    std::size_t                       focus_ = 0;
    int                               hover_ = -1;
};

class Pagination : public PaginationContract<Pagination> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(PaginationProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult increment(ActContext& ctx);
    ActResult decrement(ActContext& ctx);
    ActResult set_value(ActContext& ctx, const Value& value);

    // What it shows, in order: 0 the previous, -1 the next, -2 a gap (…), else a page.
    [[nodiscard]] std::vector<std::int64_t> slots() const;

private:
    ActResult go_to(std::int64_t page);
    [[nodiscard]] std::int64_t pages() const noexcept { return std::max<std::int64_t>(1, props.pages); }

    mutable std::vector<std::pair<std::int64_t, Rect>> at_;  // each slot where it was last painted
    mutable std::vector<parts::Label>                  digits_;  // each slot's, reused
    std::int64_t                                       hover_ = -3;
};

}  // namespace omui
