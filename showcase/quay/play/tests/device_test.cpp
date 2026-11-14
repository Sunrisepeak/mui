// Quay's picture on a device (SDL_GPU: lavapipe in Linux CI, the real GPU by tools/dev/gpu-real.sh): the frames of
// a game the guide plays — from the start, a warden near, the beacon lit — drawn by xrender.mesh.gpu as the
// reference draws them (the wardens bent on the device, shadows by maps for the reference's traced ones, the
// glow), each within the tolerance of the harbour's own case. With OMUI_REQUIRE_GPU=1 a missing device fails;
// otherwise it is said and the case passes vacuously. The device's pictures are kept beside the reference's. A test
// of `play`, the desktop member: SDL_GPU is the native systems' device here.
import std;
import mlibs.rhi;
import mlibs.image;
import xrender.rhi.sdl;
import omui.showcase.quay.picture;
import mcpplibs.os;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace picture = quay::picture;
namespace rhi = mlibs::rhi;

namespace {

constexpr std::uint32_t width = 480, height = 270;

struct Apart {
    std::size_t compared = 0, off = 0;
    double      mean = 0;
    int         worst = 0;
};
Apart apart(std::span<const std::uint8_t> reference, std::span<const std::byte> device, int near) {
    Apart all;
    double sum = 0;
    for (std::size_t pixel = 0; pixel * 4 < reference.size() && pixel * 4 < device.size(); ++pixel) {
        ++all.compared;
        int most = 0;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const int difference = std::abs(static_cast<int>(reference[pixel * 4 + channel]) - std::to_integer<int>(device[pixel * 4 + channel]));
            most = std::max(most, difference), sum += difference;
        }
        all.off += most > near, all.worst = std::max(all.worst, most);
    }
    all.mean = all.compared ? sum / (static_cast<double>(all.compared) * 3) : 0;
    return all;
}
void keep(std::span<const std::byte> rgba, std::string_view name) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "openmui-tests" / "quay";
    std::filesystem::create_directories(directory);
    const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(rgba.data()), static_cast<int>(width), static_cast<int>(height), static_cast<int>(width) * 4, mlibs::image::Alpha::straight};
    expect(mlibs::image::save_png(directory / std::format("{}.png", name), view).has_value());
}

}  // namespace

UTest cases{"quay.picture.device", [] {
    "the frames of a played game on the device are the reference's: from the start, a warden near, the beacon lit"_test = [] {
        xrender::rhi::sdl::Options options;
        options.formats = xrender::mesh::gpu::Pass::shader_formats();
        if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *library;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *video;
        auto device = xrender::rhi::sdl::create(options);
        if (!device) {
            if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", device.error().message);
            else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", device.error().message);
            return;
        }
        rhi::Device& gpu = **device;
        auto drawn = picture::Picture::make(quay::level::harbour());
        expect(fatal(drawn.has_value()));
        auto on_device = picture::OnDevice::make(gpu, *drawn, width, height);
        expect(fatal(on_device.has_value())) << (on_device ? "" : on_device.error().message);

        // The frames: the start, the first time a warden is drawn large, the end with the view up at the lamp.
        quay::State state = quay::start(quay::level::harbour());
        quay::Guide guide;
        std::vector<std::pair<std::string, quay::State>> frames{{"start", state}};
        bool near = false;
        while (state.outcome == quay::Outcome::playing && state.frame < 60 * 120) {
            quay::step(state, guide(state));
            if (!near && state.frame == 90) frames.emplace_back("warden", state), near = true;
        }
        const quay::Vec3 up = state.beacon + quay::Vec3{0, quay::beacon_height, 0} - quay::eye(state);
        state.keeper.yaw = std::atan2(up.x, -up.z), state.keeper.pitch = std::atan2(up.y, std::sqrt(up.x * up.x + up.z * up.z)) - 0.25f;
        frames.emplace_back("lit", state);

        for (const auto& [name, at] : frames) {
            drawn->draw(at, {.width = static_cast<int>(width), .height = static_cast<int>(height)});
            rhi::CommandList list;
            expect(fatal(on_device->record(list, *drawn).has_value()));
            const auto submitted = gpu.submit(list);
            expect(fatal(submitted.has_value()));
            expect(gpu.wait(*submitted).has_value());
            const auto pixels = gpu.read(on_device->colour(), rhi::Region{0, 0, width, height});
            expect(fatal(pixels.has_value()));
            keep(*pixels, std::format("quay-{}-device", name));
            const Apart all = apart(drawn->shown(), *pixels, 6);
            const auto& stats = on_device->last();
            std::println("{} on {}: {} draws, {} lights ({} left out), {} with shadows in {} views; {} of {} pixels further than 6 levels ({:.2f}%), mean {:.3f}, worst {}", name, gpu.caps().device,
                         stats.draws, stats.lamps, stats.lamps_left_out, stats.lamps_shadowed, stats.lamp_views, all.off, all.compared,
                         100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst);
            expect(eq(stats.missing, 0u) && eq(stats.lamps_left_out, 0u));
            // The harbour's own case is held to 0.4% of its pixels.
            expect(lt(static_cast<double>(all.off), 0.004 * static_cast<double>(all.compared))) << name;
        }
    };
}};
