// The device on the null backend: it opens at the mixer's rate, pulls the
// mixer at real time on its own thread while this one plays into it, and
// closes cleanly.
import std;
import xengine.audio;
import xengine.audio.device;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace audio = xengine::audio;

UTest cases{"audio.device", [] {

    "on the null backend the device pulls the mixer at real time while this thread plays into it"_test = [] {
        audio::Mixer mixer;
        audio::Sound tick{.name = "tick"};
        tick.layers.push_back({.length = 0.01f, .pitch = {880, 880}});
        const auto id = mixer.add(tick);
        auto device = audio::Device::open(mixer, {.null = true});
        expect(device.has_value()) << (device ? "" : device.error());
        if (!device) return;
        expect(eq(device->backend(), std::string("Null")));
        for (int i = 0; i < 20; ++i) {
            expect(mixer.play(id, mixer.clock()));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((device->frames() < 4800 || mixer.begun() < 20) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        expect(device->frames() >= 4800 && mixer.clock() >= 4800) << std::format("{} frames pulled", device->frames());
        expect(eq(mixer.begun(), std::int64_t{20}));
    };
}};
