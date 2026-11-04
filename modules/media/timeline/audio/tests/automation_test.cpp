import std;
import openmui.timeline.audio;
import openmui.timeline;
import openmui.audio;
import hanim.core;
import openmui.testing;
import openmui.trace;

using namespace omui::testing;
namespace tl = omui::timeline;
namespace audio = omui::audio;

namespace {
tl::Clip fixture() {
    tl::Clip clip{.name="mix", .at=tl::from_seconds(.25), .length=tl::from_seconds(1.5), .level=.75f,
        .fade={.in=tl::from_seconds(.1), .out=tl::from_seconds(.2), .ease=hanim::ease::in_out_cubic}};
    tl::Property gain{.name="gain"};
    gain.keys.key(hanim::seconds(0), .2, hanim::ease::in_out_cubic)
        .key(hanim::seconds(.5), .9).hold(hanim::seconds(.8), .4).key(hanim::seconds(1.1), .7).key(hanim::seconds(1.5), .1);
    clip.properties.push_back(std::move(gain));
    return clip;
}
void setup(audio::Mixer& mixer, audio::GainSource gain) {
    audio::Sound sound{.name="tone", .gain=.2f, .bus="music"};
    sound.layers.push_back({.length=2, .pitch={220,330}, .envelope={.attack=.01f,.hold=2,.sustain=1,.release=.01f}});
    check(mixer.automate_gain("music", std::move(gain)).has_value());
    mixer.add_effect("music", audio::echo(.075f, .2f, .15f, mixer.rate()));
    check(mixer.play(mixer.add(sound), 0));
}
}

int main(int argc, char** argv) {
    Suite suite{"timeline.audio"};
    suite.test("frozen curves use exact sample timestamps with clip-local keys and fades", [] {
        auto clip=fixture();
        const auto made=tl::GainAutomation::prepare(clip, {.outside=0}); check(made.has_value()); if (!made) return;
        const auto& gain=**made;
        check(gain.rate()==48000 && gain.begin()==clip.at && gain.end()==clip.end());
        for (std::int64_t sample=0; sample<96000; ++sample) {
            const auto t=tl::sample_time(gain.rate(), sample);
            const double expected=clip.covers(t) ? clip.value("gain",t) * clip.level_at(t) : 0;
            check(gain.at_sample(sample)==static_cast<float>(expected));
        }
        const auto value=gain.at(tl::from_seconds(.6));
        clip.level=0; clip.properties.clear();
        check(gain.at(tl::from_seconds(.6))==value, "later edits do not change the prepared DSP plan");
        check(gain.at_sample(-1)==0 && gain.at_sample(std::numeric_limits<std::int64_t>::max())==0);
        std::array<float,9> over{}; gain.sample(std::numeric_limits<std::int64_t>::max()-2,over);
        check(std::ranges::all_of(over, [](float v) { return v==0; }));
    });
    suite.test("block and random-access reads are equal including cuts and the exclusive clip end", [] {
        const auto made=tl::GainAutomation::prepare(fixture(), {.sample_rate=44100,.outside=.3f});check(made.has_value()); if(!made)return;
        const auto& gain=**made;
        std::array<float,257> values{};
        for (const std::int64_t first : {59530, 0, 77000, 10900, -31, 59000}) {
            gain.sample(first,values);
            for(std::size_t i=0;i<values.size();++i)check(values[i]==gain.at_sample(first+static_cast<std::int64_t>(i)));
        }
        const auto cut=tl::from_seconds(1.35);
        check_near(gain.at(cut-1), .4*.75, 1e-7);check_near(gain.at(cut), .7*.75, 1e-7);
        check(gain.at(gain.end())==.3f && gain.at(gain.begin()-1)==.3f);
    });
    suite.test("invalid preparation is diagnosed before any mixer is changed", [] {
        const auto expect=[](tl::Clip clip, tl::GainOptions options, tl::GainError expected) {
            const auto result=tl::GainAutomation::prepare(clip,options);check(!result && result.error()==expected);
        };
        expect(fixture(),{.sample_rate=12345},tl::GainError::sample_rate);
        expect(fixture(),{.property="missing"},tl::GainError::property);
        auto c=fixture(); c.length=0;expect(c,{},tl::GainError::clip_time);
        c=fixture(); c.at=std::numeric_limits<tl::Flicks>::max();expect(c,{},tl::GainError::clip_time);
        c=fixture(); c.fade.in=c.length;c.fade.out=1;expect(c,{},tl::GainError::clip_time);
        c=fixture();c.properties.push_back(c.properties[0]);expect(c,{},tl::GainError::property);
        c=fixture();c.properties[0].keys.key(hanim::seconds(-1),1);expect(c,{},tl::GainError::keys);
        c=fixture();c.properties[0].keys.key(hanim::seconds(1),std::numeric_limits<double>::quiet_NaN());expect(c,{},tl::GainError::value);
        c=fixture();c.fade.ease={static_cast<hanim::EaseKind>(255)};expect(c,{},tl::GainError::easing);
        c=fixture();c.properties[0].keys.key(hanim::seconds(1),1,{hanim::EaseKind::steps,{2.5,1}});expect(c,{},tl::GainError::easing);
    });
    suite.test("30 60 and 120 fps observers and reverse seeks never change PCM or emit another voice", [] {
        const auto made=tl::GainAutomation::prepare(fixture(),{.outside=0});check(made.has_value());if(!made)return;
        audio::Mixer reference;setup(reference,*made);
        const auto expected=audio::render(reference,96000);
        check(audio::peak(expected)>.04f, "the comparison contains actual automated sound");
        for(const int fps : {30,60,120}) {
            audio::Mixer mixer;setup(mixer,*made);
            std::vector<float> output(expected.size());
            for(std::int64_t frame=0;frame<2*fps;++frame) {
                const auto time=tl::frame_time({fps,1},frame);
                const auto clock=mixer.clock(), begun=mixer.begun();
                const auto forward=(*made)->at(time);
                (void)(*made)->at(tl::from_seconds(2)-time);
                check(forward==(*made)->at(time));
                check(clock==mixer.clock() && begun==mixer.begun(), "observation never advances live audio");
                const auto next=tl::sample_at(mixer.rate(),tl::frame_time({fps,1},frame+1));
                mixer.render(std::span(output).subspan(static_cast<std::size_t>(2*clock),static_cast<std::size_t>(2*(next-clock))));
            }
            check(output==expected && mixer.clock()==96000 && mixer.begun()==1);
            std::println("audio-clock fps={} frames={} pcm={:016x}",fps,mixer.clock(),audio::hash(output));
        }
    });
    return suite.run(argc,argv);
}
