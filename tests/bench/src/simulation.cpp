module omui.bench;
import std;
import mlibs.simulation;
import xengine.cellular;
import xengine.cellular.present;
import xengine.dynamics;

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
