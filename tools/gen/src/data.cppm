// omui.gen.data — a TypeScript data module (<name>.<kind>.omui.ts, D-139) as
// the document its kind's TOML file is, so one reader reads both while the
// TOML files move to TypeScript (W1). The module is evaluated by
// omui.lang.ts (deterministic, bounded); its JSON becomes a TOML table:
//
//   defineContract({ button: {…} })        → [contract.button] …
//   defineNative({ contrast: {…} })        → [native.contrast] …
//   defineOps({ product, doc, …, operations: […], tiers: […] })
//                                          → [catalog] product, doc, … and [[operation]], [[tier]]
//
// A value read from it has no line in the TypeScript: problems name the file.
export module omui.gen.data;

import std;
import tomlplusplus;

export namespace omui::gen {

// Whether `file` is a data module (<name>.<kind>.omui.ts).
[[nodiscard]] bool is_data_module(const std::filesystem::path& file);

// The data module `file` as the TOML document of `kind` ("contract", "native", "ops").
[[nodiscard]] std::expected<toml::table, std::string> data_document(const std::filesystem::path& file, std::string_view kind);

}  // namespace omui::gen
