// A law the world did not have, as the acceptance of laws of motion (the foundation plan's §6.15, T2.14d; D-181,
// D-182): magnetism is a pack — a channel, two properties, four laws, two rows — laid over the standard definition.
// No kernel, carrier or scene knows the word. In a world made of it a crate of iron slides to the lodestone, a
// crate of wood beside it stays, filings fly to the stone and sand does not; and asked why a filing is where it
// is, the world names the law.
import std;
import mlibs.agent;
import mlibs.json;
import openas;
import xengine.matter.json;
import xengine.cellular.matter.agent;
import xengine.cellular.rigid.agent;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
namespace bodies = xengine::cellular::rigid;
namespace subject = xengine::cellular::matter_agent;

namespace {

std::string pack_text() {
    const auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path() / "packs" / "magnetism.matter.omui.json";
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
const std::shared_ptr<const matter::Program>& magnetic_world() {
    static const std::shared_ptr<const matter::Program> program = [] {
        auto pack = matter::read_pack(pack_text(), "magnetism.matter.omui.json");
        expect(fatal(pack.has_value())) << (pack ? "" : pack.error().front().text());
        auto made = matter::standard_with(std::array{*pack});
        expect(fatal(made.has_value())) << (made ? "" : made.error());
        return std::make_shared<const matter::Program>(std::move(*made));
    }();
    return program;
}
constexpr ca::Shape shape{96, 40};

// A floor; the lodestone at the right; from the left a crate of wood, a crate of iron, a heap of sand, a heap of
// filings.
struct Scene {
    const matter::Program& program = *magnetic_world();
    std::unique_ptr<bodies::MatterWorld> world = *bodies::MatterWorld::make(program, {.shape = shape, .tile = {16, 16}, .seed = 9, .fill = "air", .flying = {.thrown = true}});
    xengine::dynamics::rigid::BodyId wood, iron;
    Scene() {
        const auto of = [&](std::string_view name) { return *program.material(name); };
        world->place({0, 36, 96, 4}, of("stone"));
        world->place({84, 24, 6, 12}, of("lodestone"));
        world->place({20, 32, 4, 4}, of("wood"));
        world->place({58, 32, 4, 4}, of("iron"));
        world->place({36, 33, 6, 3}, of("sand"));
        world->place({68, 33, 6, 3}, of("filings"));
        wood = world->carve({20, 32, 4, 4})->front();
        iron = world->carve({58, 32, 4, 4})->front();
    }
    [[nodiscard]] float x_of(xengine::dynamics::rigid::BodyId body) const { return std::as_const(world->rigid()).pose(body)->centre.x / world->cell(); }
    [[nodiscard]] std::uint32_t count(std::string_view name, ca::Tile region) const {
        const std::uint32_t material = *program.material(name);
        std::uint32_t found = 0;
        for (std::uint32_t y = region.y; y < region.y + region.height; ++y)
            for (std::uint32_t x = region.x; x < region.x + region.width; ++x) found += world->cells()[y * shape.width + x] == material;
        return found;
    }
    [[nodiscard]] std::uint32_t field_at(std::uint32_t x, std::uint32_t y) const {
        return world->cells()[static_cast<std::size_t>(*program.channel("field")) * shape.width * shape.height + y * shape.width + x];
    }
};

}  // namespace

static UTest cases{"matter.invented", [] {
    "magnetism, a pack: iron slides to the lodestone and wood stays; filings fly to it and sand lies; nothing in the engine knows the word"_test = [] {
        Scene scene;
        const float wood_at = scene.x_of(scene.wood), iron_at = scene.x_of(scene.iron);
        const std::uint32_t sand_before = scene.count("sand", {28, 30, 22, 6}), filings_all = scene.count("filings", {0, 0, 96, 40});
        ca::rigid_agent::WorldLattice lattice{*scene.world};
        subject::Matter matter{magnetic_world(), lattice, {.seed = 9}};
        for (int frame = 0; frame < 900; ++frame) {
            scene.world->step(), lattice.stepped(*scene.world);
            if (frame % 150 == 149)
                std::println("frame {}: field 83:{} 72:{} 60:{} 54:{} 24:{}; iron x {:.2f}, wood x {:.2f}; filings by the stone {}, flying {}", frame + 1, scene.field_at(83, 30), scene.field_at(72, 34),
                             scene.field_at(60, 34), scene.field_at(54, 34), scene.field_at(24, 34), scene.x_of(scene.iron), scene.x_of(scene.wood), scene.count("filings", {74, 20, 10, 16}), scene.world->flying().flying());
        }
        expect(gt(scene.field_at(72, 34), scene.field_at(60, 34)) && gt(scene.field_at(60, 34), 0u)) << "the field spreads from the stone and fades";
        expect(gt(scene.x_of(scene.iron), iron_at + 3.0f)) << std::format("the crate of iron: {} → {}", iron_at, scene.x_of(scene.iron));
        expect(lt(std::abs(scene.x_of(scene.wood) - wood_at), 0.05f)) << std::format("the crate of wood: {} → {}", wood_at, scene.x_of(scene.wood));
        expect(eq(scene.count("sand", {28, 30, 22, 6}), sand_before)) << "sand has nothing of it";
        expect(eq(scene.count("filings", {0, 0, 96, 40}) + static_cast<std::uint32_t>(scene.world->flying().flying()), filings_all)) << "nothing made or lost";
        expect(ge(scene.count("filings", {74, 20, 10, 16}), filings_all / 2)) << "the filings are by the stone";

        // Why a filing is where it is: it landed, thrown by the field — by the pack's law.
        agent::SubjectSession session({&matter}, {.actor = {openas::Kind::agent, "tester", {}}});
        const std::uint32_t filings = *scene.program.material("filings");
        std::string why;
        for (std::uint32_t cell = 0; cell < shape.width * shape.height && why.empty(); ++cell) {
            if (scene.world->cells()[cell] != filings || cell % shape.width < 74) continue;
            session.request(std::format(R"({{"op":"matter.explain","cell":[{},{}],"depth":2}})", cell % shape.width, cell / shape.width), [&](std::string_view record) {
                if (record.contains(R"("what":"landed")")) why = record;
            });
        }
        std::println("{}", why.substr(0, 700));
        expect(why.contains(R"("what":"thrown","material":"filings","by":"field","law":{"name":"drawn by a field")")) << why;
    };
}};
