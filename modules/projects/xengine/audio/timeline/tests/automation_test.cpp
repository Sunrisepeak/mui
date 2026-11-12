import std;
import xengine.audio.timeline;
import hanim.timeline;
import xengine.audio;
import hanim.core;
import mcpplibs.mtest;
import mlibs.trace;

using namespace mcpplibs::mtest;
namespace tl = hanim::timeline;
namespace audio = xengine::audio;

namespace {
tl::Clip fixture() {
    tl::Clip clip{.name="mix", .at=mlibs::time::from_seconds(.25), .length=mlibs::time::from_seconds(1.5), .level=.75f,
        .fade={.in=mlibs::time::from_seconds(.1), .out=mlibs::time::from_seconds(.2), .ease=hanim::ease::in_out_cubic}};
    tl::Property gain{.name="gain"};
    gain.keys.key(hanim::seconds(0), .2, hanim::ease::in_out_cubic)
        .key(hanim::seconds(.5), .9).hold(hanim::seconds(.8), .4).key(hanim::seconds(1.1), .7).key(hanim::seconds(1.5), .1);
    clip.properties.push_back(std::move(gain));
    return clip;
}
void setup(audio::Mixer& mixer, audio::GainSource gain) {
    audio::Sound sound{.name="tone", .gain=.2f, .bus="music"};
    sound.layers.push_back({.length=2, .pitch={220,330}, .envelope={.attack=.01f,.hold=2,.sustain=1,.release=.01f}});
    expect(mixer.automate_gain("music", std::move(gain)).has_value());
    mixer.add_effect("music", audio::echo(.075f, .2f, .15f, mixer.rate()));
    expect(mixer.play(mixer.add(sound), 0));
}
}

UTest cases{"timeline.audio", [] {
    "frozen curves use exact sample timestamps with clip-local keys and fades"_test = [] {
        auto clip=fixture();
        const auto made=audio::GainAutomation::prepare(clip, {.outside=0}); expect(made.has_value()); if (!made) return;
        const auto& gain=**made;
        expect(gain.rate()==48000 && gain.begin()==clip.at && gain.end()==clip.end());
        for (std::int64_t sample=0; sample<96000; ++sample) {
            const auto t=mlibs::time::sample_time(gain.rate(), sample);
            const double expected=clip.covers(t) ? clip.value("gain",t) * clip.level_at(t) : 0;
            expect(gain.at_sample(sample)==static_cast<float>(expected));
        }
        const auto value=gain.at(mlibs::time::from_seconds(.6));
        clip.level=0; clip.properties.clear();
        expect(gain.at(mlibs::time::from_seconds(.6))==value) << "later edits do not change the prepared DSP plan";
        expect(gain.at_sample(-1)==0 && gain.at_sample(std::numeric_limits<std::int64_t>::max())==0);
        std::array<float,9> over{}; gain.sample(std::numeric_limits<std::int64_t>::max()-2,over);
        expect(std::ranges::all_of(over, [](float v) { return v==0; }));
    };
    "block and random-access reads are equal including cuts and the exclusive clip end"_test = [] {
        const auto made=audio::GainAutomation::prepare(fixture(), {.sample_rate=44100,.outside=.3f});expect(made.has_value()); if(!made)return;
        const auto& gain=**made;
        std::array<float,257> values{};
        for (const std::int64_t first : {59530, 0, 77000, 10900, -31, 59000}) {
            gain.sample(first,values);
            for(std::size_t i=0;i<values.size();++i)expect(values[i]==gain.at_sample(first+static_cast<std::int64_t>(i)));
        }
        const auto cut=mlibs::time::from_seconds(1.35);
        expect(near(gain.at(cut-1), .4*.75, 1e-7));expect(near(gain.at(cut), .7*.75, 1e-7));
        expect(gain.at(gain.end())==.3f && gain.at(gain.begin()-1)==.3f);
    };
    "invalid preparation is diagnosed before any mixer is changed"_test = [] {
        const auto refused=[](tl::Clip clip, audio::GainOptions options, audio::GainError expected) {
            const auto result=audio::GainAutomation::prepare(clip,options);expect(!result && result.error()==expected);
        };
        refused(fixture(),{.sample_rate=12345},audio::GainError::sample_rate);
        refused(fixture(),{.property="missing"},audio::GainError::property);
        auto c=fixture(); c.length=0;refused(c,{},audio::GainError::clip_time);
        c=fixture(); c.at=std::numeric_limits<mlibs::time::Flicks>::max();refused(c,{},audio::GainError::clip_time);
        c=fixture(); c.fade.in=c.length;c.fade.out=1;refused(c,{},audio::GainError::clip_time);
        c=fixture();c.properties.push_back(c.properties[0]);refused(c,{},audio::GainError::property);
        c=fixture();c.properties[0].keys.key(hanim::seconds(-1),1);refused(c,{},audio::GainError::keys);
        c=fixture();c.properties[0].keys.key(hanim::seconds(1),std::numeric_limits<double>::quiet_NaN());refused(c,{},audio::GainError::value);
        c=fixture();c.fade.ease={static_cast<hanim::EaseKind>(255)};refused(c,{},audio::GainError::easing);
        c=fixture();c.properties[0].keys.key(hanim::seconds(1),1,{hanim::EaseKind::steps,{2.5,1}});refused(c,{},audio::GainError::easing);
    };
    "30 60 and 120 fps observers and reverse seeks never change PCM or emit another voice"_test = [] {
        const auto made=audio::GainAutomation::prepare(fixture(),{.outside=0});expect(made.has_value());if(!made)return;
        audio::Mixer reference;setup(reference,*made);
        const auto expected=audio::render(reference,96000);
        expect(audio::peak(expected)>.04f) << "the comparison contains actual automated sound";
        for(const int fps : {30,60,120}) {
            audio::Mixer mixer;setup(mixer,*made);
            std::vector<float> output(expected.size());
            for(std::int64_t frame=0;frame<2*fps;++frame) {
                const auto time=mlibs::time::Rate{fps,1}.frame_time(frame);
                const auto clock=mixer.clock(), begun=mixer.begun();
                const auto forward=(*made)->at(time);
                (void)(*made)->at(mlibs::time::from_seconds(2)-time);
                expect(forward==(*made)->at(time));
                expect(clock==mixer.clock() && begun==mixer.begun()) << "observation never advances live audio";
                const auto next=mlibs::time::sample_at(mixer.rate(),mlibs::time::Rate{fps,1}.frame_time(frame+1));
                mixer.render(std::span(output).subspan(static_cast<std::size_t>(2*clock),static_cast<std::size_t>(2*(next-clock))));
            }
            expect(output==expected && mixer.clock()==96000 && mixer.begun()==1);
            std::println("audio-clock fps={} frames={} pcm={:016x}",fps,mixer.clock(),audio::hash(output));
        }
    };
}};
