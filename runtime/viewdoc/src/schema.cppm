// openmui.viewdoc:schema — the documents' JSON Schema and canonical format.
//
// The schema is generated from what is registered (every component type,
// its contract's properties with their types and documentation, its events,
// the runtime's own properties and events), so constrained generation and
// editors know the same names the checker does (R8). `format_document` is
// the one canonical text of a document (R3: `omui fmt`): fields in a fixed
// order, two-space indentation, short values on one line; formatting is
// idempotent and keeps every value.
export module openmui.viewdoc:schema;

import std;
import openmui.core;
import :document;

export namespace omui::viewdoc {

// JSON Schema (draft 2020-12) of view documents over the registered
// components (without those documents define, when asked).
[[nodiscard]] Value view_schema(bool with_document_components = true);

// The canonical text of a document, or why it is not JSON.
[[nodiscard]] std::expected<std::string, Diagnostic> format_document(std::string_view json);

}  // namespace omui::viewdoc
