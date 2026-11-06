// Headless automation audition: one frozen timeline signal drives an actual
// audio bus and a read-only visualization. No window or audio device is opened.
module omui.cli;
import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import omui.core;
import xengine.timeline;
import xengine.timeline.audio;
import xengine.audio;
import xrender.vector;
import mlibs.image;
import omui.host.headless;
import omui.ui.base;

namespace omui::cli {
namespace cmd=mcpplibs::cmdline;
namespace tl=xengine::timeline;
namespace au=xengine::audio;

cmd::App audio_command() {
    cmd::App app{"audio"};
    (void)app.description("Offline audio controls and real visual inspection; no window or device");
    cmd::App preview{"automation"};
    (void)preview.description("Audition one timeline gain curve on a 220 Hz reference tone; export WAV, PNG, WebP and JSON");
    preview.arg("file").required().help("a .timeline.omui.toml file");
    preview.option("track").takes_value().help("the exact track name");
    preview.option("clip").takes_value().help("the exact clip name");
    preview.option("property").takes_value().help("keyed gain property (default gain)");
    preview.option("out").takes_value().help("output prefix; its parent directory must exist");
    preview.option("seconds").takes_value().help("whole seconds to audition, 1..10 (default 2)");
    preview.option("fps").takes_value().help("exact timeline rate in 1..120 (default 60); does not change audio");
    (void)app.subcommand(std::move(preview));
    return app;
}

int audio(const cmd::ParsedArgs& p, std::ostream& out, std::ostream&) {
    Value report=Value{}.set("kind","audio.automation").set("ok",false);
    const auto fail=[&](std::string message,int code=1) {report.set("ok",false).set("error",std::move(message));out<<report.json()<<'\n';return code;};
    if(!p.has_subcommand() || p.subcommand_name()!="automation") return fail("usage: omui audio automation FILE --track NAME --clip NAME --out PREFIX",2);
    const auto& a=p.subcommand()->get();
    const auto track=a.value("track"), clip=a.value("clip"), prefix=a.value("out");
    if(!track || !clip || !prefix || prefix->empty())return fail("--track, --clip and --out are required",2);
    int duration=2,fps=60;
    const auto integer=[&](std::string_view key,int& value) {
        if(const auto text=a.value(key)) {const auto [end,error]=std::from_chars(text->data(),text->data()+text->size(),value);return error==std::errc{} && end==text->data()+text->size();}
        return true;
    };
    if(!integer("seconds",duration) || duration<1 || duration>10 || !integer("fps",fps) || fps<1 || fps>120 || !tl::exact({fps,1}))
        return fail("seconds must be an integer in 1..10; fps must be an exact timeline rate in 1..120",2);
    const auto file=mcpplibs::os::fs::from_utf8(a.positional(0));
    const auto source=mcpplibs::os::fs::read(file);if(!source)return fail(source.error().message());
    const auto timeline=tl::read_timeline(*source,std::string(a.positional(0)));
    if(!timeline) {Value::List issues;for(const auto& issue:timeline.error())issues.emplace_back(issue.text());report.set("problems",Value(std::move(issues)));return fail("invalid timeline");}
    // A bounded audition tool, not a resampler or an arbitrary scene player.
    if(timeline->sample_rate<8000 || timeline->sample_rate>192000)return fail("audition sample rate must be 8000..192000 Hz");
    const tl::Track* chosen_track=nullptr;const tl::Clip* chosen=nullptr;
    for(const auto& t:timeline->tracks)if(t.name==*track) {if(chosen_track)return fail("ambiguous track name");chosen_track=&t;}
    if(!chosen_track)return fail("track not found");
    if(chosen_track->muted)return fail("selected track is muted");
    for(const auto& c:chosen_track->clips)if(c.name==*clip) {if(chosen)return fail("ambiguous clip name");chosen=&c;}
    if(!chosen)return fail("clip not found");
    const auto property=a.value("property");
    const auto gain=tl::GainAutomation::prepare(*chosen,{.sample_rate=timeline->sample_rate,.property=property?*property:"gain",.outside=0});
    if(!gain)return fail(std::string(tl::gain_error_name(gain.error())));
    au::Mixer mixer({.rate=timeline->sample_rate});
    au::Sound tone{.name="reference-tone",.gain=.1f,.bus="music"};
    tone.layers.push_back({.length=static_cast<float>(duration),.pitch={220,220},
        .envelope={.attack=.005f,.hold=static_cast<float>(duration),.sustain=1,.release=.005f}});
    if(!mixer.automate_gain("music",*gain) || !mixer.play(mixer.add(tone),0))return fail("mixer setup failed");
    const auto pcm=au::render(mixer,static_cast<std::int64_t>(duration)*mixer.rate());
    const auto wav=au::wav(pcm,mixer.rate(),true);
    const auto clock=mixer.clock(),begun=mixer.begun();
    constexpr int width=384,height=256,inside=352;
    std::vector<xrender::raster::Surface> frames;frames.reserve(static_cast<std::size_t>(duration*fps+1));
    Value::List observations;
    std::array<float,inside> curve{};
    float plot_max=1;
    for(int x=0;x<inside;++x) {
        curve[static_cast<std::size_t>(x)]=(*gain)->at(tl::from_seconds(static_cast<double>(x)*duration/(inside-1)));
        plot_max=std::max(plot_max,curve[static_cast<std::size_t>(x)]);
    }
    Headless host({.size={width,height},.fps={fps,1},.background=Color::hex(0x101827),
        .theme=Theme::dark_theme(),.reduce_motion=true,.parallel_raster=false,.complete_frames=true});
    const float end=static_cast<float>(duration);
    for(int index=0;index<=duration*fps;++index) {
        const auto time=tl::frame_time({fps,1},index);
        const float seconds=static_cast<float>(tl::to_seconds(time));
        const float level=(*gain)->at(time);
        auto drawing=painter({.id="curve",.width=inside,.height=128,.paint=[&curve,plot_max,seconds,end,level](PaintContext& ctx) {
            auto& list=ctx.list;
            list.fill_rect({0,0,inside,96},mlibs::geom::Color::hex(0x1d2a40));
            for(int x=0;x<inside;++x) {
                const float y=92-88*curve[static_cast<std::size_t>(x)]/plot_max;
                list.fill_rect({static_cast<float>(x),y,1,2},mlibs::geom::Color::hex(0x67d5b5));
            }
            const float cursor=(inside-2)*std::clamp(seconds/end,0.0f,1.0f);
            list.fill_rect({cursor,0,2,96},mlibs::geom::Color::hex(0xf1b56a));
            list.fill_rect({0,112,inside,12},mlibs::geom::Color::hex(0x1d2a40));
            list.fill_rect({0,112,inside*std::clamp(level/plot_max,0.0f,1.0f),12},mlibs::geom::Color::hex(0x67d5b5));
        }});
        host.mount("main",column({.id="audition",.width=width,.height=height,.padding=Edges(16),.gap=8,.background="#101827"}, {
            text({.id="title",.text="Gain automation",.style="heading",.size=20.0f}),std::move(drawing),
            text({.id="reading",.text=std::format("{:.2f} s   ·   Gain {:.3f}×",seconds,level),.size=14.0f}),
            text({.id="scale",.text=std::format("0–{:.1f}× gain · 220 Hz reference tone",plot_max),.style="caption",.size=11.0f})}));
        if(!host.step().failure.empty())return fail("visual render failed");
        const auto& pixels=host.pixels();
        observations.push_back(Value{}.set("flicks",time).set("seconds",tl::to_seconds(time)).set("gain",static_cast<double>(level))
            .set("pixel_hash",std::format("{:016x}",pixels.hash())));
        frames.push_back(pixels);
    }
    if(mixer.clock()!=clock || mixer.begun()!=begun)return fail("visual inspection changed live audio");
    const auto view=[](const xrender::raster::Surface& surface) {return mlibs::image::View{reinterpret_cast<const std::uint8_t*>(surface.pixels().data()),surface.width(),surface.height(),surface.width()*4,mlibs::image::Alpha::premultiplied};};
    xrender::raster::Surface sheet(4*width,2*height);
    for(std::size_t k=0;k<8;++k) {
        const auto& frame=frames[(frames.size()-1)*k/7];
        for(int y=0;y<height;++y)std::copy_n(frame.row(y),width,sheet.row(static_cast<int>(k/4)*height+y)+static_cast<int>(k%4)*width);
    }
    const auto png=mlibs::image::encode_png(view(sheet));if(!png)return fail("PNG encoding failed");
    const auto save=[&](std::string_view suffix,std::span<const std::byte> bytes) {
        return mcpplibs::os::fs::write_atomic(mcpplibs::os::fs::from_utf8(std::string(*prefix)+std::string(suffix)),
            std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    };
    Value::List outputs;
    const auto saved_wav=save(".wav",wav);if(!saved_wav)return fail(saved_wav.error().message());outputs.emplace_back(std::string(*prefix)+".wav");report.set("outputs",Value(outputs));
    const auto saved_png=save(".png",std::as_bytes(std::span(*png)));if(!saved_png)return fail(saved_png.error().message());outputs.emplace_back(std::string(*prefix)+".png");report.set("outputs",Value(outputs));
    std::vector<mlibs::image::View> views;views.reserve(frames.size());for(const auto& frame:frames)views.push_back(view(frame));
    const auto saved_webp=mlibs::image::save_animated_webp(mcpplibs::os::fs::from_utf8(std::string(*prefix)+".webp"),views,
        {.delay_num=1,.delay_den=static_cast<std::uint16_t>(fps),.loops=1,.lossless=true});
    if(!saved_webp)return fail("WebP export failed");outputs.emplace_back(std::string(*prefix)+".webp");
    report.set("ok",true).set("outputs",Value(std::move(outputs))).set("rate",mixer.rate()).set("samples",clock)
        .set("voices_begun",begun).set("pcm_hash",std::format("{:016x}",au::hash(pcm)))
        .set("fps",fps).set("duration_seconds",duration).set("renderer","cpu").set("dpi",1)
        .set("viewport",Value(Value::List{width,height})).set("background","#101827")
        .set("file",std::string(a.positional(0))).set("track",std::string(*track)).set("clip",std::string(*clip))
        .set("property",std::string(property?*property:"gain"))
        .set("timeline_snapshot",tl::write_timeline(*timeline)).set("plot_max",static_cast<double>(plot_max))
        .set("audition","220 Hz reference tone at 0.1 gain; no device playback; read-only omui headless view")
        .set("observations",Value(std::move(observations)));
    const auto written=mcpplibs::os::fs::write_atomic(mcpplibs::os::fs::from_utf8(std::string(*prefix)+".report.omui.json"),report.json()+"\n");
    if(!written)return fail(written.error().message());
    out<<report.json()<<'\n';return 0;
}
}
