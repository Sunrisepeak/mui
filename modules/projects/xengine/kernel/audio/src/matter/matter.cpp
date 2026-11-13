module xengine.audio.matter;

import std;

namespace xengine::audio::matter {

namespace {

namespace substance = xengine::matter;

// What a material's sound is made from: as it says (`ring`, `damping`), or from its phase and density.
struct Voice {
    float ring = 0;      // Hz; 0: none of its own
    float decay = 0.2f;  // seconds its ring takes to die
    float body = 800;    // Hz: where its noise sits (heavier: lower)
    bool  loose = false; // a powder or a liquid: no ring, a soft burst
};
Voice voice_of(const substance::Program& program, std::uint32_t material) {
    Voice voice;
    const substance::Phase phase = program.phase(material);
    const auto density_column = program.column("density"), ring_column = program.column("ring"), damping_column = program.column("damping");
    const float density = density_column ? static_cast<float>(program.number(material, *density_column)) : 1000.0f;
    voice.loose = phase != substance::Phase::solid && phase != substance::Phase::soft;
    // Where its noise sits: 3 kHz for what is as light as air down to 300 Hz for what is as heavy as iron.
    voice.body = std::clamp(30000.0f / std::sqrt(std::max(density, 1.0f)), 300.0f, 3000.0f);
    float ring = ring_column ? static_cast<float>(program.number(material, *ring_column)) : 0.0f;
    float damping = damping_column ? static_cast<float>(program.number(material, *damping_column)) : 8.0f;
    // A solid that does not say: the denser the lower, and neither a bell nor a sack.
    if (!ring_column && !voice.loose) ring = std::clamp(9000.0f / std::sqrt(std::max(density, 1.0f)), 80.0f, 2000.0f);
    if (voice.loose) ring = 0;
    voice.ring = ring;
    damping = std::clamp(damping, 1.0f, 16.0f);
    voice.decay = 1.6f / (damping * damping) + 0.03f;  // 1.63 s for a bell … 0.036 s for a sack
    return voice;
}

constexpr std::array<Recipe, 7> recipes{{
    {"land", Shape::strike, 0.8f},
    {"strike", Shape::strike, 1.0f},
    {"shatter", Shape::crumble, 1.0f},
    {"ignite", Shape::sizzle, 0.5f},
    {"boil", Shape::sizzle, 0.45f},
    {"freeze", Shape::strike, 0.35f},
    {"melt", Shape::sizzle, 0.3f},
}};

}  // namespace

std::span<const Recipe> standard_recipes() noexcept { return recipes; }

Sound sound_of(const substance::Program& program, std::uint32_t material, const Recipe& recipe) {
    Sound sound;
    if (material >= program.materials().size()) return sound;
    const Voice voice = voice_of(program, material);
    sound.name = std::format("matter/{}/{}", recipe.event, program.materials()[material].name);
    sound.gain = recipe.gain;
    const std::uint32_t seed = 1 + material * 7 + static_cast<std::uint32_t>(recipe.shape) * 131;
    switch (recipe.shape) {
        case Shape::strike: {
            // The touch: a short burst where the thing's noise sits.
            sound.layers.push_back({.wave = Wave::noise, .length = voice.loose ? 0.12f : 0.035f, .envelope = {.attack = 0.001f, .decay = voice.loose ? 0.1f : 0.03f, .release = 0.005f},
                                    .filter = FilterKind::bandpass, .cutoff = {voice.body * 1.6f, voice.body * 0.8f}, .q = voice.loose ? 0.6f : 1.2f, .gain = voice.loose ? 0.8f : 0.5f, .seed = seed});
            if (voice.ring > 0) {
                // Its ring and an overtone that dies sooner (a bar's second mode is 2.76 times its first).
                sound.layers.push_back({.wave = Wave::sine, .length = voice.decay + 0.02f, .pitch = {voice.ring, voice.ring}, .envelope = {.attack = 0.001f, .decay = voice.decay, .release = 0.02f}, .gain = 0.7f});
                sound.layers.push_back({.wave = Wave::sine, .length = voice.decay * 0.5f + 0.02f, .pitch = {voice.ring * 2.76f, voice.ring * 2.76f},
                                        .envelope = {.attack = 0.001f, .decay = voice.decay * 0.5f, .release = 0.02f}, .gain = 0.3f});
            }
            break;
        }
        case Shape::crumble: {
            sound.layers.push_back({.wave = Wave::noise, .length = 0.35f, .envelope = {.attack = 0.002f, .decay = 0.3f, .release = 0.03f}, .filter = FilterKind::lowpass,
                                    .cutoff = {voice.body * 4.0f, voice.body * 0.6f}, .gain = 0.9f, .drive = 0.3f, .seed = seed});
            if (voice.ring > 0)
                sound.layers.push_back({.wave = Wave::triangle, .length = voice.decay * 0.4f + 0.05f, .pitch = {voice.ring, voice.ring * 0.7f},
                                        .envelope = {.attack = 0.001f, .decay = voice.decay * 0.4f, .release = 0.03f}, .gain = 0.4f});
            break;
        }
        case Shape::sizzle: {
            sound.layers.push_back({.wave = Wave::noise, .length = 0.5f, .envelope = {.attack = 0.04f, .hold = 0.1f, .decay = 0.3f, .release = 0.06f}, .filter = FilterKind::highpass,
                                    .cutoff = {voice.body * 2.0f, voice.body * 3.0f}, .gain = 0.5f, .seed = seed});
            break;
        }
    }
    return sound;
}

std::expected<Sounds, std::string> Sounds::make(const substance::Program& program, Mixer& mixer, std::span<const Recipe> given) {
    Sounds sounds;
    sounds.mixer_ = &mixer;
    sounds.materials_ = program.materials().size();
    for (const Recipe& recipe : given) {
        if (recipe.event.empty() || std::ranges::find(sounds.events_, recipe.event) != sounds.events_.end())
            return std::unexpected(std::format("a recipe for the event `{}`: each event once, by its name", recipe.event));
        sounds.events_.push_back(recipe.event);
        for (std::uint32_t material = 0; material < sounds.materials_; ++material) sounds.sounds_.push_back(mixer.add(sound_of(program, material, recipe)));
    }
    return sounds;
}

std::int32_t Sounds::sound(std::string_view event, std::uint32_t material) const noexcept {
    const auto found = std::ranges::find(events_, event);
    if (found == events_.end() || material >= materials_) return -1;
    return sounds_[static_cast<std::size_t>(found - events_.begin()) * materials_ + material];
}

bool Sounds::hear(const Heard& heard) noexcept {
    const std::int32_t index = sound(heard.event, heard.material);
    if (index < 0 || !mixer_) return false;
    const float strength = std::clamp(heard.strength, 0.0f, 1.0f);
    if (strength == 0) return false;
    // Harder is louder, and a little brighter.
    return mixer_->play(index, heard.at, strength, std::clamp(heard.pan, -1.0f, 1.0f), 0.94f + 0.12f * strength);
}

}  // namespace xengine::audio::matter
