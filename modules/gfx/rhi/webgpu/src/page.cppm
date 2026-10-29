// openmui.rhi.webgpu:page — the page's WebGPU, as the device sees it: no
// Emscripten type crosses this line (the page is native/browser/page.cpp,
// calling src/js/omui_webgpu.js; off the Web, native/elsewhere/none.cpp).
// Enumerations cross as rhi's values; resources by their slot index.
module openmui.rhi.webgpu:page;

import std;

namespace omui::rhi::webgpu::page {

struct Adapter {
    std::string   name;
    bool          software = false;
    std::uint32_t max_texture = 8192;
    bool          depth_stencil = false;  // depth32float-stencil8
};

// The device the page made as the program loaded, or why there is none.
[[nodiscard]] std::expected<Adapter, std::string> device();
// Whether the program was linked to suspend for the page's promises.
[[nodiscard]] bool suspends();

enum class Kind : std::uint32_t { buffer, texture, sampler, graphics, compute };

// Each false: the page failed (error() says why).
bool buffer(std::uint32_t id, std::uint32_t size, std::uint32_t usage, std::string_view label);
bool texture(std::uint32_t id, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t usage, std::uint32_t samples,
             std::string_view label);
bool sampler(std::uint32_t id, std::uint32_t min, std::uint32_t mag, std::uint32_t u, std::uint32_t v);
// `words`: device.cpp's graphics_words / compute_words; the code WGSL.
bool graphics(std::uint32_t id, std::span<const std::uint32_t> words, std::span<const std::byte> vs, std::string_view vs_entry,
              std::span<const std::byte> fs, std::string_view fs_entry, std::string_view label);
bool compute(std::uint32_t id, std::span<const std::uint32_t> words, std::span<const std::byte> cs, std::string_view entry, std::string_view label);
void destroy(Kind kind, std::uint32_t id);
// rhi's wire words of a validated list.
bool run(std::span<const std::uint32_t> words);

// 1 done, 0 failed, -1 the program cannot wait.
[[nodiscard]] int wait();
[[nodiscard]] int read(std::uint32_t texture, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::span<std::byte> out);
[[nodiscard]] int read(std::uint32_t buffer, std::uint32_t offset, std::span<std::byte> out);

// What WebGPU reported since the last call ("" nothing); whether the device is lost.
[[nodiscard]] std::string error();
[[nodiscard]] bool lost();

bool canvas(std::string_view selector);
// 1 shown, 0 nothing to show, -1 failed.
[[nodiscard]] int present(std::uint32_t texture);

}  // namespace omui::rhi::webgpu::page
