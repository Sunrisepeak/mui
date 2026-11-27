module omui.host.sdl3.gpu;

import std;
import mlibs.compose;
import mlibs.rhi;
import mrender.rhi.sdl;
import mrender.frame;
import mrender.vector;
import mrender.effect;
import mlibs.trace;
import omui.host.sdl3;

namespace omui::host::sdl3::gpu {

namespace {

mrender::rhi::sdl::Options options_of(const GpuDeviceOptions& asked) {
    mrender::rhi::sdl::Options options;
    options.formats = mrender::gpu_shader_formats();
    options.vulkan_library = asked.vulkan_library;
    options.video_driver = asked.video_driver;
    options.window = asked.window;
    options.vsync = asked.vsync;
    options.hardware_only = asked.hardware_only;
    return options;
}

struct Renderer final : GpuRenderer {
    explicit Renderer(mrender::GpuFrame frame) : frame_(std::move(frame)) {}
    mlibs::rhi::Result<void> render(std::span<const mrender::FrameLayer> layers, mlibs::rhi::Texture target, std::uint32_t width, std::uint32_t height,
                                    const mrender::GpuFrameOptions& options) override {
        auto rendered = frame_.render(layers, target, width, height, options);
        if (!rendered) return std::unexpected(rendered.error());
        return {};
    }
    mrender::GpuFrame frame_;
};

struct Making final : GpuMaking {
    Making() = default;
    Making(const Making&) = delete;
    Making& operator=(const Making&) = delete;
    // Not taken: waited for, the renderer let go before its device.
    ~Making() override {
        if (!making_.valid()) return;
        auto device = making_.get();
        renderer_.reset();
    }
    bool ready() const override { return making_.ready(); }
    mlibs::rhi::Result<std::unique_ptr<mlibs::rhi::Device>> get() override { return making_.get(); }
    std::unique_ptr<GpuRenderer> renderer() override { return std::move(renderer_); }
    std::unique_ptr<GpuRenderer> renderer_;  // made on the making thread; read once the making is ready
    mrender::rhi::sdl::Making    making_;    // last: gone first, its thread done before what it writes goes
};

// The device, and its renderer's pipelines beside it on the same thread (a host drawing its first frames with the CPU
// meanwhile takes both over at once): none for a software device an automatic host lets go.
std::unique_ptr<GpuMaking> begin(const GpuDeviceOptions& asked) {
    auto making = std::make_unique<Making>();
    making->making_ = mrender::rhi::sdl::create_async(
        options_of(asked), [into = making.get(), automatic = asked.hardware_only, told = asked.made](mlibs::rhi::Device* device) {
            if (device && !(device->caps().software && automatic)) {
                mlibs::trace::Span span("device.renderer", "rhi");
                if (auto frame = mrender::GpuFrame::create(*device)) into->renderer_ = std::make_unique<Renderer>(std::move(*frame));
            }
            if (told) told();
        });
    return making;
}

mlibs::rhi::Result<std::unique_ptr<mlibs::rhi::Device>> create(const GpuDeviceOptions& asked) { return mrender::rhi::sdl::create(options_of(asked)); }

mlibs::rhi::Result<void> claim(mlibs::rhi::Device& device, std::uint32_t window, bool vsync) { return mrender::rhi::sdl::claim(device, window, vsync); }

mlibs::rhi::Result<std::unique_ptr<GpuRenderer>> renderer(mlibs::rhi::Device& device) {
    auto frame = mrender::GpuFrame::create(device);
    if (!frame) return std::unexpected(frame.error());
    return std::unique_ptr<GpuRenderer>(std::make_unique<Renderer>(std::move(*frame)));
}

struct Effects final : GpuEffects {
    explicit Effects(mrender::effect::Options options) : library_(std::move(options)) {}
    mrender::effect::Library& library() noexcept override { return library_; }
    std::vector<std::string> load(const std::filesystem::path& directory) override {
        std::vector<std::string> problems;
        if (auto loaded = library_.load_directory(directory); !loaded)
            for (const mrender::effect::Diagnostic& problem : loaded.error()) problems.push_back(std::format("effect: {}", problem.text()));
        return problems;
    }
    Reloaded reload() override {
        Reloaded out;
        for (const mrender::effect::Reload& one : library_.poll()) {
            if (one.ok()) {
                out.reloaded.push_back(std::format("effect {} reloaded in {} ms", one.name, static_cast<double>(one.took.count()) / 1000.0));
            } else {
                for (const mrender::effect::Diagnostic& problem : one.problems) out.problems.push_back(std::format("effect {}: {}", one.name, problem.text()));
            }
        }
        return out;
    }
    mrender::effect::Library library_;
};

std::unique_ptr<GpuEffects> effects(const mlibs::rhi::Device* device, std::function<std::expected<std::string, std::string>(const std::filesystem::path&)> data) {
    return std::make_unique<Effects>(
        mrender::effect::Options{.formats = device ? device->caps().formats : mrender::gpu_shader_formats(), .data = std::move(data)});
}

}  // namespace

// Made when installed, not when the program starts (mlibs.compose: a table initialised at start-up would keep
// the backend and the renderer in a program that never installs them).
void install() {
    static const GpuService service{"sdl-gpu", &begin, &create, &claim, &renderer, &effects};
    (void)mlibs::compose::process().install(gpu_slot, service, service.name);
}

}  // namespace omui::host::sdl3::gpu
