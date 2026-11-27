// The director (FE4; the presentation design's §4.2): a show's timeline from what each beat of a history changed —
// how long a beat lasts, the words read, where the camera looks, the transition into each step — as a pure function
// whose rules are data.
import std;
import hanim.timeline;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace tl = hanim::timeline;

namespace {

// Five steps of a design on a 1280 × 800 frame: the page appears whole; nothing visible changes; a heading's words
// change in a small place; something close to it moves; nothing visible changes at the end.
std::vector<tl::Beat> history() {
    return {
        {.source = "design:site#1", .caption = "A page begins", .regions = {{0, 0, 1280, 800}}, .appeared = 12},
        {.source = "design:site#2", .caption = "Its words settle", .retexted = 1},
        {.source = "design:site#3", .caption = "A heading changes its words", .regions = {{900, 100, 80, 40}}, .retexted = 2},
        {.source = "design:site#4", .regions = {{910, 110, 60, 30}}, .moved = 3},
        {.source = "design:site#5"},
    };
}

const tl::Track& track(const tl::Timeline& show, std::string_view name) {
    return *std::ranges::find(show.tracks, name, &tl::Track::name);
}

std::int64_t frames(const tl::Timeline& show, mlibs::time::Flicks length) { return length / show.rate.frame_length(); }

}  // namespace

