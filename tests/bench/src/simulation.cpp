module omui.bench;
import std;
import mlibs.simulation;
import xengine.cellular;
import xengine.cellular.present;
import xengine.dynamics;
import xengine.matter;

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
    // D-165: the matter kernel's reference step — the laws on any carrier, before a carrier's own path (the lattice's, T1.7).
    add("simulation/matter-reference-4096", "64x64 instances of matter, each beside four: a strip of wood alight under air — the standard laws' shape (3 transfers, 1 adjustment, 9 reactions and conversions), the reference step",
        [](State& s) {
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
                               {.when = "temperature >= ignition && burned < fuel && near.oxygen > 0", .add = {{"burned", "burn_rate"}, {"temperature", "heat"}}, .flag = "burning"},
                               {.when = "fuel > 0 && burned >= fuel", .become = "burnt"},
                               {.when = "phase == liquid && boiling > 0 && temperature >= boiling", .become = "boiled"},
                               {.when = "phase == gas && temperature < boiling", .become = "condensed"},
                               {.when = "phase == liquid && temperature < melting", .become = "frozen"},
                               {.when = "melting > 0 && temperature >= melting", .become = "melted"}};
            auto program = matter::Program::prepare(definition);
            if (!program) { s.fail(program.error().message); return; }
            const std::uint32_t side = 64;
            std::vector<matter::Pair> pairs;
            for (std::uint32_t y = 0; y < side; ++y)
                for (std::uint32_t x = 0; x < side; ++x) {
                    if (x + 1 < side) pairs.push_back({y * side + x, y * side + x + 1, 1});
                    if (y + 1 < side) pairs.push_back({y * side + x, (y + 1) * side + x, 1});
                }
            std::vector<std::uint32_t> state = program->fresh(side * side, *program->material("air")), next(state.size());
            for (std::uint32_t y = 16; y < 48; y += 2)
                for (std::uint32_t x = 4; x < 60; ++x) program->place(state, side * side, y * side + x, *program->material(y == 32 ? "water" : "wood"));
            for (std::uint32_t y = 16; y < 48; y += 2) state[*program->channel("temperature") * side * side + y * side + 4] = 700 * 256;
            matter::Workspace workspace;
            for (int warm = 0; warm < 40; ++warm) { (void)program->step({side * side, pairs}, state, next, workspace); state.swap(next); }
            const auto burning = std::ranges::count_if(std::span(state).subspan(*program->channel("burning") * side * side, side * side), [](std::uint32_t value) { return value != 0; });
            s.items(side * side);
            while (s.next()) (void)program->step({side * side, pairs}, state, next, workspace);
            s.note("scope", std::format("{} instances alight; the reference step alone (near sums, flows, laws, commit), no events kept; a carrier's own path and drawing excluded", burning));
        });
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
