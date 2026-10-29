// openmui.video:card — the A/V sync and colour card: a test signal for a
// player, an encoder or a pipeline, and the frames a video's tests and tools
// use when no view is at hand.
//
//   bars       the seven 75% colour bars, the reversed castellations under
//              them, an eight-step grey ramp (white to black)
//   sweep      a line that crosses the bars once a second, a step a frame: a
//              dropped or repeated frame shows as a jump or a stall
//   counter    the timecode (HH:MM:SS:FF, FF counted from the frame the
//              second begins in) and the frame number
//   flash      the box at the bottom right turns white for the frame each
//              second begins in — and `card_track` beeps (1 kHz, −6 dBFS)
//              for exactly that frame's samples, from its first sample: on a
//              player that keeps them together the flash and the beep agree
//
// Integers only, so every frame is the same bytes everywhere.
export module openmui.video:card;

import std;
import openmui.image;
import :timing;
import :encode;

export namespace omui::video {

// Whether frame `index` is the one a second begins in.
[[nodiscard]] bool card_flash(const Timing& timing, std::int64_t index) noexcept;
// Frame `index` drawn into `image` (its size; made opaque, straight).
void draw_card(imageio::Image& image, const Timing& timing, std::int64_t index);

class Card final : public FrameSource {
public:
    explicit Card(const Format& format);
    [[nodiscard]] std::expected<imageio::View, std::string> frame(std::int64_t index) override;

private:
    Timing         timing_;
    imageio::Image image_;
};

// The card's sound: the beeps, `format.samples()` long, format.channels
// interleaved (none: empty).
[[nodiscard]] std::vector<float> card_track(const Format& format);

}  // namespace omui::video
