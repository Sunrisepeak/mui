// Kiln heard (the plan's §6.15, T2.14f): Mui's bomb at the stone wall is a crumble of stone, heard to her right,
// in the frames the wall shatters; the barricade lit is wood's sizzle; a cavern at rest is silent. Nothing in the
// game or in this package says so: the world says what its laws did, and the sounds are made from the materials.
// The game is the same frames heard or not.
import std;
import omui.showcase.kiln.sound;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace audio = xengine::audio;

namespace {

kiln::Input casting_at(const kiln::State& state, float x, float y) {
    kiln::Input input;
    input.player.pressed = 1u << kiln::cast_button;
    input.player.held = input.player.pressed;
    input.player.pointer_x = static_cast<std::int16_t>(x / static_cast<float>(state.world->shape().width) * 32767);
    input.player.pointer_y = static_cast<std::int16_t>(y / static_cast<float>(state.world->shape().height) * 32767);
    return input;
}
struct Take {
    std::vector<float> samples;          // stereo, a pair a frame of sound
    std::vector<std::uint32_t> began;    // sounds begun, a frame of the game
    std::map<std::string, std::uint32_t> events;  // "shatter stone" → how many
    std::map<std::string, std::size_t> first;     // … → the frame of the game it was first said
};
// A frame of the game is 800 frames of sound at 48 kHz.
void play(kiln::State& state, kiln::Sound& sound, Take& take, int frames, const kiln::Input& input = {}) {
    const std::size_t block = static_cast<std::size_t>(sound.mixer().rate() / kiln::steps_per_second) * 2;
    for (int frame = 0; frame < frames; ++frame) {
        kiln::step(state, frame == 0 ? input : kiln::Input{});
        take.began.push_back(sound.hear(state));
        for (const xengine::matter::Event& event : sound.last())
            if (event.name < kiln::matter().event_names().size()) {
                const std::string name = std::format("{} {}", kiln::matter().event_names()[event.name], kiln::matter().materials()[event.material].name);
                ++take.events[name];
                take.first.try_emplace(name, take.began.size() - 1);
            }
        const std::size_t at = take.samples.size();
        take.samples.resize(at + block);
        sound.mixer().render(std::span(take.samples).subspan(at));
    }
}
double energy(std::span<const float> samples, std::size_t channel) {
    double sum = 0;
    for (std::size_t index = channel; index < samples.size(); index += 2) sum += double(samples[index]) * samples[index];
    return sum;
}
std::string said(const Take& take) {
    std::string text;
    for (const auto& [name, count] : take.events) text += std::format("{}{} × {}", text.empty() ? "" : ", ", name, count);
    return text;
}
Take bomb_at_the_wall(std::uint64_t* checksum = nullptr) {
    auto state = *kiln::start(kiln::level::first());
    auto sound = *kiln::Sound::make();
    state.cat.x = 70.5f, state.cat.y = 34.0f, state.cat.spell = kiln::Spell::bomb;
    Take take;
    play(state, sound, take, 10);
    play(state, sound, take, 240, casting_at(state, 77.0f, 30.0f));
    if (checksum) *checksum = kiln::checksum(state);
    return take;
}

}  // namespace

static UTest cases{"kiln.sound", [] {
    "before anything is cast the cavern is not silent: the lava's top is setting, far to Mui's right — and nothing else"_test = [] {
        auto state = *kiln::start(kiln::level::first());
        auto sound = *kiln::Sound::make();
        Take take;
        play(state, sound, take, 120);
        std::println("the cavern, nothing done: {}; left {:.3f} right {:.3f}", said(take), energy(take.samples, 0), energy(take.samples, 1));
        expect(eq(take.events.size(), std::size_t{1}) && take.events.contains("freeze lava")) << said(take);
        expect(gt(energy(take.samples, 1), 4 * energy(take.samples, 0))) << "the pit is the far side of the cavern";
    };
    "a bomb at the wall is heard: stone's crumble as it shatters, to Mui's right; the same bits each time"_test = [] {
        std::uint64_t heard_checksum = 0;
        const Take take = bomb_at_the_wall(&heard_checksum), again = bomb_at_the_wall();
        const std::size_t first = take.first.contains("shatter stone") ? take.first.at("shatter stone") : 0;
        std::println("a bomb at the wall: {}; the wall first shatters at frame {}; left {:.2f} right {:.2f}; hash {:016x}", said(take), first, energy(take.samples, 0), energy(take.samples, 1),
                     audio::hash(take.samples));
        expect(gt(take.events.contains("shatter stone") ? take.events.at("shatter stone") : 0u, 8u));
        expect(ge(first, std::size_t{10}) && le(first, std::size_t{40})) << "not before the bolt lands, then at once";
        expect(gt(energy(take.samples, 1), energy(take.samples, 0))) << "the wall is to her right";
        expect(eq(audio::hash(take.samples), audio::hash(again.samples)));
        // The game unheard: the same frames.
        auto state = *kiln::start(kiln::level::first());
        state.cat.x = 70.5f, state.cat.y = 34.0f, state.cat.spell = kiln::Spell::bomb;
        for (int frame = 0; frame < 10; ++frame) kiln::step(state, {});
        kiln::step(state, casting_at(state, 77.0f, 30.0f));
        for (int frame = 1; frame < 240; ++frame) kiln::step(state, {});
        expect(eq(kiln::checksum(state), heard_checksum)) << "hearing it changed nothing";
    };
    "the barricade lit is wood catching"_test = [] {
        auto state = *kiln::start(kiln::level::first());
        auto sound = *kiln::Sound::make();
        // The barricade: the wood nearest Mui's start, found on the lattice.
        const std::uint32_t wood = *kiln::matter().material("wood"), width = state.world->shape().width;
        std::uint32_t at = 0;
        const auto cells = state.world->cells().first(std::size_t{width} * state.world->shape().height);
        for (std::uint32_t x = static_cast<std::uint32_t>(state.cat.x); x < width && !at; ++x)
            for (std::uint32_t y = 0; y < state.world->shape().height && !at; ++y)
                if (cells[std::size_t{y} * width + x] == wood) at = y * width + x;
        expect(fatal(at != 0u));
        state.cat.spell = kiln::Spell::fire;
        Take take;
        play(state, sound, take, 300, casting_at(state, static_cast<float>(at % width) + 0.5f, static_cast<float>(at / width) + 1.5f));
        std::println("fire at the barricade: {}", said(take));
        expect(gt(take.events.contains("ignite wood") ? take.events.at("ignite wood") : 0u, 3u));
        expect(gt(energy(take.samples, 0) + energy(take.samples, 1), 0.0));
    };
}};
