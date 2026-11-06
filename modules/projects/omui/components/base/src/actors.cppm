// openmui.ui.base:actors — who else acts here, and what each did (agent
// connect §3.2): the person's agent link and the journal's actions of other
// actors, newest last. Each record is a node of the semantic tree (an item,
// `<id>/<entry>`); the last its history holds is taken back from here; the
// one hovered or focused points out the node it acted on.
export module openmui.ui.base:actors;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :actor_log_contract;

export namespace omui {

class ActorLog : public ActorLogContract<ActorLog> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(ActorLogProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw, layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);

    // The records it shows, oldest first (the newest `limit` of its actors').
    [[nodiscard]] std::vector<const ActionEntry*> records() const;
    // Its words: the phrase's own, or the language's the application gave.
    [[nodiscard]] std::string phrase(std::string_view key) const;
    // What its first line says now; empty: no line (no link to speak of).
    [[nodiscard]] std::string status() const;
    // A record in words, as its row and its node say it.
    [[nodiscard]] std::string said(const ActionEntry& e) const;

private:
    ActResult undo(std::uint64_t entry, const Actor* actor);
    void point(int row);
    [[nodiscard]] spec::SemanticNode item(std::string_view path, const ActionEntry& e) const;

    mutable parts::Label                  status_, empty_;
    mutable std::vector<parts::Label>     times_, texts_, undos_;
    mutable std::vector<Rect>             rows_, undo_at_;  // where each record, and its Undo, was last painted
    mutable std::vector<std::uint64_t>    shown_;           // their entries, in that order
    int                                   hover_ = -1;
    std::size_t                           focus_ = 0;
    bool                                  pointing_ = false;
};

}  // namespace omui
