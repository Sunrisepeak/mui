// openmui.rhi.webgl:page — the page's WebGL2, as the device sees it: no
// Emscripten type crosses this line (the page is native/browser/page.cpp,
// calling src/js/omui_webgl.js; off the Web, native/elsewhere/none.cpp).
// Enumerations cross as rhi's values; resources by their slot index.
module openmui.rhi.webgl:page;

import std;

namespace omui::rhi::webgl::page {

struct Context {
    std::string   name;
    bool          software = false;
    std::uint32_t max_texture = 2048;
    std::uint32_t max_samples = 4;
    bool          color_float = false;       // EXT_color_buffer_float: float colour targets
    bool          color_half_float = false;  // or at least half-float ones
};

// A context on the canvas `selector` names ("": one nobody sees), or why
// there is none. close() lets it go.
[[nodiscard]] std::expected<Context, std::string> open(std::string_view selector);
void close();

enum class Kind : std::uint32_t { buffer, texture, sampler, graphics, compute };

// Each false: the page failed (error() says why).
bool buffer(std::uint32_t id, std::uint32_t size, std::uint32_t usage);
bool texture(std::uint32_t id, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t samples);
bool sampler(std::uint32_t id, std::uint32_t min, std::uint32_t mag, std::uint32_t u, std::uint32_t v);
// `words`: the pipeline's (rhi::wire::encode); the code ESSL.
bool graphics(std::uint32_t id, std::span<const std::uint32_t> words, std::span<const std::byte> vs, std::span<const std::byte> fs,
              std::string_view label);
void destroy(Kind kind, std::uint32_t id);
// rhi's wire words of a validated list.
bool run(std::span<const std::uint32_t> words);
bool read(std::uint32_t texture, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::span<std::byte> out);
bool read(std::uint32_t buffer, std::uint32_t offset, std::span<std::byte> out);

// What went wrong since the last call ("" nothing); whether the context is lost.
[[nodiscard]] std::string error();
[[nodiscard]] bool lost();

// 1 shown, 0 nothing to show, -1 failed.
[[nodiscard]] int present(std::uint32_t texture);

}  // namespace omui::rhi::webgl::page
