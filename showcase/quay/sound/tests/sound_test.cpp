// Quay heard: the game's events become sounds where they happened, heard at the watchkeeper's ears — a warden's shot
// from the right reaches the right ear first and louder; the same shot from behind a container, as far off, is
// fainter and darker (the steel takes its highs); the guide's game, rendered offline twice, is the same samples.
import std;
import omui.showcase.quay.sound;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace audio = xengine::audio;
namespace space = xengine::audio::space;
namespace math = mlibs::math;

namespace {

constexpr std::int32_t block = 800;  // a step's samples at 48 kHz

double rms(std::span<const float> interleaved, std::size_t channel) {
    double sum = 0;
    for (std::size_t frame = 0; 2 * frame + channel < interleaved.size(); ++frame) sum += static_cast<double>(interleaved[2 * frame + channel]) * interleaved[2 * frame + channel];
    return std::sqrt(sum / static_cast<double>(interleaved.size() / 2));
}
// How bright: the sample-to-sample change against the signal (a first difference weighs the highs).
double brightness(std::span<const float> interleaved, std::size_t channel) {
    double change = 0, level = 0;
    for (std::size_t frame = 1; 2 * frame + channel < interleaved.size(); ++frame) {
        const double now = interleaved[2 * frame + channel], before = interleaved[2 * (frame - 1) + channel];
        change += (now - before) * (now - before), level += now * now;
    }
    return level > 0 ? std::sqrt(change / level) : 0;
}
std::size_t onset(std::span<const float> interleaved, std::size_t channel) {
    for (std::size_t frame = 0; 2 * frame + channel < interleaved.size(); ++frame)
        if (std::abs(interleaved[2 * frame + channel]) > 1e-4f) return frame;
    return interleaved.size();
}
// One sound of the set, emitted at `at` in the harbour with the listener at the watchkeeper's start, rendered for
// `seconds`.
std::vector<float> heard_alone(std::string_view name, quay::Vec3 at, float seconds) {
    const quay::State state = quay::start(quay::level::harbour());
    auto sound = quay::Sound::make(state);
    expect(fatal(sound.has_value())) << (sound ? "" : sound.error());
    sound->space().listen({.at = quay::eye(state), .forward = {1, 0, 0}});  // along the quay
    const auto which = sound->sound(name);
    expect(fatal(which.has_value())) << name;
    expect(fatal(sound->space().emit(*which, at, 1, "test").has_value()));
    std::vector<float> out(static_cast<std::size_t>(2 * 48000 * seconds));
    for (std::size_t first = 0; first < out.size(); first += 2 * block) sound->mixer().render(std::span(out).subspan(first, std::min<std::size_t>(2 * block, out.size() - first)));
    return out;
}

}  // namespace

UTest cases{"quay.sound", [] {
    "a warden's shot on the right reaches the right ear first and louder"_test = [] {
        // The watchkeeper at the start looks along the quay (+x); a warden 8 m off to the right (+z), nothing between.
        const quay::Vec3 eye = quay::eye(quay::start(quay::level::harbour()));
        const auto samples = heard_alone("warden-shot", eye + quay::Vec3{0, 0, 8}, 0.6f);
        const double left = rms(samples, 0), right = rms(samples, 1);
        const std::size_t left_first = onset(samples, 0), right_first = onset(samples, 1);
        std::println("a shot 8 m to the right: left {:.1f} dB, right {:.1f} dB; right ear first by {} samples", 20 * std::log10(left), 20 * std::log10(right),
                     static_cast<std::int64_t>(left_first) - static_cast<std::int64_t>(right_first));
        expect(gt(right, left * 1.2)) << "louder on its side";
        expect(lt(right_first, left_first)) << "the near ear first";
    };

    "the same shot behind a container, as far off, is fainter and darker: the steel takes its highs"_test = [] {
        const quay::Vec3 eye = quay::eye(quay::start(quay::level::harbour()));
        // Both 13 m off: one along the open quay, one behind the container at (−20, −6).
        const quay::Vec3 open = eye + quay::Vec3{13, 0, 0}, behind = quay::Vec3{-17, 1.3f, -7};
        expect(lt(std::abs(math::length(open - eye) - math::length(behind - eye)), 0.3f));
        const auto clear = heard_alone("rifle", open, 0.8f), through = heard_alone("rifle", behind, 0.8f);
        const double clear_level = rms(clear, 0) + rms(clear, 1), through_level = rms(through, 0) + rms(through, 1);
        const double clear_bright = brightness(clear, 0) + brightness(clear, 1), through_bright = brightness(through, 0) + brightness(through, 1);
        const space::Reach reached = space::reach({.at = eye, .forward = {1, 0, 0}}, behind, quay::start(quay::level::harbour()).statics->walls);
        std::println("13 m off: in the open {:.1f} dB, brightness {:.3f}; behind the container ({} surfaces) {:.1f} dB, brightness {:.3f}", 20 * std::log10(clear_level), clear_bright, reached.walls,
                     20 * std::log10(through_level), through_bright);
        expect(ge(reached.walls, 2u)) << "in one face and out of the other";
        expect(lt(through_level, clear_level * 0.3)) << "fainter by more than 10 dB";
        expect(lt(through_bright, clear_bright * 0.7)) << "darker";
    };

    "the guide's game heard: its events become sounds, and rendered twice it is the same samples"_test = [] {
        const auto render = [](std::uint64_t frames, std::map<std::string, std::uint32_t>& begun) {
            quay::State state = quay::start(quay::level::harbour());
            auto sound = quay::Sound::make(state);
            expect(fatal(sound.has_value()));
            quay::Guide guide;
            mlibs::hash::Fnv1a hash;
            std::vector<float> out(2 * block);
            double loudest = 0;
            while (state.frame < frames) {  // past the end too: a finished game's steps only count frames, and the beacon is heard
                quay::step(state, guide(state));
                sound->hear(state);
                sound->mixer().render(out);
                for (const space::Heard& one : sound->space().heard())
                    if (one.arrived) ++begun[std::string(one.name)];
                for (const float sample : out) hash.f32(sample), loudest = std::max(loudest, static_cast<double>(std::abs(sample)));
            }
            return std::pair{hash.value(), loudest};
        };
        std::map<std::string, std::uint32_t> first_heard, second_heard;
        const auto [first, loudest] = render(60 * 30, first_heard);
        const auto [second, unused] = render(60 * 30, second_heard);
        std::string said;
        for (const auto& [name, blocks] : first_heard) said += std::format(" {} ({} blocks)", name, blocks);
        std::println("the guide's game, 30 s, heard:{}; loudest sample {:.3f}; samples {:016x}", said, loudest, first);
        expect(eq(first, second)) << "the same samples";
        for (const std::string_view name : {"rifle", "warden-shot", "step", "servo", "warden-down", "beacon"}) expect(first_heard.contains(std::string(name))) << name;
        expect(lt(loudest, 1.0)) << "nothing clips";
    };
}};
