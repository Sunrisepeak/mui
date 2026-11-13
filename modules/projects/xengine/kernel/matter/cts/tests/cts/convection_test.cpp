// Warm air rises, as a pack (the foundation plan's §6.15.6, item g): one law over the standard definition — a
// gas is lighter by its warmth — and the lattice weighing a cell by its density as the laws leave it. With it
// heat is carried and not only spread: it goes up, it goes up a shaft and out at an open edge; without it the
// same heat stays where it was made. The standard definition does not have it, and nothing of it changes.
import std;
import xengine.matter.json;
import xengine.cellular.rigid;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
namespace bodies = xengine::cellular::rigid;
namespace verbs = xengine::cellular::verbs;

namespace {

const matter::Program& standard() {
    static const matter::Program program = *matter::standard();
    return program;
}
const matter::Program& convecting() {
    static const matter::Program program = [] {
        const auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path() / "packs" / "convection.matter.omui.json";
        std::ifstream in(file, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        auto pack = matter::read_pack(text, "convection.matter.omui.json");
        expect(fatal(pack.has_value())) << (pack ? "" : pack.error().front().text());
        auto made = matter::standard_with(std::array{*pack});
        expect(fatal(made.has_value())) << (made ? "" : made.error());
        return std::move(*made);
    }();
    return program;
}
constexpr ca::Shape shape{48, 48};

struct Room {
    const matter::Program& program;
    std::unique_ptr<bodies::MatterWorld> world;
    std::uint32_t temperature;
    // A room of air walled with stone; `shaft`: two cells of its roof left out, open to the sky beyond the edge.
    Room(const matter::Program& of, bool shaft) : program(of), world(*bodies::MatterWorld::make(of, {.shape = shape, .tile = {16, 16}, .seed = 3, .fill = "air"})), temperature(*of.channel("temperature")) {
        const std::uint32_t stone = *program.material("stone");
        world->place({0, 0, 48, 8}, stone);
        world->place({0, 40, 48, 8}, stone);
        world->place({0, 0, 8, 48}, stone);
        world->place({40, 0, 8, 48}, stone);
        if (shaft) world->place({23, 0, 2, 8}, *program.material("air"));
    }
    [[nodiscard]] double kelvin(std::uint32_t x, std::uint32_t y) const {
        return world->cells()[static_cast<std::size_t>(temperature) * shape.width * shape.height + y * shape.width + x] / 256.0;
    }
    // The room's air (the 32 × 32 inside): the warmth it holds above a day's, and the row that warmth is at.
    struct Warmth {
        double held = 0, row = 0;
    };
    [[nodiscard]] Warmth warmth() const {
        Warmth all;
        for (std::uint32_t y = 8; y < 40; ++y)
            for (std::uint32_t x = 8; x < 40; ++x) {
                const double above = std::max(0.0, kelvin(x, y) - 293.0);
                all.held += above, all.row += above * y;
            }
        all.row = all.held > 0 ? all.row / all.held : 0;
        return all;
    }
    // All of it, the walls' too: what the world holds above a day's warmth. Less is what left by its edges.
    [[nodiscard]] double held_by_all() const {
        double held = 0;
        for (std::uint32_t y = 0; y < shape.height; ++y)
            for (std::uint32_t x = 0; x < shape.width; ++x) held += std::max(0.0, kelvin(x, y) - 293.0);
        return held;
    }
    void heat_the_floor() { verbs::heat(*world, {24, 37}, 3, 900); }
    void run(int steps) {
        for (int step = 0; step < steps; ++step) world->step();
    }
};

}  // namespace

static UTest cases{"matter.convection", [] {
    "the pack is one law and names no material; the standard definition does not have it"_test = [] {
        expect(eq(convecting().laws().size(), standard().laws().size() + 1));
        expect(convecting().laws().back().name == "warm is lighter" && convecting().laws().back().kind == matter::LawKind::adjust && !convecting().laws().back().recipe);
        expect(eq(convecting().materials().size(), standard().materials().size()));
    };

    "air heated at the floor of a closed room: with the law its warmth goes up to the roof; without it it stays by the floor"_test = [] {
        Room still(standard(), false), moving(convecting(), false);
        still.heat_the_floor(), moving.heat_the_floor();
        const double began = still.warmth().row;
        still.run(240), moving.run(240);
        const Room::Warmth stays = still.warmth(), rises = moving.warmth();
        std::println("a closed room, 240 steps after its floor's air was heated: the warmth's row {:.1f} at first; {:.1f} without the law, {:.1f} with it (the roof is row 8, the floor 39)", began, stays.row, rises.row);
        expect(gt(stays.row, began - 3.0)) << "spreading alone leaves it where it was";
        expect(lt(rises.row, stays.row - 6.0)) << "carried, it is rows higher";
        // Carried or spread, in a closed room it is the same warmth: nothing left.
        expect(lt(std::abs(rises.held - stays.held), stays.held * 0.2)) << std::format("{} against {}", rises.held, stays.held);
    };

    "what the law does not do, measured: with a shaft open to the sky the world loses no more of its warmth — at an edge nothing leaves but by conduction, and air cooler than 512 K is as heavy as the air about it"_test = [] {
        Room still(standard(), true), moving(convecting(), true);
        still.heat_the_floor(), moving.heat_the_floor();
        const double began = still.held_by_all();
        still.run(3000), moving.run(3000);
        // Said as it is. Carried to the roof the warmth is further from the walls and the floor, through which a
        // world gives heat to what is beyond its edges; the shaft's top gives no faster than any other cell of
        // the edge, for a gas does not leave a field. And a density holds sixteenths: a sixteenth for each 256 K
        // is the least the law can say, so air must be past 512 K to be lighter than a day's — the plume rises
        // while it is hot and stops where it has mixed. Both are below this pack: the carrier's edge (the sky's
        // air changing places with the shaft's), and how finely a density is held.
        const double stays = still.held_by_all() / began, leaves = moving.held_by_all() / began;
        std::println("a room with a shaft, 3000 steps on: of the warmth the world held, {:.0f}% is left without the law, {:.0f}% with it", 100 * stays, 100 * leaves);
        expect(lt(stays, 1.0) && lt(leaves, 1.0)) << "either way some left by the edges";
        expect(gt(leaves, stays)) << "and, as things are, less of it with the law: when this fails the edge or the density has changed, and the case is to be rewritten";
    };

    "the same world twice is the same bits, with the law as without"_test = [] {
        Room one(convecting(), true), other(convecting(), true);
        one.heat_the_floor(), other.heat_the_floor();
        one.run(300), other.run(300);
        expect(std::ranges::equal(one.world->cells(), other.world->cells()));
    };
}};
