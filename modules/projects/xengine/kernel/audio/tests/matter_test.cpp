// What matter sounds like, made from what it is (xengine.audio.matter; the foundation plan's §6.15, T2.14f,
// D-183): the same event for two materials is two sounds, by their numbers and no code of theirs; a material that
// says nothing of sound still sounds; what is heard is the same bits wherever it is rendered.
import std;
import xengine.audio.matter;
import xengine.matter.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace audio = xengine::audio;
namespace heard = xengine::audio::matter;
namespace matter = xengine::matter;

namespace {

std::string pack_text() {
    const auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "packs" / "acoustics.matter.omui.json";
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
const matter::Program& with_acoustics() {
    static const matter::Program program = [] {
        auto pack = matter::read_pack(pack_text(), "acoustics.matter.omui.json");
        expect(fatal(pack.has_value())) << (pack ? "" : pack.error().front().text());
        auto made = matter::standard_with(std::array{*pack});
        expect(fatal(made.has_value())) << (made ? "" : made.error());
        return std::move(*made);
    }();
    return program;
}
// One event of one material, alone, rendered for a second and a half.
std::vector<float> rendered(const matter::Program& program, std::string_view event, std::string_view material, float strength = 1.0f) {
    audio::Mixer mixer;
    auto sounds = heard::Sounds::make(program, mixer);
    expect(fatal(sounds.has_value())) << (sounds ? "" : sounds.error());
    expect(sounds->hear({.event = event, .material = *program.material(material), .strength = strength, .at = 0}));
    return audio::render(mixer, mixer.rate() * 3 / 2);
}
double energy(std::span<const float> samples, std::size_t from = 0, std::size_t to = std::numeric_limits<std::size_t>::max()) {
    double sum = 0;
    for (std::size_t index = from; index < std::min(to, samples.size()); ++index) sum += static_cast<double>(samples[index]) * samples[index];
    return sum;
}
// The frequency a sound is strongest at, among a few: a sum against a sine and a cosine at each (Goertzel's way).
double strength_at(std::span<const float> samples, double hertz, double rate) {
    double real = 0, imaginary = 0;
    for (std::size_t frame = 0; frame * 2 < samples.size(); ++frame) {
        const double angle = 2.0 * std::numbers::pi * hertz * static_cast<double>(frame) / rate;
        real += samples[frame * 2] * std::cos(angle), imaginary += samples[frame * 2] * std::sin(angle);
    }
    return std::sqrt(real * real + imaginary * imaginary);
}

}  // namespace

static UTest cases{"audio.matter", [] {
    "a thing lands: iron rings high and long, wood lower and short, sand does not ring — by their numbers alone"_test = [] {
        const matter::Program& program = with_acoustics();
        const auto iron = rendered(program, "land", "iron"), wood = rendered(program, "land", "wood"), sand = rendered(program, "land", "sand");
        expect(audio::hash(iron) != audio::hash(wood) && audio::hash(wood) != audio::hash(sand));
        // Each rings at the pitch it says.
        expect(gt(strength_at(iron, 880, 48000), 8.0 * strength_at(iron, 330, 48000)) && gt(strength_at(wood, 330, 48000), 8.0 * strength_at(wood, 880, 48000)));
        // Iron is still sounding after a quarter of a second; wood and sand are not.
        const std::size_t quarter = 24000;  // a quarter of a second of stereo samples
        std::println("after 0.25 s — iron {:.4f}, wood {:.6f}, sand {:.6f} of energy left", energy(iron, quarter), energy(wood, quarter), energy(sand, quarter));
        expect(gt(energy(iron, quarter), 1.0) && lt(energy(wood, quarter), 1e-6) && lt(energy(sand, quarter), 1e-6));
        // Sand has no ring of its own: it is one layer, a burst of noise, where iron is the burst and two tones.
        expect(eq(heard::sound_of(program, *program.material("sand"), heard::standard_recipes()[0]).layers.size(), std::size_t{1}) &&
               eq(heard::sound_of(program, *program.material("iron"), heard::standard_recipes()[0]).layers.size(), std::size_t{3}) && gt(energy(sand), 0.01));
        // Harder is louder.
        expect(gt(energy(rendered(program, "land", "iron", 1.0f)), 3.0 * energy(rendered(program, "land", "iron", 0.4f))));
    };
    "a material that says nothing of sound still sounds: from its phase and its density; an event with no recipe is not heard"_test = [] {
        const matter::Program plain = *matter::standard();  // no pack: no `ring`, no `damping`
        expect(!plain.column("ring").has_value());
        const auto iron = rendered(plain, "land", "iron"), water = rendered(plain, "land", "water");
        expect(gt(energy(iron), 0.01) && gt(energy(water), 0.001) && audio::hash(iron) != audio::hash(water));
        audio::Mixer mixer;
        auto sounds = *heard::Sounds::make(plain, mixer);
        expect(eq(sounds.count(), heard::standard_recipes().size() * plain.materials().size()));
        expect(!sounds.hear({.event = "sneeze", .material = 1}) && !sounds.hear({.event = "land", .material = 9999}) && !sounds.hear({.event = "land", .material = 1, .strength = 0}));
        const std::array<heard::Recipe, 2> twice{{{"land", heard::Shape::strike, 1}, {"land", heard::Shape::crumble, 1}}};
        audio::Mixer other;
        expect(!heard::Sounds::make(plain, other, twice).has_value()) << "an event has one recipe";
    };
    "a wall of stone shatters and its rubble lands: a scene of events rendered offline, its bits pinned"_test = [] {
        const matter::Program& program = with_acoustics();
        const auto scene = [&] {
            audio::Mixer mixer;
            auto sounds = *heard::Sounds::make(program, mixer);
            const std::uint32_t stone = *program.material("stone"), sand = *program.material("sand"), wood = *program.material("wood"), iron = *program.material("iron");
            expect(sounds.hear({.event = "shatter", .material = stone, .strength = 1.0f, .pan = 0.2f, .at = 0}));
            for (int grain = 0; grain < 12; ++grain)
                expect(sounds.hear({.event = "land", .material = sand, .strength = 0.3f + 0.05f * static_cast<float>(grain % 5), .pan = -0.5f + 0.09f * static_cast<float>(grain), .at = 9600 + grain * 1700}));
            expect(sounds.hear({.event = "land", .material = iron, .strength = 0.9f, .pan = -0.3f, .at = 30000}));
            expect(sounds.hear({.event = "ignite", .material = wood, .strength = 0.7f, .pan = 0.4f, .at = 40000}));
            return audio::render(mixer, 96000);
        };
        const auto first = scene(), again = scene();
        expect(eq(audio::hash(first), audio::hash(again))) << "the same events, the same bits";
        std::println("the scene: {} samples, hash {:016x}", first.size(), audio::hash(first));
        expect(eq(audio::hash(first), std::uint64_t{0x3546273b7a988dd2})) << std::format("{:016x}", audio::hash(first));
    };
    "a step's events are heard as they are said: forty cells of a wall that shatter are one crumble, from where they were; an event with no recipe is none"_test = [] {
        const matter::Program& program = with_acoustics();
        const std::uint32_t stone = *program.material("stone"), wood = *program.material("wood");
        const auto name = [&](std::string_view event) {
            const auto names = program.event_names();
            return static_cast<std::uint32_t>(std::ranges::find(names, event) - names.begin());
        };
        expect(fatal(name("shatter") < program.event_names().size() && name("ignite") < program.event_names().size()));
        // A lattice 64 wide: a wall at x 48–51 shatters (40 cells), a plank at x 8 catches; one event is unnamed.
        std::vector<matter::Event> events;
        for (std::uint32_t cell = 0; cell < 40; ++cell) events.push_back({.kind = matter::EventKind::became, .instance = (10 + cell / 4) * 64 + 48 + cell % 4, .name = name("shatter"), .from = stone, .material = stone});
        events.push_back({.kind = matter::EventKind::rose, .instance = 20 * 64 + 8, .name = name("ignite"), .material = wood});
        events.push_back({.kind = matter::EventKind::became, .instance = 5, .name = std::numeric_limits<std::uint32_t>::max(), .material = stone});
        const auto heard_from = [&](float ear) {
            audio::Mixer mixer;
            auto sounds = *heard::Sounds::make(program, mixer);
            const std::uint32_t played = sounds.hear(events, {.width = 64, .at = ear, .reach = 24, .when = 0});
            expect(eq(played, 2u)) << "a crumble of stone and a sizzle of wood";
            return audio::render(mixer, 24000);
        };
        const auto middle = heard_from(32), again = heard_from(32);
        expect(eq(audio::hash(middle), audio::hash(again))) << "the same events, the same bits";
        // Stereo, a pair a frame: with the ear by the plank the wall is all to the right.
        const auto side = [](std::span<const float> samples, std::size_t channel) {
            double sum = 0;
            for (std::size_t index = channel; index < samples.size(); index += 2) sum += double(samples[index]) * samples[index];
            return sum;
        };
        const auto by_plank = heard_from(8);
        std::println("a wall shatters at x 48–51: ear at 32 left {:.3f} right {:.3f}; ear at 8 left {:.3f} right {:.3f}", side(middle, 0), side(middle, 1), side(by_plank, 0), side(by_plank, 1));
        expect(gt(side(middle, 1), side(middle, 0)) && gt(side(by_plank, 1), 2 * side(by_plank, 0)));
        audio::Mixer quiet;
        auto none = *heard::Sounds::make(program, quiet);
        expect(eq(none.hear(std::span(events).last(1), {.width = 64}), 0u)) << "unnamed: not heard";
    };
    "what goes on is not a sound a frame, and what lands is as loud as it came down"_test = [] {
        const matter::Program& program = with_acoustics();
        const std::uint32_t wood = *program.material("wood"), sand = *program.material("sand"), iron = *program.material("iron");
        const auto names = program.event_names();
        const std::uint32_t ignite = static_cast<std::uint32_t>(std::ranges::find(names, "ignite") - names.begin());
        audio::Mixer mixer;
        auto sounds = *heard::Sounds::make(program, mixer);
        // Wood catching, cell after cell, every frame of a second (800 samples a frame).
        std::uint32_t began = 0;
        std::vector<float> block(1600);
        for (int frame = 0; frame < 60; ++frame) {
            const std::array<matter::Event, 1> burning{{{.kind = matter::EventKind::rose, .instance = 20, .name = ignite, .material = wood}}};
            began += sounds.hear(burning, {.width = 64, .at = 32, .when = mixer.clock()});
            mixer.render(block);
        }
        std::println("wood catching every frame of a second: {} sounds", began);
        expect(ge(began, 3u) && le(began, 5u)) << "the recipe's `again` (0.3 s), not sixty";
        // Landings: grains that only settle are not heard; one that came down fast is, and iron beside it is its own sound.
        const std::array<matter::Impact, 3> settling{{{.instance = 10, .material = sand, .speed = 1.0f}, {.instance = 11, .material = sand, .speed = 2.0f}, {.instance = 12, .material = sand, .speed = 0.5f}}};
        expect(eq(sounds.hear(settling, {.width = 64, .at = 32, .when = mixer.clock()}), 0u));
        const std::array<matter::Impact, 3> falling{{{.instance = 10, .material = sand, .speed = 1.0f}, {.instance = 11, .material = sand, .speed = 45.0f}, {.instance = 40, .material = iron, .speed = 30.0f}}};
        expect(eq(sounds.hear(falling, {.width = 64, .at = 32, .when = mixer.clock()}), 2u));
        expect(eq(sounds.hear(falling, {.width = 64, .at = 32, .when = mixer.clock()}), 0u)) << "the same instant: once";
    };
}};
