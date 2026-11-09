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

// A JSON snapshot (D-159: the spec's registry and its contracts, `<name>.json`)
// as the TOML document its reader takes; its keys in their order.
[[nodiscard]] std::expected<toml::table, std::string> json_document(const std::filesystem::path& file);

// The data module `file` as the TOML document of `kind` ("contract", "native", "ops").
[[nodiscard]] std::expected<toml::table, std::string> data_document(const std::filesystem::path& file, std::string_view kind);

// What a data module stands for, as the JSON snapshot a reader takes
// (`{"<kind>": <its value>}`, D-139/D-159) and that snapshot's name
// (`brand.theme.omui.ts` → `brand.theme.omui.json`): what `omui-gen embed`
// carries in a program for a theme or a style.
struct Snapshot {
    std::string name;
    std::string json;
};
[[nodiscard]] std::expected<Snapshot, std::string> snapshot(const std::filesystem::path& file);

}  // namespace omui::gen
