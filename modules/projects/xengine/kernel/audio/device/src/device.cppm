// xengine.audio.device — a mixer heard: the system's default output pulling
// blocks from an xengine.audio Mixer on its own audio thread (a program plays
// into the mixer from any one thread meanwhile). `null` opens miniaudio's
// null backend instead: the device's clock runs at real time and nothing
// sounds — a headless program, a test.
export module xengine.audio.device;

import std;
import xengine.audio;

export namespace xengine::audio {

struct DeviceOptions {
    bool         null = false;   // no sound: the null backend
    std::int32_t period = 256;   // frames the device asks for at once (its latency)
};

// What an owner renders, handed to a device: a ring of stereo frames one thread fills (`push`) and the device's
// thread drains (`pull`), nothing locked or allocated after it is made. For sources that are the owner's alone —
// a place's sounds moved by a game's step (`xengine.audio.space`) — the owner renders its mixer on its own thread,
// keeps the feed `lead` frames ahead, and the device plays exactly the samples an offline render makes. What the
// device asks for and the feed has not got plays as silence and is counted (`starved`).
class Feed {
public:
    explicit Feed(std::int32_t rate, std::int32_t capacity = 16384);  // frames a second; frames it can hold
    Feed(const Feed&) = delete;
    Feed& operator=(const Feed&) = delete;

    // The owner's side: frames held now; how many of `frames` (interleaved stereo) it took.
    [[nodiscard]] std::int32_t queued() const noexcept;
    std::int32_t push(std::span<const float> frames) noexcept;
    // Renders `mixer` a block at a time until `lead` frames are held (at most the capacity); frames rendered.
    std::int32_t fill(Mixer& mixer, std::int32_t lead) noexcept;

    // The device's side: `out` (interleaved stereo) filled, silence past what is held.
    void pull(std::span<float> out) noexcept;
    [[nodiscard]] std::int64_t starved() const noexcept { return starved_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::int32_t rate() const noexcept { return rate_; }
    [[nodiscard]] std::int32_t capacity() const noexcept { return capacity_; }

private:
    std::int32_t               rate_, capacity_;
    std::vector<float>         ring_;
    std::array<float, 2 * block_frames> block_{};
    std::atomic<std::uint64_t> written_{0}, read_{0}, starved_{0};
};

class Device {
public:
    // Opens the output at the mixer's rate, stereo, and starts it; why not, when it cannot.
    [[nodiscard]] static std::expected<Device, std::string> open(Mixer& mixer, DeviceOptions options = {});
    // Opens the output at the feed's rate, playing what its owner pushes (the feed outlives the device).
    [[nodiscard]] static std::expected<Device, std::string> open(Feed& feed, DeviceOptions options = {});
    // Whether `null` can open here: miniaudio's null backend runs a thread of its own, and a build without
    // threads (the Web's) has none — a headless program there plays into a Mixer or a Feed offline instead.
    [[nodiscard]] static bool has_null() noexcept;
    Device(Device&&) noexcept;
    Device& operator=(Device&&) noexcept;
    ~Device();  // stops and closes it

    [[nodiscard]] std::string backend() const;      // "PulseAudio", "WASAPI", "Core Audio", "Null"…
    [[nodiscard]] std::int64_t frames() const noexcept;  // pulled from the mixer so far

private:
    struct Impl;
    explicit Device(std::unique_ptr<Impl> impl) noexcept;
    [[nodiscard]] static std::expected<Device, std::string> start(std::unique_ptr<Impl> impl, std::int32_t rate, DeviceOptions options);
    std::unique_ptr<Impl> impl_;
};

}  // namespace xengine::audio
