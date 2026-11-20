// omui.semantic:tree — the semantic tree of a running interface.
//
// The tree is read from the runtime when its semantic revision moves, and
// kept: a snapshot is a copy. A refresh reads the runtime's nodes again and a
// virtualising node's items only when their revision moved (a long list's ten
// thousand rows are carried as they were while it scrolls); what differs from
// the last refresh is marked, and `changes` answers what was marked since its
// last call — the work is the difference, not the tree. Compact trees lift the children of nodes
// that mean nothing (role none, or a nameless generic container without
// actions) into their parent, so an agent reads `main/save`, not five
// wrappers deep; ids stay the full paths.
export module omui.semantic:tree;

import std;
import omui.core;
import omui.view;
import :query;

export namespace omui::semantic {

struct TreeOptions {
    bool compact = true;
};

struct SnapshotOptions {
    std::string root;               // a subtree (empty: the whole tree)
    int         depth = -1;         // levels below the root; -1: all
    bool        interactive = false;  // only nodes an agent can operate (and their ancestors' ids)
    std::size_t offset = 0;         // paging, in pre-order
    std::size_t limit = 0;          // 0: all
    std::size_t children_limit = 0;  // the ids a node's `children` lists at most (its first); 0: all
};

struct Snapshot {
    std::uint64_t                   revision = 0;
    std::vector<spec::SemanticNode> nodes;   // pre-order
    std::size_t                     total = 0;  // before paging
    // A node whose children were cut to children_limit: how many it has
    // (`children_total`; the rest are read with root = that node, paged).
    std::unordered_map<std::string, std::size_t> children_total;
    [[nodiscard]] Value to_value() const;
};

// One action of a transaction, by id.
struct Step {
    std::string id;
    std::string action;
    Value       args;
    std::string answer;  // empty: the transaction's
};

struct WaitState {
    bool        satisfied = false;
    std::string why;  // what is still missing
};

// What changed between two snapshots, node by node by id: `added` ({id, now}),
// `removed` ({id, was}) and `changed` ({id, fields: {role|name|description|
// value|state: {before, after}}}) — what a person or an agent reads, not where
// it is drawn (a preview's answer, a live edit's).
[[nodiscard]] Value changes_between(const Snapshot& before, const Snapshot& after);

class Tree {
public:
    explicit Tree(Runtime& runtime, TreeOptions options = {});

    [[nodiscard]] Snapshot snapshot(const SnapshotOptions& options = {});
    // What changed since the previous call (the first call adds everything).
    [[nodiscard]] spec::SemanticDelta changes();
    [[nodiscard]] std::vector<spec::SemanticNode> find(const Query& query);
    [[nodiscard]] std::optional<spec::SemanticNode> node(std::string_view id);
    // Performs an action by id: the runtime's act, with S-1/S-2/S-4 (and an
    // error envelope for an id that names nothing).
    Envelope act(std::string_view id, std::string_view action, const Invocation& call = {});
    // A transaction (S-13): the steps performed in order, as `call` would
    // perform each, and kept as one step of history. The first that fails
    // ends it: those done are taken back, last first, and its error is the
    // answer (with its exit code: 2 when it needs an answer). An irreversible
    // or external step is refused before it runs. A dry run checks each step
    // against the tree as it is now and answers their outcomes. `expect`:
    // what must hold once the steps are done, read at this very time after a
    // frame (the state, not motion: no `settled`); the first that does not
    // hold takes every step back, and the answer names it and why — the
    // steps and their check in one call, nothing left half done (C7).
    Envelope transaction(std::span<const Step> steps, const Invocation& call = {}, std::string label = {},
                         std::span<const Condition> expect = {});
    // Steps in order, each as `act` performs it (its own effect, answer and history), not as one: for what a
    // transaction cannot hold (a step with no way back). The first that does not succeed ends them and those
    // before it stay done — the answer says how many (`done`) and carries the step's own options (the answer it
    // asks for); `expect` is read as a transaction's, and one that does not hold is said, nothing taken back. A
    // dry run answers each step's outcome against the tree as it is now.
    Envelope sequence(std::span<const Step> steps, const Invocation& call = {}, std::span<const Condition> expect = {});
    // What the steps would make, shown before anything is allowed (Part 2
    // §4.1, A): the ones that can be taken back run inside a transaction in
    // a preview (no journal, no motion), `look` sees the state they make (a
    // picture), and everything is taken back. A step that asks the person
    // (irreversible, external) is not done: it is listed, with its effect —
    // the picture is without it. A step with no known way back cannot be
    // shown without being done: the preview refuses, nothing done. The
    // answer: the steps run, those not done, the semantic changes (added,
    // removed, changed: name, value, state, role, description), what `look`
    // gave, and whether the tree is, after, as it was before (`restored`).
    Envelope preview(std::span<const Step> steps, const Invocation& call = {}, const std::function<Value()>& look = {});
    // What a person has set in the interface, by semantic id (W16, D-154):
    // the values of what takes one (set_value), what is checked (toggle),
    // and where the focus is — to be put back in another instance of the
    // application (a new build, a restart) by `restore`. A masked value (a
    // password) is not kept.
    [[nodiscard]] Value state();
    // `state`'s values put back where the same id, with the same role, takes
    // them: the answer says what was restored, what is not there any more
    // and what could not be. The history is left as it was before: putting
    // back is not a step anyone takes back.
    Value restore(const Value& state, const Invocation& call = {});
    [[nodiscard]] WaitState check(const Condition& condition);
    // Every node against the spec's clauses.
    [[nodiscard]] std::vector<spec::Violation> violations();
    [[nodiscard]] std::uint64_t revision();
    // A hash of the whole tree: two runs that end in the same state agree.
    [[nodiscard]] std::uint64_t digest();

    [[nodiscard]] Runtime& runtime() noexcept { return runtime_; }

private:
    // A node of the last refresh: where it is in `nodes_`, and what `changes` knows of it.
    struct Entry {
        std::size_t   at = 0;
        std::uint64_t pass = 0;        // the refresh that placed it
        bool          item = false;    // an item no node stands for, as its holder stated it
        bool          told = false;    // `changes` has answered it
        bool          touched = false; // changed since
    };
    // A virtualising node's items as last read, kept while their revision says they are the same.
    struct Items {
        std::uint64_t                   revision = 0;
        std::uint64_t                   pass = 0;
        std::vector<spec::SemanticNode> items;
        std::vector<std::string>        skipped, others;  // what the holder's children were last made of, besides them
    };

    void refresh();
    // `expect` read after a still frame (the state the steps made): the first that does not hold, its index and why.
    std::optional<std::pair<std::size_t, std::string>> unmet(std::span<const Condition> expect);
    // Marks a node changed since the last `changes`.
    void touch(const std::string& id, Entry& entry);

    Runtime&                                     runtime_;
    TreeOptions                                  options_;
    std::uint64_t                                read_ = 0;   // the runtime revision the cache is of
    bool                                         fresh_ = false;
    std::uint64_t                                pass_ = 0;   // refreshes made
    std::vector<spec::SemanticNode>              nodes_;
    std::vector<spec::SemanticNode>              spare_;      // the last but one's, emptied: room for the next
    std::unordered_map<std::string, Entry>       index_;
    std::unordered_map<std::string, Items>       items_;      // by holder
    std::vector<std::string>                     touched_;    // changed since the last `changes` (their entries say so)
    std::vector<std::string>                     gone_;       // answered by `changes`, removed since
};

}  // namespace omui::semantic
