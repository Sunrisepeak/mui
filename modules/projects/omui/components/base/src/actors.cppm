// openmui.ui.base:actors — who else acts here, and what each did (agent
// connect §3.2): the person's agent link and the journal's actions of other
// actors, newest last. Each record is a node of the semantic tree (an item,
// `<id>/<entry>`); the last its history holds is taken back from here; the
// one hovered or focused points out the node it acted on.
export module omui.ui.base:actors;

import std;
import omui.core;
import omui.view;
import :parts;
import :actor_log_contract;
import :agent_presence_contract;

export namespace omui {

class ActorLog : public ActorLogContract<ActorLog> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(ActorLogProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
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

// Another actor at work here, as a model (the site plan §9.5): who, what it
// acts on now (its node's box in window coordinates), the steps before it
// (oldest first) and whether it waits for the person's answer. Every skin
// draws this — the framework's reticle (AgentPresence), the site's Mui — and
// tests and agents read the same.
struct PresenceStep {
    std::string path, action;
    Rect        bounds;  // window coordinates; empty: the node is not there now
    double      at = 0;
};
struct AgentPresenceState {
    std::string                 actor;  // empty: no one of these actors has acted
    std::optional<PresenceStep> now;
    std::vector<PresenceStep>   trail;
    bool                        asking = false;  // its last request waits for the person's answer
    bool                        taken_over = false;  // the person has taken over from every agent (S-14)
    std::optional<AgentNote>    note;  // what it says it is about to do, or hands back with (openas §4.4, §5.3)
};
// From the journal: the actors whose names begin with `actors`, the newest
// action theirs and up to `trail` before it on other nodes; what was done
// inside `except` (the presence's own subtree, a log's records) left out.
[[nodiscard]] AgentPresenceState agent_presence_state(const Services& services, std::string_view actors, std::size_t trail = 3,
                                                      std::string_view except = {});

class AgentPresence : public AgentPresenceContract<AgentPresence> {
public:
    void mounted();
    void prop_changed(AgentPresenceProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    [[nodiscard]] AgentPresenceState state() const;
    // What it says now: "guide activate …/save", "guide asks you", or nothing.
    [[nodiscard]] std::string said() const;

private:
    [[nodiscard]] std::string phrase(std::string_view key) const;
    mutable parts::Label tag_, card_;
};

}  // namespace omui
