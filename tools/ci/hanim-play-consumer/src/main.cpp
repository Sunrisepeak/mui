// Public console consumer. This translation unit never imports a UI or test API.
import std;
import hanim;
import hanim.render.vector;
import mlibs.raster;
import mlibs.image;
import openmui.hash;
import mlibs.control;
import mcpplibs.os;
import nlohmann.json;

using namespace hanim;
namespace render = omui::render;
namespace raster = mlibs::raster;
namespace geom = omui::geom;
using Json = nlohmann::json;

namespace {
struct Options {
    double time = .5;
    double bend = 0;
    int width = 320, height = 240;
    std::array<int,4> region{0,0,320,240};
    bool cropped = false, retarget = false, agent_review = false, deny_capture = false, properties = false, parts = false, channels = false;
    std::string output;
};
int fail(std::string_view message,int code=1) {
    std::println("{}",Json{{"kind","hanim.play-consumer"},{"ok",false},{"error",message}}.dump());
    return code;
}
template<class T> bool number(std::string_view text,T& result) {
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),result);
    return error==std::errc{} && end==text.data()+text.size();
}
template<std::size_t N> bool integers(std::string_view text,std::array<int,N>& values) {
    for(std::size_t i=0;i<N;++i) {
        const auto comma=text.find(',');
        if(!number(text.substr(0,comma),values[i]))return false;
        if(i+1==N)return comma==text.npos;
        if(comma==text.npos)return false;
        text.remove_prefix(comma+1);
    }
    return false;
}
std::optional<Options> parse(int argc,char** argv) {
    Options options;
    for(int i=1;i<argc;++i) {
        const std::string_view key=argv[i];
        if(key=="--retarget") {options.retarget=true;continue;}
        if(key=="--properties") {options.properties=true;continue;}
        if(key=="--parts") {options.parts=true;continue;}
        if(key=="--channels") {options.channels=true;continue;}
        if(key=="--agent-review") {options.agent_review=true;continue;}
        if(key=="--agent-deny-capture") {options.deny_capture=true;continue;}
        if(i+1==argc)return {};
        const std::string_view value=argv[++i];
        if(key=="--out") options.output=value;
        else if(key=="--time") {if(!number(value,options.time))return {};}
        else if(key=="--bend") {if(!number(value,options.bend))return {};}
        else if(key=="--region") {if(!integers(value,options.region))return {};options.cropped=true;}
        else if(key=="--viewport") {
            std::array<int,2> size{};if(!integers(value,size))return {};
            options.width=size[0];options.height=size[1];
        } else return {};
    }
    if(options.deny_capture && !options.agent_review)return {};
    if((options.properties || options.parts || options.channels) && options.retarget)return {}; // separate property retarget contract is tested by the API probe
    if(int(options.properties)+int(options.parts)+int(options.channels)>1)return {};
    if(!options.cropped)options.region={0,0,options.width,options.height};
    const auto [x,y,w,h]=options.region;
    if(!std::isfinite(options.time) || options.time<0 || options.time>1 || !std::isfinite(options.bend) ||
       std::abs(options.bend)>2 || options.output.empty() ||
       options.width<32 || options.height<32 || options.width>2048 || options.height>2048 ||
       x<0 || y<0 || w<=0 || h<=0 || x>options.width-w || y>options.height-h)return {};
    return options;
}
Json object(ObjectRef ref) {return {{"owner",ref.owner},{"slot",ref.slot},{"generation",ref.generation}};}
ObjectDescription describe(Contour shape,Rgba fill,int width,int height) {
    const double scale=std::min(width,height)*.3;
    return {{{.id="shape",.local={.tx=width*.5,.ty=height*.5,.sx=scale,.sy=scale},.contours={shape},.fill=fill}}};
}

