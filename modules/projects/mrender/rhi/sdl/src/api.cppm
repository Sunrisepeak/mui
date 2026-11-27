// openmui.rhi.sdl — omui.rhi on SDL_GPU (architecture §8.2, D-33): Vulkan,
// Metal or Direct3D 12, whichever SDL finds for the shader formats the
// program's shaders carry. A thin mapping: rhi's descriptions to SDL's,
// a submission's commands to one SDL command buffer (uploads staged once,
// copy passes around the render and compute passes in the list's order), a
// fence per submission, readback through a download buffer.
//
// A Device is used from one thread at a time. SDL's video subsystem is
// initialised while a device exists (a window can be claimed later); on a
// machine with no display, SDL_VIDEO_DRIVER=offscreen — which can drive
// Vulkan but not Metal (it makes no Metal views), so a headless device on
// macOS names a video driver that can (Options::video_driver = "cocoa").
export module mrender.rhi.sdl.api;

import std;
import mlibs.rhi;

namespace mrender::rhi::sdl { using namespace mlibs::rhi; }  // the interface it implements
export namespace mrender::rhi::sdl {

struct Options {
    // The formats the program's shaders are compiled to: SDL picks a backend
    // that takes one of them (openmui.shader's formats_for(target)).
    ShaderFormats formats = mlibs::rhi::formats({ShaderFormat::spirv});
    // A backend to insist on; none: SDL's choice for the platform.
    std::optional<Backend> backend;
    bool debug = false;             // the backend's validation layer, when installed
    bool prefer_low_power = false;  // an integrated GPU over a discrete one
    // The Vulkan loader to use (SDL_HINT_VULKAN_LIBRARY); empty: the system's.
    std::string vulkan_library;
    // The video driver to bring SDL's video subsystem up with, when the
    // device is the first to (SDL_HINT_VIDEO_DRIVER, over SDL_VIDEO_DRIVER,
    // for this initialisation only); empty: SDL's choice. A subsystem a
    // host has already brought up keeps its driver.
    std::string video_driver;
    // A window (SDL_GetWindowID) the device draws into: claimed for the GPU,
    // shown by Device::present. 0: none (offscreen work only). A window of
    // the offscreen video driver that the backend makes no swapchain for has
    // nothing to show: the device draws, and present() answers false.
    std::uint32_t window = 0;
    bool          vsync = true;  // the swapchain waits for the display
    // Only the Vulkan drivers of the GPUs this machine has, never one that draws on the CPU (lavapipe, SwiftShader):
    // for a program that chooses its renderer itself, whose own CPU renderer is the better one there. The loader
    // otherwise loads every driver installed (on Linux each with what it links: LLVM for a software one). Not where
    // the environment chooses the loader's drivers already (VK_LOADER_DRIVERS_SELECT or _DISABLE, VK_DRIVER_FILES,
    // VK_ICD_FILENAMES). Linux; elsewhere the system loads the drivers of its adapters only.
    bool          hardware_only = false;
};

// The Vulkan loader's driver choice for `hardware_only` on a Linux machine whose GPUs are of these PCI vendors (0x10de
// NVIDIA, 0x1002 AMD, 0x8086 Intel, 0x1af4 virtio): the variable and its globs over the drivers' file names —
// VK_LOADER_DRIVERS_SELECT naming those vendors' drivers when every vendor is one known here, else
// VK_LOADER_DRIVERS_DISABLE naming the drivers that draw on the CPU.
struct VulkanDriverChoice {
    std::string variable, globs;
};
[[nodiscard]] VulkanDriverChoice vulkan_driver_choice(std::span<const std::uint32_t> pci_vendors);
// The PCI vendors of this machine's display devices (/sys/class/drm/card*/device/vendor; none elsewhere).
[[nodiscard]] std::vector<std::uint32_t> display_vendors();

// A device, or why there is none (Errc::unsupported: no GPU, driver or
// backend for those formats; the caller falls back to the CPU renderer).
[[nodiscard]] Result<std::unique_ptr<Device>> create(const Options& options = {});

// A device made on a thread of its own while the caller goes on (a window's host loading its fonts and drawing its
// first frames meanwhile; the driver's start, ~0.3 s on a discrete GPU, overlapped rather than waited for). SDL's video
// is brought up on the calling thread, as SDL asks; the device is made on the other. `ready` says whether it is done
// (made or failed) without waiting; `get` waits for it and hands it over (once); a window is then claimed with
// `claim`, on the calling thread. Options::window is not used here.
class Making {
public:
    Making() = default;
    Making(Making&&) noexcept;
    Making& operator=(Making&&) noexcept;
    ~Making();  // not taken: waited for and let go
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] Result<std::unique_ptr<Device>> get();

private:
    friend Making create_async(const Options& options, std::function<void(Device*)> made);
    std::future<Result<std::unique_ptr<Device>>> made_;
    bool video_held_ = false;  // released here when no device came (a device releases its own)
};
// `made`, when given, runs on the making thread once the device is made (null: none was), before `ready` says so (at
// once on the calling thread when no thread was started): the caller's own work on the device while it is still that
// thread's (a renderer's pipelines), and its notice that the device is there (a sleeping loop woken). A Device is used
// from one thread at a time: what `made` does with it is done before `get` hands it over.
[[nodiscard]] Making create_async(const Options& options = {}, std::function<void(Device*)> made = {});

// A window (SDL_GetWindowID) claimed for a device this module made (`create` or `create_async`), its swapchain
// waiting for the display or not: what Options::window does at creation, later.
[[nodiscard]] Result<void> claim(Device& device, std::uint32_t window, bool vsync = true);

// The options this machine's environment gives a device, over `base`: the Vulkan loader and SDL's video driver
// named by OMUI_VULKAN_LIBRARY and OMUI_GPU_VIDEO_DRIVER when they are set (`omui-devtools gpu` sets them: CI's
// software device, this machine's GPU without a display). Every program and test that opens a device as the
// machine allows takes it from here.
[[nodiscard]] Options environment_options(Options base = {});

// Whether a missing device is a failure where this runs (OMUI_REQUIRE_GPU=1: CI's GPU rows) or only said.
[[nodiscard]] bool device_required();

// The backends this build of SDL can drive, in its order of preference.
[[nodiscard]] std::vector<Backend> backends();

}  // namespace mrender::rhi::sdl
