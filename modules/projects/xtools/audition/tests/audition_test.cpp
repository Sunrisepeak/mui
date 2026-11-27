import std;
import xtools.audition;
import omui.core;
import mlibs.image;
import meng.audio;
import mlibs.testing;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace fs=mcpplibs::os::fs;

namespace {
// The timeline as its author writes it: a TypeScript data module, evaluated by the command.
constexpr std::string_view curve=R"(import { defineTimeline } from "omui/define";

export default defineTimeline({
  name: "Audition", rate: 60, sample_rate: 48000, size: [384, 256],
  tracks: [{
    name: "music", kind: "animation",
    clips: [{
      name: "fade", source: "gain-control", at: "0.25s", length: "1.5s", level: 0.75,
      fade: { in: "0.1s", out: "0.2s", ease: "in_out_cubic" },
      properties: [{ name: "gain", keys: [["0s", 0.2, "in_out_cubic"], ["0.5s", 0.9], ["0.8s", 0.4, "hold"], ["1.1s", 0.7], ["1.5s", 0.1]] }],
    }],
  }],
});
)";
struct Result {int code;std::string out,err;};
// The operation's answer: its exit code, its data record's text as `out` (the report), what went wrong as `err`.
// As the command line gives it: `xtools audition automation FILE …`, the subject alone.
Result invoke(std::vector<std::string> args) {
    xtools::audition::Audition audition;
    mlibs::agent::SubjectSession session({&audition}, {.command_line = true});
    const std::vector<std::string_view> words(args.begin(), args.end());
    std::ostringstream out,err;const auto code=mlibs::agent::command_line(session,words,out,err,{.product="audition"});
    std::string data,said;
    std::istringstream lines(out.str());
    for(std::string line;std::getline(lines,line);)
        if(const auto record=parse_json(line);record && record->get("kind")) {
            if(record->get("kind")->as_string()=="data" && record->get("data") && data.empty())data=record->get("data")->json();
            if(record->get("kind")->as_string()=="error" && record->get("text"))said+=std::string(record->get("text")->as_string());
        }
    return {code,data,said+err.str()};
}
}

UTest cases{"xtools.audition", [] {
    "headless gain audition exports actual WAV and an omui visual process at three observation rates"_test = [] {
        const auto directory=scratch_dir("audio-automation");
        const auto file=directory/"audition.timeline.xdef.ts";
        expect(fs::write_atomic(file,curve).has_value());
        std::string previous_pcm,previous_wav;
        for(const int fps : {30,60,120}) {
            const auto prefix=utf8(directory/std::format("process-{}",fps));
            const auto result=invoke({"automation",utf8(file),"--track","music","--clip","fade","--out",prefix,"--fps",std::to_string(fps)});
            expect(result.code==0) << result.out+result.err;if(result.code!=0)return;
            const auto report=parse_json(result.out);expect(report.has_value());if(!report)return;
            expect(report->get("ok")->as_bool());
            expect(report->get("samples")->as_int()==96000 && report->get("voices_begun")->as_int()==1);
            const auto* samples=report->get("observations")->list();
            expect(samples && samples->size()==static_cast<std::size_t>(2*fps+1));if(!samples)return;
            expect(samples->front().get("seconds")->as_double()==0 && samples->back().get("seconds")->as_double()==2);
            expect(samples->front().get("gain")->as_double()==0 && samples->back().get("gain")->as_double()==0);
            const auto encoded=fs::read(fs::from_utf8(prefix+".wav"));expect(encoded.has_value());if(!encoded)return;
            const auto pcm=std::string(report->get("pcm_hash")->as_string());
            const auto sound=meng::audio::read_wav(std::as_bytes(std::span(encoded->data(),encoded->size())));
            expect(sound && sound->rate==48000 && sound->samples.size()==96000 && meng::audio::peak(sound->samples)>.01f);
            if(previous_pcm.empty()) {previous_pcm=pcm;previous_wav=*encoded;}
            else expect(pcm==previous_pcm && *encoded==previous_wav) << "observation frame rate does not alter actual WAV bytes";
            const auto image=mlibs::image::load_png(fs::from_utf8(prefix+".png"));expect(image.has_value());
            if(image)expect(image->width==1536 && image->height==512);
            const auto replay=mlibs::image::load_animated_webp(fs::from_utf8(prefix+".webp"));expect(replay.has_value());
            if(replay)expect(replay->frames.size()==samples->size());
            std::println("audio-audition fps={} pcm={} frames={} output={}",fps,pcm,samples->size(),prefix);
        }
    };
    "bad arguments or curve selection return diagnostics before creating output"_test = [] {
        const auto directory=scratch_dir("audio-automation-errors");
        const auto file=directory/"audition.timeline.xdef.ts";
        expect(fs::write_atomic(file,curve).has_value());
        const auto prefix=utf8(directory/"preserve");
        expect(fs::write_atomic(fs::from_utf8(prefix+".wav"),"keep").has_value());
        auto result=invoke({"automation",utf8(file),"--track","music","--clip","missing","--out",prefix});
        expect(result.code==1 && result.out.contains("clip not found"));
        // An argument out of range: refused before anything is written (exit 1, its error invalid-argument).
        result=invoke({"automation",utf8(file),"--track","music","--clip","fade","--out",prefix,"--fps","0"});
        expect(result.code==1 && result.out.contains("fps"));
        result=invoke({"automation",utf8(file),"--track","music","--clip","fade","--out",prefix,"--seconds","nan"});
        expect(result.code==1);
        result=invoke({"automation",utf8(file),"--track","music","--clip","fade","--out",prefix,"--fps","11"});
        expect(result.code==1 && result.out.contains("exact timeline rate"));
        const auto kept=fs::read(fs::from_utf8(prefix+".wav"));expect(kept && *kept=="keep");
    };
    "report write failure returns false and identifies files already exported"_test = [] {
        const auto directory=scratch_dir("audio-automation-export-error");
        const auto file=directory/"audition.timeline.xdef.ts";
        expect(fs::write_atomic(file,curve).has_value());
        const auto prefix=utf8(directory/"blocked");
        (void)scratch_dir("audio-automation-export-error/blocked.report.xdef.json");
        const auto result=invoke({"automation",utf8(file),"--track","music","--clip","fade","--out",prefix,"--seconds","1","--fps","1"});
        expect(result.code==1);
        const auto report=parse_json(result.out);expect(report.has_value());if(!report)return;
        expect(!report->get("ok")->as_bool() && report->get("error"));
        expect(report->get("outputs")->list()->size()==3);
    };
}};
