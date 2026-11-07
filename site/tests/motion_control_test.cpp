import std;
import omui;
import omui.site;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;
namespace {
constexpr std::string_view root="main/body/view/motion/page/motion_gallery";
struct Scene {
    Headless host{HeadlessOptions{.size={1100,1000},.parallel_raster=false}};
    std::shared_ptr<AgentAccess> access=std::make_shared<AgentAccess>(std::make_shared<mlibs::control::Access>(mlibs::control::policy_for_profile("dev")));
    std::shared_ptr<const AgentSession> session;
    Scene() {
        host.runtime().set_agent_access(access);session=access->open_session();
        auto app=site::app("motion/component-motion","en");host.runtime().set_theme(app.theme);app.setup(host.runtime());
        for(auto& v:app.verbs)check(host.runtime().verbs().add(v.desc,v.fn).has_value());
        host.mount("main",app.view());for(int i=0;i<120;++i)step();
    }
    void step(){check(host.advance(seconds(1./60)).failure.empty());}
    Envelope call(std::string_view name,Value args={},bool preview=false) {
        return host.runtime().verbs().invoke(name,Invocation{.args=std::move(args),.actor={"agent","motion-review",{}},
            .remote=true,.dry_run=preview,.access=access,.session=session});
    }
    Value inspect(){auto r=call("motion.inspect");check(r.exit_code()==0 && r.data_value());return r.data_value()?*r.data_value():Value{};}
    Rect card(){return host.runtime().drawn_bounds(host.runtime().find(std::string(root)+"/stage/scene/composition/card"));}
    void only(std::vector<std::string> names){auto policy=access->policy();policy.user.operations=std::move(names);access->set_policy(policy);}
    std::uint64_t pixels() {
        const auto area=host.runtime().drawn_bounds(host.runtime().find(std::string(root)+"/stage"));const auto& image=host.pixels();Hash h;
        const int x=std::clamp(int(area.x),0,image.width()),end=std::clamp(int(area.right()),x,image.width());
        for(int y=std::clamp(int(area.y),0,image.height());y<std::clamp(int(area.bottom()),0,image.height());++y)
            h.bytes(std::as_bytes(std::span(image.row(y)+x,std::size_t(end-x))));
        return h.value();
    }
};
}
int main(int argc,char** argv) {
    Suite suite{"site.motion-control"};
    suite.test("native semantic seek drives the real site and replays every process frame",[] {
        Scene s;auto initial=s.inspect();check(initial.get("available") && initial.get("available")->as_bool());
        const auto output=scratch_dir("site-motion-control");
        std::vector<std::uint64_t> hashes;
        for(int i=0;i<=60;++i) {
            auto r=s.call("motion.seek",Value{}.set("progress",i/60.));check(r.exit_code()==0);s.step();s.step();
            const auto state=s.inspect();check(std::abs(state.get("progress")->as_double()-i/60.)<1e-9);
            hashes.push_back(s.pixels());
            check(s.host.save_png(output/std::format("bridge-{:03}.png",i),
                s.host.runtime().drawn_bounds(s.host.runtime().find(std::string(root)+"/stage"))).has_value());
        }
        check(hashes.front()!=hashes[30] && hashes[30]!=hashes.back());
        for(int i=60;i>=0;--i){check(s.call("motion.seek",Value{}.set("progress",i/60.)).exit_code()==0);s.step();s.step();check(s.pixels()==hashes[std::size_t(i)]);}
        check(s.call("motion.seek",Value{}.set("progress",.4)).exit_code()==0);s.step();const auto before=s.card();
        check(s.call("motion.seek",Value{}.set("progress",.9),true).exit_code()==0);s.step();check(s.card().x==before.x);
        check(s.call("motion.seek",Value{}.set("progress",2)).exit_code()!=0);s.step();check(s.card().x==before.x);
        check(s.call("motion.seek",Value{}.set("progress","0.5")).exit_code()!=0);
    });
    suite.test("UI entry and host operation restrictions both apply and revocation preserves person controls",[] {
        Scene s;check(s.call("motion.seek",Value{}.set("progress",.25)).exit_code()==0);s.step();const auto before=s.card();
        s.only({"invoke","motion.inspect"});check(s.call("motion.seek",Value{}.set("progress",.9)).exit_code()!=0);s.step();check(s.card().x==before.x);
        s.only({"motion.seek"});check(s.call("motion.seek",Value{}.set("progress",.9)).exit_code()!=0);s.step();check(s.card().x==before.x);
        s.only({"invoke","motion.*"});check(s.call("motion.seek",Value{}.set("progress",.5)).exit_code()==0);s.step();
        check(s.access->revoke_session(*s.session));check(s.call("motion.seek",Value{}.set("progress",.9)).exit_code()!=0);
        const auto id=s.host.runtime().find(std::string(root)+"/transport/buttons/state-b");const auto at=s.host.runtime().drawn_bounds(id).center();
        s.host.click(at);s.step();
        const auto local=s.host.runtime().verbs().invoke("motion.inspect",{});check(local.exit_code()==0);
        check(local.data_value() && local.data_value()->get("progress")->as_double()==1);
    });
    suite.test("host transport reanchors without restarting and reports preferences",[] {
        Scene s;check(s.call("motion.seek",Value{}.set("progress",.5)).exit_code()==0);
        check(s.call("motion.rate",Value{}.set("rate",1.5)).exit_code()==0);
        const auto old_rate=s.inspect().get("rate")->as_double();check(std::abs(old_rate)==1.5);
        check(s.call("motion.reverse").exit_code()==0);check(s.inspect().get("rate")->as_double()==-old_rate);
        check(s.call("motion.resume").exit_code()==0);s.step();s.step();
        check((s.inspect().get("progress")->as_double()-.5)*old_rate<0);
        check(s.call("motion.pause").exit_code()==0);const auto progress=s.inspect().get("progress")->as_double();
        for(int i=0;i<4;++i)s.step();check(s.inspect().get("progress")->as_double()==progress);
        check(s.call("motion.rate",Value{}.set("rate",0)).exit_code()!=0);check(s.inspect().get("rate")->as_double()==-old_rate);
        s.host.runtime().set_reduce_motion(true);check(s.call("motion.resume").exit_code()==0);s.step();s.step();
        check(s.inspect().get("suspension")->as_string()=="motion-preference");
    });
    return suite.run(argc,argv);
}
