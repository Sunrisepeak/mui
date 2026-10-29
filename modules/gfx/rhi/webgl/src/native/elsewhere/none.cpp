// No page (any target but the Web): no WebGL2. create() answers
// Errc::unsupported with this reason, and nothing else is reached.
module openmui.rhi.webgl;

import std;
import :page;

namespace omui::rhi::webgl::page {

std::expected<Context, std::string> open(std::string_view) { return std::unexpected(std::string("WebGL2 is the Web's: this program runs in no page")); }
void close() {}
bool buffer(std::uint32_t, std::uint32_t, std::uint32_t) { return false; }
bool texture(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) { return false; }
bool sampler(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) { return false; }
bool graphics(std::uint32_t, std::span<const std::uint32_t>, std::span<const std::byte>, std::span<const std::byte>, std::string_view) { return false; }
void destroy(Kind, std::uint32_t) {}
bool run(std::span<const std::uint32_t>) { return false; }
bool read(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::span<std::byte>) { return false; }
bool read(std::uint32_t, std::uint32_t, std::span<std::byte>) { return false; }
std::string error() { return {}; }
bool lost() { return false; }
int present(std::uint32_t) { return -1; }

}  // namespace omui::rhi::webgl::page
