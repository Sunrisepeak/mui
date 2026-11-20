// The feed: a single-producer, single-consumer ring of stereo frames. `written_` and `read_` count frames from
// the start; each side moves only its own count (release) and reads the other's (acquire).
module meng.audio.device;

import std;
import meng.audio;

namespace meng::audio {

Feed::Feed(std::int32_t rate, std::int32_t capacity)
    : rate_(rate), capacity_(std::max(capacity, 2 * block_frames)), ring_(static_cast<std::size_t>(capacity_) * 2, 0.0f) {}

std::int32_t Feed::queued() const noexcept {
    return static_cast<std::int32_t>(written_.load(std::memory_order_relaxed) - read_.load(std::memory_order_acquire));
}

std::int32_t Feed::push(std::span<const float> frames) noexcept {
    const std::uint64_t written = written_.load(std::memory_order_relaxed);
    const auto room = static_cast<std::uint64_t>(capacity_) - (written - read_.load(std::memory_order_acquire));
    const auto taken = static_cast<std::uint64_t>(std::min<std::uint64_t>(frames.size() / 2, room));
    const auto capacity = static_cast<std::uint64_t>(capacity_);
    for (std::uint64_t frame = 0; frame < taken; ++frame) {
        const std::size_t at = static_cast<std::size_t>((written + frame) % capacity) * 2;
        ring_[at] = frames[static_cast<std::size_t>(frame) * 2];
        ring_[at + 1] = frames[static_cast<std::size_t>(frame) * 2 + 1];
    }
    written_.store(written + taken, std::memory_order_release);
    return static_cast<std::int32_t>(taken);
}

std::int32_t Feed::fill(Mixer& mixer, std::int32_t lead) noexcept {
    const std::int32_t target = std::min(lead, capacity_);
    std::int32_t rendered = 0;
    while (queued() + block_frames <= target) {
        mixer.render(std::span<float>(block_));
        rendered += push(block_);
    }
    return rendered;
}

void Feed::pull(std::span<float> out) noexcept {
    const std::uint64_t read = read_.load(std::memory_order_relaxed);
    const std::uint64_t held = written_.load(std::memory_order_acquire) - read;
    const std::uint64_t wanted = out.size() / 2;
    const std::uint64_t given = std::min(held, wanted);
    const auto capacity = static_cast<std::uint64_t>(capacity_);
    for (std::uint64_t frame = 0; frame < given; ++frame) {
        const std::size_t at = static_cast<std::size_t>((read + frame) % capacity) * 2;
        out[static_cast<std::size_t>(frame) * 2] = ring_[at];
        out[static_cast<std::size_t>(frame) * 2 + 1] = ring_[at + 1];
    }
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(given * 2), out.end(), 0.0f);
    read_.store(read + given, std::memory_order_release);
    if (given < wanted) starved_.fetch_add(wanted - given, std::memory_order_relaxed);
}

}  // namespace meng::audio
