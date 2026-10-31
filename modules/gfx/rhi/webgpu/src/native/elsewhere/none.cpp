// No page (any target but the Web): no WebGPU. create() answers
// Errc::unsupported with this reason, and nothing else is reached.
module openmui.rhi.webgpu;

import std;
import :page;

namespace omui::rhi::webgpu::page {

std::expected<Adapter, std::string> device() { return std::unexpected(std::string("WebGPU is the Web's: this program runs in no page")); }
bool suspends() { return false; }
bool buffer(std::uint32_t, std::uint32_t, std::uint32_t, std::string_view) { return false; }
bool texture(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::string_view) { return false; }
bool sampler(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) { return false; }
bool graphics(std::uint32_t, std::span<const std::uint32_t>, std::span<const std::byte>, std::string_view, std::span<const std::byte>, std::string_view,
              std::string_view) {
    return false;
}
bool compute(std::uint32_t, std::span<const std::uint32_t>, std::span<const std::byte>, std::string_view, std::string_view) { return false; }
void destroy(Kind, std::uint32_t) {}
bool run(std::span<const std::uint32_t>) { return false; }
int wait() { return -1; }
int read(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::span<std::byte>) { return -1; }
int read(std::uint32_t, std::uint32_t, std::span<std::byte>) { return -1; }
std::string error() { return {}; }
bool lost() { return false; }
bool canvas(std::string_view) { return false; }
int present(std::uint32_t) { return -1; }

}  // namespace omui::rhi::webgpu::page
