// xengine.audio.space: a sound four times as far is 12 dB down; 34.3 m away it arrives 0.1 s late; from the right
// the right ear hears it first (by the head's width over the speed of sound) and louder; coming nearer at a tenth
// of the speed of sound it is c ÷ (c − v) higher; through a wall it is the wall's `through` down and darker; and
// what was heard is said as data. The same emissions, the same samples.
import std;
import xengine.audio.space;
import mlibs.geom;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace audio = xengine::audio;
namespace space = xengine::audio::space;
namespace geom = mlibs::geom;
namespace math = mlibs::math;

namespace {

constexpr std::int32_t rate = 48000;

// A steady sine of `pitch` Hz for `seconds` (no glide, no envelope to speak of).
audio::Sound tone(float pitch, float seconds) {
    audio::Sound sound{.name = std::format("tone {}", pitch)};
    sound.layers.push_back({.wave = audio::Wave::sine, .length = seconds, .pitch = {pitch, pitch}, .envelope = {.attack = 0.001f, .decay = 0, .sustain = 1, .release = 0.001f}, .gain = 0.25f});
    return sound;
}

struct Rig {
    audio::Mixer                  mixer{audio::Options{.ceiling = 1e30f}};
    std::shared_ptr<space::Space> place;
    std::uint32_t                 sound = 0;
    explicit Rig(const audio::Sound& made, space::Options options = {}) {
        auto built = space::Space::make(mixer, options);
        expect(fatal(built.has_value()));
        place = *built;
        mixer.add_source(place);
        sound = place->add(made);
    }
    // `frames` more, in blocks of `block`, `between` called before each.
    std::vector<float> run(std::int64_t frames, std::int32_t block = 480, const std::function<void(std::int64_t)>& between = {}) {
        std::vector<float> out(static_cast<std::size_t>(2 * frames));
        for (std::int64_t at = 0; at < frames; at += block) {
            if (between) between(at);
            const auto count = std::min<std::int64_t>(block, frames - at);
            mixer.render(std::span(out).subspan(static_cast<std::size_t>(2 * at), static_cast<std::size_t>(2 * count)));
        }
        return out;
    }
};

double rms(std::span<const float> interleaved, std::size_t channel, std::size_t first, std::size_t last) {
    double sum = 0;
    for (std::size_t frame = first; frame < last; ++frame) sum += static_cast<double>(interleaved[2 * frame + channel]) * interleaved[2 * frame + channel];
    return std::sqrt(sum / static_cast<double>(last - first));
}
std::size_t onset(std::span<const float> interleaved, std::size_t channel, float above = 1e-4f) {
    for (std::size_t frame = 0; 2 * frame + channel < interleaved.size(); ++frame)
        if (std::abs(interleaved[2 * frame + channel]) > above) return frame;
    return interleaved.size();
}
// Rising zero crossings a second over [first, last).
double pitch_of(std::span<const float> interleaved, std::size_t channel, std::size_t first, std::size_t last) {
    std::vector<double> crossings;
    for (std::size_t frame = first + 1; frame < last; ++frame) {
        const float before = interleaved[2 * (frame - 1) + channel], now = interleaved[2 * frame + channel];
        if (before < 0 && now >= 0) crossings.push_back(static_cast<double>(frame - 1) + before / (before - now));
    }
    if (crossings.size() < 2) return 0;
    return static_cast<double>(crossings.size() - 1) / (crossings.back() - crossings.front()) * rate;
}

}  // namespace

