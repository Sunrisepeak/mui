module omui.inspector;
import std;
import omui;
import mcpplibs.os;

namespace omui::inspector::cellular {
int batch(const std::filesystem::path& input,const std::filesystem::path& prefix,std::ostream& out) {
    Value report=Value{}.set("kind","cellular.process").set("version",1).set("ok",false);
    Value::List outputs;
    const auto fail=[&](std::string why,int code=1) {
        report.set("ok",false).set("error",std::move(why)).set("outputs",Value(outputs));out<<report.json()<<'\n';return code;
    };
    const auto source=mcpplibs::os::fs::read(input);
    if (!source) return fail(source.error().message());
    if (source->size()>2*1024*1024) return fail("batch input exceeds 2 MiB",2);
    const auto job=parse_json(*source);
    if (!job || !job->map()) return fail("batch input must be a JSON object",2);
    for (const auto& [key,value]:*job->map())
        if (key!="project" && key!="commands" && key!="samples" && key!="size") return fail("unknown batch key: "+key,2);
    const auto* commands=job->get("commands");
    if (!commands || !commands->list() || commands->list()->size()>64) return fail("commands must be an array of at most 64 operations",2);
    const auto integer=[](const Value* v,int low,int high) {return v && v->type()==Value::Type::integer && v->as_int()>=low && v->as_int()<=high;};
    int frames=0,step=1,width=640,height=760;
    if (const auto* sampling=job->get("samples")) {
        if (!sampling->map() || !integer(sampling->get("frames"),1,120) || !integer(sampling->get("step"),1,4))
            return fail("samples needs integer frames 1..120 and step 1..4",2);
        for (const auto& [key,value]:*sampling->map()) if (key!="frames" && key!="step") return fail("unknown samples key",2);
        frames=static_cast<int>(sampling->get("frames")->as_int());step=static_cast<int>(sampling->get("step")->as_int());
    }
    if (const auto* size=job->get("size")) {
        if (!size->list() || size->list()->size()!=2 || !integer(&(*size->list())[0],260,1280) || !integer(&(*size->list())[1],240,1000))
            return fail("size must be [width 260..1280, height 240..1000]",2);
        width=static_cast<int>((*size->list())[0].as_int());height=static_cast<int>((*size->list())[1].as_int());
    }
    if (static_cast<std::int64_t>(width)*height*4*(frames+1)>128*1024*1024) return fail("recording exceeds 128 MiB pixel budget",2);
    if (prefix.empty()) return fail("output prefix is required",2);
    auto session=std::make_shared<Session>();const auto spec=app(session);
    Headless host({.size={static_cast<float>(width),static_cast<float>(height)},.reduce_motion=true,
        .parallel_raster=false,.complete_frames=true});
    auto access=std::make_shared<AgentAccess>();
    access->set_policy({.mode=AgentMode::development,.source="trusted-local-cellular-cli"});
    host.runtime().set_agent_access(std::move(access));
    spec.setup(host.runtime());
    for (const auto& v:spec.verbs) {
        const auto added=host.runtime().verbs().add(v.desc,v.fn);
        if (!added) return fail(added.error());
    }
    agent::Protocol protocol(host.runtime(),{.default_actor="agent:cellular-cli",.capture=[&](Rect r){return host.png(r);}});
    Value::List receipts;
    const auto invoke=[&](std::string_view op,Value args)->std::expected<Value,std::string> {
        const auto request=Value{}.set("op","invoke").set("verb","cellular."+std::string(op)).set("args",std::move(args));
        Value::List records;Value data;int status=-1;
        protocol.request(request.json(),[&](std::string_view line) {
            const auto parsed=parse_json(line);if (!parsed) return;
            records.push_back(*parsed);
            if (const auto* body=parsed->get("data")) data=*body;
            if (const auto* code=parsed->get("exitCode")) status=static_cast<int>(code->as_int());
        });
        receipts.push_back(Value{}.set("request",request).set("records",Value(records)));
        if (status!=0) return std::unexpected(Value(std::move(records)).json());
        return data;
    };
    if (const auto* p=job->get("project")) {
        const auto result=invoke("load",Value{}.set("project",*p));if (!result) return fail(result.error());
    }
    for (const auto& command:*commands->list()) {
        if (!command.map() || !command.get("op") || command.get("op")->type()!=Value::Type::text)
            return fail("each command needs an op and optional args",2);
        for (const auto& [key,value]:*command.map()) if (key!="op" && key!="args") return fail("unknown command key",2);
        const auto op=command.get("op")->as_string();
        if (op=="play") return fail("batch playback uses samples; use --cellular --serve --headless for live play",2);
        const auto result=invoke(op,command.get("args")?*command.get("args"):Value{});
        if (!result) return fail(result.error());
    }
    host.mount("main",spec.view());host.step();
    const auto start=session->snapshot();const int rate=static_cast<int>(start.get("rate")->as_int());
    const auto initial=session->project();
    Value::List observations;
    const auto output_path=[&](std::string_view suffix) {return mcpplibs::os::fs::from_utf8(generic_utf8(prefix)+std::string(suffix));};
    for (int frame=0;frame<=frames;++frame) {
        if (frame) {const auto result=invoke("advance",Value{}.set("count",step));if (!result) return fail(result.error());host.step();}
        const auto path=output_path(std::format(".frame-{:03}.png",frame));
        const auto written=host.save_png(path);if (!written) return fail(written.error());
        outputs.emplace_back(generic_utf8(path));if (frames) host.record();
        observations.push_back(Value{}.set("index",frame).set("time_numerator",frame*step).set("time_denominator",rate)
            .set("state",session->snapshot()).set("pixel_hash",std::format("{:016x}",host.pixels().hash())).set("image",generic_utf8(path)));
    }
    if (frames) {
        const auto path=output_path(".webp");
        const auto written=host.save_recording(path,{.delay_num=static_cast<std::uint16_t>(step),.delay_den=static_cast<std::uint16_t>(rate),.loops=1,.lossless=true});
        if (!written) return fail(written.error());outputs.emplace_back(generic_utf8(path));
    }
    const auto saved=output_path(".cellular.omui.json");
    const auto written=mcpplibs::os::fs::write_atomic(saved,session->project().json()+"\n");
    if (!written) return fail(written.error().message());outputs.emplace_back(generic_utf8(saved));
    report.set("ok",true).set("outputs",Value(outputs)).set("input",*job).set("initial_sample_project",initial)
        .set("renderer","omui-headless-cpu").set("dpi",1).set("viewport",Value(Value::List{width,height}))
        .set("background","#ffffff").set("observations",Value(std::move(observations))).set("receipts",Value(std::move(receipts)))
        .set("replay_timing","rational sample timestamps; WebP rounds cumulative timestamps to milliseconds")
        .set("scope","deterministic fixed-step process; does not certify realtime frame pacing or hardware performance");
    const auto result=mcpplibs::os::fs::write_atomic(output_path(".report.omui.json"),report.json()+"\n");
    if (!result) return fail(result.error().message());
    out<<report.json()<<'\n';return 0;
}
}
