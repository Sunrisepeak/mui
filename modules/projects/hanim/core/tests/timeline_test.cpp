import std;
import hanim.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using hanim::seconds;

namespace {

hanim::Clip ramp(double from, double to, double length) {
    return hanim::Tween<double>{.from = from, .to = to, .duration = seconds(length), .ease = hanim::ease::linear};
}

std::map<std::string, double> at(const hanim::Timeline& tl, double t) {
    std::map<std::string, double> out;
    tl.sample(seconds(t), [&](std::string_view target, const hanim::Value& v) {
        out[std::string(target)] = v.as<double>().value_or(-1);
    });
    return out;
}

}  // namespace

UTest cases{"hanim.timeline", [] {

    "transport metadata shares the exact repeat mapping including boundary direction"_test = [] {
        hanim::TimeMap map;map.repeat=std::numeric_limits<double>::infinity();map.alternate=true;map.fill=hanim::Fill::both;
        for(const double t:{0.,.25,1.,1.25,2.,8.25}) {
            const auto sample=hanim::sample_time(map,seconds(0),seconds(1),seconds(t));
            expect(sample.has_value());if(!sample)return;
            expect(eq(std::bit_cast<std::uint64_t>(double(sample->position.count())), std::bit_cast<std::uint64_t>(double(hanim::map_time(map,seconds(0),seconds(1),seconds(t))->count()))));
            expect(!sample->ended);
        }
        const auto turn=hanim::sample_time(map,seconds(0),seconds(1),seconds(1));
        expect(turn->backwards && turn->iteration==1 && turn->position==seconds(1));
        map.repeat=1;
        const auto end=hanim::sample_time(map,seconds(0),seconds(1),seconds(3));
        expect(end->ended && !end->backwards && end->position==seconds(1));
        const auto before=hanim::sample_time(map,seconds(1),seconds(1),seconds(0));
        expect(before->before_start && !before->ended && before->position==seconds(0));
    };

    "time maps: offset, rate, trim, fill"_test = [] {
        hanim::TimeMap m;
        auto natural = seconds(2);
        expect(!hanim::map_time(m, seconds(1), natural, seconds(0.5)).has_value()) << "before start, no backwards fill";
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(1), natural, seconds(2))->count())), std::bit_cast<std::uint64_t>(double(1.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(1), natural, seconds(9))->count())), std::bit_cast<std::uint64_t>(double(2.0))));
        m.rate = 2;
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(0), natural, seconds(0.5))->count())), std::bit_cast<std::uint64_t>(double(1.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::mapped_length(m, natural).count())), std::bit_cast<std::uint64_t>(double(1.0))));
        m = {};
        m.trim = seconds(0.5);
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(0), natural, seconds(0))->count())), std::bit_cast<std::uint64_t>(double(0.5))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::mapped_length(m, natural).count())), std::bit_cast<std::uint64_t>(double(1.5))));
        m = {};
        m.fill = hanim::Fill::none;
        expect(!hanim::map_time(m, seconds(0), natural, seconds(2)).has_value()) << "ended, no forwards fill";
        m.fill = hanim::Fill::both;
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(1), natural, seconds(0))->count())), std::bit_cast<std::uint64_t>(double(0.0))));
    };

    "time maps: repeat, alternate, reverse"_test = [] {
        hanim::TimeMap m;
        auto natural = seconds(1);
        m.repeat = 3;
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(0), natural, seconds(2.25))->count())), std::bit_cast<std::uint64_t>(double(0.25))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(0), natural, seconds(5))->count())), std::bit_cast<std::uint64_t>(double(1.0))));
        m.alternate = true;
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(0), natural, seconds(1.25))->count())), std::bit_cast<std::uint64_t>(double(0.75))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(0), natural, seconds(2.25))->count())), std::bit_cast<std::uint64_t>(double(0.25))));
        m = {};
        m.reverse = true;
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(0), natural, seconds(0.25))->count())), std::bit_cast<std::uint64_t>(double(0.75))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(0), natural, seconds(4))->count())), std::bit_cast<std::uint64_t>(double(0.0))));
        m = {};
        m.repeat = 2.5;
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::mapped_length(m, natural).count())), std::bit_cast<std::uint64_t>(double(2.5))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(0), natural, seconds(9))->count())), std::bit_cast<std::uint64_t>(double(0.5))));
        m.repeat = std::numeric_limits<double>::infinity();
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::map_time(m, seconds(0), natural, seconds(1000.5))->count())), std::bit_cast<std::uint64_t>(double(0.5))));
        expect(std::isinf(hanim::mapped_length(m, natural).count()));
    };

    "at, then, with, stagger"_test = [] {
        hanim::Timeline tl;
        tl.at(seconds(0), "a", ramp(0, 1, 1))
            .then("b", ramp(0, 10, 2), seconds(0.5))  // starts at 1.5
            .with("c", ramp(5, 6, 1), seconds(0.5));  // starts at 2.0
        std::array<std::string, 3> cards{"card/0", "card/1", "card/2"};
        tl.stagger(seconds(4), seconds(0.25), cards, ramp(0, 1, 1));
        expect(eq(std::bit_cast<std::uint64_t>(double(tl.duration().count())), std::bit_cast<std::uint64_t>(double(4.0 + 0.5 + 1.0))));
        auto v = at(tl, 2.5);
        expect(eq(std::bit_cast<std::uint64_t>(double(v["a"])), std::bit_cast<std::uint64_t>(double(1.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(v["b"])), std::bit_cast<std::uint64_t>(double(5.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(v["c"])), std::bit_cast<std::uint64_t>(double(5.5))));
        expect(!v.contains("card/0"));
        v = at(tl, 4.5);
        expect(eq(std::bit_cast<std::uint64_t>(double(v["card/0"])), std::bit_cast<std::uint64_t>(double(0.5))));
        expect(eq(std::bit_cast<std::uint64_t>(double(v["card/1"])), std::bit_cast<std::uint64_t>(double(0.25))));
        expect(eq(std::bit_cast<std::uint64_t>(double(v["card/2"])), std::bit_cast<std::uint64_t>(double(0.0))));
    };

    "nested timelines prefix targets and map time"_test = [] {
        hanim::Timeline scene;
        scene.at(seconds(0), "title.opacity", ramp(0, 1, 1)).marker(seconds(0.5), "beat");
        hanim::Timeline film;
        film.nest(seconds(2), scene, "intro/")
            .nest(seconds(10), scene, "outro/", {.rate = 2});
        auto v = at(film, 2.5);
        expect(eq(std::bit_cast<std::uint64_t>(double(v["intro/title.opacity"])), std::bit_cast<std::uint64_t>(double(0.5))));
        expect(!v.contains("outro/title.opacity"));
        v = at(film, 10.25);
        expect(eq(std::bit_cast<std::uint64_t>(double(v["outro/title.opacity"])), std::bit_cast<std::uint64_t>(double(0.5))));
        auto markers = film.all_markers();
        expect(markers.size() == 2);
        expect(eq(std::bit_cast<std::uint64_t>(double(markers[0].time.count())), std::bit_cast<std::uint64_t>(double(2.5))));
        expect(eq(markers[0].name, std::string("intro/beat")));
        expect(eq(std::bit_cast<std::uint64_t>(double(markers[1].time.count())), std::bit_cast<std::uint64_t>(double(10.25))));
        expect(eq(std::bit_cast<std::uint64_t>(double(film.duration().count())), std::bit_cast<std::uint64_t>(double(10.5))));
    };

    "later placements win on the same target"_test = [] {
        hanim::Timeline tl;
        tl.at(seconds(0), "x", ramp(0, 1, 10)).at(seconds(1), "x", ramp(100, 200, 1));
        std::vector<double> applied;
        tl.sample(seconds(1.5), [&](std::string_view, const hanim::Value& v) { applied.push_back(*v.as<double>()); });
        expect(applied.size() == 2);
        expect(eq(std::bit_cast<std::uint64_t>(double(applied.back())), std::bit_cast<std::uint64_t>(double(150.0))));
    };

    "values keep their type"_test = [] {
        hanim::Timeline tl;
        tl.at(seconds(0), "fill", hanim::Tween<hanim::Rgba>{.from = hanim::Rgba::bytes(255, 0, 0),
                                                          .to = hanim::Rgba::bytes(0, 0, 255),
                                                          .duration = seconds(1)});
        tl.at(seconds(0), "pos", hanim::Tween<hanim::Vec2>{.from = {0, 0}, .to = {10, 20}, .duration = seconds(1)});
        tl.at(seconds(0), "fn", hanim::Clip::function(seconds(1), [](seconds t) { return t.count() * 2.0; }));
        for (auto& [target, v] : tl.sample(seconds(1))) {
            if (target == "fill") expect(v.as<hanim::Rgba>() == hanim::Rgba::bytes(0, 0, 255));
            if (target == "pos") expect(v.as<hanim::Vec2>() == hanim::Vec2{10, 20});
            if (target == "fn") expect(eq(std::bit_cast<std::uint64_t>(double(*v.as<double>())), std::bit_cast<std::uint64_t>(double(2.0))));
            expect(!v.as<hanim::Transform2D>().has_value());
        }
    };

    "markers are sorted and found by name"_test = [] {
        hanim::Timeline tl;
        tl.marker(seconds(2), "b").marker(seconds(1), "a").marker(seconds(3), "c");
        expect(eq(tl.markers()[0].name, std::string("a")));
        expect(eq(std::bit_cast<std::uint64_t>(double(tl.marker_time("c")->count())), std::bit_cast<std::uint64_t>(double(3.0))));
        expect(!tl.marker_time("zz").has_value());
    };
}};
