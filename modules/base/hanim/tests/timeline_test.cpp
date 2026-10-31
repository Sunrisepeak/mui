import std;
import hanim;
import openmui.testing;

using namespace omui::testing;
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

int main(int argc, char** argv) {
    Suite s{"hanim.timeline"};

    s.test("time maps: offset, rate, trim, fill", [] {
        hanim::TimeMap m;
        auto natural = seconds(2);
        check(!hanim::map_time(m, seconds(1), natural, seconds(0.5)).has_value(), "before start, no backwards fill");
        check_bits(hanim::map_time(m, seconds(1), natural, seconds(2))->count(), 1.0);
        check_bits(hanim::map_time(m, seconds(1), natural, seconds(9))->count(), 2.0);
        m.rate = 2;
        check_bits(hanim::map_time(m, seconds(0), natural, seconds(0.5))->count(), 1.0);
        check_bits(hanim::mapped_length(m, natural).count(), 1.0);
        m = {};
        m.trim = seconds(0.5);
        check_bits(hanim::map_time(m, seconds(0), natural, seconds(0))->count(), 0.5);
        check_bits(hanim::mapped_length(m, natural).count(), 1.5);
        m = {};
        m.fill = hanim::Fill::none;
        check(!hanim::map_time(m, seconds(0), natural, seconds(2)).has_value(), "ended, no forwards fill");
        m.fill = hanim::Fill::both;
        check_bits(hanim::map_time(m, seconds(1), natural, seconds(0))->count(), 0.0);
    });

    s.test("time maps: repeat, alternate, reverse", [] {
        hanim::TimeMap m;
        auto natural = seconds(1);
        m.repeat = 3;
        check_bits(hanim::map_time(m, seconds(0), natural, seconds(2.25))->count(), 0.25);
        check_bits(hanim::map_time(m, seconds(0), natural, seconds(5))->count(), 1.0);
        m.alternate = true;
        check_bits(hanim::map_time(m, seconds(0), natural, seconds(1.25))->count(), 0.75);
        check_bits(hanim::map_time(m, seconds(0), natural, seconds(2.25))->count(), 0.25);
        m = {};
        m.reverse = true;
        check_bits(hanim::map_time(m, seconds(0), natural, seconds(0.25))->count(), 0.75);
        check_bits(hanim::map_time(m, seconds(0), natural, seconds(4))->count(), 0.0);
        m = {};
        m.repeat = 2.5;
        check_bits(hanim::mapped_length(m, natural).count(), 2.5);
        check_bits(hanim::map_time(m, seconds(0), natural, seconds(9))->count(), 0.5);
        m.repeat = std::numeric_limits<double>::infinity();
        check_bits(hanim::map_time(m, seconds(0), natural, seconds(1000.5))->count(), 0.5);
        check(std::isinf(hanim::mapped_length(m, natural).count()));
    });

    s.test("at, then, with, stagger", [] {
        hanim::Timeline tl;
        tl.at(seconds(0), "a", ramp(0, 1, 1))
            .then("b", ramp(0, 10, 2), seconds(0.5))  // starts at 1.5
            .with("c", ramp(5, 6, 1), seconds(0.5));  // starts at 2.0
        std::array<std::string, 3> cards{"card/0", "card/1", "card/2"};
        tl.stagger(seconds(4), seconds(0.25), cards, ramp(0, 1, 1));
        check_bits(tl.duration().count(), 4.0 + 0.5 + 1.0);
        auto v = at(tl, 2.5);
        check_bits(v["a"], 1.0);
        check_bits(v["b"], 5.0);
        check_bits(v["c"], 5.5);
        check(!v.contains("card/0"));
        v = at(tl, 4.5);
        check_bits(v["card/0"], 0.5);
        check_bits(v["card/1"], 0.25);
        check_bits(v["card/2"], 0.0);
    });

    s.test("nested timelines prefix targets and map time", [] {
        hanim::Timeline scene;
        scene.at(seconds(0), "title.opacity", ramp(0, 1, 1)).marker(seconds(0.5), "beat");
        hanim::Timeline film;
        film.nest(seconds(2), scene, "intro/")
            .nest(seconds(10), scene, "outro/", {.rate = 2});
        auto v = at(film, 2.5);
        check_bits(v["intro/title.opacity"], 0.5);
        check(!v.contains("outro/title.opacity"));
        v = at(film, 10.25);
        check_bits(v["outro/title.opacity"], 0.5);
        auto markers = film.all_markers();
        check(markers.size() == 2);
        check_bits(markers[0].time.count(), 2.5);
        check_eq(markers[0].name, std::string("intro/beat"));
        check_bits(markers[1].time.count(), 10.25);
        check_bits(film.duration().count(), 10.5);
    });

    s.test("later placements win on the same target", [] {
        hanim::Timeline tl;
        tl.at(seconds(0), "x", ramp(0, 1, 10)).at(seconds(1), "x", ramp(100, 200, 1));
        std::vector<double> applied;
        tl.sample(seconds(1.5), [&](std::string_view, const hanim::Value& v) { applied.push_back(*v.as<double>()); });
        check(applied.size() == 2);
        check_bits(applied.back(), 150.0);
    });

    s.test("values keep their type", [] {
        hanim::Timeline tl;
        tl.at(seconds(0), "fill", hanim::Tween<hanim::Rgba>{.from = hanim::Rgba::bytes(255, 0, 0),
                                                          .to = hanim::Rgba::bytes(0, 0, 255),
                                                          .duration = seconds(1)});
        tl.at(seconds(0), "pos", hanim::Tween<hanim::Vec2>{.from = {0, 0}, .to = {10, 20}, .duration = seconds(1)});
        tl.at(seconds(0), "fn", hanim::Clip::function(seconds(1), [](seconds t) { return t.count() * 2.0; }));
        for (auto& [target, v] : tl.sample(seconds(1))) {
            if (target == "fill") check(v.as<hanim::Rgba>() == hanim::Rgba::bytes(0, 0, 255));
            if (target == "pos") check(v.as<hanim::Vec2>() == hanim::Vec2{10, 20});
            if (target == "fn") check_bits(*v.as<double>(), 2.0);
            check(!v.as<hanim::Transform2D>().has_value());
        }
    });

    s.test("markers are sorted and found by name", [] {
        hanim::Timeline tl;
        tl.marker(seconds(2), "b").marker(seconds(1), "a").marker(seconds(3), "c");
        check_eq(tl.markers()[0].name, std::string("a"));
        check_bits(tl.marker_time("c")->count(), 3.0);
        check(!tl.marker_time("zz").has_value());
    });

    return s.run(argc, argv);
}
