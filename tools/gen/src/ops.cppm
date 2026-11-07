// omui.gen.ops — a product's operation catalog (`<product>.ops.omui.toml`,
// convergence plan §10.3) to its generated code and listings:
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
};
struct Operation {
    std::string name, effect, summary, result, cost = "instant", undo;
    bool person = false, cancellable = false;
    // Its effect is that of what it runs (a node's action, an application's verb),
    // decided where that runs; `effect` is the most it can be.
    bool delegated = false;
    std::vector<OpParameter> parameters;
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
    std::string generated = "src/control";  // where its outputs go, beside the catalog
    std::vector<Operation> operations;
    std::vector<Tier> tiers;
};

// The catalog in `text` (named `file` in messages), or every problem in it.
[[nodiscard]] std::expected<Catalog, std::vector<std::string>> read_catalog(std::string_view text,
                                                                            std::string_view file);
[[nodiscard]] std::string emit_catalog_cpp(const Catalog& catalog, std::string_view source);
[[nodiscard]] std::string emit_catalog_json(const Catalog& catalog);
[[nodiscard]] std::string emit_catalog_markdown(const Catalog& catalog);

}  // namespace omui::gen
