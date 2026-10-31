// openmui.audio.device — a mixer heard: the system's default output pulling
// blocks from an openmui.audio Mixer on its own audio thread (a program plays
// into the mixer from any one thread meanwhile). `null` opens miniaudio's
// null backend instead: the device's clock runs at real time and nothing
// sounds — a headless program, a test.
export module openmui.audio.device;

import std;
import openmui.audio;

export namespace omui::audio {

struct DeviceOptions {
    bool         null = false;   // no sound: the null backend
    std::int32_t period = 256;   // frames the device asks for at once (its latency)
};

class Device {
public:
    // Opens the output at the mixer's rate, stereo, and starts it; why not, when it cannot.
    [[nodiscard]] static std::expected<Device, std::string> open(Mixer& mixer, DeviceOptions options = {});
    Device(Device&&) noexcept;
    Device& operator=(Device&&) noexcept;
    ~Device();  // stops and closes it

    [[nodiscard]] std::string backend() const;      // "PulseAudio", "WASAPI", "Core Audio", "Null"…
    [[nodiscard]] std::int64_t frames() const noexcept;  // pulled from the mixer so far

private:
    struct Impl;
    explicit Device(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui::audio
