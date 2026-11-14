// omui.gen.ops — a product's operation catalog (`<product>.ops.omui.ts`,
// convergence plan §10.3; read as its JSON) to its generated code and listings:
//
//   - (when the catalog names a module) a C++ partition `<module>:catalog` holding `catalog`, an
//   array of
//     mlibs::control::CatalogEntry (names, openas effects, parameters, result,
//     cost, undo, cancellation), which the product's control adapter uses;
//   - `<product>.ops.json`: the operations as tools (name, description, a JSON
//     schema of the parameters), what CLI and MCP bindings list;
//   - `<product>.ops.md`: the reference a person reads.
//
// Every problem is reported with its line, before anything is written.
export module omui.gen.ops;

import std;

export namespace omui::gen {

struct OpParameter {
    std::string name, type, doc;
    bool optional = false;
    // Of a position: the form it is written in, which belongs to the subject's interface and not to every
    // world (the foundation plan's §6.13). `plane`: a point of a flat view, each coordinate 0 to 1;
    // `lattice-2d`: a cell [x, y] or a region [x, y, width, height] of a lattice of two dimensions. A world of
    // three dimensions adds forms (a ray, a volume) beside these; an operation's existing parameter keeps its own.
    std::string space;
};
struct Operation {
    std::string name, effect, summary, result, cost = "instant", undo;
    bool person = false, cancellable = false;
    bool undoable = false;  // the host's history takes it back (undo, redo)
    // Its effect is that of what it runs (a node's action, an application's verb),
    // decided where that runs; `effect` is the most it can be.
    bool delegated = false;
    std::vector<OpParameter> parameters;
    std::vector<std::string> examples;  // requests of it, each a JSON object on one line
    int line = 0;
};
// A task to begin with (D-186): the requests that do it, in order, each a JSON object on one line.
struct Task {
    std::string name, summary;
    std::vector<std::string> steps;
    int line = 0;
};
// An agent player's tier (plan §8.4): what it is shown, how soon its actions
// take effect after what it saw, how often it may act, which operations it uses.
struct Tier {
    std::string name, view = "drawn", doc;
    int reaction_ms = 0, actions_per_second = 0;
    std::vector<std::string> operations;
    int line = 0;
};
struct Catalog {
    std::string product, module, ns, doc;
    std::string partition = "catalog";  // the C++ partition (and its file's name)
    std::string generated = "src/control";  // where its outputs go, beside the catalog
    std::vector<Operation> operations;
    std::vector<Tier> tiers;
    std::vector<Task> tasks;
};

// The catalog in `text` — its JSON snapshot, `{"ops": {…}}` — (named `file`
// in messages), or every problem in it.
[[nodiscard]] std::expected<Catalog, std::vector<std::string>> read_catalog(std::string_view text,
                                                                            std::string_view file);
// The catalog file: <product>.ops.omui.ts (defineOps, D-139), or its JSON.
[[nodiscard]] std::expected<Catalog, std::vector<std::string>> read_catalog_file(const std::filesystem::path& file);
[[nodiscard]] std::string emit_catalog_cpp(const Catalog& catalog, std::string_view source);
[[nodiscard]] std::string emit_catalog_json(const Catalog& catalog);
[[nodiscard]] std::string emit_catalog_markdown(const Catalog& catalog);

}  // namespace omui::gen
