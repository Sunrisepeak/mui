// xengine.matter.json — a definition from its JSON snapshot (what a *.matter.omui.ts data module evaluates
// to, D-139), and the standard one carried in the program. The package's feature `json` (on by default):
// the kernel alone (`default-features = false`) takes no JSON — what dynamics and hanim's engine link.
//
//     auto program = matter::prepare_document(text, "world.matter.omui.json");   // problems by pointer, placed
//     auto standard = matter::standard();                                         // standard.matter.omui.ts
export module xengine.matter.json;

import std;
import mlibs.json;
export import xengine.matter;
export import :standard_snapshot;

export namespace xengine::matter {

// A definition from its JSON snapshot ({"matter": {columns?, channels?, materials, laws}}), on the
// standard columns and channels. Problems by pointer.
[[nodiscard]] std::expected<Definition, std::vector<mlibs::json::Problem>> read(std::string_view text, std::string_view file);
// Read and prepared; a problem of preparing is placed in the text like one of reading.
[[nodiscard]] std::expected<Program, std::vector<mlibs::json::Problem>> prepare_document(std::string_view text, std::string_view file, Limits limits = {});
// The standard definition (standard.matter.omui.ts: the laws every world shares and a first table of
// materials), prepared from the snapshot this module carries (`standard_snapshot`).
[[nodiscard]] std::expected<Program, std::vector<mlibs::json::Problem>> standard();

}  // namespace xengine::matter
