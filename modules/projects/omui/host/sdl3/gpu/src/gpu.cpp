module omui.host.sdl3.gpu;

import std;
import mlibs.compose;
import mlibs.rhi;
import xrender.rhi.sdl;
import xrender.frame;
import xrender.vector;
import xrender.effect;
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
    options.hardware_only = asked.hardware_only;
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

struct Effects final : GpuEffects {
    explicit Effects(xrender::effect::Options options) : library_(std::move(options)) {}
    xrender::effect::Library& library() noexcept override { return library_; }
    std::vector<std::string> load(const std::filesystem::path& directory) override {
        std::vector<std::string> problems;
        if (auto loaded = library_.load_directory(directory); !loaded)
            for (const xrender::effect::Diagnostic& problem : loaded.error()) problems.push_back(std::format("effect: {}", problem.text()));
        return problems;
    }
    Reloaded reload() override {
        Reloaded out;
        for (const xrender::effect::Reload& one : library_.poll()) {
            if (one.ok()) {
                out.reloaded.push_back(std::format("effect {} reloaded in {} ms", one.name, static_cast<double>(one.took.count()) / 1000.0));
            } else {
                for (const xrender::effect::Diagnostic& problem : one.problems) out.problems.push_back(std::format("effect {}: {}", one.name, problem.text()));
            }
        }
        return out;
    }
    xrender::effect::Library library_;
};

std::unique_ptr<GpuEffects> effects(const mlibs::rhi::Device* device, std::function<std::expected<std::string, std::string>(const std::filesystem::path&)> data) {
    return std::make_unique<Effects>(
        xrender::effect::Options{.formats = device ? device->caps().formats : xrender::gpu_shader_formats(), .data = std::move(data)});
}

}  // namespace

// Made when installed, not when the program starts (mlibs.compose: a table initialised at start-up would keep
// the backend and the renderer in a program that never installs them).
void install() {
    static const GpuService service{"sdl-gpu", &begin, &create, &claim, &renderer, &effects};
    (void)mlibs::compose::process().install(gpu_slot, service, service.name);
}

}  // namespace omui::host::sdl3::gpu
