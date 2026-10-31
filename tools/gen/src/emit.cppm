// openmui.gen.emit — every artifact the registry and contracts produce.
//
// Each emitter is a pure function from the model to text; the same model gives
// the same bytes, which is what lets CI check that committed outputs are
// current (`omui-gen … --check`).
export module openmui.gen.emit;

import std;
import openmui.gen.model;

export namespace omui::gen {

// spec/include/openmui.h — the C ABI.
[[nodiscard]] std::string emit_c_header(const Registry& reg);
// openmui.h's types, functions and their layout check, for modules/ui/abi.
[[nodiscard]] std::string emit_abi_types(const Registry& reg);
[[nodiscard]] std::string emit_abi_functions(const Registry& reg);
[[nodiscard]] std::string emit_abi_layout(const Registry& reg);

// spec/src/generated/registry.cppm — partition openmui.spec:registry: the
// enumerations and tables of roles, states, effects, origins, actions,
// extensions, profiles, motion tokens and the expression language.
[[nodiscard]] std::string emit_spec_registry(const Registry& reg);

// spec/src/generated/catalog.cppm — partition openmui.spec:catalog: the Core
// component contracts as constexpr ContractInfo tables.
[[nodiscard]] std::string emit_spec_catalog(const Registry& reg);
[[nodiscard]] std::string emit_contract_tables(const Contract& c, std::string_view id);

// C++ for a component package (component-binding §2.3, §2.4): per contract a
// module partition `<module>:<name>_contract` (typed props, choice enums, the
// builder's options, the CRTP base that dispatches props and actions and
// emits events) and the builder's definition.
struct CppOptions {
    std::string module;                 // the component package's module, e.g. openmui.ui.base
    std::string ns = "omui::ui";        // the namespace of the components
    bool        catalog = false;        // the contracts are the spec's Core ones (spec::catalog)
};
[[nodiscard]] std::string emit_cpp_contract(const Contract& c, const Registry& reg, const CppOptions& o);
[[nodiscard]] std::string emit_cpp_builder(const Contract& c, const Registry& reg, const CppOptions& o);

// spec/docs/registry.md — the registry as reference tables.
[[nodiscard]] std::string emit_docs(const Registry& reg);

// Typed builder declarations for script languages, one file per contract set.
// `module` names the package the builders come from (for the header comment).
[[nodiscard]] std::string emit_dts(const std::vector<Contract>& contracts, std::string_view module);
[[nodiscard]] std::string emit_pyi(const std::vector<Contract>& contracts, std::string_view module);
[[nodiscard]] std::string emit_lua(const std::vector<Contract>& contracts, std::string_view module);

// A native module's C++ (component-binding §8): the partition
// `<module>:<name>_native` with its records, the declarations of the
// functions the package implements, their conversions, and define(), which
// registers them for scripts (openmui.native).
[[nodiscard]] std::string emit_cpp_native(const NativeModule& m, const CppOptions& o, std::string_view source);
// Script stubs of native modules: the TypeScript module declarations
// (`openmui:native/<name>`), a Python stub per module (openmui/native/<name>.pyi)
// and the LuaLS annotations of omui.native.<name>.
[[nodiscard]] std::string emit_native_dts(const std::vector<NativeModule>& modules, std::string_view module);
[[nodiscard]] std::string emit_native_pyi(const NativeModule& m, std::string_view module);
[[nodiscard]] std::string emit_native_lua(const std::vector<NativeModule>& modules, std::string_view module);

// A data file as a module partition `<module>:<name>`, its text an exported
// `inline constexpr std::string_view <name>` in namespace `ns`: how a package
// carries a theme, a document or a table in its code, generated, never
// copied by hand.
[[nodiscard]] std::string emit_embed(std::string_view text, std::string_view source, std::string_view module, std::string_view ns,
                                     std::string_view name);
// The files embedded under one tag as one table, partition `<module>:<name>`:
// an exported array `<name>` of (the file's name, its text), so a program
// finds its embedded files by name — no list kept by hand beside them.
// `files`: each file's name and its partition's (and constant's) name.
[[nodiscard]] std::string emit_embed_index(std::span<const std::pair<std::string, std::string>> files, std::string_view module, std::string_view ns,
                                           std::string_view name);

// The JSON Schema fragment of view documents for a contract set: one
// definition per component type with its properties and events.
[[nodiscard]] std::string emit_view_schema(const std::vector<Contract>& contracts, std::string_view module);

}  // namespace omui::gen
