import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;
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
        app.setup(host.runtime());for(const auto& v:app.verbs)expect(host.runtime().verbs().add(v.desc,v.fn).has_value());
        host.mount("main",app.view());host.settle(seconds(.5));
    }
};
constexpr std::string_view field="main/body/view/reactions/page/cellular/field";
}
UTest cases{"site.reactions", [] {
    "real workbench route, Agent controls, editable configuration and narrow layout"_test = [] {
        const auto route=site::parse_site_route("zh/explore/reactions");
        expect(route && route->place=="reactions" && route->language=="zh");
        for (const auto width:{260.0f,700.0f,1280.0f}) for (const auto lang:{"en","zh"}) {
            Site s(width,lang);auto& rt=s.host.runtime();const auto node=rt.find(field);
            expect(rt.valid(node));
            const auto bounds=rt.screen_bounds(node);expect(bounds.x>=0 && bounds.width>0 && bounds.x+bounds.width<=width+1);
            const auto initial=rt.invoke("cellular.snapshot");expect(initial.exit_code()==0 && initial.data_value());
            const auto initial_hash=std::string(initial.data_value()->get("state_hash")->as_string());
            expect(rt.invoke("cellular.advance",{.args=Value{}.set("count",16)}).exit_code()==0);s.host.step();
            const auto changed=rt.invoke("cellular.snapshot");
            expect(changed.data_value()->get("state_hash")->as_string()!=initial_hash);
            expect(rt.invoke("cellular.seek",{.args=Value{}.set("frame",0)}).exit_code()==0);s.host.step();
            expect(rt.invoke("cellular.snapshot").data_value()->get("state_hash")->as_string()==initial_hash);
            const auto folder=scratch_dir(std::format("site-reactions-{}-{}",static_cast<int>(width),lang));
            expect(s.host.save_png(folder/"page.png").has_value());
            expect(s.host.tree().violations().empty());
            expect(site::static_html(lang).contains("explore/reactions"));
        }
    };
    "leaving the workbench cancels live playback and navigation returns to the same project"_test = [] {
        Site s(1280,"en");auto& rt=s.host.runtime();
        expect(rt.invoke("cellular.play",{.args=Value{}.set("enabled",true)}).exit_code()==0);
        s.host.advance(seconds(.25));
        expect(s.host.click("main/bar/mark"));s.host.settle(seconds(.5));
        const auto paused=rt.invoke("cellular.snapshot");
        expect(paused.exit_code()==0 && !paused.data_value()->get("playing")->as_bool());
        const auto tick=paused.data_value()->get("tick")->as_int();s.host.advance(seconds(.3));
        expect(rt.invoke("cellular.snapshot").data_value()->get("tick")->as_int()==tick);
        rt.set_route("explore/reactions");s.host.settle(seconds(.5));
        expect(rt.valid(rt.find(field)));
        expect(rt.invoke("cellular.snapshot").data_value()->get("tick")->as_int()==tick);
    };
}};
