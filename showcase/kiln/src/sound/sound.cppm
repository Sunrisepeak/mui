// Kiln heard.
//
//   auto sound = *kiln::Sound::make();
//   kiln::step(state, input);
//   sound.hear(state);                    // what the world's laws did this frame, from where Mui stands
//   sound.mixer().render(block);          // a host's device, or a test's buffer
//
// Nothing here says what sounds when: a step of the world says what its laws did (a cell of stone became sand by
// the law that shatters; wood's burning rose), and the sound of an event for a material is made from the
// material's own numbers (meng.audio.matter). It is of the presentation, as the picture is: the state is
// read, and the game is the same game with or without it.
export module omui.showcase.kiln.sound;
import std;
export import omui.showcase.kiln.sim;
export import meng.audio.matter;

export namespace kiln {

class Sound {
public:
    [[nodiscard]] static std::expected<Sound, std::string> make();
    // After a step of the game: its world's events of that frame, heard `reach` cells to a side of Mui. How
    // many sounds began. A world that has not stepped since the last call is not heard again (`last` is empty).
    std::uint32_t hear(const State& state);
    [[nodiscard]] meng::audio::Mixer& mixer() noexcept { return *mixer_; }
    [[nodiscard]] std::span<const meng::matter::Event> last() const noexcept { return events_; }

    float reach = 40;  // cells: what is that far to a side is wholly on that side

private:
    std::unique_ptr<meng::audio::Mixer> mixer_;
    std::optional<meng::audio::matter::Sounds> sounds_;
    std::vector<meng::matter::Event> events_;  // a frame's
    std::uint64_t heard_ = 0;  // the world's frame last heard
};

}  // namespace kiln
