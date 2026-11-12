module omui.bench;

import std;
import xengine.audio;
import hanim.timeline;
import xengine.audio.timeline;
import hanim.core;

namespace omui::bench {

namespace {

constexpr std::string_view sounds = R"({"sound": {
  "punch": {"layers": [
    {"wave": "noise", "length": 0.09, "envelope": {"attack": 0.001, "decay": 0.08, "sustain": 0, "release": 0.01}, "filter": {"kind": "lowpass", "cutoff": [4000, 500], "q": 0.8}, "gain": 0.6},
    {"wave": "sine", "length": 0.16, "pitch": [170, 55], "envelope": {"attack": 0.002, "decay": 0.14, "sustain": 0, "release": 0.02}, "drive": 0.3}
  ]},
  "bell": {"layers": [
    {"wave": "sine", "length": 1.2, "pitch": 660, "envelope": {"attack": 0.003, "decay": 1.1, "sustain": 0, "release": 0.05}},
    {"wave": "saw", "length": 0.8, "pitch": 1650, "filter": {"kind": "bandpass", "cutoff": [3000, 1200], "q": 2}, "envelope": {"attack": 0.003, "decay": 0.7, "sustain": 0, "release": 0.05}, "gain": 0.3}
  ]}
}})";

}  // namespace

void add_audio_benchmarks() {
    add("audio/automated-mix", "128 stereo frames: 48 voices, echo and sample-clock gain from an immutable 64-key timeline curve",
        [](State& s) {
            xengine::audio::Mixer mixer({.voices = 48});
            const auto list = *xengine::audio::read_sounds(sounds, "bench.sound.omui.json");
            std::vector<std::int32_t> ids;
            for (const auto& sound : list) ids.push_back(mixer.add(sound));
            mixer.add_effect("sfx", xengine::audio::echo(.09f, .3f, .25f, mixer.rate()));
            hanim::timeline::Clip clip{.length=mlibs::time::from_seconds(3600)};
            hanim::timeline::Property gain{.name="gain"};
            for(int i=0;i<64;++i) gain.keys.key(hanim::seconds(i * 3600. / 63), i%2 ? .9 : .2, hanim::ease::in_out_cubic);
            clip.properties.push_back(std::move(gain));
            const auto automation=xengine::audio::GainAutomation::prepare(clip);
            if(!automation || !mixer.automate_gain("sfx",*automation)) { s.fail("gain preparation/bind failed"); return; }
            std::array<float,2*xengine::audio::block_frames> block{};
            std::int64_t played=0;
            auto fill=[&] { for(auto k=mixer.sounding();k<48;++k,++played) (void)mixer.play(ids[static_cast<std::size_t>(played%2)],mixer.clock(),.5f); };
            for(int i=0;i<64;++i) {fill();mixer.render(block);}
            s.items(xengine::audio::block_frames);
            while(s.next()) {s.pause();fill();s.resume();mixer.render(block);}
            s.counter("voices",48);
            s.note("scope","CPU mix plus sample-accurate keyed gain, 128 stereo frames; setup, voice refill and files excluded. No device latency, GUI or GPU measured.");
        });
    add("audio/mix-48", "a block of 128 frames at 48 kHz with 48 voices sounding (filtered noise, gliding sines, saws), an echo and the limiter (D-86)",
        [](State& s) {
            xengine::audio::Mixer mixer({.voices = 48});
            const auto list = *xengine::audio::read_sounds(sounds, "bench.sound.omui.json");
            std::vector<std::int32_t> ids;
            for (const auto& snd : list) ids.push_back(mixer.add(snd));
            mixer.add_effect("sfx", xengine::audio::echo(0.09f, 0.3f, 0.25f, mixer.rate()));
            std::vector<float> block(2 * xengine::audio::block_frames);
            std::int64_t played = 0;
            auto keep_full = [&] {  // every voice busy: as many sounds asked for as have ended (they begin in the next block)
                for (std::int32_t k = mixer.sounding(); k < 48; ++k, ++played)
                    mixer.play(ids[static_cast<std::size_t>(played % 2)], mixer.clock(), 0.5f, static_cast<float>(played % 5) / 2 - 1);
            };
            for (int i = 0; i < 64; ++i) keep_full(), mixer.render(block);  // warm
            s.items(xengine::audio::block_frames);
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