// A native host adapter in the consumer, not another protocol/dispatcher in
// hanim. The engine owns all playback and the shared renderer owns all pixels.
template<class Draw, class Save>
std::expected<Json,std::string> review_agent(Engine& engine, PlaybackRef playback,
    const raster::Surface& pixels, const Options& options, Draw&& draw, Save&& save) {
    namespace control = mlibs::control;
    auto access=std::make_shared<control::Access>(control::policy_for_profile("dev"));
    const control::Invocation request{.actor={openas::Kind::agent,"reviewer",{"person:forged"}},
        .remote=true,.access=access,.session=access->open_session()};
    control::InvocationContext context;
    control::Invocation queued;
    std::size_t executions=0;
    auto invoke=[&](const control::Invocation& given,std::string_view operation,openas::Effect effect,auto&& execute) {
        control::Invocation copy;
        const auto& bound=control::bind_invocation(given,copy,access,context.current(),operation);
        // This provider filters domain operations. The bound call retains
        // its origin, while authorization checks the operation entered now.
        if(bound.remote && access->decide(operation,effect,false,bound.actor,bound.session.get(),operation)!=openas::Decision::run)
            return false;
        if(bound.dry_run)return true;
        control::InvocationScope scope(context,bound);
        queued=*context.current();
        ++executions;
        return execute();
    };
    auto seek=[&](const control::Invocation& call,double t) {
        return invoke(call,"animation.seek",openas::Effect::reversible,[&] {return bool(engine.seek(playback,seconds{t}));});
    };
    auto capture=[&](const control::Invocation& call) {
        return invoke(call,"animation.capture",openas::Effect::read,draw);
    };
    if(options.deny_capture) {
        auto policy=access->policy();policy.user.operations=std::vector<std::string>{"animation.seek"};access->set_policy(policy);
    }
    Json samples=Json::array();std::vector<std::uint64_t> hashes;std::string kept_region;
    for(int i=0;i<=60;++i) {
        const double time=static_cast<double>(i)/60;
        if(!seek(request,time))return std::unexpected("Agent seek refused");
        if(!capture(request))return std::unexpected("Agent capture refused");
        const auto info=engine.inspect(playback);
        if(!info || info->position!=seconds{time})return std::unexpected("Agent sample position mismatch");
        const auto suffix=std::format(".agent-{:03}.png",i);
        if(!save(suffix))return std::unexpected("Agent process PNG write failed");
        hashes.push_back(pixels.hash());
        samples.push_back({{"index",i},{"time_numerator",i},{"time_denominator",60},
            {"sample_time_seconds",info->position.count()},{"plan_revision",info->plan_revision},
            {"target_revision",info->target_revision},{"pixel_fnv1a",std::format("{:016x}",pixels.hash())},
            {"png",options.output+suffix}});
        if(options.parts || options.channels) {
            omui::hash::Fnv1a hash;
            const int cx=static_cast<int>(options.width*.85),cy=static_cast<int>(options.height*.14);
            for(int y=std::max(0,cy-12);y<std::min(options.height,cy+12);++y)
                for(int x=std::max(0,cx-12);x<std::min(options.width,cx+12);++x)
                hash.bytes(std::bit_cast<std::array<std::uint8_t,4>>(pixels.at(x,y)));
            const auto value=hash.hex();
            if(i && value!=kept_region)return std::unexpected("unselected part's real pixel region changed");
            kept_region=value;samples.back()["kept_region_fnv1a"]=value;
        }
    }
    for(int i=60;i>=0;--i)
        if(!seek(request,static_cast<double>(i)/60) || !capture(request) || pixels.hash()!=hashes[static_cast<std::size_t>(i)])
            return std::unexpected("reverse process replay disagrees");
    if(queued.actor.via!=std::vector{request.session->owner()})return std::unexpected("host attribution missing");
    const auto saved_position=engine.inspect(playback)->position;
    const auto saved_pixels=pixels.hash();
    // A read-only user layer restricts the same trusted development session.
    auto policy=access->policy();policy.user.cap=openas::Cap{openas::Effect::read};access->set_policy(policy);
    const auto before=executions;
    if(seek(request,.9) || executions!=before || engine.inspect(playback)->position!=saved_position || !capture(request))
        return std::unexpected("read-only policy did not isolate mutation");
    policy.user.cap.reset();access->set_policy(policy);
    auto dry=request;dry.dry_run=true;
    if(!seek(dry,.9) || engine.inspect(playback)->position!=saved_position)return std::unexpected("dry run changed playback");
    auto capture_only=request;
    capture_only.session=access->open_session({.operations=std::vector<std::string>{"animation.capture"}});
    if(!invoke(capture_only,"animation.capture",openas::Effect::read,[&] {return !seek({},.9) && draw();}) ||
       engine.inspect(playback)->position!=saved_position || !capture(request))
        return std::unexpected("nested operation escaped its session filter");
    auto other=std::make_shared<control::Access>(control::policy_for_profile("dev"));
    auto foreign=request;foreign.session=other->open_session();
    if(seek(foreign,.9) || capture(foreign))return std::unexpected("foreign session accepted");
    // Copy a live call at scheduling time; revoke before its worker resumes.
    const auto continuation=queued;
    if(!access->revoke_session(*request.session))return std::unexpected("revocation failed");
    const auto before_worker=executions;
    {
        control::InvocationScope worker(context,continuation);
        if(seek({},.9) || capture({}))return std::unexpected("deferred call lost revoked authority");
    }
    if(executions!=before_worker || engine.inspect(playback)->position!=saved_position || pixels.hash()!=saved_pixels)
        return std::unexpected("refused continuation changed state or pixels");
    return Json{{"provider","native consumer adapter"},{"authorization","mlibs.control/openas"},
        {"policy_source","profile:dev"},{"session_owner",request.session->owner()},
        {"frames",std::move(samples)},{"reverse_replayed",61},{"read_only_preserved_state",true},
        {"dry_run_preserved_state",true},{"foreign_session_refused",true},{"revoked_continuation_refused",true},
        {"claimed_provenance_replaced",true},{"nested_operation_filter_preserved",true},{"executions",executions}};
}
} // namespace

