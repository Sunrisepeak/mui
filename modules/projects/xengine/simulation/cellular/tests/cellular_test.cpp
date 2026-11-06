import std;
import xengine.simulation;
import xengine.simulation.cellular;
import mlibs.testing;

using namespace mlibs::testing;
namespace ca=xengine::simulation::cellular;
namespace {
std::vector<ca::Rule> reaction_rules() {
    using C=ca::Condition;using W=ca::WriteKind;
    return {
        {{C::self({1,{1,1}}),C::self({2,{3,0xffffffffu}})},{{1,W::set,2}}},
        {{C::self({1,{1,1}})},{{2,W::add_saturating,1}}},
        {{C::self({0,{1,1}}),C::self({1,{0,0}}),C::neighbors({1,{1,1}},ca::cardinal,{1,4})},{{1,W::set,1},{2,W::set,0}}}
    };
}
std::vector<std::uint32_t> initial(ca::Shape shape) {
    const auto n=shape.width*shape.height;std::vector<std::uint32_t> a(n*3);
    std::fill_n(a.begin(),n,1u);
    for(std::uint32_t y=0;y<shape.height;++y)a[y*shape.width+shape.width/2]=0;
    a[n+(shape.height/2)*shape.width+2]=1;
    return a;
}
// Independent scalar fixture oracle; no Program encoding, tile loop or helpers.
std::vector<std::uint32_t> reference(ca::Shape shape,const std::vector<std::uint32_t>& a) {
    auto b=a;const int w=static_cast<int>(shape.width),h=static_cast<int>(shape.height),n=w*h;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const int i=y*w+x;
        if(a[static_cast<std::size_t>(n+i)]==1) {
            if(a[static_cast<std::size_t>(2*n+i)]>=3)b[static_cast<std::size_t>(n+i)]=2;
            else ++b[static_cast<std::size_t>(2*n+i)];
        } else if(a[static_cast<std::size_t>(i)]==1 && a[static_cast<std::size_t>(n+i)]==0) {
            bool active=false;
            for(const auto [dx,dy]:std::array<std::pair<int,int>,4>{{{-1,0},{1,0},{0,-1},{0,1}}})
                if(x+dx>=0 && x+dx<w && y+dy>=0 && y+dy<h)active|=a[static_cast<std::size_t>(n+(y+dy)*w+x+dx)]==1;
            if(active){b[static_cast<std::size_t>(n+i)]=1;b[static_cast<std::size_t>(2*n+i)]=0;}
        }
    }
    return b;
}
struct Model {
    struct State {ca::Program program;ca::Shape shape;std::vector<std::uint32_t> current,next;};
    struct Input {bool ignite=false;std::uint32_t x=0,y=0;};
    static void step(State& s,const Input& in) {
        if(in.ignite && in.x<s.shape.width && in.y<s.shape.height)s.current[s.shape.width*s.shape.height+in.y*s.shape.width+in.x]=1;
        auto result=s.program.step(s.shape,s.current,s.next);check(result.has_value());s.current.swap(s.next);
    }
};
}
int main(int argc,char** argv) {
    Suite s{"simulation.cellular"};
    s.test("state/threshold/neighborhood rules agree with a scalar oracle across tile sizes and both orders", [] {
        const ca::Shape shape{19,11};auto rules=reaction_rules();auto prepared=ca::Program::prepare(3,rules);
        check(prepared.has_value());if(!prepared)return;
        rules.clear();check(prepared->halo()==1 && prepared->channels()==3);
        for(auto tile:std::array<ca::Shape,4>{{{1,1},{7,3},{32,32},{19,11}}})for(bool reverse:{false,true}) {
            auto state=initial(shape),next=state;
            for(int frame=0;frame<24;++frame) {
                const auto expected=reference(shape,state);
                const auto step=prepared->step(shape,state,next,{.tile=tile,.reverse_tiles=reverse});
                check(step && step->cells==209 && next==expected);
                state.swap(next);
            }
            // The inert separator prevents the wave crossing into the right half.
            for(std::uint32_t y=0;y<shape.height;++y)for(std::uint32_t x=10;x<shape.width;++x)check(state[209+y*shape.width+x]==0);
        }
    });
    s.test("partial tile writes leave the rest untouched; constant/clamp/wrap read the full logical halo", [] {
        using C=ca::Condition;const std::array<ca::Offset,1> left{{{-1,0}}};
        const std::array rules{ca::Rule{{C::neighbors({0,{9,9}},left,{1,1})},{{0,ca::WriteKind::set,1}}}};
        const auto p=ca::Program::prepare(1,rules);check(p.has_value());if(!p)return;
        const std::vector<std::uint32_t> input{2,3,9,4,5,6};
        std::vector<std::uint32_t> output(6,77);
        check(p->step_tile({3,2},{0,0,1,1},input,output,{ca::Edge::wrap}).has_value());
        check(output==std::vector<std::uint32_t>{1,77,77,77,77,77});
        check(p->step({3,2},input,output,{.boundary={ca::Edge::clamp}}).has_value());check(output==input);
        check(p->step({3,2},input,output,{.boundary={ca::Edge::constant,9}}).has_value());
        check(output==std::vector<std::uint32_t>{1,3,9,1,5,6});
    });
    s.test("first match and saturating writes are explicit; errors preserve output and moved plans are unprepared", [] {
        const std::array rules{ca::Rule{{},{{0,ca::WriteKind::add_saturating,10},{1,ca::WriteKind::subtract_saturating,10}}},ca::Rule{{},{{0,ca::WriteKind::set,3}}}};
        auto p=ca::Program::prepare(2,rules);check(p.has_value());if(!p)return;
        std::vector<std::uint32_t> input{0xfffffffau,7},output{44,55};
        const auto result=p->step({1,1},input,output);check(result && result->changed==1 && result->matched==1);
        check(output==std::vector<std::uint32_t>{0xffffffffu,0});
        output={44,55};check(!p->step({2,1},input,output));check(output==std::vector<std::uint32_t>{44,55});
        check(!p->step({1,1},input,input));check(input==std::vector<std::uint32_t>{0xfffffffau,7});
        check(!p->step({1,1},input,output,{.tile={0,1}}));check(output==std::vector<std::uint32_t>{44,55});
        auto moved=std::move(*p);check(!p->step({1,1},input,output));check(moved.step({1,1},input,output).has_value());
        auto bad=reaction_rules();bad[0].writes.push_back(bad[0].writes[0]);
        check(ca::Program::prepare(3,bad).error().code==ca::Error::duplicate_write);
        bad=reaction_rules();bad[2].when[2].offsets[0].x=33;check(!ca::Program::prepare(3,bad));
        check(!ca::Program::prepare(0,rules));check(!ca::Program::prepare(2,rules,{.max_words=5}));
    });
    s.test("the shared owned-state history rebuilds cellular states and inputs without a second checkpoint engine", [] {
        const ca::Shape shape{19,11};const auto p=ca::Program::prepare(3,reaction_rules());check(p.has_value());if(!p)return;
        auto state=initial(shape);
        xengine::simulation::Timeline<Model> history({*p,shape,state,state},30,5);
        std::vector<std::vector<std::uint32_t>> frames{state};
        for(int f=0;f<25;++f) {history.step({f==8,15,5});frames.push_back(history.state().current);}
        for(int f:{25,0,8,15,10,24}) {check(history.seek(f));check(history.state().current==frames[static_cast<std::size_t>(f)]);}
        check(history.seek(8));history.step({false});check(history.state().current!=frames[9]);
        check(!history.seek(10));check(history.seek(8));history.step({true,15,5});check(history.state().current==frames[9]);
    });
    return s.run(argc,argv);
}
