// omui.host.sdl3:gpu — the slot the GPU path is installed at (mlibs.compose, D-238): the device for a window and
// the renderer that draws a frame's layers on it. The host draws with it when a provider is installed and the
// options and the machine say the GPU draws; without one — an application built without it, as its world says —
// the CPU renderer draws every frame, as where a machine has no GPU. omui.host.sdl3.gpu provides SDL_GPU's
// device (mlibs.rhi over Vulkan, Metal or Direct3D 12) and xrender's renderer; omui.sdk installs it.
export module omui.host.sdl3:gpu;

import std;
import mlibs.compose;
import mlibs.rhi;
import xrender.frame;
import xrender.effect;

export namespace omui {

// What a device is made with: the Vulkan loader and SDL's video driver the host was given (empty: the system's,
// SDL's choice), and, made for a window at once, the window (SDL_GetWindowID) and whether its swapchain waits.
struct GpuDeviceOptions {
    std::string   vulkan_library;
    std::string   video_driver;
    std::uint32_t window = 0;
    bool          vsync = true;
    // The renderer chosen by the host (automatic): only the drivers of the machine's GPUs loaded, none that draws on
    // the CPU — the host's CPU renderer is the better one there (xrender.rhi.sdl's hardware_only).
    bool          hardware_only = false;
};

// A device being made on a thread of its own while the host goes on; `get` waits for it, once.
class GpuMaking {
public:
    virtual ~GpuMaking() = default;
    [[nodiscard]] virtual mlibs::rhi::Result<std::unique_ptr<mlibs::rhi::Device>> get() = 0;
};

// The renderer of a device: a frame's layers drawn into a colour target the host made, the work submitted.
class GpuRenderer {
public:
    virtual ~GpuRenderer() = default;
    [[nodiscard]] virtual mlibs::rhi::Result<void> render(std::span<const xrender::FrameLayer> layers, mlibs::rhi::Texture target, std::uint32_t width,
                                                          std::uint32_t height, const xrender::GpuFrameOptions& options) = 0;
};

// A program's effect files (xrender.effect), which the GPU renderer draws: loaded from a directory, and loaded
// again when a file changes. What it says is the host's to log.
class GpuEffects {
public:
    virtual ~GpuEffects() = default;
    [[nodiscard]] virtual xrender::effect::Library& library() noexcept = 0;
    // The directory's effects loaded; a line for each problem.
    [[nodiscard]] virtual std::vector<std::string> load(const std::filesystem::path& directory) = 0;
    // The effects whose files changed, loaded again: a line for each that was (`reloaded`), a line for each
    // problem of one that was not.
    struct Reloaded {
        std::vector<std::string> reloaded;
        std::vector<std::string> problems;
    };
    [[nodiscard]] virtual Reloaded reload() = 0;
};

// The GPU path's provider: only what the host calls.
struct GpuService {
    std::string_view name;  // "sdl-gpu"
    // A device begun on a thread of its own, for no window yet (`claim` gives it one).
    std::unique_ptr<GpuMaking> (*begin)(const GpuDeviceOptions& options) = nullptr;
    // A device made now, for the options' window.
    mlibs::rhi::Result<std::unique_ptr<mlibs::rhi::Device>> (*create)(const GpuDeviceOptions& options) = nullptr;
    // A window claimed for a device this provider made.
    mlibs::rhi::Result<void> (*claim)(mlibs::rhi::Device& device, std::uint32_t window, bool vsync) = nullptr;
    // The renderer's pipelines and textures on the device.
    mlibs::rhi::Result<std::unique_ptr<GpuRenderer>> (*renderer)(mlibs::rhi::Device& device) = nullptr;
    // The effect library for a device's shader formats (no device: the formats the renderer's shaders are
    // compiled to), reading effect modules through `data` (empty: `.effect.xdef.json` alone).
    std::unique_ptr<GpuEffects> (*effects)(const mlibs::rhi::Device* device,
                                           std::function<std::expected<std::string, std::string>(const std::filesystem::path&)> data) = nullptr;
};

inline constexpr mlibs::compose::Slot<GpuService> gpu_slot{
    {.id = "omui.host.sdl3.gpu",
     .summary = "the GPU path of a desktop window: SDL_GPU's device and xrender's renderer on it",
     .absent = "build with omui.sdk's feature `gpu` (worlds `app` and `full` have it), or depend on omui.host.sdl3.gpu and call "
               "omui::host::sdl3::gpu::install() before a host is made; without it the CPU renderer draws"}};

}  // namespace omui
