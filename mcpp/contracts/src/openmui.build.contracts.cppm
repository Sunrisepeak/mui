// openmui.build.contracts — the build plugin for contracts: a package's
// interface written once becomes code inside its build (component-binding
// §2.3, §8, §11): components() for *.contract.omui.toml, natives() for
// *.native.omui.toml.
//
// For each contract in `contract/*.contract.omui.toml`, two actions run
// `omui-gen cpp`: one makes the partition `<module>:<name>_contract`
// (declared as the module interface it provides, with its imports, since a
// file that does not exist yet cannot be scanned), one the builder, an
// implementation unit of the package's module. Both join the package's
// compile set. One more runs `omui-gen stubs` for the TS / Python / Lua stubs
// and the view-document schema. Every edge re-runs when a contract, the
// registry or the generator changes: they are its inputs.
export module openmui.build.contracts;

import std;
import mcpp;
export import openmui.build.plan;

export namespace openmui::build {

struct component_options {
    std::string module;                                   // the package's module (acme.ui)
    std::string ns = "omui::ui";                          // the components' namespace
    std::string contracts = "contract";                   // the directory of *.contract.omui.toml
    std::string registry;                                 // spec/registry (default: beside this plugin in the openmui tree)
    std::string out_dir = std::string(mcpp::out_dir());   // generated files go under out_dir/openmui
    bool        stubs = true;                             // TS / Python / Lua stubs and the schema
};

// The contract names a file declares (its `[contract.<name>]` tables).
[[nodiscard]] std::vector<std::string> contract_names(const std::filesystem::path& file);

[[nodiscard]] std::vector<edge> plan(const component_options& opt);

// Plans and submits; false (and a message) when the build cannot go on.
bool components(component_options opt);

// ── native modules (component-binding §8) ───────────────────────────────────

struct native_options {
    std::string module;                                   // the package's module (acme.finder)
    std::string ns;                                       // its C++ namespace (default: the module's, acme::finder)
    std::string natives = "native";                       // the directory of *.native.omui.toml
    std::string registry;                                 // spec/registry (default: beside this plugin in the openmui tree)
    std::string out_dir = std::string(mcpp::out_dir());   // generated files go under out_dir/openmui
    bool        stubs = true;                             // TS / Python / Lua stubs
};

// The native modules a file declares (its `[native.<name>]` tables).
[[nodiscard]] std::vector<std::string> native_names(const std::filesystem::path& file);

[[nodiscard]] std::vector<edge> plan_natives(const native_options& opt);

// Plans and submits the native modules' edges; false (and a message) when
// the build cannot go on.
bool natives(native_options opt);

}  // namespace openmui::build
