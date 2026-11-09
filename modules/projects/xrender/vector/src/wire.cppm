// openmui.render.vector:wire — a display list as bytes, and back (W16,
// D-154: an application in development draws into the host's window; its
// display lists cross the process boundary). Every command is kept exactly —
// floats as their bits, spans as their elements, node and part markers as
// they are — so a list read back renders the same pixels and hashes the same.
// Images, fonts and effects travel as their references: both sides hold the
// same resources (the same fonts loaded, the images and effects sent ahead).
//
//     const std::string bytes = xrender::encode(list);
//     xrender::DisplayList again;
//     if (auto read = xrender::decode(bytes, again); !read) … read.error() …
export module xrender.vector.api:wire;

import std;
import :display;

export namespace xrender {

// The list's commands as bytes (a header with the format's version first).
[[nodiscard]] std::string encode(const DisplayList& list);
// The commands of `bytes` appended to `into`; why not, and nothing appended,
// when they are not a list this format wrote.
[[nodiscard]] std::expected<void, std::string> decode(std::string_view bytes, DisplayList& into);

}  // namespace xrender
