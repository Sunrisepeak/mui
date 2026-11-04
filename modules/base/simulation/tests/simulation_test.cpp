import std;
import openmui.simulation;
import openmui.testing;

using namespace omui::testing;
namespace sim=omui::simulation;
namespace {
struct Owned {
    struct State { std::vector<std::int64_t> values; };
    struct Input { std::int64_t add=0; };
    static void step(State& s,const Input& in) {for(auto& x:s.values)x=x*3+in.add;}
};
struct Counter {
    using State = std::int64_t;
    struct Input {std::int64_t add=0;};
    static void step(State& state,const Input& input) {state+=input.add;}
};
}
int main(int argc,char** argv) {
    Suite s{"simulation"};
    s.test("owned snapshots do not alias mutable field buffers and replay a branch exactly", [] {
        Owned::State initial{{1,2,3,4}};
        sim::Timeline<Owned> t(initial,30,3);std::vector<Owned::State> expected{initial};
        for(int i=0;i<15;++i){t.step({i});expected.push_back(t.state());}
        initial.values[0]=999;
        for(const auto f:{0,7,14,15,3}) {check(t.seek(f));check(t.state().values==expected[static_cast<std::size_t>(f)].values);}
        check(t.seek(7));t.step({99});check(t.latest()==8 && !t.seek(9));
        check(t.seek(7));t.step({7});check(t.state().values==expected[8].values);
    });
    s.test("a clock handles signed origins, non-finite inputs and huge stalls without invalid integer conversion", [] {
        sim::Clock c(120,8);check(c.advance(-5)==0);check(c.advance(-4.5)==8);
        check(c.advance(std::numeric_limits<double>::infinity())==0);
        check(c.advance(-4.5)==0);
        check(c.advance(std::numeric_limits<double>::max())==8 && c.alpha()==0);
        c.reset(0);c.scale=-1;check(c.advance(1)==0);c.scale=1;check(c.advance(1)==0);
        c.scale=std::numeric_limits<double>::quiet_NaN();check(c.advance(2)==0);
        c.scale=1;check(c.advance(2)==0);
        sim::Clock bad(std::numeric_limits<double>::infinity());check(bad.step()==1);
    });
    s.test("branching after ring eviction never makes overwritten past frames reachable again", [] {
        for(bool explicit_branch:{false,true}) {
            sim::Timeline<Counter> history(0,12,3);
            for(int i=0;i<50;++i)history.step({i});
            const auto oldest=history.earliest(),cut=oldest+2;
            check(oldest>0 && history.seek(cut));
            if(explicit_branch) {
                history.branch();check_eq(history.earliest(),oldest);
                const auto kept=history.state();check(!history.seek(oldest-1));check_eq(history.state(),kept);
            }
            history.step({1000});const auto changed=cut*(cut-1)/2+1000;
            check_eq(history.earliest(),oldest);check_eq(history.state(),changed);
            check(!history.seek(oldest-1));check(history.seek(oldest));check_eq(history.state(),oldest*(oldest-1)/2);
            check(history.seek(cut+1));check_eq(history.state(),changed);check_eq(history.input(cut).add,std::int64_t{1000});
        }
    });
    return s.run(argc,argv);
}
