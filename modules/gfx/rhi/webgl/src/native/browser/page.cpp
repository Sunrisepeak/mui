// The page for openmui.rhi.webgl:page: src/js/omui_webgl.js, linked into the
// program by build.mcpp (an Emscripten library). Every call is a plain one
// across the boundary — numbers, and pointers into this program's memory —
// and everything leaves as the partition's types.
module openmui.rhi.webgl;

import std;
import :page;

extern "C" {
int   omui_webgl_open(const char* selector, std::uint32_t* out);
void  omui_webgl_close();
char* omui_webgl_name();
char* omui_webgl_why();
int   omui_webgl_buffer(std::uint32_t id, std::uint32_t size, std::uint32_t usage);
int   omui_webgl_texture(std::uint32_t id, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t samples);
int   omui_webgl_sampler(std::uint32_t id, std::uint32_t min, std::uint32_t mag, std::uint32_t u, std::uint32_t v);
int   omui_webgl_graphics(std::uint32_t id, const std::uint32_t* words, std::uint32_t count, const std::byte* vs, std::uint32_t vs_length,
                          const std::byte* fs, std::uint32_t fs_length, const char* label);
void  omui_webgl_destroy(std::uint32_t kind, std::uint32_t id);
int   omui_webgl_run(const std::uint32_t* words, std::uint32_t count);
int   omui_webgl_read_texture(std::uint32_t id, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::byte* out);
int   omui_webgl_read_buffer(std::uint32_t id, std::uint32_t offset, std::uint32_t length, std::byte* out);
char* omui_webgl_error();
int   omui_webgl_lost();
int   omui_webgl_present(std::uint32_t id);
}

namespace omui::rhi::webgl::page {

namespace {

// A string the page made (stringToNewUTF8), taken and freed.
std::string take(char* text) {
    if (!text) return {};
    std::string s(text);
    std::free(text);
    return s;
}

std::uint32_t length(std::span<const std::byte> b) { return static_cast<std::uint32_t>(b.size()); }

}  // namespace

std::expected<Context, std::string> open(std::string_view selector) {
    std::array<std::uint32_t, 5> info{};
    if (!omui_webgl_open(std::string(selector).c_str(), info.data())) return std::unexpected(take(omui_webgl_why()));
    return Context{take(omui_webgl_name()), info[0] != 0, info[1], info[2], info[3] != 0, info[4] != 0};
}

void close() { omui_webgl_close(); }

bool buffer(std::uint32_t id, std::uint32_t size, std::uint32_t usage) { return omui_webgl_buffer(id, size, usage) != 0; }

bool texture(std::uint32_t id, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t samples) {
    return omui_webgl_texture(id, format, width, height, samples) != 0;
}

bool sampler(std::uint32_t id, std::uint32_t min, std::uint32_t mag, std::uint32_t u, std::uint32_t v) {
    return omui_webgl_sampler(id, min, mag, u, v) != 0;
}

bool graphics(std::uint32_t id, std::span<const std::uint32_t> words, std::span<const std::byte> vs, std::span<const std::byte> fs,
              std::string_view label) {
    return omui_webgl_graphics(id, words.data(), static_cast<std::uint32_t>(words.size()), vs.data(), length(vs), fs.data(), length(fs),
                               std::string(label).c_str()) != 0;
}

void destroy(Kind kind, std::uint32_t id) { omui_webgl_destroy(static_cast<std::uint32_t>(kind), id); }

bool run(std::span<const std::uint32_t> words) { return omui_webgl_run(words.data(), static_cast<std::uint32_t>(words.size())) != 0; }

bool read(std::uint32_t texture, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::span<std::byte> out) {
    return omui_webgl_read_texture(texture, x, y, width, height, out.data()) != 0;
}

bool read(std::uint32_t buffer, std::uint32_t offset, std::span<std::byte> out) {
    return omui_webgl_read_buffer(buffer, offset, static_cast<std::uint32_t>(out.size()), out.data()) != 0;
}

std::string error() { return take(omui_webgl_error()); }

bool lost() { return omui_webgl_lost() != 0; }

int present(std::uint32_t texture) { return omui_webgl_present(texture); }

}  // namespace omui::rhi::webgl::page
