// openmui.ui.base:navigating — finding one's way and the choices at hand: the
// choices for an element where the person asked for them (context menu),
// more about an element while it is pointed at (hover card), the
// application's commands found by name (command palette), its places down
// its side (sidebar) and a row of controls the arrows move between (toolbar).
export module omui.ui.base:navigating;

import std;
import omui.core;
import omui.view;
import :parts;
import :context_menu_contract;
import :hover_card_contract;
import :command_palette_contract;
import :sidebar_contract;
import :toolbar_contract;

export namespace omui {

class ContextMenu : public ContextMenuContract<ContextMenu> {
public:
    static constexpr bool overlay = true;
    [[nodiscard]] Placement placement() const;

    ContextMenu() = default;
    ContextMenu(const ContextMenu&) = delete;
    ContextMenu& operator=(const ContextMenu&) = delete;
    ~ContextMenu();

    void mounted();
    void prop_changed(ContextMenuProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult show(ActContext& ctx, const ShowArgs& args);
    ActResult dismiss(ActContext& ctx);

private:
    void open_at(Point at);
    void close();
    NodeId                     target_;
    std::vector<std::uint64_t> listening_;
    std::uint64_t              timer_ = 0;
    Point                      at_;
    bool                       was_open_ = false;
};

class HoverCard : public HoverCardContract<HoverCard> {
public:
    static constexpr bool overlay = true;
    // Below the element before it, beside it where there is no room.
    [[nodiscard]] Placement placement() const { return {Placement::Side::below, Placement::Anchor::previous, 6, false, false, true, false}; }

    HoverCard() = default;
    HoverCard(const HoverCard&) = delete;
    HoverCard& operator=(const HoverCard&) = delete;
    ~HoverCard();

    void mounted();
    void prop_changed(HoverCardProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult show(ActContext& ctx);
    ActResult dismiss(ActContext& ctx);

private:
    void pointed(bool at);
    void set_open(bool open);
    NodeId                     target_;
    std::vector<std::uint64_t> listening_;
    std::uint64_t              timer_ = 0;
    bool                       over_card_ = false, over_target_ = false;
};

class CommandPalette : public CommandPaletteContract<CommandPalette> {
public:
    static constexpr bool overlay = true;
    [[nodiscard]] Placement placement() const { return {Placement::Side::center, Placement::Anchor::parent, 0, true, true, true, false}; }

    void mounted();
    void prop_changed(CommandPaletteProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    [[nodiscard]] std::vector<Element> compose();
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult show(ActContext& ctx);
    ActResult dismiss(ActContext& ctx);
    ActResult set_query(ActContext& ctx, const Value& query);
    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);
    ActResult run(ActContext& ctx);

    // The verbs offered that match `query` (every word of it found in the verb's name or summary), best first.
    [[nodiscard]] std::vector<const VerbDesc*> matching(std::string_view query) const;

private:
    struct Shown {
        std::string verb, title;
    };
    void refresh();
    ActResult run_verb(const std::string& verb);
    void set_open(bool open);
    std::vector<Shown>                shown_;   // what matches now, as listed
    mutable std::vector<parts::Label> titles_;
    std::size_t                       chosen_ = 0;
    std::string                       outcome_;  // why the last command did not run
    bool                              was_open_ = false;
};

class Sidebar : public SidebarContract<Sidebar> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(SidebarProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult set_selected(ActContext& ctx, const Value& selected);
    ActResult expand(ActContext& ctx);
    ActResult collapse(ActContext& ctx);
    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);

private:
    ActResult choose(std::string value);
    ActResult fold(bool collapsed);
    [[nodiscard]] std::string label_of(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> index_of(std::string_view value) const;
    [[nodiscard]] spec::SemanticNode item(std::string_view path, std::size_t index) const;
    mutable std::vector<parts::Label> labels_, initials_;
    mutable std::vector<Rect>         at_;
    int                               hover_ = -1;
};

class Toolbar : public ToolbarContract<Toolbar> {
public:
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);
    ActResult first(ActContext& ctx);
    ActResult last(ActContext& ctx);

private:
    // Its controls that take focus, in order, and which of them has it.
    [[nodiscard]] std::vector<NodeId> controls() const;
    ActResult move(int by, bool to_end = false);
};

}  // namespace omui
