// Bodies heard by what they are made of (xengine.audio.matter over xengine.dynamics.objects; the M6.5 design's
// T5.4): a crate dropped onto a floor lands, and the landing is heard as its material's — the same lines of code
// for a wooden crate and an iron one, and they sound different; the same bits twice.
import std;
import xengine.audio.matter;
import xengine.dynamics.objects;
import xengine.matter.json;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace audio = xengine::audio;
namespace heard = xengine::audio::matter;
namespace matter = xengine::matter;
namespace rigid3 = xengine::dynamics::rigid3;

namespace {

std::string pack_text() {
    const auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "packs" / "acoustics.matter.xdef.json";
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

struct Dropped {
    std::vector<float> samples;
    std::uint32_t      landings = 0;
};

// A crate of a material dropped from a metre onto a floor; its landings heard, two seconds rendered.
Dropped drop(const matter::Program& program, std::string_view material) {
    audio::Mixer mixer;
    auto sounds = heard::Sounds::make(program, mixer);
    auto world = rigid3::World::make({});
    auto objects = rigid3::Objects::make(program);
    expect(fatal(sounds.has_value() && world.has_value() && objects.has_value()));
    (void)world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({5, 0.5f, 5}));
    const auto crate = *world->add({.at = {0, 1.5f, 0}}, rigid3::Shape::box({0.3f, 0.3f, 0.3f}));
    (void)objects->add(crate, *program.material(material));
    Dropped out;
    std::vector<matter::Impact> impacts;
    std::vector<float> samples;
    const auto per_step = static_cast<std::size_t>(mixer.rate() / 60);
    for (int step = 0; step < 120; ++step) {
        world->step();
        objects->impacts(*world, impacts);
        out.landings += static_cast<std::uint32_t>(impacts.size());
        (void)sounds->hear(impacts, {.when = mixer.clock()}, 8);  // 8 m/s: as loud as a landing gets
        const std::vector<float> block = audio::render(mixer, static_cast<std::int64_t>(per_step));
        out.samples.insert(out.samples.end(), block.begin(), block.end());
    }
    return out;
}

double energy(std::span<const float> samples) {
    double sum = 0;
    for (const float sample : samples) sum += static_cast<double>(sample) * static_cast<double>(sample);
    return sum;
}
std::uint64_t bits(std::span<const float> samples) {
    mlibs::hash::Fnv1a hash;
    for (const float sample : samples) hash.le(std::bit_cast<std::uint32_t>(sample), 4);
    return hash.value();
}

}  // namespace

UTest cases{"audio.bodies", [] {
    "a wooden crate and an iron one dropped the same way land with their own sounds; the same bits twice"_test = [] {
        auto pack = matter::read_pack(pack_text(), "acoustics.matter.xdef.json");
        expect(fatal(pack.has_value()));
        auto program = matter::standard_with(std::array{*pack});
        expect(fatal(program.has_value())) << (program ? "" : program.error());
        const Dropped wood = drop(*program, "wood"), iron = drop(*program, "iron"), again = drop(*program, "iron");
        std::println("a wooden crate: {} landings, energy {:.4f}; an iron one: {} landings, energy {:.4f}; bits {:016x} and {:016x}", wood.landings, energy(wood.samples), iron.landings,
                     energy(iron.samples), bits(wood.samples), bits(iron.samples));
        expect(gt(wood.landings, 0u) && gt(iron.landings, 0u)) << "both land";
        expect(gt(energy(wood.samples), 0.0) && gt(energy(iron.samples), 0.0)) << "both heard";
        expect(bits(wood.samples) != bits(iron.samples)) << "each material its own landing, with no code of its own";
        expect(eq(bits(again.samples), bits(iron.samples))) << "the same bits twice";
    };
}};
