module omui.host.sdl3.gpu;

import std;
import mlibs.compose;
import mlibs.rhi;
import xrender.rhi.sdl;
import xrender.frame;
import xrender.vector;
import omui.host.sdl3;

namespace omui::host::sdl3::gpu {

namespace {

xrender::rhi::sdl::Options options_of(const GpuDeviceOptions& asked) {
    xrender::rhi::sdl::Options options;
    options.formats = xrender::gpu_shader_formats();
    options.vulkan_library = asked.vulkan_library;
    options.video_driver = asked.video_driver;
    options.window = asked.window;
    options.vsync = asked.vsync;
    return options;
}

struct Making final : GpuMaking {
    explicit Making(xrender::rhi::sdl::Making making) : making_(std::move(making)) {}
    mlibs::rhi::Result<std::unique_ptr<mlibs::rhi::Device>> get() override { return making_.get(); }
    xrender::rhi::sdl::Making making_;
};

struct Renderer final : GpuRenderer {
    explicit Renderer(xrender::GpuFrame frame) : frame_(std::move(frame)) {}
    mlibs::rhi::Result<void> render(std::span<const xrender::FrameLayer> layers, mlibs::rhi::Texture target, std::uint32_t width, std::uint32_t height,
                                    const xrender::GpuFrameOptions& options) override {
        auto rendered = frame_.render(layers, target, width, height, options);
        if (!rendered) return std::unexpected(rendered.error());
        return {};
    }
    xrender::GpuFrame frame_;
};

std::unique_ptr<GpuMaking> begin(const GpuDeviceOptions& asked) {
    return std::make_unique<Making>(xrender::rhi::sdl::create_async(options_of(asked)));
}

mlibs::rhi::Result<std::unique_ptr<mlibs::rhi::Device>> create(const GpuDeviceOptions& asked) { return xrender::rhi::sdl::create(options_of(asked)); }

mlibs::rhi::Result<void> claim(mlibs::rhi::Device& device, std::uint32_t window, bool vsync) { return xrender::rhi::sdl::claim(device, window, vsync); }

mlibs::rhi::Result<std::unique_ptr<GpuRenderer>> renderer(mlibs::rhi::Device& device) {
    auto frame = xrender::GpuFrame::create(device);
    if (!frame) return std::unexpected(frame.error());
    return std::unique_ptr<GpuRenderer>(std::make_unique<Renderer>(std::move(*frame)));
}

}  // namespace

// Made when installed, not when the program starts (mlibs.compose: a table initialised at start-up would keep
// the backend and the renderer in a program that never installs them).
void install() {
    static const GpuService service{"sdl-gpu", &begin, &create, &claim, &renderer};
    (void)mlibs::compose::process().install(gpu_slot, service, service.name);
}

}  // namespace omui::host::sdl3::gpu
