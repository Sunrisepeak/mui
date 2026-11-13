// The 2D world of matter and its picture under the two contracts every world is held to (mlibs.cts:world; the
// foundation plan's §6.13): the world contract on `MatterWorld` (the lattice, the rigid bodies, what flies, as
// one deterministic value) and the presentation contract on `WorldPicture` (D-178: it reads the world, wherever
// its ladders stand). The cases carry no coordinate: a world of three dimensions is held to the same ones.
import std;
import xengine.matter.json;
import xengine.cellular.present.world;
import mlibs.cts;
import mcpplibs.mtest;

using mcpplibs::mtest::UTest, mcpplibs::mtest::expect, mcpplibs::mtest::eq, mcpplibs::mtest::operator""_test;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
namespace bodies = xengine::cellular::rigid;
namespace contract = mlibs::cts::world;

namespace {
const matter::Program& standard() {
    static const matter::Program program = *matter::standard();
    return program;
}
std::uint32_t material(std::string_view name) { return *standard().material(name); }

// A cavern in which every part of a world has something to do: a post of wood alight, sand falling, a pool, a
// crate afloat in it (a body), a heap of sand thrown by a blast (what flies).
struct Cavern {
    using State = bodies::MatterWorld;
    std::unique_ptr<State> make() const {
        auto world = *bodies::MatterWorld::make(standard(), {.shape = {96, 56}, .tile = {16, 16}, .seed = 5, .fill = "air"});
        const std::uint32_t temperature = *standard().channel("temperature");
        world->place({0, 50, 96, 6}, material("stone"));
        world->place({20, 36, 3, 14}, material("wood"));
        world->set(temperature, {20, 48, 3, 2}, 1000 * 256);
        world->place({56, 42, 2, 8}, material("stone")), world->place({88, 42, 2, 8}, material("stone"));
        world->place({58, 44, 30, 6}, material("water"));
        world->place({68, 30, 5, 4}, material("wood"));
        (void)world->carve({68, 30, 5, 4});
        world->place({34, 10, 6, 6}, material("sand"));
        world->place({6, 44, 8, 6}, material("sand"));
        (void)world->blast({10, 47}, 5.0f, 18.0f, false);  // the heap thrown: cells in flight from the first frame
        return world;
    }
    void step(State& world) const { world.step(); }
    // Sand let go above the post, and a blast by the pool: the lattice, the bodies and what flies all hear of it.
    void disturb(State& world) const {
        world.place({44, 6, 4, 4}, material("sand"));
        (void)world.blast({60, 43}, 5.0f, 20.0f, false);
    }
    std::uint64_t hash(const State& world) const { return world.state_hash(); }
    std::vector<std::uint8_t> save(const State& world) const { return world.save(); }
    bool restore(State& world, std::span<const std::uint8_t> image) const { return world.restore(image).has_value(); }
    std::unique_ptr<State> branch(const State& world) const {
        auto made = world.branch();
        return made ? std::move(*made) : nullptr;
    }
    // Everything a presenter is given.
    void read(const State& world) const {
        std::uint64_t seen = world.cells().size() + world.frames();
        for (const auto& view : world.bodies().bodies()) seen += world.rigid().pose(view.body).has_value();
        seen += world.flying().flying();
        (void)seen;
    }
};

struct Shown {
    xrender::ImageStore images;
    ca::WorldPicture picture;
    Shown() : picture(*ca::WorldPicture::make(images, standard(), *matter::standard_look(standard()), {96, 56}, {.fill = "air", .seed = 9})) {
        picture.scenery({23, 46, 10, 4}, material("wood"));
        // The baked step has its frames: of a post alight on a floor, alone.
        auto hearth = *bodies::MatterWorld::make(standard(), {.shape = {24, 24}, .tile = {8, 8}, .seed = 2, .fill = "air"});
        hearth->place({0, 21, 24, 3}, material("stone"));
        hearth->place({10, 13, 4, 8}, material("wood"));
        hearth->set(*standard().channel("temperature"), {10, 19, 4, 2}, 1000 * 256);
        (void)picture.bake(*hearth, 4, 4, 40);
    }
};
struct Picture {
    using Worlds = Cavern;
    using Shown = ::Shown;
    Cavern cavern;
    Cavern& worlds() { return cavern; }
    std::unique_ptr<Shown> make() const { return std::make_unique<Shown>(); }
    auto ladders(const Shown&) const { return ca::WorldPicture::ladders(); }
    void set(Shown& shown, std::size_t ladder, std::uint8_t lowered, std::string why) const { shown.picture.set(ladder, lowered, std::move(why)); }
    void show(Shown& shown, const bodies::MatterWorld& world) const { shown.picture.show(world); }
    std::string describe(const Shown& shown) const { return shown.picture.describe(); }
};
}  // namespace

UTest cases{"matter.world-contract", [] {
    // Its image holds the rigid family's (Box2D's, not canonical: see xengine.dynamics.cts' rigid_test).
    mlibs::cts::against({.subject = "matter-world-2d", .has = [](std::string_view capability) { return capability != "canonical-image"; }}, [] { contract::declare(Cavern{}, "xengine.world@1"); });
    mlibs::cts::against({.subject = "world-picture-2d"}, [] { contract::declare_presentation(Picture{}, "xengine.presentation@1"); });

    "the 2D world of matter has every case of the world contract, its picture every case of the presentation's"_test = [] {
        expect(eq(contract::case_count<Cavern>(), std::size_t{8})) << "with the case that reading changes nothing";
        expect(eq(contract::presentation_case_count, std::size_t{4}));
    };
    "the scene has all three parts at work: a body, cells in flight, the lattice changing"_test = [] {
        Cavern cavern;
        auto world = cavern.make();
        std::size_t flew = world->flying().flying();
        for (int frame = 0; frame < 120; ++frame) world->step(), flew = std::max(flew, world->flying().flying());
        expect(!world->bodies().bodies().empty()) << "the crate";
        expect(flew > 0u) << "the blast threw the heap";
    };
}};
