// omui.gen.ops — a product's operation catalog (`<product>.ops.omui.toml`,
// convergence plan §10.3) to its generated code and listings:
//
//   - a C++ partition `<module>:catalog` holding `catalog`, an array of
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
    std::vector<OpParameter> parameters;
    int line = 0;
};
struct Catalog {
    std::string product, module, ns, doc;
    std::vector<Operation> operations;
};

// The catalog in `text` (named `file` in messages), or every problem in it.
[[nodiscard]] std::expected<Catalog, std::vector<std::string>> read_catalog(std::string_view text,
                                                                            std::string_view file);
[[nodiscard]] std::string emit_catalog_cpp(const Catalog& catalog, std::string_view source);
[[nodiscard]] std::string emit_catalog_json(const Catalog& catalog);
[[nodiscard]] std::string emit_catalog_markdown(const Catalog& catalog);

}  // namespace omui::gen
