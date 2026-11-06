module openmui.bench;
import std;
import xengine.simulation;
import xengine.simulation.cellular;

namespace omui::bench {
namespace {
namespace ca=xengine::simulation::cellular;
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
void add_simulation_benchmarks() {
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
    add("simulation/checkpoint-64","64x64 cells: one reaction step and one deep snapshot in the shared timeline",[](State& s) {
        auto fixture=setup(64);if(!fixture){s.fail("rule preparation failed");return;}
        xengine::simulation::Timeline<Model> history(*fixture,32,1);s.items(64*64);
        while(s.next()) {
            s.pause();if(!history.seek(0)){s.fail("history reset failed");return;}s.resume();
            history.step({});
        }
        s.note("scope","scalar step plus owned-state snapshot every tick; fixed topology; checkpoint bank allocation and rewind excluded");
    });
}
}
