// openmui.semantic:tree — the semantic tree of a running interface.
//
// The tree is read from the runtime when its semantic revision moves, and
// kept: a snapshot is a copy, `changes` compares with what the last call saw
// and answers only the difference. Compact trees lift the children of nodes
// that mean nothing (role none, or a nameless generic container without
// actions) into their parent, so an agent reads `main/save`, not five
// wrappers deep; ids stay the full paths.
export module openmui.semantic:tree;

import std;
import openmui.core;
import openmui.view;
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
};

struct Snapshot {
    std::uint64_t                   revision = 0;
    std::vector<spec::SemanticNode> nodes;   // pre-order
    std::size_t                     total = 0;  // before paging
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
    // against the tree as it is now and answers their outcomes.
    Envelope transaction(std::span<const Step> steps, const Invocation& call = {}, std::string label = {});
    [[nodiscard]] WaitState check(const Condition& condition);
    // Every node against the spec's clauses.
    [[nodiscard]] std::vector<spec::Violation> violations();
    [[nodiscard]] std::uint64_t revision();
    // A hash of the whole tree: two runs that end in the same state agree.
    [[nodiscard]] std::uint64_t digest();

    [[nodiscard]] Runtime& runtime() noexcept { return runtime_; }

private:
    void refresh();

    Runtime&                                     runtime_;
    TreeOptions                                  options_;
    std::uint64_t                                read_ = 0;   // the runtime revision the cache is of
    bool                                         fresh_ = false;
    std::vector<spec::SemanticNode>              nodes_;
    std::unordered_map<std::string, std::size_t> index_;
    std::unordered_map<std::string, spec::SemanticNode> seen_;  // what `changes` last answered
};

}  // namespace omui::semantic
