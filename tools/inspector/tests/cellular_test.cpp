import std;
import openmui;
import openmui.inspector;
import mlibs.testing;
import mcpplibs.os;

using namespace omui;
using namespace mlibs::testing;
namespace lab=omui::inspector::cellular;
namespace {
struct Fixture {
    std::shared_ptr<lab::Session> session=std::make_shared<lab::Session>();
    Headless host;
    AppSpec spec;
    explicit Fixture(float width=960,bool zh=false,float scale=1)
        :host({.size={width,900},.scale=scale,.reduce_motion=true,.parallel_raster=false,.complete_frames=true}),
         spec(lab::app(session,zh)) {
        auto access=std::make_shared<AgentAccess>();
        access->set_policy({.mode=AgentMode::development,.source="test-host"});
        host.runtime().set_agent_access(std::move(access));
        spec.setup(host.runtime());for (const auto& v:spec.verbs) check(host.runtime().verbs().add(v.desc,v.fn).has_value());
        host.mount("main",spec.view());host.step();
    }
    std::string picture() {
        const auto node=host.runtime().find("main/cellular/field");
        check(host.runtime().valid(node));
        const auto image=host.png(host.runtime().screen_bounds(node));check(image.has_value());
        return image?std::format("{:016x}",mlibs::hash::fnv1a(std::as_bytes(std::span(*image)))):"";
    }
};
std::vector<Value> talk(agent::Protocol& p,std::string_view op,Value args={}) {
    std::vector<Value> records;
    p.request(Value{}.set("op","invoke").set("verb","cellular."+std::string(op)).set("args",std::move(args)).json(),
        [&](std::string_view line){const auto v=parse_json(line);check(v.has_value());if(v)records.push_back(*v);});
    return records;
}
int status(const std::vector<Value>& records) {
    return records.empty()?-1:static_cast<int>(records.back().get("exitCode")->as_int(-1));
}
Value paint(int x,int y) {return Value{}.set("x",x).set("y",y).set("radius",1).set("brush","spark");}
}
int main(int argc,char** argv) {
    Suite suite{"inspector.cellular"};
    suite.test("untrusted projects and operations fail atomically with integer and capacity checks",[] {
        lab::Session s;const auto before=s.snapshot();const auto project=s.project();
        for (auto key:{"version","rate","tick"}) {
            Value bad=project;bad.set(key,1.5);
            check(!s.perform("load",Value{}.set("project",bad)));check(s.snapshot()==before);
        }
        auto cells=*project.get("cells")->list();cells[0]=Value(-1);
        auto bad=project;bad.set("cells",Value(cells));
        check(!s.perform("load",Value{}.set("project",bad)));check(s.snapshot()==before);
        cells.pop_back();bad=project;bad.set("cells",Value(std::move(cells)));
        check(!s.perform("load",Value{}.set("project",bad)));check(s.snapshot()==before);
        check(!s.perform("configure",Value{}.set("config",Value{}.set("unknown",1))));
        check(!s.perform("advance",Value{}.set("count",1.0)));
        check(!s.perform("advance",Value{}.set("count",121)));
        check(!s.perform("seek",Value{}.set("frame",1)));
        check(!s.perform("paint",paint(999,1)));
        auto stale=paint(1,1);stale.set("revision",1);
        check(!s.perform("paint",stale));check(s.snapshot()==before);
        check(s.perform("describe")->get("sop")->as_string().contains("capture"));
    });
    suite.test("every intermediate state, edited input, seek and exported project reproduce",[] {
        lab::Session s;std::vector<std::string> hashes{s.state_hash()};
        for (int i=0;i<70;++i) {
            check(s.perform("advance",Value{}.set("count",1)).has_value());hashes.push_back(s.state_hash());
        }
        check(hashes[0]!=hashes[15] && hashes[15]!=hashes[35]);
        for (int i=70;i>=0;--i) {
            check(s.perform("seek",Value{}.set("frame",i)).has_value());check(s.state_hash()==hashes[static_cast<std::size_t>(i)]);
        }
        check(s.perform("paint",paint(52,28)).has_value());check(s.snapshot().get("tick")->as_int()==0);
        const auto painted=s.state_hash();check(!s.perform("seek",Value{}.set("frame",2)));
        check(s.perform("advance",Value{}.set("count",30)).has_value());
        lab::Session loaded;check(loaded.perform("load",Value{}.set("project",s.project())).has_value());
        check(loaded.state_hash()==s.state_hash());
        for (int i=0;i<10;++i) {
            check(s.perform("advance",Value{}.set("count",1)).has_value());
            check(loaded.perform("advance",Value{}.set("count",1)).has_value());check(loaded.state_hash()==s.state_hash());
        }
        check(s.perform("seek",Value{}.set("frame",1)).has_value());check(s.state_hash()==painted);
        check(s.perform("configure",Value{}.set("config",Value{}.set("rate",24))).has_value());
        check(s.snapshot().get("rate")->as_int()==24 && s.snapshot().get("frame")->as_int()==0);
    });
    suite.test("real pointer and Agent paint the same cells; edits have working actor undo and redo",[] {
        Fixture human,automated;
        const auto before=human.session->state_hash(),picture=human.picture();
        const auto box=human.host.runtime().screen_bounds(human.host.runtime().find("main/cellular/field"));
        const float cell=std::min(box.width/64,box.height/36);
        human.host.click({box.x+(box.width-cell*64)/2+12.5f*cell,box.y+(box.height-cell*36)/2+9.5f*cell});
        human.host.step();check(human.session->state_hash()!=before);
        agent::Protocol protocol(automated.host.runtime());
        check(status(talk(protocol,"paint",paint(12,9)))==0);automated.host.step();
        check(automated.session->state_hash()==human.session->state_hash());
        check(human.picture()!=picture && automated.picture()==human.picture());
        check(human.host.runtime().undo(human.host.runtime().root()).exit_code()==0);human.host.step();
        check(human.session->state_hash()==before && human.picture()==picture);
        check(human.host.runtime().redo(human.host.runtime().root()).exit_code()==0);human.host.step();
        check(human.session->state_hash()==automated.session->state_hash());
    });
    suite.test("live continuation preserves authority; revoked sessions and read-only layers stop writes",[] {
        Fixture f;auto binding=f.host.runtime().agent_access_binding();
        const auto session=binding->open_session();
        agent::Protocol p(f.host.runtime(),{.session=session});
        check(!f.host.runtime().current_invocation());
        check(status(talk(p,"play",Value{}.set("enabled",true)))==0);
        f.host.advance(seconds(.35));const auto tick=f.session->snapshot().get("tick")->as_int();check(tick>0);
        check(binding->revoke_session(*session));f.host.advance(seconds(.3));
        check(f.session->snapshot().get("tick")->as_int()==tick && !f.session->snapshot().get("playing")->as_bool());
        auto policy=binding->policy();policy.user.cap=openas::Cap{openas::Effect::read};binding->set_policy(policy);
        agent::Protocol reader(f.host.runtime());const auto hash=f.session->state_hash();
        check(status(talk(reader,"snapshot"))==0);check(status(talk(reader,"paint",paint(3,3)))!=0);
        check(status(talk(reader,"load",Value{}.set("project",lab::example_project())))!=0);
        check(f.session->state_hash()==hash);
        // Physical local use remains available under the Agent-only restriction.
        check(f.host.runtime().invoke("cellular.advance",{.args=Value{}.set("count",1)}).exit_code()==0);
    });
    suite.test("loading and undoing a smaller project keeps keyboard editing and project controls valid",[] {
        Fixture f;auto& rt=f.host.runtime();const auto original=f.session->state_hash();
        auto project=f.session->project();project.set("size",Value(Value::List{2,2}));
        project.set("cells",Value(Value::List(12,Value(0))));
        auto brushes=*project.get("brushes")->list();brushes.resize(1);brushes[0].set("id","custom").set("label","Custom");
        project.set("brushes",Value(std::move(brushes)));
        check(rt.invoke("cellular.load",{.args=Value{}.set("project",project)}).exit_code()==0);f.host.step();
        check(f.session->cursor_x.peek()==1 && f.session->cursor_y.peek()==1 && f.session->brush.peek()=="custom");
        rt.focus(rt.find("main/cellular/field"));
        f.host.key(Key::left);f.host.key(Key::up);f.host.key(Key::space);f.host.step();
        check(f.session->cursor_x.peek()==0 && f.session->cursor_y.peek()==0);
        const auto inspected=rt.invoke("cellular.inspect",{.args=Value{}.set("x",0).set("y",0)});
        check(inspected.exit_code()==0 && inspected.data_value());
        if (inspected.data_value()) check((*inspected.data_value()->get("values")->list())[1].as_int()==1);
        check(rt.undo(rt.root()).exit_code()==0); // keyboard paint
        check(rt.undo(rt.root()).exit_code()==0);f.host.step(); // project load
        check(f.session->state_hash()==original && f.session->brush.peek()=="spark");
        const auto draft=parse_json(f.session->draft.peek());const auto config=f.session->perform("config");
        check(draft && config);if (draft && config) check(*draft==*config);
        check(rt.redo(rt.root()).exit_code()==0);f.host.step();
        check(f.session->shape()==Size(2,2) && f.session->brush.peek()=="custom");
    });
    suite.test("resizing one workspace recomputes real pointer coordinates without changing its project",[] {
        Fixture f;auto& rt=f.host.runtime();const auto initial=f.session->state_hash();
        for (const float width:{260.0f,1280.0f,700.0f}) {
            rt.resize({width,900},1);f.host.step();
            const auto b=rt.screen_bounds(rt.find("main/cellular/field"));
            check(b.width>0 && b.x>=0 && b.x+b.width<=width+1);
            const float cell=std::min(b.width/64,b.height/36);
            f.host.click({b.x+(b.width-cell*64)/2+42.5f*cell,b.y+(b.height-cell*36)/2+28.5f*cell});
            f.host.step();check(f.session->cursor_x.peek()==42 && f.session->cursor_y.peek()==28);
            check(f.session->state_hash()!=initial);
            check(rt.undo(rt.root()).exit_code()==0);f.host.step();check(f.session->state_hash()==initial);
        }
    });
    suite.test("actual intermediate pixels and reverse replay are exact at narrow Chinese and high DPI layouts",[] {
        for (const auto [width,zh,scale]:std::array<std::tuple<float,bool,float>,3>{{{960,false,1},{260,true,1},{640,true,2}}}) {
            Fixture f(width,zh,scale);std::vector<std::string> pixels{f.picture()};
            const auto folder=scratch_dir(std::format("cellular-{}-{}",static_cast<int>(width),static_cast<int>(scale)));
            check(f.host.save_png(folder/"first.png").has_value());
            for (int i=0;i<24;++i) {
                check(f.session->perform("advance",Value{}.set("count",1)).has_value());f.host.step();
                pixels.push_back(f.picture());
                if (i==7 || i==15 || i==23) check(f.host.save_png(folder/std::format("tick-{}.png",i+1)).has_value());
            }
            check(pixels[0]!=pixels[8] && pixels[8]!=pixels[16] && pixels[16]!=pixels[24],"canvas pixels, not just status text, change");
            for (int i=24;i>=0;--i) {
                check(f.session->perform("seek",Value{}.set("frame",i)).has_value());f.host.step();check(f.picture()==pixels[static_cast<std::size_t>(i)]);
            }
            const auto b=f.host.runtime().screen_bounds(f.host.runtime().find("main/cellular/field"));
            check(b.x>=0 && b.x+b.width<=width+1 && b.width>0);
            check(f.host.tree().violations().empty());
        }
    });
    suite.test("batch CLI performs real Agent operations and exports reloadable state and timed visual evidence",[] {
        const auto folder=scratch_dir("cellular-cli");
        const auto job=R"({"commands":[{"op":"paint","args":{"x":45,"y":24,"radius":1,"brush":"spark"}}],"samples":{"frames":8,"step":1},"size":[640,760]})";
        check(mcpplibs::os::fs::write_atomic(folder/"job.json",job).has_value());
        std::ostringstream out;
        check(lab::batch(folder/"job.json",folder/"process",out)==0,out.str().substr(0,500));
        const auto report=parse_json(out.str());check(report && report->get("ok")->as_bool());
        if (!report) return;
        const auto* observations=report->get("observations")->list();check(observations && observations->size()==9);
        if (observations) {
            check(observations->front().get("pixel_hash")->as_string()!=observations->back().get("pixel_hash")->as_string());
            check(observations->back().get("time_numerator")->as_int()==8 && observations->back().get("time_denominator")->as_int()==12);
        }
        const auto saved=mcpplibs::os::fs::read(folder/"process.cellular.omui.json");check(saved.has_value());
        if (saved) {lab::Session s;const auto p=parse_json(*saved);check(p.has_value());if(p)check(s.perform("load",Value{}.set("project",*p)).has_value());}
        const auto animation=mcpplibs::os::fs::read(folder/"process.webp");check(animation && animation->size()>20);
        std::ostringstream bad;
        check(lab::batch(folder/"job.json",folder/"missing"/"result",bad)!=0);
        const auto failed=parse_json(bad.str());check(failed && !failed->get("ok")->as_bool());
    });
    return suite.run(argc,argv);
}
