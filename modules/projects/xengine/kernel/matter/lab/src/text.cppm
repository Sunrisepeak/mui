// xengine.matter.lab:text — the lab's sources as JSON values and as the text it keeps (within the module).
module xengine.matter.lab:text;

import std;
import nlohmann.json;
import mlibs.agent;

namespace xengine::matter::lab {

using Json = nlohmann::ordered_json;
using mlibs::agent::json_text;  // a string as JSON

// A text as a value; nothing when it is no JSON.
[[nodiscard]] std::optional<Json> parsed(std::string_view text);
// A value on one line, as a person writes it: {"name": "burns", "when": "…"}.
[[nodiscard]] std::string inline_text(const Json& value);
// A value a row a line: its members (or elements) each on a line, those of them that hold rows broken again,
// down to `depth` levels; ends in a newline.
[[nodiscard]] std::string rows_text(const Json& value, int depth);

// What a path is a source of.
enum class Source : std::uint8_t { none, material, laws, channels, columns, look, scene };
[[nodiscard]] Source source_of(std::string_view path) noexcept;
// The material a path is the row of ("materials/wood.json" → "wood"); empty when it is none.
[[nodiscard]] std::string_view material_of(std::string_view path) noexcept;
[[nodiscard]] std::string material_path(std::string_view name);
// A name a law's expression can say: a letter or _, then letters, digits, _.
[[nodiscard]] bool is_name(std::string_view text) noexcept;
// A path as UTF-8 text, with `/` between its parts.
[[nodiscard]] std::string utf8(const std::filesystem::path& path);

}  // namespace xengine::matter::lab