UTest cases{"timeline.direct", [] {
    "a beat lasts by how much of the frame it changed, a still one 0.3 s; its words are read before it ends"_test = [] {
        const auto beats = history();
        const tl::Timeline show = tl::direct(beats, 1280, 800);
        expect(show.width == 1280 && show.height == 800 && show.rate.num == 60);
        expect(tl::check(show).empty()) << "a timeline that holds together";
        const auto& states = track(show, "states");
        expect(states.kind == tl::Kind::video && eq(states.clips.size(), std::size_t{5}));
        std::vector<std::int64_t> lengths;
        for (const auto& clip : states.clips) lengths.push_back(frames(show, clip.length));
        // The whole frame: the longest (2.4 s). Nothing visible: 0.3 s, both times. A small place: 0.82 s, lengthened
        // to read the words it carries with the still beat before it (43 characters at 18 a second: 143 frames for
        // the two). Close by: 0.81 s.
        expect(lengths == std::vector<std::int64_t>{144, 18, 125, 49, 18}) << std::format("{}", lengths);
        expect(eq(states.clips[1].source, std::string("design:site#2")) && eq(states.clips[1].at, show.rate.frame_time(144)));
        expect(eq(show.markers.size(), std::size_t{5}) && eq(show.markers[2].name, std::string("beat 3")) && eq(show.markers[2].at, show.rate.frame_time(162)));
    };

    "words: a still beat's join the next visible beat's, a still beat at the end joins the one before; one clip a group"_test = [] {
        const tl::Timeline show = tl::direct(history(), 1280, 800);
        const auto& captions = track(show, "captions");
        expect(captions.kind == tl::Kind::events && eq(captions.clips.size(), std::size_t{2}));
        if (captions.clips.size() != 2) return;
        expect(eq(captions.clips[0].name, std::string("A page begins")) && eq(frames(show, captions.clips[0].length), std::int64_t{144}));
        expect(eq(captions.clips[1].name, std::string("Its words settle\nA heading changes its words")));
        expect(eq(captions.clips[1].at, show.rate.frame_time(144)) && eq(frames(show, captions.clips[1].length), std::int64_t{143}));
    };

    "the camera: the whole frame for a whole change, pushed in on a small one inside the frame, kept for a place close by"_test = [] {
        const tl::Timeline show = tl::direct(history(), 1280, 800);
        const auto& camera = track(show, "camera");
        expect(camera.kind == tl::Kind::animation && eq(camera.clips.size(), std::size_t{1}));
        if (camera.clips.empty()) return;
        const tl::Clip& clip = camera.clips.front();
        expect(eq(clip.length, show.rate.frame_time(354)));
        const auto at = [&](std::int64_t frame, std::string_view property) { return clip.value(property, show.rate.frame_time(frame)); };
        expect(eq(at(0, "zoom"), 1.0) && eq(at(0, "x"), 640.0) && eq(at(0, "y"), 400.0)) << "the whole frame first";
        expect(eq(at(161, "zoom"), 1.0)) << "still there through the still beat";
        // 80 px wide: a third of 800 is 266.7 px, so 3.33 — the deepest is 3. Its centre (940, 120) is kept where a
        // picture pushed in 3 times stays inside the frame (y from 133.3).
        expect(at(170, "zoom") > 1.0 && at(170, "zoom") < 3.0) << "on the way, eased";
        expect(eq(at(198, "zoom"), 3.0) && eq(at(198, "x"), 940.0) && std::abs(at(198, "y") - 800.0 / 6) < 1e-9) << std::format("{} {} {}", at(198, "x"), at(198, "y"), at(198, "zoom"));
        expect(eq(at(300, "zoom"), 3.0) && eq(at(300, "x"), 940.0)) << "a place close by: the camera stays";
        expect(eq(clip.properties.front().keys.keys().size(), std::size_t{3})) << "one move";
    };

    "the transition into a step: what most of its nodes had happen, a registered recipe's name"_test = [] {
        const tl::Timeline show = tl::direct(history(), 1280, 800);
        std::vector<std::string> names;
        for (const auto& clip : track(show, "states").clips) names.push_back(clip.name);
        expect(names == std::vector<std::string>{"settle_in", "morph", "morph", "morph", "fade_in"}) << std::format("{}", names);
        tl::Direction rules;
        rules.relocate = "arc_to";
        expect(eq(tl::direct(history(), 1280, 800, rules).tracks[0].clips[3].name, std::string("arc_to"))) << "the rules name it";
    };

    "how long a beat's change is seen to move, when measured, rides on its states clip for the player; unmeasured, nothing"_test = [] {
        auto beats = history();
        beats[0].moving = 1.23456;
        beats[2].moving = 0.5;
        const tl::Timeline show = tl::direct(beats, 1280, 800);
        const auto& clips = track(show, "states").clips;
        expect(eq(clips[0].value("moving", 0), 1.235) && eq(clips[2].value("moving", clips[2].at + clips[2].length / 2), 0.5)) << "a constant, to the millisecond";
        expect(clips[1].properties.empty() && clips[4].properties.empty()) << "not measured: no property";
        const auto read = tl::read_timeline(tl::write_timeline(show), "show.timeline.xdef.json");
        expect(read.has_value() && eq(read->tracks[0].clips[0].value("moving", 0), 1.235)) << "and read back";
        expect(eq(tl::write_timeline(tl::direct(history(), 1280, 800)), tl::write_timeline(tl::direct(history(), 1280, 800)))) << "the rest unchanged";
    };

    "its sound, when the rules give one: a score under the whole show, a cue on each visible beat's first frame"_test = [] {
        tl::Direction rules;
        rules.score = "main.music.xdef.ts";
        rules.cue = "step";
        const tl::Timeline show = tl::direct(history(), 1280, 800, rules);
        const auto& music = track(show, "music");
        expect(music.kind == tl::Kind::audio && music.clips.size() == 1 && music.clips[0].source == "main.music.xdef.ts" && music.clips[0].at == 0 &&
               music.clips[0].length == show.duration()) << "the score, start to end";
        const auto& states = track(show, "states").clips;
        const auto& cues = track(show, "sfx").clips;
        expect(eq(cues.size(), std::size_t{3})) << "beats 1, 3 and 4 change what is seen; 2 and 5 do not";
        if (cues.size() == 3) {
            expect(cues[0].at == states[0].at && cues[1].at == states[2].at && cues[2].at == states[3].at) << "on the beats' own frames";
            expect(cues[1].source == "sound:step" && cues[1].name == "beat 3" && frames(show, cues[1].length) == 1);
        }
        expect(eq(show.tracks[0].name, std::string("states"))) << "the picture's tracks first, as before";
        expect(std::ranges::none_of(tl::direct(history(), 1280, 800).tracks, [](const tl::Track& t) { return t.kind == tl::Kind::audio || t.name == "sfx"; }))
            << "no sound unless the rules give one";
        const auto read = tl::read_direction(R"({"$format":"direction@1","direction":{"score":"main.music.xdef.ts","cue":"step"}})", "main.direction.xdef.json");
        expect(read.has_value() && read->score == "main.music.xdef.ts" && read->cue == "step") << "read from the rules";
    };

    "a pure function: the same beats and rules, the same timeline to the bit — written, read back and written again"_test = [] {
        const auto beats = history();
        const std::string first = tl::write_timeline(tl::direct(beats, 1280, 800));
        expect(eq(first, tl::write_timeline(tl::direct(beats, 1280, 800))));
        const auto read = tl::read_timeline(first, "show.timeline.xdef.json");
        expect(read.has_value()) << (read ? std::string() : read.error().front().text());
        if (read) expect(eq(tl::write_timeline(*read), first)) << "the file is the show";
        mlibs::testing::Hash hash;
        hash.text(first);
        std::println("    timeline.direct {:016x}", hash.value());
        expect(eq(hash.value(), std::uint64_t{0x64d6274d4172a078})) << "the same on every platform (GCC, LLVM and wasm)";
        expect(tl::direct({}, 1280, 800).duration() == 0) << "no beats, no show";
    };

    "the rules are data: a key absent keeps its default, each problem at its place with the nearest right word"_test = [] {
        const auto none = tl::read_direction(R"({"$format": "direction@1", "direction": {}})", "show.direction.xdef.json");
        expect(none.has_value() && *none == tl::Direction{}) << "every default";
        const auto some = tl::read_direction(R"({"$format": "direction@1", "direction": {"still": 0.2, "appear": "rise_in", "rate": 30}})", "show.direction.xdef.json");
        expect(some.has_value() && some->still == 0.2 && some->appear == "rise_in" && some->rate.num == 30);
        const auto misspelt = tl::read_direction(R"({"$format": "direction@1", "direction": {"stil": 0.5}})", "show.direction.xdef.json");
        expect(!misspelt.has_value() && misspelt.error().size() == 1 && misspelt.error().front().hint.contains("still") &&
               misspelt.error().front().at == "/direction/stil")
            << (misspelt ? std::string() : misspelt.error().front().text());
        const auto backwards = tl::read_direction(R"({"$format": "direction@1", "direction": {"shortest": 3, "longest": 1}})", "show.direction.xdef.json");
        expect(!backwards.has_value()) << "the longest shorter than the shortest";
        const auto other = tl::read_direction(R"({"$format": "timeline@1", "direction": {}})", "show.direction.xdef.json");
        expect(!other.has_value()) << "another kind's file";
    };
}};
