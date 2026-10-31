// The page for openmui.rhi.webgpu:page: src/js/omui_webgpu.js, linked into
// the program by build.mcpp (an Emscripten library). Every call is a plain
// one across the boundary — numbers, and pointers into this program's
// memory — and everything leaves as the partition's types.
module openmui.rhi.webgpu;

import std;
import :page;

extern "C" {
int   omui_webgpu_device(std::uint32_t* out);
char* omui_webgpu_name();
char* omui_webgpu_why();
int   omui_webgpu_suspends();
int   omui_webgpu_buffer(std::uint32_t id, std::uint32_t size, std::uint32_t usage, const char* label);
int   omui_webgpu_texture(std::uint32_t id, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t usage,
                          std::uint32_t samples, const char* label);
int   omui_webgpu_sampler(std::uint32_t id, std::uint32_t min, std::uint32_t mag, std::uint32_t u, std::uint32_t v);
int   omui_webgpu_graphics(std::uint32_t id, const std::uint32_t* words, std::uint32_t count, const std::byte* vs, std::uint32_t vs_length,
                           const char* vs_entry, const std::byte* fs, std::uint32_t fs_length, const char* fs_entry, const char* label);
int   omui_webgpu_compute(std::uint32_t id, const std::uint32_t* words, std::uint32_t count, const std::byte* cs, std::uint32_t cs_length,
                          const char* entry, const char* label);
void  omui_webgpu_destroy(std::uint32_t kind, std::uint32_t id);
int   omui_webgpu_run(const std::uint32_t* words, std::uint32_t count);
char* omui_webgpu_error();
int   omui_webgpu_lost();
int   omui_webgpu_wait();
int   omui_webgpu_read_texture(std::uint32_t id, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::byte* out);
int   omui_webgpu_read_buffer(std::uint32_t id, std::uint32_t offset, std::uint32_t length, std::byte* out);
int   omui_webgpu_canvas(const char* selector);
int   omui_webgpu_present(std::uint32_t id);
}

namespace omui::rhi::webgpu::page {

namespace {

// A string the page made (stringToNewUTF8), taken and freed.
std::string take(char* text) {
    if (!text) return {};
    std::string s(text);
    std::free(text);
    return s;
}

std::string z(std::string_view s) { return std::string(s); }

std::uint32_t length(std::span<const std::byte> b) { return static_cast<std::uint32_t>(b.size()); }

}  // namespace

std::expected<Adapter, std::string> device() {
    std::array<std::uint32_t, 3> info{};
    if (!omui_webgpu_device(info.data())) return std::unexpected(take(omui_webgpu_why()));
    return Adapter{take(omui_webgpu_name()), info[0] != 0, info[1], info[2] != 0};
}

bool suspends() { return omui_webgpu_suspends() != 0; }

bool buffer(std::uint32_t id, std::uint32_t size, std::uint32_t usage, std::string_view label) {
    return omui_webgpu_buffer(id, size, usage, z(label).c_str()) != 0;
}

bool texture(std::uint32_t id, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t usage, std::uint32_t samples,
             std::string_view label) {
    return omui_webgpu_texture(id, format, width, height, usage, samples, z(label).c_str()) != 0;
}

bool sampler(std::uint32_t id, std::uint32_t min, std::uint32_t mag, std::uint32_t u, std::uint32_t v) {
    return omui_webgpu_sampler(id, min, mag, u, v) != 0;
}

bool graphics(std::uint32_t id, std::span<const std::uint32_t> words, std::span<const std::byte> vs, std::string_view vs_entry,
              std::span<const std::byte> fs, std::string_view fs_entry, std::string_view label) {
    return omui_webgpu_graphics(id, words.data(), static_cast<std::uint32_t>(words.size()), vs.data(), length(vs), z(vs_entry).c_str(), fs.data(),
                                length(fs), z(fs_entry).c_str(), z(label).c_str()) != 0;
}

bool compute(std::uint32_t id, std::span<const std::uint32_t> words, std::span<const std::byte> cs, std::string_view entry, std::string_view label) {
    return omui_webgpu_compute(id, words.data(), static_cast<std::uint32_t>(words.size()), cs.data(), length(cs), z(entry).c_str(),
                               z(label).c_str()) != 0;
}

void destroy(Kind kind, std::uint32_t id) { omui_webgpu_destroy(static_cast<std::uint32_t>(kind), id); }

bool run(std::span<const std::uint32_t> words) { return omui_webgpu_run(words.data(), static_cast<std::uint32_t>(words.size())) != 0; }

int wait() { return omui_webgpu_wait(); }

int read(std::uint32_t texture, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::span<std::byte> out) {
    return omui_webgpu_read_texture(texture, x, y, width, height, out.data());
}

int read(std::uint32_t buffer, std::uint32_t offset, std::span<std::byte> out) {
    return omui_webgpu_read_buffer(buffer, offset, static_cast<std::uint32_t>(out.size()), out.data());
}

std::string error() { return take(omui_webgpu_error()); }

bool lost() { return omui_webgpu_lost() != 0; }

bool canvas(std::string_view selector) { return omui_webgpu_canvas(z(selector).c_str()) != 0; }

int present(std::uint32_t texture) { return omui_webgpu_present(texture); }

}  // namespace omui::rhi::webgpu::page
