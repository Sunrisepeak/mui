// The device on the null backend: it opens at the mixer's rate, pulls the
// mixer at real time on its own thread while this one plays into it, and
// closes cleanly.
import std;
import meng.audio;
import meng.audio.device;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace audio = meng::audio;

UTest cases{"audio.device", [] {

    "on the null backend the device pulls the mixer at real time while this thread plays into it"_test = [] {
        if (!audio::Device::has_null()) skip("no null audio backend in this build (no threads: the Web's) — Device::has_null()");
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

    "a feed kept ahead by its lead, on one clock that falls behind: short exactly by what a gap asked past what it held"_test = [] {
        // The device and its owner on a clock of frames, no real time: the device asks a period each period, the owner
        // tops the feed up to the lead every 5 ms. Once, the owner wakes late, or the device's thread does and asks its
        // periods back to back (red 129's case). The device is short only in a gap between two fills that asked more
        // than the fill left held, by exactly that much — the same count on every machine (red 140).
        audio::Mixer mixer;
        constexpr std::int32_t lead = 4096, period = 256;
        const std::int64_t rate = mixer.rate(), tick = rate / 200, end = 2 * rate;
        // `owner_late`: the owner's one late wake (frames past its 5 ms); `device_late`: the device's one stall, every
        // period due meanwhile asked as it ends. Answers what the feed counted and what the gaps say it must have.
        const auto run = [&](std::int64_t owner_late, std::int64_t device_late) {
            const std::int64_t late_at = 40 * tick + tick / 2;  // where the one stall begins
            std::vector<std::int64_t> fills, pulls;
            bool slept = false;
            for (std::int64_t at = tick; at < end; at += tick) {
                if (!slept && at > late_at) at += owner_late, slept = true;  // and on from where it woke
                fills.push_back(at);
            }
            for (std::int64_t due = 0; due < end; due += period) pulls.push_back(due >= late_at && due < late_at + device_late ? late_at + device_late : due);
            audio::Feed feed(static_cast<std::int32_t>(rate));
            feed.fill(mixer, lead);  // ahead before the device starts
            std::array<float, 2 * period> out{};
            std::int64_t expected = 0, held = feed.queued(), asked = 0;
            std::size_t next_fill = 0, next_pull = 0;
            while (next_pull < pulls.size()) {
                // Ties go to the device: the harder order for the feed.
                if (next_fill < fills.size() && fills[next_fill] < pulls[next_pull]) {
                    expected += std::max<std::int64_t>(0, asked - held);
                    feed.fill(mixer, lead);
                    held = feed.queued(), asked = 0;
                    ++next_fill;
                } else {
                    feed.pull(out);
                    asked += period;
                    ++next_pull;
                }
            }
            expected += std::max<std::int64_t>(0, asked - held);
            return std::pair{feed.starved(), expected};
        };
        // The lead covers a gap between two fills (or a burst of the device's) of its 4096 frames less a block and a
        // period: 3712 frames, 77 ms at 48 kHz — an owner that wakes 72 ms late. 150 ms is past it, either way.
        const std::int64_t covered = lead - audio::block_frames - period - tick;
        for (const auto& [owner_late, device_late] : std::array<std::pair<std::int64_t, std::int64_t>, 5>{
                 {{0, 0}, {covered, 0}, {0, covered}, {3 * rate / 20, 0}, {0, 3 * rate / 20}}}) {
            const auto [starved, expected] = run(owner_late, device_late);
            if (std::max(owner_late, device_late) <= covered) expect(eq(starved, std::int64_t{0})) << std::format("late {} / {}: none short", owner_late, device_late);
            else expect(gt(expected, std::int64_t{0}) && eq(starved, expected)) << std::format("late {} / {}: {} short, the gaps say {}", owner_late, device_late, starved, expected);
        }
    };

    "on the null backend the device drains a feed its owner keeps ahead"_test = [] {
        if (!audio::Device::has_null()) skip("no null audio backend in this build (no threads: the Web's) — Device::has_null()");
        audio::Mixer mixer;
        audio::Feed feed(mixer.rate());
        constexpr std::int32_t lead = 4096;
        feed.fill(mixer, lead);  // ahead before the device starts
        // The owner tops the feed up to `lead` every 5 ms. Between two of its looks the device can be short only by what it
        // asked past what the feed held at the first — whenever either thread runs late on a shared machine (red 129: the
        // device's thread caught up its periods back to back; red 140: 1280 frames short where the old count allowed 384,
        // which began only once the device was open and judged a window by the lead, not by what the feed held). Counted
        // from the first fill, each look reading the device's count and then what is held, with a period for a request
        // in flight across a look. On an idle machine that allows nothing: the device is never short.
        const std::int64_t period = audio::DeviceOptions{}.period;  // the device's request
        std::int64_t allowed = 0, before = 0, held = feed.queued();
        auto device = audio::Device::open(feed, {.null = true});
        expect(device.has_value()) << (device ? "" : device.error());
        if (!device) return;
        const auto look = [&] {
            const std::int64_t now = device->frames();
            allowed += std::max<std::int64_t>(0, now - before + period - held);
            before = now;
            held = feed.queued();
        };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (device->frames() < 9600 && std::chrono::steady_clock::now() < deadline) {
            feed.fill(mixer, lead);
            look();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const std::int64_t starved = feed.starved();
        look();
        std::println("the null device drained {} frames; {} starved; what the feed held at each look allows {}", device->frames(), starved, allowed);
        expect(ge(device->frames(), std::int64_t{9600}));
        expect(le(starved, allowed)) << "kept ahead, the device is short only where it asked more than the feed held";
    };
}};
