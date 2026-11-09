// omui.gen.data — the documents the generator reads (D-139, D-159): a JSON
// snapshot as it is, or a TypeScript data module (<name>.<kind>.omui.ts)
// evaluated by omui.lang.ts (deterministic, bounded) into the snapshot it
// stands for:
//
//   defineContract({ button: {…} })        → { "contract": { "button": {…} } }
//   defineNative({ contrast: {…} })        → { "native": { "contrast": {…} } }
//   defineOps({ product, doc, …, operations: […], tiers: […] })
//                                          → { "ops": { "product": …, "operations": […] } }
//
// Each value keeps its place (a JSON pointer, and its line and column in the
// snapshot): a module's own lines are not in it, so problems name the
// pointer.
export module omui.gen.data;

import std;
import mlibs.json;

export namespace omui::gen {

// Whether `file` is a data module (<name>.<kind>.omui.ts).
[[nodiscard]] bool is_data_module(const std::filesystem::path& file);

// A JSON snapshot (D-159: the spec's registry and its contracts, `<name>.json`), read.
[[nodiscard]] std::expected<mlibs::json::Document, std::string> json_document(const std::filesystem::path& file);

// The data module `file`, evaluated, as the document of `kind` ("contract", "native", "ops").
[[nodiscard]] std::expected<mlibs::json::Document, std::string> data_document(const std::filesystem::path& file, std::string_view kind);

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
