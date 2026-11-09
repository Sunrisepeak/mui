import std;
import mlibs.simulation;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace sim=mlibs::simulation;
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
UTest cases{"simulation", [] {
    "owned snapshots do not alias mutable field buffers and replay a branch exactly"_test = [] {
        Owned::State initial{{1,2,3,4}};
        sim::Timeline<Owned> t(initial,30,3);std::vector<Owned::State> expected{initial};
        for(int i=0;i<15;++i){t.step({i});expected.push_back(t.state());}
        initial.values[0]=999;
        for(const auto f:{0,7,14,15,3}) {expect(t.seek(f));expect(t.state().values==expected[static_cast<std::size_t>(f)].values);}
        expect(t.seek(7));t.step({99});expect(t.latest()==8 && !t.seek(9));
        expect(t.seek(7));t.step({7});expect(t.state().values==expected[8].values);
    };
    "a clock handles signed origins, non-finite inputs and huge stalls without invalid integer conversion"_test = [] {
        sim::Clock c(120,8);expect(c.advance(-5)==0);expect(c.advance(-4.5)==8);
        expect(c.advance(std::numeric_limits<double>::infinity())==0);
        expect(c.advance(-4.5)==0);
        expect(c.advance(std::numeric_limits<double>::max())==8 && c.alpha()==0);
        c.reset(0);c.scale=-1;expect(c.advance(1)==0);c.scale=1;expect(c.advance(1)==0);
        c.scale=std::numeric_limits<double>::quiet_NaN();expect(c.advance(2)==0);
        c.scale=1;expect(c.advance(2)==0);
        sim::Clock bad(std::numeric_limits<double>::infinity());expect(bad.step()==1);
    };
    "branching after ring eviction never makes overwritten past frames reachable again"_test = [] {
        for(bool explicit_branch:{false,true}) {
            sim::Timeline<Counter> history(0,12,3);
            for(int i=0;i<50;++i)history.step({i});
            const auto oldest=history.earliest(),cut=oldest+2;
            expect(oldest>0 && history.seek(cut));
            if(explicit_branch) {
                history.branch();expect(eq(history.earliest(),oldest));
                const auto kept=history.state();expect(!history.seek(oldest-1));expect(eq(history.state(),kept));
            }
            history.step({1000});const auto changed=cut*(cut-1)/2+1000;
            expect(eq(history.earliest(),oldest));expect(eq(history.state(),changed));
            expect(!history.seek(oldest-1));expect(history.seek(oldest));expect(eq(history.state(),oldest*(oldest-1)/2));
            expect(history.seek(cut+1));expect(eq(history.state(),changed));expect(eq(history.input(cut).add,std::int64_t{1000}));
        }
    };
    "tiers: what a simulation promises; only a deterministic CPU reference is authoritative"_test = []{
        using namespace mlibs::simulation;
        expect(authoritative(Tier{})) << "the default: plausible, cross-platform, the CPU reference";
        expect(authoritative(Tier{.determinism=Determinism::fixed_point}));
        expect(!authoritative(Tier{.backend=Backend::gpu})) << "a GPU tier is an acceleration, never what a rollback relies on";
        expect(!authoritative(Tier{.determinism=Determinism::same_binary}));
        expect(name(Fidelity::cosmetic)=="cosmetic" && name(Determinism::cross_platform)=="cross_platform" && name(Backend::simd)=="simd");
    };
}};
