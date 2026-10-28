module openmui.bench;

import std;
import openmui.audio;

namespace omui::bench {

namespace {

constexpr std::string_view sounds = R"([sound.punch]
[[sound.punch.layer]]
wave = "noise"
length = 0.09
envelope = { attack = 0.001, decay = 0.08, sustain = 0, release = 0.01 }
filter = { kind = "lowpass", cutoff = [4000, 500], q = 0.8 }
gain = 0.6

[[sound.punch.layer]]
wave = "sine"
length = 0.16
pitch = [170, 55]
envelope = { attack = 0.002, decay = 0.14, sustain = 0, release = 0.02 }
drive = 0.3

[sound.bell]
[[sound.bell.layer]]
wave = "sine"
length = 1.2
pitch = 660
envelope = { attack = 0.003, decay = 1.1, sustain = 0, release = 0.05 }

[[sound.bell.layer]]
wave = "saw"
length = 0.8
pitch = 1650
filter = { kind = "bandpass", cutoff = [3000, 1200], q = 2 }
envelope = { attack = 0.003, decay = 0.7, sustain = 0, release = 0.05 }
gain = 0.3
)";

}  // namespace

void add_audio_benchmarks() {
    add("audio/mix-48", "a block of 128 frames at 48 kHz with 48 voices sounding (filtered noise, gliding sines, saws), an echo and the limiter (D-86)",
        [](State& s) {
            audio::Mixer mixer({.voices = 48});
            const auto list = *audio::read_sounds(sounds, "bench.sound.omui.toml");
            std::vector<std::int32_t> ids;
            for (const auto& snd : list) ids.push_back(mixer.add(snd));
            mixer.add_effect("sfx", audio::echo(0.09f, 0.3f, 0.25f, mixer.rate()));
            std::vector<float> block(2 * audio::block_frames);
            std::int64_t played = 0;
            auto keep_full = [&] {  // every voice busy: as many sounds asked for as have ended (they begin in the next block)
                for (std::int32_t k = mixer.sounding(); k < 48; ++k, ++played)
                    mixer.play(ids[static_cast<std::size_t>(played % 2)], mixer.clock(), 0.5f, static_cast<float>(played % 5) / 2 - 1);
            };
            for (int i = 0; i < 64; ++i) keep_full(), mixer.render(block);  // warm
            s.items(audio::block_frames);
            while (s.next()) {
                s.pause();
                keep_full();
                s.resume();
                mixer.render(block);
            }
            s.counter("voices", 48);
        });
}

}  // namespace omui::bench
