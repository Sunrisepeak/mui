import std;
import openmui;
import openmui.site;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;
namespace {
struct Site {
    Headless host;
    AppSpec app;
    explicit Site(float width,std::string_view lang)
        :host({.size={width,900},.reduce_motion=true,.parallel_raster=false,.complete_frames=true}),
         app(site::app("reactions",lang)) {
        auto access=std::make_shared<AgentAccess>();
        access->set_policy({.mode=AgentMode::development,.source="site-test"});
        host.runtime().set_agent_access(std::move(access));
        app.setup(host.runtime());for(const auto& v:app.verbs)check(host.runtime().verbs().add(v.desc,v.fn).has_value());
        host.mount("main",app.view());host.settle(seconds(.5));
    }
};
constexpr std::string_view field="main/body/view/reactions/page/cellular/field";
}
int main(int argc,char** argv) {
    Suite suite{"site.reactions"};
    suite.test("real workbench route, Agent controls, editable configuration and narrow layout",[] {
        const auto route=site::parse_site_route("zh/explore/reactions");
        check(route && route->place=="reactions" && route->language=="zh");
        for (const auto width:{260.0f,700.0f,1280.0f}) for (const auto lang:{"en","zh"}) {
            Site s(width,lang);auto& rt=s.host.runtime();const auto node=rt.find(field);
            check(rt.valid(node));
            const auto bounds=rt.screen_bounds(node);check(bounds.x>=0 && bounds.width>0 && bounds.x+bounds.width<=width+1);
            const auto initial=rt.invoke("cellular.snapshot");check(initial.exit_code()==0 && initial.data_value());
            const auto initial_hash=std::string(initial.data_value()->get("state_hash")->as_string());
            check(rt.invoke("cellular.advance",{.args=Value{}.set("count",16)}).exit_code()==0);s.host.step();
            const auto changed=rt.invoke("cellular.snapshot");
            check(changed.data_value()->get("state_hash")->as_string()!=initial_hash);
            check(rt.invoke("cellular.seek",{.args=Value{}.set("frame",0)}).exit_code()==0);s.host.step();
            check(rt.invoke("cellular.snapshot").data_value()->get("state_hash")->as_string()==initial_hash);
            const auto folder=scratch_dir(std::format("site-reactions-{}-{}",static_cast<int>(width),lang));
            check(s.host.save_png(folder/"page.png").has_value());
            check(s.host.tree().violations().empty());
            check(site::static_html(lang).contains("explore/reactions"));
        }
    });
    suite.test("leaving the workbench cancels live playback and navigation returns to the same project",[] {
        Site s(1280,"en");auto& rt=s.host.runtime();
        check(rt.invoke("cellular.play",{.args=Value{}.set("enabled",true)}).exit_code()==0);
        s.host.advance(seconds(.25));
        check(s.host.click("main/bar/mark"));s.host.settle(seconds(.5));
        const auto paused=rt.invoke("cellular.snapshot");
        check(paused.exit_code()==0 && !paused.data_value()->get("playing")->as_bool());
        const auto tick=paused.data_value()->get("tick")->as_int();s.host.advance(seconds(.3));
        check(rt.invoke("cellular.snapshot").data_value()->get("tick")->as_int()==tick);
        rt.set_route("explore/reactions");s.host.settle(seconds(.5));
        check(rt.valid(rt.find(field)));
        check(rt.invoke("cellular.snapshot").data_value()->get("tick")->as_int()==tick);
    });
    return suite.run(argc,argv);
}
