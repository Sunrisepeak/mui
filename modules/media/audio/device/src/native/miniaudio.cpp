// The device over miniaudio (mcpplibs.mwrap.miniaudio): a context (the null
// backend, or the system's own in miniaudio's order), a playback device of
// 32-bit float stereo at the mixer's rate, and its data callback rendering
// the mixer into what the device asks for.
module openmui.audio.device;

import std;
import openmui.audio;
import mcpplibs.mwrap.miniaudio;

namespace omui::audio {

struct Device::Impl {
    ma_context                context{};
    ma_device                 device{};
    Mixer*                    mixer = nullptr;
    std::atomic<std::int64_t> frames{0};
    bool                      context_open = false, device_open = false;

    ~Impl() {
        if (device_open) {
            ma_device_stop(&device);
            ma_device_uninit(&device);
        }
        if (context_open) ma_context_uninit(&context);
    }
};

Device::Device(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Device::Device(Device&&) noexcept = default;
Device& Device::operator=(Device&&) noexcept = default;
Device::~Device() = default;

std::expected<Device, std::string> Device::open(Mixer& mixer, DeviceOptions options) {
    auto impl = std::make_unique<Impl>();
    impl->mixer = &mixer;
    ma_backend null = ma_backend_null;
    if (ma_context_init(options.null ? &null : nullptr, options.null ? 1 : 0, nullptr, &impl->context) != MA_SUCCESS)
        return std::unexpected(std::string(options.null ? "no null audio backend" : "no audio backend on this system"));
    impl->context_open = true;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = static_cast<ma_uint32>(mixer.rate());
    config.periodSizeInFrames = static_cast<ma_uint32>(std::max(32, options.period));
    config.pUserData = impl.get();
    config.dataCallback = [](ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(device->pUserData);
        self->mixer->render(std::span(static_cast<float*>(output), static_cast<std::size_t>(frames) * 2));
        self->frames.fetch_add(frames, std::memory_order_relaxed);
    };
    if (ma_device_init(&impl->context, &config, &impl->device) != MA_SUCCESS) return std::unexpected(std::string("the output device would not open"));
    impl->device_open = true;
    if (ma_device_start(&impl->device) != MA_SUCCESS) return std::unexpected(std::string("the output device would not start"));
    return Device(std::move(impl));
}

std::string Device::backend() const { return ma_get_backend_name(impl_->context.backend); }

std::int64_t Device::frames() const noexcept { return impl_->frames.load(std::memory_order_relaxed); }

}  // namespace omui::audio