int main(int argc,char** argv) {
    const auto parsed=parse(argc,argv);
    if(!parsed)return fail("usage: --out FILE.png [--time SECONDS(0..1)] [--bend FRACTION(-2..2)] [--viewport W,H] [--region X,Y,W,H] [--retarget | --properties | --parts | --channels] [--agent-review] [--agent-deny-capture]",2);
    const auto& options=*parsed;
    auto made=Engine::make();if(!made)return fail("Engine::make");
    auto engine=std::move(*made);
    constexpr double k=.5522847498307936;
    const std::array lines{ShapeSegment::line({-1,1}),ShapeSegment::line({1,1})};
    const std::array curves{ShapeSegment::cubic({k,-1},{1,-k},{1,0}),ShapeSegment::cubic({1,k},{k,1},{0,1}),
        ShapeSegment::cubic({-k,1},{-1,k},{-1,0}),ShapeSegment::cubic({-1,-k},{-k,-1},{0,-1})};
    const std::array diamond{ShapeSegment::line({-1,0}),ShapeSegment::line({0,1}),ShapeSegment::line({1,0})};
    const auto triangle=Contour::copy({{0,-1},lines}),circle=Contour::copy({{0,-1},curves}),third=Contour::copy({{0,-1},diamond});
    if(!triangle || !circle || !third)return fail("public contour preparation");
    auto source=engine.create_object(describe(*triangle,{1,.25f,.1f,1},options.width,options.height));
    const auto target=engine.create_object(describe(*circle,{.1f,.6f,1,1},options.width,options.height));
    const auto next=engine.create_object(describe(*third,{.8f,.15f,.9f,1},options.width,options.height));
    if(!source || !target || !next)return fail("public object creation");
    const auto effects=fx::chain({fx::blur(0,1.5f),fx::tone({}, {.r=.3f,.g=.8f,.b=1,.amount=.35f})});
    // Exercise the public compiler and multi-track player from a real external
    // project too. Pixel export below remains the fixed single-segment fixture.
    const auto a=engine.capture_object(*source),b=engine.capture_object(*target),c=engine.capture_object(*next);
    if(!a || !b || !c)return fail("program endpoint capture");
    const auto compiled=PreparedProgram::prepare(parallel({
        sequence({bind(*a,*b,effects),bind(*a,*c,effects)}),
        bind(*b,*c,{}, {.duration=seconds(2)})}));
    if(!compiled)return fail("public Program compilation");
    const auto instance=engine.play(*compiled);if(!instance)return fail("prepared Program play");
    if(!engine.seek(*instance,seconds(1.5)) || !engine.pause(*instance) || !engine.advance(seconds(10)))return fail("program seek/pause/advance");
    const auto program_info=engine.inspect(*instance);
    const auto program_frame=engine.frame(*instance,0);
    if(!program_info || program_info->track_count!=2 || program_info->segment_count!=3 || program_info->position!=seconds(1.5) ||
       !program_frame || program_frame->animation->source_object()!=*source || program_frame->animation->to().object()!=*next)
        return fail("prepared Program phase/identity");
    if(!engine.reverse(*instance) || !engine.resume(*instance) || !engine.advance(seconds(.5)) ||
       engine.inspect(*instance)->position!=seconds(1) || !engine.cancel(*instance,CancelMode::restore) || !engine.release(*instance))
        return fail("program reverse/restore/release");
    if(options.parts || options.channels) {
        ObjectDescription group{{{.id="group"},
            {.id="left",.parent="group",.local={.tx=options.width*.25,.ty=options.height*.45,.sx=25,.sy=25},.contours={*triangle},.fill=Rgba{1,.25f,.1f,1}},
            {.id="right",.parent="group",.local={.tx=options.width*.7,.ty=options.height*.55,.sx=28,.sy=28},.contours={*triangle},.fill=Rgba{.8f,.6f,.1f,1}},
            {.id="badge",.parent="group",.local={.tx=options.width*.85,.ty=options.height*.14,.sx=8,.sy=8},.contours={*triangle},.fill=Rgba{.2f,.9f,.4f,1}}}};
        source=engine.create_object(group);if(!source)return fail("selected-part object creation");
    }
    auto animation=(options.parts || options.channels) ? anim::to(*source,PropertySet{
        {.position=Vec2{options.width*.42,options.height*.58},.part="left"},
        {.rotation=std::numbers::pi,.part="right"}, {.fill=Rgba{.1f,.6f,1,1},.part="right"}})
        : options.properties ? anim::to(*source,{.position=Vec2{options.width*.66,options.height*.5},
        .rotation=std::numbers::pi,.scale=Vec2{options.height*.19,options.height*.19},.fill=Rgba{.1f,.6f,1,1}})
        : anim::morph(*source,*target);
    animation=animation.duration(seconds(1)).easing(ease::linear);
    if(options.properties || options.parts || options.channels)animation=animation.with_effect(fx::translation_path(PointPath::bend(options.bend)));
    else animation=animation.with_effect(fx::vertex_path(PointPath::bend(options.bend)));
    if(!options.parts && !options.channels)for(const auto& effect:effects)animation=animation.with_effect(effect);
    const Program program=options.channels?parallel(
        anim::to(*source,{.position=Vec2{options.width*.42,options.height*.58},.part="left"}).duration(seconds(1))
            .easing(ease::in_out_cubic).with_effect(fx::translation_path(PointPath::bend(options.bend))),
        sequence(anim::to(*source,{.rotation=std::numbers::pi,.part="right"}).duration(seconds(.5)).easing(ease::out_quad),
                 anim::to(*source,{.rotation=2*std::numbers::pi,.part="right"}).duration(seconds(.5)).easing(ease::in_quad)),
        anim::to(*source,{.fill=Rgba{.1f,.6f,1,1},.part="right"}).duration(seconds(.75))) : Program(animation);
    const auto played=engine.play(program);
    if(!played)return fail("Engine::play");
    // Execute real controls, then explicitly select the requested output time.
    if(!engine.pause(*played) || !engine.advance(seconds{.1}))return fail("pause/advance");
    auto info=engine.inspect(*played);
    if(!info || info->state!=PlaybackState::paused || info->position!=seconds{0})return fail("pause changed playback time");
    if(!engine.seek(*played,seconds{.7}) || !engine.reverse(*played) || !engine.resume(*played) || !engine.advance(seconds{.1}))return fail("seek/reverse/resume/advance");
    info=engine.inspect(*played);
    if(!info || std::abs(info->position.count()-.6)>1e-12 || info->rate!=-1)return fail("reverse did not move backwards");
    if(!engine.pause(*played) || !engine.reverse(*played))return fail("restore forward paused playback");

    render::CpuRenderer renderer;
    raster::Surface pixels(options.width,options.height);
    render::DisplayList list,recording;
    std::vector<geom::Affine> world;
    std::vector<float> parameters;
    std::vector<geom::Rect> layers;
    vector_render::PreparedVector binding;
    auto rebind=[&]() {
        const auto frame=engine.frame(*played);if(!frame)return false;
        auto result=vector_render::PreparedVector::bind(*frame->animation,*frame->effects);if(!result)return false;
        binding=std::move(*result);const auto sizes=binding.requirements();
        world.resize(sizes.world_transforms);parameters.resize(sizes.backend_parameters);layers.resize(sizes.effect_layers);
        return true;
    };
    auto draw=[&]() {
        const auto frame=engine.frame(*played);if(!frame)return false;
        list.clear();
        if(!binding.record(frame->geometry,frame->effect_values,list,{&recording,world,parameters,layers}))return false;
        const auto report=renderer.render(list,pixels,{}, {.background=geom::Color::hex(0x111827)});
        return !report.effects_skipped && !report.external_skipped && !report.invalid_paints && !report.missing_images && !report.missing_glyphs;
    };
    auto save_full=[&](std::string_view suffix) {
        const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(pixels.row(0)),options.width,options.height,options.width*4,mlibs::image::Alpha::premultiplied};
        const auto bytes=mlibs::image::encode_png(view);if(!bytes)return false;
        return bool(mcpplibs::os::fs::write_atomic(mcpplibs::os::fs::from_utf8(options.output+std::string(suffix)),std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size())));
    };
    if(!rebind())return fail("public vector binding");
    auto coverage=[&](raster::Surface& output) {
        const auto frame=engine.frame(*played);if(!frame)return false;
        const std::array white{vector_render::MaterialBinding{"shape",render::Paint::solid({1,1,1,1}),render::Paint::solid({1,1,1,1})}};
        const auto bare=vector_render::PreparedVector::bind(*frame->animation,{},white);if(!bare)return false;
        const auto sizes=bare->requirements();
        std::vector<geom::Affine> matrices(sizes.world_transforms);std::vector<float> params(sizes.backend_parameters);
        std::vector<geom::Rect> bounds(sizes.effect_layers);render::DisplayList staged,commands;
        if(!bare->record(frame->geometry,{},commands,{&staged,matrices,params,bounds}))return false;
        const auto report=renderer.render(commands,output,{},{});
        return !report.invalid_paints && !report.effects_skipped;
    };
    int continuity_max=0,coverage_max=0;std::size_t coverage_off_boundary=0,styled_off_boundary=0,coverage_sum=0;
    if(options.retarget) {
        if(!engine.seek(*played,seconds{.7}) || !draw() || !save_full(".retarget-before.png"))return fail("pre-retarget draw");
        const std::vector<raster::Rgba8> before(pixels.pixels().begin(),pixels.pixels().end());
        raster::Surface original_coverage(options.width,options.height),retarget_coverage(options.width,options.height);
        if(!coverage(original_coverage))return fail("pre-retarget bare coverage");
        MorphOptions path_options;path_options.shape.point_path=PointPath::bend(options.bend);
        if(!engine.retarget(*played,anim::morph_to(*next,path_options),{.duration=seconds{1},.easing=ease::linear}) || !rebind() || !draw())return fail("retarget/rebind/current draw");
        for(std::size_t i=0;i<before.size();++i) {
            const auto a=std::bit_cast<std::array<std::uint8_t,4>>(before[i]),b=std::bit_cast<std::array<std::uint8_t,4>>(pixels.pixels()[i]);
            for(int c=0;c<4;++c)continuity_max=std::max(continuity_max,std::abs(int(a[c])-int(b[c])));
        }
        if(!save_full(".retarget-after.png") || !coverage(retarget_coverage))return fail("retarget witness export/coverage");
        for(int y=0;y<options.height;++y)for(int x=0;x<options.width;++x) {
            const auto a=original_coverage.at(x,y).a,b=retarget_coverage.at(x,y).a;
            const auto delta=std::abs(int(a)-int(b));coverage_max=std::max(coverage_max,delta);coverage_sum+=delta;
            bool edge=a>0 && a<255,styled_edge=false;
            const auto original=before[std::size_t(y)*options.width+x];
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                const auto xx=std::clamp(x+dx,0,options.width-1),yy=std::clamp(y+dy,0,options.height-1);
                edge|=original_coverage.at(xx,yy).a!=a;
                styled_edge|=before[std::size_t(yy)*options.width+xx]!=original;
            }
            if(delta && !edge)++coverage_off_boundary;
            if(original!=pixels.at(x,y) && !styled_edge)++styled_off_boundary;
        }
        // Exact subdivision can change independent CPU flattening by <=1/16
        // device pixel. Reuse the vector consumer's documented 18/255 boundary
        // coverage budget, with exact uniform interiors/exteriors, never a
        // whole-image tolerance. Blur/tone still use the real renderer above.
        if(coverage_max>18 || coverage_off_boundary || coverage_sum>8192 || continuity_max>18 || styled_off_boundary)
            return fail(std::format("retarget coverage max={} off-edge={} sum={}; styled max={} off-edge={}",coverage_max,coverage_off_boundary,coverage_sum,continuity_max,styled_off_boundary));
    }
    Json agent_review;
    if(options.agent_review) {
        auto reviewed=review_agent(engine,*played,pixels,options,draw,save_full);
        if(!reviewed)return fail(reviewed.error(),3);
        agent_review=std::move(*reviewed);
    }
    if(!engine.seek(*played,seconds{options.time}) || !draw())return fail("requested time draw");
    const auto frame=engine.frame(*played);if(!frame)return fail("public frame query");
    const auto [x,y,width,height]=options.region;
    const mlibs::image::View crop{reinterpret_cast<const std::uint8_t*>(pixels.row(y)+x),width,height,options.width*4,mlibs::image::Alpha::premultiplied};
    const auto encoded=mlibs::image::encode_png(crop);if(!encoded)return fail(encoded.error());
    const auto decoded=mlibs::image::decode_png(*encoded);
    if(!decoded || decoded->width!=width || decoded->height!=height)return fail("PNG encode/decode crop dimensions");
    omui::hash::Fnv1a pixel_hash;
    std::size_t visible=0;int crop_delta=0;
    for(int row=0;row<height;++row)for(int column=0;column<width;++column) {
        const auto original=pixels.at(x+column,y+row);const auto i=(std::size_t(row)*width+column)*4;
        const auto alpha=decoded->rgba[i+3];
        const auto reconstructed=raster::premultiply(geom::Color{decoded->rgba[i]/255.f,decoded->rgba[i+1]/255.f,decoded->rgba[i+2]/255.f,alpha/255.f});
        const auto a=std::bit_cast<std::array<std::uint8_t,4>>(original),b=std::bit_cast<std::array<std::uint8_t,4>>(reconstructed);
        pixel_hash.bytes(a);visible+=original!=raster::premultiply(geom::Color::hex(0x111827));
        for(int c=0;c<4;++c)crop_delta=std::max(crop_delta,std::abs(int(a[c])-int(b[c])));
    }
    if(crop_delta>1)return fail("encoded region does not match real viewport pixels");
    const auto output=mcpplibs::os::fs::from_utf8(options.output);
    const auto write=mcpplibs::os::fs::write_atomic(output,std::string_view(reinterpret_cast<const char*>(encoded->data()),encoded->size()));
    if(!write)return fail(write.error().message());
    Json result{{"kind","hanim.play-consumer"},{"ok",true},{"fixture",options.channels?"independent-channels":options.parts?"selected-parts":options.properties?"triangle-properties":"triangle-circle"},
        {"source",object(*source)},{"initial_target",object(*target)},{"target",object(frame->info.target)},
        {"requested_time_seconds",options.time},{"sample_time_seconds",frame->info.position.count()},
        {"duration_seconds",frame->info.duration.count()},{"sample_progress",frame->info.sample_progress?Json(*frame->info.sample_progress):Json(nullptr)},{"playback_state",frame->info.state==PlaybackState::paused?"paused":"other"},{"playback_rate",frame->info.rate},
        {"viewport",{{"width",options.width},{"height",options.height}}},
        {"region",{{"x",x},{"y",y},{"width",width},{"height",height}}},
        {"backend","cpu-reference"},{"output",options.output},{"png_bytes",encoded->size()},
        {"pixel_fnv1a",pixel_hash.hex()},{"visible_pixels",visible},{"crop_roundtrip_max_u8",crop_delta},
        {"parts",frame->geometry.parts.size()},{"contours",frame->geometry.contours.size()},{"cubics",frame->geometry.cubics.size()},
        {"effects",(options.parts || options.channels)?Json::array():Json::array({"blur","tone"})},{"controls",{{"pause_holds",true},{"reverse_moves_backwards",true},{"seek",true},
            {"retarget",options.retarget},{"retarget_from_seconds",options.retarget?.7:0},{"retarget_pixel_max_u8",continuity_max},{"retarget_coverage_max_u8",coverage_max},
            {"retarget_coverage_off_boundary",coverage_off_boundary},{"retarget_styled_off_boundary",styled_off_boundary},
            {"retarget_coverage_total_u8",coverage_sum}}}};
    result[(options.properties || options.parts || options.channels)?"translation_path":"vertex_path"]={{"stage","after-correspondence"},{"space","endpoint-chord"},{"bend",options.bend}};
    if(options.channels) {
        result["channels"]=Json::array();
        for(const auto& channel:frame->animation->channels())result["channels"].push_back({
            {"part",std::string(frame->animation->parts()[channel.part].id)}, {"channel",std::string(property_channel_name(channel.channel))},
            {"clip",channel.clip},{"start",channel.start.count()},{"duration",channel.duration.count()},
            {"ease_kind",static_cast<unsigned>(channel.easing.kind)},{"ease_parameters",channel.easing.p},
            {"path_control1",Json::array({channel.translation_path.control1.x,channel.translation_path.control1.y})},
            {"path_control2",Json::array({channel.translation_path.control2.x,channel.translation_path.control2.y})},
            {"anchor_source",channel.anchor_source},{"source_snapshot",channel.source_snapshot}});
    }
    if(options.agent_review)result["agent_review"]=std::move(agent_review);
    const auto provenance=mcpplibs::os::fs::write_atomic(mcpplibs::os::fs::from_utf8(options.output+".provenance.omui.json"),result.dump(2)+"\n");
    if(!provenance)return fail(provenance.error().message());
    std::println("{}",result.dump());
}
