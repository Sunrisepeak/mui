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

    "a feed hands the owner's render over whole: drained in odd pieces across the ring's end, the same bits as offline; short, silence counted"_test = [] {
        audio::Sound tone{.name = "tone"};
        tone.layers.push_back({.length = 0.2f, .pitch = {440, 660}});
        audio::Mixer offline, owner;
        const auto first = offline.add(tone), second = owner.add(tone);
        expect(offline.play(first, 0) && owner.play(second, 0));
        std::vector<float> expected(2 * 9600);
        offline.render(expected);

        audio::Feed feed(owner.rate(), 1000);  // a small ring: it wraps many times
        std::vector<float> drained;
        std::array<float, 2 * 333> piece{};
        while (drained.size() < expected.size()) {
            feed.fill(owner, 900);
            feed.pull(piece);
            drained.insert(drained.end(), piece.begin(), piece.end());
        }
        drained.resize(expected.size());
        expect(drained == expected) << "the samples drained are the offline render's";
        expect(eq(feed.starved(), std::int64_t{0}));

        audio::Feed empty(owner.rate());
        std::array<float, 2 * 64> out{};
        out.fill(1.0f);
        empty.pull(out);
        expect(std::ranges::all_of(out, [](float sample) { return sample == 0.0f; }) && eq(empty.starved(), std::int64_t{64}));
    };

    "on the null backend the device drains a feed its owner keeps ahead"_test = [] {
        audio::Mixer mixer;
        audio::Feed feed(mixer.rate());
        feed.fill(mixer, 4096);  // ahead before the device starts
        auto device = audio::Device::open(feed, {.null = true});
        expect(device.has_value()) << (device ? "" : device.error());
        if (!device) return;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (device->frames() < 9600 && std::chrono::steady_clock::now() < deadline) {
            feed.fill(mixer, 4096);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::println("the null device drained {} frames; {} starved", device->frames(), feed.starved());
        expect(ge(device->frames(), std::int64_t{9600}));
        expect(lt(feed.starved(), device->frames() / 10)) << "kept ahead, the device is seldom short";
    };
}};