UTest cases{"xengine.audio.space", [] {
    "four times as far is 12 dB down, and 34.3 m away it arrives 0.1 s late"_test = [] {
        const auto level_at = [](float distance) {
            Rig rig(tone(500, 0.5f));
            expect(rig.place->emit(rig.sound, {0, 0, -distance}).has_value());
            const auto out = rig.run(rate / 2);
            return rms(out, 0, rate / 8, rate / 2 - rate / 8);
        };
        const double near = level_at(1), far = level_at(4);
        const double down = 20 * std::log10(far / near);
        std::println("a 500 Hz tone ahead at 1 m and at 4 m: {:.2f} dB apart", down);
        expect(lt(std::abs(down + 12.04), 0.1)) << down;

        Rig rig(tone(500, 0.2f));
        expect(rig.place->emit(rig.sound, {0, 0, -34.3f}).has_value());
        const auto out = rig.run(rate / 2);
        const std::size_t heard = std::min(onset(out, 0), onset(out, 1));
        std::println("34.3 m ahead: first heard at sample {} ({:.4f} s)", heard, static_cast<double>(heard) / rate);
        expect(ge(heard, std::size_t{4795}) && le(heard, std::size_t{4810})) << heard << " against 4,800";
    };

    "from the right: the right ear first, by the head's width over the speed of sound, and louder; heard at 90 degrees"_test = [] {
        Rig rig(tone(3000, 0.3f));
        expect(rig.place->emit(rig.sound, {5, 0, 0}, 1, "a door to the right").has_value());
        std::vector<space::Heard> seen;
        const auto out = rig.run(rate / 4, 480, [&](std::int64_t at) {
            if (at == 4800) seen.assign(rig.place->heard().begin(), rig.place->heard().end());
        });
        const std::size_t left = onset(out, 0, 1e-5f), right = onset(out, 1, 1e-5f);
        const double expected = 2 * 0.0875 / 343 * rate;
        std::println("from the right: the right ear at {}, the left at {} ({} samples, by the head {:.1f}); levels {:.4f} and {:.4f}", right, left, left - right, expected,
                     rms(out, 1, rate / 8, rate / 4 - 1), rms(out, 0, rate / 8, rate / 4 - 1));
        expect(lt(right, left) && le(std::abs(static_cast<double>(left - right) - expected), 3.0));
        expect(gt(rms(out, 1, rate / 8, rate / 4 - 1), rms(out, 0, rate / 8, rate / 4 - 1) * 1.2)) << "the far ear in the head's shade";
        expect(fatal(eq(seen.size(), std::size_t{1})));
        std::println("heard: {} ({}) at {:.1f} m, azimuth {:.1f}, elevation {:.1f}, {:.1f} dB left and {:.1f} dB right", seen[0].name, seen[0].cause, seen[0].distance, seen[0].azimuth,
                     seen[0].elevation, seen[0].left, seen[0].right);
        expect(lt(std::abs(seen[0].azimuth - 90.0f), 0.01f) && lt(std::abs(seen[0].elevation), 0.01f) && seen[0].arrived && gt(seen[0].right, seen[0].left));
        expect(seen[0].cause == std::string_view("a door to the right"));
    };

    "coming nearer at a tenth of the speed of sound it is c over (c - v) higher, going away c over (c + v) lower"_test = [] {
        const auto measured = [](float toward) {
            Rig rig(tone(1000, 3));
            const float speed = 34.3f * toward;
            const float from = toward > 0 ? -80.0f : -5.0f;
            const auto id = rig.place->emit(rig.sound, {0, 0, from}).value();
            float doppler = 0;
            const auto out = rig.run(rate * 2, 480, [&](std::int64_t at) {
                rig.place->move(id, {0, 0, from + speed * static_cast<float>(at) / rate});
                if (at == rate) doppler = rig.place->heard()[0].doppler;
            });
            return std::pair{pitch_of(out, 0, rate / 2, rate + rate / 2), doppler};
        };
        const auto [nearer, said_nearer] = measured(1);
        const auto [away, said_away] = measured(-1);
        std::println("1 kHz coming nearer at 34.3 m/s: {:.2f} Hz (said {:.4f}); going away: {:.2f} Hz (said {:.4f}); exact {:.2f} and {:.2f}", nearer, said_nearer, away, said_away, 1000 / 0.9,
                     1000 / 1.1);
        expect(lt(std::abs(nearer - 1000 / 0.9), 1.0) && lt(std::abs(said_nearer - 1 / 0.9f), 0.002f));
        expect(lt(std::abs(away - 1000 / 1.1), 1.0) && lt(std::abs(said_away - 1 / 1.1f), 0.002f));
    };

    "through a wall it is the wall's share down and darker, and said to have come through one"_test = [] {
        const geom::Mesh panel = geom::sheet(20, 20, 2, 2);  // in y = 0, facing up
        const geom::Bvh tree = geom::bvh_of(panel);
        // Turned to stand between the listener and a sound 4 m ahead, at z = −2.
        const std::array<space::Wall, 1> wall{{{.mesh = &panel, .tree = &tree, .place = math::translation({0, 0, -2}) * math::rotation(math::Quat::about({1, 0, 0}, 1.5707964f)), .through = 0.1f}}};
        const auto level = [&](bool walled, float pitch) {
            Rig rig(tone(pitch, 0.5f));
            if (walled) rig.place->walls(wall);
            expect(rig.place->emit(rig.sound, {0, 0, -4}).has_value());
            std::uint32_t walls = 0;
            const auto out = rig.run(rate / 2, 480, [&](std::int64_t at) {
                if (at == 9600) walls = rig.place->heard()[0].walls;
            });
            return std::pair{rms(out, 0, rate / 8, rate / 2 - rate / 8), walls};
        };
        const auto [open_low, none] = level(false, 200);
        const auto [walled_low, one] = level(true, 200);
        const auto [open_high, none_high] = level(false, 6000);
        const auto [walled_high, one_high] = level(true, 6000);
        const double low_down = 20 * std::log10(walled_low / open_low), high_down = 20 * std::log10(walled_high / open_high);
        std::println("through a wall: 200 Hz {:.1f} dB, 6 kHz {:.1f} dB (the wall's share −20 dB)", low_down, high_down);
        expect(eq(none, 0u) && eq(one, 1u) && eq(none_high, 0u) && eq(one_high, 1u));
        expect(lt(std::abs(low_down + 20), 0.5)) << "the low tone: the wall's share";
        expect(lt(high_down, low_down - 3)) << "the high one darker still";
        // What a game's own ears are told, by the same rule: 4 m (−12.04 dB) through one wall (−20 dB).
        const space::Reach reached = space::reach({}, {0, 0, -4}, wall);
        expect(eq(reached.walls, 1u) && lt(std::abs(reached.level + 32.04f), 0.01f) && lt(std::abs(reached.azimuth), 0.01f) && eq(reached.bright, 1000.0f)) << reached.level;
    };

    "the same emissions are the same samples; heard is a block's own"_test = [] {
        const auto once = [] {
            Rig rig(tone(700, 0.4f));
            const auto id = rig.place->emit(rig.sound, {3, 1, -6}, 0.8f, "step").value();
            (void)rig.place->emit(rig.sound, {-2, 0, 2}, 0.5f, "echo").value();
            return audio::hash(rig.run(rate / 2, 512, [&](std::int64_t at) { rig.place->move(id, {3 - static_cast<float>(at) / rate * 4, 1, -6}); }));
        };
        const std::uint64_t first = once(), second = once();
        std::println("two sounds, one moving: {:016x}", first);
        expect(eq(first, second));
        Rig rig(tone(700, 0.05f));
        expect(rig.place->emit(rig.sound, {0, 0, -1}).has_value());
        (void)rig.run(rate / 4);
        expect(eq(rig.place->sounding(), std::size_t{0})) << "gone when all of it has passed";
        expect(!rig.place->emit(99, {0, 0, 0}).has_value());
    };
}};
