module omui.bench;
import std;
import mlibs.simulation;
import mlibs.memory;
import omui.core;
import xengine.cellular;
import xengine.cellular.present;
import xengine.dynamics;
import xengine.dynamics.rigid;
import xengine.dynamics.rigid3;
import xengine.matter.json;
import xengine.cellular.matter;
import xengine.cellular.matter.agent;
import xengine.cellular.present.matter;
import xengine.cellular.rigid;
import mlibs.trace;

namespace omui::bench {
namespace {
namespace ca=xengine::cellular;
struct Model {
    struct State {ca::Program program;ca::Shape shape;std::vector<std::uint32_t> current,next;};
    struct Input {bool unused=false;};
    static void step(State& s,const Input&) {
        (void)s.program.step(s.shape,s.current,s.next,{.boundary={ca::Edge::wrap}});s.current.swap(s.next);
    }
};
std::optional<Model::State> setup(std::uint32_t side) {
    using C=ca::Condition;using W=ca::WriteKind;
    const std::array rules{
        ca::Rule{{C::self({1,{1,1}}),C::self({2,{3,0xffffffffu}})},{{1,W::set,2}}},
        ca::Rule{{C::self({1,{1,1}})},{{2,W::add_saturating,1}}},
        ca::Rule{{C::self({0,{1,1}}),C::self({1,{0,0}}),C::neighbors({1,{1,1}},ca::cardinal,{1,4})},{{1,W::set,1},{2,W::set,0}}}
    };
    auto p=ca::Program::prepare(3,rules);if(!p)return {};
    const auto cells=side*side;std::vector<std::uint32_t> field(cells*3);
    for(std::uint32_t i=0;i<cells;++i) {field[i]=i%7!=0;field[cells+i]=i%13==0;}
    return Model::State{std::move(*p),{side,side},field,field};
}
}
namespace {
// A 1024 × 1024 field on screen (D-132): a path everywhere, a fire spreading
// from the centre — after 60 steps its front crosses a few dozen tiles.
struct Field {
    Model::State state;
    std::vector<std::uint32_t> after;     // the cells after the steps
    std::vector<std::uint8_t> mask;       // the last step's changed tiles
};
std::optional<Field> spreading(std::uint32_t side, int steps) {
    auto fixture=setup(side);if(!fixture)return {};
    const auto cells=side*side;
    std::ranges::fill(fixture->current,0u);
    std::fill_n(fixture->current.begin(),cells,1u);
    fixture->current[cells+(side/2)*side+side/2]=1;
    const auto grid=ca::tiles({side,side},{32,32});
    Field f{std::move(*fixture),{},std::vector<std::uint8_t>(grid.width*grid.height)};
    for(int k=0;k<steps;++k){(void)f.state.program.step(f.state.shape,f.state.current,f.state.next,{.changed_tiles=f.mask});f.state.current.swap(f.state.next);}
    f.after=f.state.current;
    return f;
}
ca::Palette fire() {
    return {{{{{1,{1,1}}},mlibs::geom::Color::hex(0xff7a1a)},{{{1,{2,2}}},mlibs::geom::Color::hex(0x3a2a20)},{{{0,{1,1}}},mlibs::geom::Color::hex(0x8fa3b8)}},
            mlibs::geom::Color::hex(0x101820)};
}
}
void add_simulation_benchmarks() {
    add("simulation/cellular-1024-present","1024x1024 cells on screen: the tiles one step of a spreading fire changed, recoloured and named in the image (D-132)",[](State& s) {
        auto f=spreading(1024,60);if(!f){s.fail("rule preparation failed");return;}
        xrender::ImageStore store;
        auto view=ca::FieldPresenter::make(store,{1024,1024},3,fire());if(!view){s.fail(view.error());return;}
        (void)view->present(f->after);
        const auto changed=static_cast<std::size_t>(std::ranges::count(f->mask,1));
        s.items(changed*32*32);
        while(s.next()) (void)view->present(f->after,f->mask);
        s.note("scope",std::format("{} of {} tiles changed by the step; recolour and image update only — the step, upload and drawing excluded",changed,f->mask.size()));
    });
    add("simulation/cellular-1024-present-all","1024x1024 cells on screen, every tile changed: the whole field recoloured (the worst case)",[](State& s) {
        auto f=spreading(1024,1);if(!f){s.fail("rule preparation failed");return;}
        xrender::ImageStore store;
        auto view=ca::FieldPresenter::make(store,{1024,1024},3,fire());if(!view){s.fail(view.error());return;}
        std::ranges::fill(f->mask,1);s.items(1024*1024);
        while(s.next()) (void)view->present(f->after,f->mask);
        s.note("scope","three swatches tested per cell, one pixel a cell; the step, upload and drawing excluded");
    });
    add("simulation/cellular-1024-frame","1024x1024 cells drawn 1:1 after one step of a spreading fire: present, damage, incremental CPU raster of the changed rows",[](State& s) {
        auto f=spreading(1024,60);if(!f){s.fail("rule preparation failed");return;}
        xrender::ImageStore store;
        auto view=ca::FieldPresenter::make(store,{1024,1024},3,fire());if(!view){s.fail(view.error());return;}
        (void)view->present(f->after);
        const xrender::Resources resources{&store,nullptr};
        xrender::DisplayList list;view->draw(list,{0,0,1024,1024});
        xrender::raster::Surface surface(1024,1024);xrender::CpuRenderer renderer;xrender::DamageTracker tracker;
        (void)renderer.render(list,surface,resources,{.rows=tracker.update(list,1024,1024,1,resources)});
        std::size_t rows=0;
        while(s.next()) {
            (void)view->present(f->after,f->mask);
            const auto spans=tracker.update(list,1024,1024,1,resources);
            rows=0;for(auto r:spans)rows+=static_cast<std::size_t>(r.bottom-r.top);
            (void)renderer.render(list,surface,resources,{.rows=spans});
        }
        s.note("scope",std::format("one DrawImage for the whole field; {} of 1024 rows redrawn; the step excluded",rows));
    });
    // D-167: a step of the same 1024 × 1024 field with every tile run, with only the tiles awake, and with none awake.
    add("simulation/cellular-1024-step","1024x1024 cells, one step of a spreading fire with every tile run: 3 channels, first-match reaction rules",[](State& s) {
        auto f=spreading(1024,60);if(!f){s.fail("rule preparation failed");return;}
        s.items(1024*1024);
        while(s.next()) (void)f->state.program.step(f->state.shape,f->state.current,f->state.next);
        s.note("scope","the scalar CPU step alone, every tile; presenting and drawing excluded");
    });
    add("simulation/cellular-1024-step-sparse","1024x1024 cells, the same step with only the tiles awake run (D-167): what is at rest sleeps, the cells the same bits",[](State& s) {
        auto fixture=setup(1024);if(!fixture){s.fail("rule preparation failed");return;}
        auto& field=*fixture;
        const auto cells=1024u*1024u;
        std::ranges::fill(field.current,0u);
        std::fill_n(field.current.begin(),cells,1u);
        field.current[cells+512*1024+512]=1;
        const auto grid=ca::tiles(field.shape,{32,32});
        std::vector<std::uint8_t> activity(grid.width*grid.height,0);
        // Sixty steps in, as the dense one; `next` then holds the cells of a step ago, which a tile asleep keeps.
        for(int k=0;k<60;++k){(void)field.program.step(field.shape,field.current,field.next,{.activity=activity,.alternate=true});field.current.swap(field.next);}
        const auto resting=activity;
        const auto awake=activity.size()-static_cast<std::size_t>(std::ranges::count_if(activity,[](std::uint8_t byte){return ca::asleep(byte);}));
        s.items(awake*32*32);
        while(s.next()) {
            activity=resting;
            (void)field.program.step(field.shape,field.current,field.next,{.activity=activity,.alternate=true});
        }
        s.note("scope",std::format("{} of {} tiles awake (the fire's front and the tiles beside it); two buffers in turn, so a tile asleep is not copied",awake,activity.size()));
    });
    add("simulation/cellular-1024-rest","1024x1024 cells with nothing happening: a step of a field wholly asleep (D-167)",[](State& s) {
        auto fixture=setup(1024);if(!fixture){s.fail("rule preparation failed");return;}
        auto& field=*fixture;
        std::ranges::fill(field.current,0u);
        std::fill_n(field.current.begin(),1024u*1024u,1u);
        const auto grid=ca::tiles(field.shape,{32,32});
        std::vector<std::uint8_t> activity(grid.width*grid.height,0);
        for(int k=0;k<2;++k){(void)field.program.step(field.shape,field.current,field.next,{.activity=activity,.alternate=true});field.current.swap(field.next);}
        if(!std::ranges::all_of(activity,[](std::uint8_t byte){return ca::asleep(byte);})){s.fail("the field is not asleep");return;}
        s.items(activity.size());
        while(s.next()) (void)field.program.step(field.shape,field.current,field.next,{.activity=activity,.alternate=true});
        s.note("scope","1024 tiles looked at, none run, no cell read or written");
    });
    add("simulation/cellular-256","256x256 integer cells: 3 channels, first-match reaction rules, 32x32 tiles",[](State& s) {
        auto fixture=setup(256);if(!fixture){s.fail("rule preparation failed");return;}
        const auto initial=fixture->current;
        Model::step(*fixture,{});s.items(256*256);
        while(s.next()) {
            s.pause();fixture->current=initial;s.resume();
            Model::step(*fixture,{});
        }
        s.note("scope","one scalar CPU step with immutable reads and wrap boundary; state reset, preparation, upload, rendering and GPU excluded");
    });
    // D-133: a soft body's step (the Mui-sized ring of 24, a UI's; a ring of 96), standing on a ledge — the solver alone.
    for (const int points : {24, 96}) {
        add(std::format("dynamics/softbody-{}", points), std::format("one XPBD step of a {}-particle soft body standing on a ledge: 8 substeps of edges, area, shape matching, collision", points),
            [points](State& s) {
                namespace dyn = xengine::dynamics;
                std::vector<std::array<double, 2>> outline;
                for (int index = 0; index < points; ++index) {
                    const double angle = 2 * std::numbers::pi * index / points;
                    outline.push_back({100 + 30 * std::cos(angle), 100 + 30 * std::sin(angle)});
                }
                auto body = dyn::SoftBody::ring(outline, {.shape_stiffness = 0.12, .upright = 1});
                if (!body) { s.fail("the ring was refused"); return; }
                dyn::BodyInput input;
                input.ledges[0] = {0, 400, 200};
                input.ledge_count = 1;
                for (int settle = 0; settle < 240; ++settle) dyn::SoftBodyModel::step(*body, input);
                s.items(static_cast<std::size_t>(points));
                while (s.next()) dyn::SoftBodyModel::step(*body, input);
                s.note("scope", "the solver's step alone, at rest on a ledge; the cage carrying a figure (hanim) and drawing excluded");
            });
    }
    // D-165: the matter kernel's reference step — the laws on any carrier — and the same world on the lattice
    // (the program lowered, T1.7): the same bits, by the carrier's own path.
    struct MatterWorld {
        xengine::matter::Program program;
        std::uint32_t side = 64;
        std::vector<std::uint32_t> state, next;
    };
    // 64x64 of air with strips of wood alight at one end and one of water — the standard laws' shape
    // (3 transfers, 1 adjustment, 10 reactions and conversions), stepped 40 times by the reference.
    const auto matter_world = []() -> std::optional<MatterWorld> {
        namespace matter = xengine::matter;
        using P = matter::Property;
        matter::Definition definition = matter::Definition::standard();
        definition.materials = {{"air", {P{"phase", "gas"}, P{"density", 1.2}, P{"oxygen", 1.0}}, {}},
                                {"wood", {P{"ignition", 570.0}, P{"fuel", 400.0}, P{"heat", 80.0}, P{"porous", 2.0}, P{"burnt", "ash"}}, {}},
                                {"ash", {P{"phase", "granular"}}, {}},
                                {"water", {P{"phase", "liquid"}, P{"thermal", 2.0}, P{"conduct", 2.0}, P{"moist", 64.0}, P{"porous", 8.0}, P{"melting", 273.0}, P{"boiling", 373.0}, P{"boiled", "steam"}}, {}},
                                {"steam", {P{"phase", "gas"}, P{"boiling", 373.0}, P{"condensed", "water"}, P{"starts_at", 380.0}}, {}}};
        definition.laws = {{.transfer = "temperature", .by = "thermal"},
                           {.transfer = "charge", .by = "conduct"},
                           {.transfer = "wetness", .by = "porous"},
                           {.when = "wetness > 0", .raise = {{"ignition", "wetness * 40"}}},
                           {.when = "moist > 0", .set = {{"wetness", "moist"}}},
                           {.when = "moist == 0 && wetness > 0 && temperature >= 373", .take = {{"wetness", "1"}}},
                           {.when = "warmth > 0", .add = {{"temperature", "warmth"}}},
                           {.when = "temperature >= ignition && burned < fuel && near.oxygen > 0", .add = {{"burned", "burn_rate"}}, .flag = "burning"},
                           {.when = "burning > 0 && temperature < flame", .add = {{"temperature", "heat"}}},
                           {.when = "fuel > 0 && burned >= fuel", .become = "burnt"},
                           {.when = "phase == liquid && boiling > 0 && temperature >= boiling", .become = "boiled"},
                           {.when = "phase == gas && temperature < boiling", .become = "condensed"},
                           {.when = "phase == liquid && temperature < melting", .become = "frozen"},
                           {.when = "melting > 0 && temperature >= melting", .become = "melted"}};
        auto program = matter::Program::prepare(definition);
        if (!program) return std::nullopt;
        MatterWorld world{.program = std::move(*program)};
        const std::uint32_t side = world.side;
        world.state = world.program.fresh(side * side, *world.program.material("air"));
        world.next.resize(world.state.size());
        for (std::uint32_t y = 16; y < 48; y += 2)
            for (std::uint32_t x = 4; x < 60; ++x) world.program.place(world.state, side * side, y * side + x, *world.program.material(y == 32 ? "water" : "wood"));
        for (std::uint32_t y = 16; y < 48; y += 2) world.state[*world.program.channel("temperature") * side * side + y * side + 4] = 700 * 256;
        return world;
    };
    const auto pairs_of = [](std::uint32_t side) {
        std::vector<xengine::matter::Pair> pairs;
        for (std::uint32_t y = 0; y < side; ++y)
            for (std::uint32_t x = 0; x < side; ++x) {
                if (x + 1 < side) pairs.push_back({y * side + x, y * side + x + 1, 1});
                if (y + 1 < side) pairs.push_back({y * side + x, (y + 1) * side + x, 1});
            }
        return pairs;
    };
    add("simulation/matter-reference-4096", "64x64 instances of matter, each beside four: strips of wood alight under air — the standard laws' shape (3 transfers, 1 adjustment, 10 reactions and conversions), the reference step",
        [matter_world, pairs_of](State& s) {
            auto world = matter_world();
            if (!world) { s.fail("the definition was refused"); return; }
            const std::uint32_t side = world->side;
            const auto pairs = pairs_of(side);
            xengine::matter::Workspace workspace;
            for (int warm = 0; warm < 40; ++warm) { (void)world->program.step({side * side, pairs}, world->state, world->next, workspace); world->state.swap(world->next); }
            const auto burning = std::ranges::count_if(std::span(world->state).subspan(*world->program.channel("burning") * side * side, side * side), [](std::uint32_t value) { return value != 0; });
            s.items(side * side);
            while (s.next()) (void)world->program.step({side * side, pairs}, world->state, world->next, workspace);
            s.note("scope", std::format("{} instances alight; the reference step alone (near sums, flows, laws, commit), no events kept; a carrier's own path and drawing excluded", burning));
        });
    add("simulation/matter-lattice-4096", "the same 64x64 of matter on the lattice: the program lowered to a table, transfers and reactions (T1.7), every tile run — the reference's bits",
        [matter_world, pairs_of](State& s) {
            auto world = matter_world();
            if (!world) { s.fail("the definition was refused"); return; }
            const std::uint32_t side = world->side;
            const auto lattice = ca::lower(world->program, {.moves = false});
            if (!lattice) { s.fail("the program was not lowered"); return; }
            const auto pairs = pairs_of(side);
            xengine::matter::Workspace workspace;
            std::vector<std::uint32_t> cells = world->state, cells_next(cells.size());
            for (int warm = 0; warm < 40; ++warm) {
                (void)world->program.step({side * side, pairs}, world->state, world->next, workspace);
                world->state.swap(world->next);
                (void)lattice->step({side, side}, cells, cells_next, {.boundary = ca::nothing_beyond});
                cells.swap(cells_next);
            }
            if (cells != world->state) { s.fail("the lattice parted from the reference"); return; }
            s.items(side * side);
            while (s.next()) (void)lattice->step({side, side}, cells, cells_next, {.boundary = ca::nothing_beyond});
            s.note("scope", std::format("{} words; the same state as the reference after 40 steps; the scalar CPU step, every tile; cells do not move here", lattice->words().size()));
        });
    add("simulation/matter-lattice-256-sparse", "256x256 of the standard matter with cells that move: a basin of water, sand, and a strip of wood alight — only the tiles awake run (D-167)",
        [](State& s) {
            const auto program = xengine::matter::standard();
            if (!program) { s.fail("the standard definition was refused"); return; }
            const auto lattice = ca::lower(*program);
            if (!lattice) { s.fail("the program was not lowered"); return; }
            const std::uint32_t side = 256;
            const ca::Shape shape{side, side}, tile{32, 32};
            const std::size_t area = static_cast<std::size_t>(side) * side;
            std::vector<std::uint32_t> cells = program->fresh(area, *program->material("air")), cells_next(cells.size());
            const auto fill = [&](std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::string_view material) {
                for (std::uint32_t y = y0; y <= y1; ++y)
                    for (std::uint32_t x = x0; x <= x1; ++x) program->place(cells, area, static_cast<std::size_t>(y) * side + x, *program->material(material));
            };
            fill(0, 250, 255, 255, "stone");
            fill(20, 200, 22, 249, "stone");
            fill(100, 200, 102, 249, "stone");
            fill(23, 215, 99, 249, "water");
            fill(50, 150, 70, 170, "sand");
            fill(150, 249, 230, 249, "wood");
            cells[*program->channel("temperature") * area + 249 * side + 150] = 700 * 256;
            const ca::Shape grid = ca::tiles(shape, tile);
            std::vector<std::uint8_t> activity(static_cast<std::size_t>(grid.width) * grid.height, 0);
            std::uint64_t tick = 0;
            const auto step = [&] {
                const auto stepped = lattice->step(shape, cells, cells_next, {.tile = tile, .boundary = ca::nothing_beyond, .tick = tick++, .activity = activity, .alternate = true});
                cells.swap(cells_next);
                return stepped ? *stepped : ca::Statistics{};
            };
            // The sand has fallen and the water found its level; the fire is on its way along the wood.
            ca::Statistics last;
            for (int warm = 0; warm < 400; ++warm) last = step();
            s.items(last.cells ? last.cells : 1);
            std::uint64_t run = 0, steps = 0;
            while (s.next()) {
                run += step().cells;
                ++steps;
            }
            s.note("scope", std::format("{} cells run a step of {} on average ({} of {} tiles asleep at the first); laws, flows and moves; two buffers in turn; drawing excluded",
                                        steps ? run / steps : 0, area, last.asleep, activity.size()));
        });
    // M6.2: the whole 2D world — the lattice, what flies, the coupling and the rigid step — on a cavern the size of a
    // showcase's: what a game pays a frame, and what a playtest that branches ahead pays thousands of times.
    namespace rigid = xengine::cellular::rigid;
    const auto cavern = [](State& s, const xengine::matter::Program& program) -> std::unique_ptr<rigid::MatterWorld> {
        auto made = rigid::MatterWorld::make(program, {.shape = {128, 64}, .tile = {16, 16}, .seed = 17, .fill = "air"});
        if (!made) { s.fail(made.error()); return nullptr; }
        rigid::MatterWorld& world = **made;
        const auto put = [&](std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::string_view material) {
            world.place({x, y, width, height}, *program.material(material));
        };
        put(0, 56, 128, 8, "stone");
        put(0, 0, 2, 56, "stone"), put(126, 0, 2, 56, "stone");
        put(40, 48, 2, 8, "stone"), put(70, 48, 2, 8, "stone");
        put(42, 50, 28, 6, "water");
        put(50, 44, 4, 4, "wood"), put(60, 44, 4, 4, "wood");
        if (!world.carve({50, 44, 14, 4})) { s.fail("the crates were not carved"); return nullptr; }
        put(20, 46, 3, 10, "wood");
        world.set(*program.channel("temperature"), {20, 55, 3, 1}, 900 * 256);
        // The crates are afloat, the fire has taken the barricade and its heat is about the cavern: a frame costs
        // from here what it goes on costing.
        for (int warm = 0; warm < 900; ++warm) world.step();
        return std::move(*made);
    };
    add("simulation/world-128x64-frame", "a 128x64 cavern of the standard matter with bodies in it: a floor, a pool with crates afloat, a barricade alight — one frame of the whole world (lattice, flight, coupling, rigid)",
        [cavern](State& s) {
            const auto program = xengine::matter::standard();
            if (!program) { s.fail("the standard definition was refused"); return; }
            const auto made = cavern(s, *program);
            if (!made) return;
            rigid::MatterWorld& world = *made;
            s.items(128 * 64);
            while (s.next()) world.step();
            // Where a frame's time goes: the same frames again, traced (outside what was measured).
            mlibs::trace::start();
            for (int frame = 0; frame < 120; ++frame) world.step();
            const mlibs::trace::Trace trace = mlibs::trace::stop();
            std::string stages;
            double whole = 0;
            const auto summary = trace.summary();
            for (const auto& stat : summary)
                if (stat.name == "simulation.world.step") whole = stat.total_ms;
            for (const auto& stat : summary)
                if (whole > 0 && (stat.name.starts_with("simulation.world.") || stat.name == "simulation.cellular.step") && stat.name != "simulation.world.step")
                    stages += std::format("{}{} {:.0f}%", stages.empty() ? "" : ", ", stat.name.substr(stat.name.rfind('.') + 1) == "step" ? "lattice" : stat.name.substr(stat.name.rfind('.') + 1), 100.0 * stat.total_ms / whole);
            s.note("scope", std::format("{} bodies; the frame's parts: {}; drawing excluded", world.bodies().bodies().size(), stages));
        });
    // T2.9: the rigid family beside the solver it wraps. The same bodies on a World and on Box2D called directly,
    // each stepped from the same start in the same run, in turn: the wrapper's cost is a number, not a guess.
    namespace solid = xengine::dynamics::rigid;
    const auto beside = [](State& s, const std::vector<solid::Piece>& pieces, int steps) {
        using clock = std::chrono::steady_clock;
        // A round each; compared by their medians, so one round the machine was busy in does not decide it.
        std::vector<double> ours, direct;
        std::size_t rounds = 0;
        bool same = true;
        s.items(static_cast<std::uint64_t>(steps));
        while (s.next()) {
            s.pause();
            auto world = solid::World::make();
            auto plain = solid::Direct::make(pieces);
            if (!world || !plain) { s.fail("the scene was refused"); return; }
            std::vector<solid::BodyId> ids;
            for (const solid::Piece& piece : pieces) {
                const auto made = world->add(piece.body, piece.shape, piece.material);
                if (!made) { s.fail("a body was refused"); return; }
                ids.push_back(*made);
            }
            // Box2D directly (outside what the row times) and the World (what the row times), which goes first
            // taking turns: whichever is second finds the caches warm.
            const bool world_first = rounds % 2 == 1;
            const auto run_world = [&] {
                s.resume();
                const auto from = clock::now();
                for (int step = 0; step < steps; ++step) world->step();
                const auto to = clock::now();
                s.pause();
                ours.push_back(std::chrono::duration<double, std::milli>(to - from).count());
            };
            if (world_first) run_world();
            const auto began = clock::now();
            for (int step = 0; step < steps; ++step) plain->step();
            const auto between = clock::now();
            if (!world_first) run_world();
            direct.push_back(std::chrono::duration<double, std::milli>(between - began).count());
            ++rounds;
            for (std::size_t index = 0; index < ids.size() && same; ++index) {
                const auto pose = std::as_const(*world).pose(ids[index]);
                const solid::Vec at = plain->at(index);
                same = pose && pose->at.x == at.x && pose->at.y == at.y;
            }
            s.resume();
        }
        if (!same) { s.fail("the two worlds parted: not the same scene, so not a comparison"); return; }
        const auto median = [](std::vector<double>& values) {
            if (values.empty()) return 0.0;
            std::ranges::sort(values);
            return values[values.size() / 2];
        };
        const double through = median(ours), plain_ms = median(direct);
        if (!rounds || !(plain_ms > 0)) return;
        s.counter("direct_ms", plain_ms);
        s.counter("overhead_percent", (through - plain_ms) / plain_ms * 100.0);
        s.note("scope", std::format("{} bodies, {} steps a round from the same start; the median round: Box2D directly {:.3f} ms, through the World {:.3f} ms; every body at the same place in both, bit for bit",
                                    pieces.size(), steps, plain_ms, through));
    };
    add("simulation/rigid-pyramid-20", "a pyramid of 210 boxes on a floor, 240 steps from the moment it is built — xengine.dynamics.rigid's World beside Box2D called directly on the same bodies",
        [beside](State& s) {
            std::vector<solid::Piece> pieces{{{.kind = solid::Kind::fixed, .at = {0, -0.5f}}, solid::Shape::box(40, 0.5f), {}}};
            for (int row = 0; row < 20; ++row)
                for (int box = 0; box < 20 - row; ++box)
                    pieces.push_back({{.at = {(static_cast<float>(box) - static_cast<float>(20 - row) * 0.5f) * 1.05f, 0.5f + static_cast<float>(row) * 1.02f}}, solid::Shape::box(0.5f, 0.5f), {}});
            beside(s, pieces, 240);
        });
    add("simulation/rigid-rain-1000", "1000 boxes and circles let go above a walled floor, 240 steps: they fall, pile and slide — the World beside Box2D called directly on the same bodies",
        [beside](State& s) {
            std::vector<solid::Piece> pieces{{{.kind = solid::Kind::fixed, .at = {0, -0.5f}}, solid::Shape::box(30, 0.5f), {}},
                                             {{.kind = solid::Kind::fixed, .at = {-30, 20}}, solid::Shape::box(0.5f, 21), {}},
                                             {{.kind = solid::Kind::fixed, .at = {30, 20}}, solid::Shape::box(0.5f, 21), {}}};
            for (int index = 0; index < 1000; ++index) {
                const float x = -24.0f + static_cast<float>(index % 40) * 1.2f + static_cast<float>((index / 40) % 2) * 0.4f, y = 2.0f + static_cast<float>(index / 40) * 1.3f;
                pieces.push_back({{.at = {x, y}, .angle = static_cast<float>(index % 7) * 0.2f}, index % 3 ? solid::Shape::box(0.4f, 0.4f) : solid::Shape::circle(0.45f), {}});
            }
            beside(s, pieces, 240);
        });
    // M6.4: the 3D rigid family beside the solver it wraps — the same bodies on a rigid3 World and on Jolt called
    // directly, taking turns from the same start, as the 2D rows do; every body at the same place in both.
    namespace solid3 = xengine::dynamics::rigid3;
    add("simulation/rigid3-tower-512", "a block of 8 × 8 × 8 boxes on a floor struck by a heavy ball, 240 steps from the moment it is built — xengine.dynamics.rigid3's World beside Jolt called directly on the same bodies",
        [](State& s) {
            using clock = std::chrono::steady_clock;
            std::vector<solid3::Piece> pieces{{{.kind = solid3::Kind::fixed, .at = {0, -0.5f, 0}}, solid3::Shape::box({40, 0.5f, 40}), {}}};
            for (int level = 0; level < 8; ++level)
                for (int row = 0; row < 8; ++row)
                    for (int column = 0; column < 8; ++column)
                        pieces.push_back({{.at = {static_cast<float>(column) * 1.02f - 3.57f, 0.5f + static_cast<float>(level) * 1.01f, static_cast<float>(row) * 1.02f - 3.57f}},
                                          solid3::Shape::box({0.5f, 0.5f, 0.5f}), {.density = 600}});
            pieces.push_back({{.at = {-16, 3, 0}, .velocity = {30, 2, 0}, .bullet = true}, solid3::Shape::sphere(1), {.density = 7800}});
            constexpr int steps = 240;
            std::vector<double> ours, direct;
            std::size_t rounds = 0;
            bool same = true;
            s.items(static_cast<std::uint64_t>(steps));
            while (s.next()) {
                s.pause();
                auto world = solid3::World::make();
                auto plain = solid3::Direct::make(pieces);
                if (!world || !plain) { s.fail("the scene was refused"); return; }
                std::vector<solid3::BodyId> ids;
                for (const solid3::Piece& piece : pieces) {
                    const auto made = world->add(piece.body, piece.shape, piece.material);
                    if (!made) { s.fail("a body was refused"); return; }
                    ids.push_back(*made);
                }
                // Jolt directly (outside what the row times) and the World (what the row times), which goes first
                // taking turns: whichever is second finds the caches warm.
                const bool world_first = rounds % 2 == 1;
                const auto run_world = [&] {
                    s.resume();
                    const auto from = clock::now();
                    for (int step = 0; step < steps; ++step) world->step();
                    const auto to = clock::now();
                    s.pause();
                    ours.push_back(std::chrono::duration<double, std::milli>(to - from).count());
                };
                if (world_first) run_world();
                const auto began = clock::now();
                for (int step = 0; step < steps; ++step) plain->step();
                const auto between = clock::now();
                if (!world_first) run_world();
                direct.push_back(std::chrono::duration<double, std::milli>(between - began).count());
                ++rounds;
                for (std::size_t index = 0; index < ids.size() && same; ++index) {
                    const auto pose = world->pose(ids[index]);
                    same = pose && pose->at == plain->at(index);
                }
                s.resume();
            }
            if (!same) { s.fail("the two worlds parted: not the same scene, so not a comparison"); return; }
            const auto median = [](std::vector<double>& values) {
                if (values.empty()) return 0.0;
                std::ranges::sort(values);
                return values[values.size() / 2];
            };
            const double through = median(ours), plain_ms = median(direct);
            if (!rounds || !(plain_ms > 0)) return;
            s.counter("direct_ms", plain_ms);
            s.counter("overhead_percent", (through - plain_ms) / plain_ms * 100.0);
            s.note("scope", std::format("{} bodies, {} steps a round from the same start, one thread; the median round: Jolt directly {:.3f} ms, through the World {:.3f} ms; every body at the same place in both, bit for bit",
                                        pieces.size(), steps, plain_ms, through));
        });
    add("simulation/world-128x64-branch", "the same cavern branched: the whole world copied — the field, what flies, the coupling, the rigid world from its own image — what a timeline's snapshot and an agent's `world.branch` cost before a frame is run",
        [cavern](State& s) {
            const auto program = xengine::matter::standard();
            if (!program) { s.fail("the standard definition was refused"); return; }
            auto made = cavern(s, *program);
            if (!made) return;
            const rigid::WorldValue world(std::move(made));
            std::uint64_t kept = 0;
            while (s.next()) {
                const rigid::WorldValue branch = world;
                kept += static_cast<bool>(branch);
            }
            if (!kept) s.fail("the world did not branch");
            s.note("scope", std::format("{} bytes of rigid image; nothing stepped", world->rigid().save().size()));
        });
    add("simulation/matter-256-causes", "the same 256x256 of matter as an agent's subject: a step with the record of causes made at its commit (D-169) — what watching a field costs",
        [](State& s) {
            const auto program = xengine::matter::standard();
            if (!program) { s.fail("the standard definition was refused"); return; }
            const std::uint32_t side = 256;
            // Two worlds alike; the second is not watched: what a step costs with no record.
            const auto make = [&]() -> std::optional<ca::matter_agent::World> {
                auto world = ca::matter_agent::World::make(*program, {side, side}, "air");
                if (!world) return std::nullopt;
                const auto fill = [&](std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::string_view material) {
                    return world->matter->place(*program->material(material), {x0, y0, x1 - x0 + 1, y1 - y0 + 1});
                };
                const bool placed = fill(0, 250, 255, 255, "stone") && fill(20, 200, 22, 249, "stone") && fill(100, 200, 102, 249, "stone") && fill(23, 215, 99, 249, "water") &&
                                    fill(50, 150, 70, 170, "sand") && fill(150, 249, 230, 249, "wood") &&
                                    world->matter->apply(*program->channel("temperature"), ca::agent::EditKind::set, 700 * 256, {150, 249, 1, 1});
                if (!placed) return std::nullopt;
                return std::move(*world);
            };
            auto watched = make(), alone = make();
            if (!watched || !alone) { s.fail("the world was not made"); return; }
            alone->field->unwatch(alone->matter.get());
            for (int warm = 0; warm < 400; ++warm) watched->field->step(), alone->field->step();
            if (watched->field->state_hash() != alone->field->state_hash()) { s.fail("watching a field changed it"); return; }
            const auto began = std::chrono::steady_clock::now();
            const int unwatched_steps = 40;
            for (int step = 0; step < unwatched_steps; ++step) alone->field->step();
            const double unwatched = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count() / unwatched_steps;
            const std::size_t before = watched->matter->records().size();
            s.items(static_cast<std::size_t>(side) * side);
            std::uint64_t steps = 0;
            while (s.next()) {
                watched->field->step();
                ++steps;
            }
            s.note("scope", std::format("a step and its commit's record: where each cell of the tiles awake came from (the kernel's `origins`), each against itself a step ago, and those whose "
                                        "material or named state changed judged again by the kernel's `why`; {} records of {} steps here ({} kept in all); the same field unwatched: {:.2f} ms a step (mean of {})",
                                        watched->matter->records().size() - before, steps, watched->matter->records().size(), unwatched, unwatched_steps));
        });
    add("simulation/matter-256-shade", "256x256 of the standard matter on screen by the standard appearance, every tile: each cell's shade and its light (D-168; the worst case)",
        [](State& s) {
            const auto program = xengine::matter::standard();
            if (!program) { s.fail("the standard definition was refused"); return; }
            const auto look = xengine::matter::standard_look(*program);
            if (!look) { s.fail("the standard appearance was refused"); return; }
            const auto lattice = ca::lower(*program);
            if (!lattice) { s.fail("the program was not lowered"); return; }
            const std::uint32_t side = 256;
            const ca::Shape shape{side, side}, tile{32, 32};
            const std::size_t area = static_cast<std::size_t>(side) * side;
            std::vector<std::uint32_t> cells = program->fresh(area, *program->material("air")), cells_next(cells.size());
            const auto fill = [&](std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::string_view material) {
                for (std::uint32_t y = y0; y <= y1; ++y)
                    for (std::uint32_t x = x0; x <= x1; ++x) program->place(cells, area, static_cast<std::size_t>(y) * side + x, *program->material(material));
            };
            // Every kind of look at once: stone, a basin of water, sand in the air, wood alight, lava.
            fill(0, 250, 255, 255, "stone");
            fill(20, 200, 22, 249, "stone");
            fill(100, 200, 102, 249, "stone");
            fill(23, 215, 99, 249, "water");
            fill(50, 150, 70, 170, "sand");
            fill(150, 240, 230, 249, "wood");
            fill(110, 245, 140, 249, "lava");
            cells[*program->channel("temperature") * area + 240 * side + 150] = 900 * 256;
            for (std::uint64_t tick = 0; tick < 120; ++tick) {
                if (!lattice->step(shape, cells, cells_next, {.tile = tile, .boundary = ca::nothing_beyond, .tick = tick})) { s.fail("a step was refused"); return; }
                cells.swap(cells_next);
            }
            xrender::ImageStore store;
            auto view = ca::FieldPresenter::make(store, shape, static_cast<std::uint32_t>(program->channels().size()), ca::lower(*look), tile);
            if (!view) { s.fail(view.error()); return; }
            const ca::Shape grid = ca::tiles(shape, tile);
            const std::vector<std::uint8_t> every(static_cast<std::size_t>(grid.width) * grid.height, 1);
            (void)view->present(cells, every);
            (void)view->present(cells, every);  // both surfaces of each image have been made
            const xrender::raster::Surface* light = store.get(view->light());
            const auto lit = light ? std::ranges::count_if(light->pixels(), [](xrender::raster::Rgba8 pixel) { return pixel.a != 0; }) : 0;
            s.items(area);
            while (s.next()) (void)view->present(cells, every);
            s.note("scope", std::format("{} states over {} materials, {} cells giving light; the pixel and the light of every cell, two images updated; the step, upload and drawing excluded",
                                        look->states().size(), program->materials().size(), lit));
        });
    // M6.1's scale: a 1024 x 1024 world of the standard matter on the CPU — a lake at rest, sand pouring, wood
    // alight, lava beside water, open to air — stepped sparsely in pieces on the shared pool (Execution::parallel),
    // and checked to be the bits of the same steps run one after another.
    const auto matter_1024 = [](State& s, bool sparse) {
        const auto program = xengine::matter::standard();
        if (!program) { s.fail("the standard definition was refused"); return; }
        const auto lattice = ca::lower(*program, {.seed = 3});
        if (!lattice) { s.fail("the program was not lowered"); return; }
        const std::uint32_t side = 1024;
        const std::size_t area = std::size_t{side} * side;
        const ca::Shape shape{side, side}, tile{32, 32};
        const auto air = *program->material("air");
        const auto outside = program->fresh(1, air);
        std::vector<std::uint32_t> cells = program->fresh(area, air);
        const auto fill = [&](std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::string_view material) {
            for (std::uint32_t y = y0; y <= y1; ++y)
                for (std::uint32_t x = x0; x <= x1; ++x) program->place(cells, area, std::size_t{y} * side + x, *program->material(material));
        };
        fill(0, 1000, 1023, 1023, "stone");
        fill(40, 840, 44, 999, "stone");
        fill(420, 840, 424, 999, "stone");
        fill(45, 860, 419, 999, "water");
        fill(500, 120, 600, 320, "sand");
        fill(600, 990, 900, 999, "wood");
        fill(930, 940, 1000, 999, "lava");
        fill(905, 960, 925, 999, "water");
        const auto temperature = *program->channel("temperature");
        for (std::uint32_t y = 990; y <= 999; ++y) cells[temperature * area + std::size_t{y} * side + 600] = 900 * 256;
        const auto tiles = ca::tiles(shape, tile);
        struct World { std::vector<std::uint32_t> cells, other; std::vector<std::uint8_t> activity; std::uint64_t tick = 0; };
        World pooled{cells, std::vector<std::uint32_t>(cells.size()), std::vector<std::uint8_t>(std::size_t{tiles.width} * tiles.height)}, alone = pooled;
        const auto pool_body = [](std::uint32_t count, mlibs::memory::FunctionRef<void(std::uint32_t)> piece) {
            // A piece at a time: what is awake clusters (a fire's front), so chunks of neighbouring tiles would leave one thread with it.
            Tasks::shared().parallel_for(count, 1, [&](std::size_t begin, std::size_t end) { for (std::size_t index = begin; index < end; ++index) piece(static_cast<std::uint32_t>(index)); });
        };
        const ca::ForEach pool = pool_body;
        std::uint64_t awake = 0, steps = 0;
        const auto step = [&](World& world, const ca::ForEach* parallel) {
            ca::Execution execution{.tile = tile, .boundary = ca::open_to(outside), .tick = world.tick++, .parallel = parallel};
            if (sparse) { execution.activity = world.activity; execution.alternate = true; }
            const auto stepped = lattice->step(shape, world.cells, world.other, execution);
            world.cells.swap(world.other);
            return stepped ? stepped->cells : 0;
        };
        for (int warm = 0; warm < 120; ++warm) (void)step(pooled, &pool), (void)step(alone, nullptr);
        if (pooled.cells != alone.cells || pooled.activity != alone.activity) { s.fail("the step in pieces on the pool parted from the step run one after another"); return; }
        s.items(area);
        while (s.next()) awake += step(pooled, &pool), ++steps;
        s.counter("threads", static_cast<double>(Tasks::shared().threads()));
        s.note("scope", std::format("9 channels, 17 materials, 19 laws; {:.1f}% of the cells run a step on average (tiles of 32 awake); in pieces on the shared pool of {} threads, "
                                    "the same bits as one after another (checked after 120 steps)",
                                    steps ? 100.0 * static_cast<double>(awake) / static_cast<double>(steps * area) : 0.0, Tasks::shared().threads()));
    };
    add("simulation/matter-1024-sparse", "1024x1024 of the standard matter, cells that move, open to air: a lake at rest, sand pouring, wood alight, lava by water — the tiles awake run in pieces on the pool (M6.1)",
        [matter_1024](State& s) { matter_1024(s, true); });
    add("simulation/matter-1024-dense", "the same 1024x1024 world with every tile run in pieces on the pool: the worst case, every cell awake",
        [matter_1024](State& s) { matter_1024(s, false); });
    add("simulation/checkpoint-64","64x64 cells: one reaction step and one deep snapshot in the shared timeline",[](State& s) {
        auto fixture=setup(64);if(!fixture){s.fail("rule preparation failed");return;}
        mlibs::simulation::Timeline<Model> history(*fixture,32,1);s.items(64*64);
        while(s.next()) {
            s.pause();if(!history.seek(0)){s.fail("history reset failed");return;}s.resume();
            history.step({});
        }
        s.note("scope","scalar step plus owned-state snapshot every tick; fixed topology; checkpoint bank allocation and rewind excluded");
    });
}
}
