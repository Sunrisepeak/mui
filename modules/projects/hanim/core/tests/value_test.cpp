import std;
import hanim.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using hanim::seconds;

UTest cases{"hanim.value", [] {

    "OKLab of reference colours"_test = [] {
        auto white = hanim::to_oklab(hanim::Rgba{1, 1, 1, 1});
        expect(near(white.l, 1.0, 1e-7));
        expect(near(white.a, 0.0, 1e-7));
        expect(near(white.b, 0.0, 1e-7));
        auto red = hanim::to_oklab(hanim::Rgba{1, 0, 0, 1});
        expect(near(red.l, 0.627955, 1e-5));
        expect(near(red.a, 0.224863, 1e-5));
        expect(near(red.b, 0.125846, 1e-5));
        auto back = hanim::from_oklab(red, 1.0f);
        expect(near(back.r, 1.0, 1e-5));
        expect(near(back.g, 0.0, 1e-5));
    };

    "colour interpolation ends exactly and stays in gamut"_test = [] {
        auto a = hanim::Rgba::bytes(255, 0, 0);
        auto b = hanim::Rgba::bytes(0, 128, 255, 128);
        expect(hanim::lerp(a, b, 0.0) == a);
        expect(hanim::lerp(a, b, 1.0) == b);
        for (int i = 0; i <= 20; ++i) {
            auto c = hanim::lerp(a, b, i / 20.0);
            expect(c.r >= 0 && c.r <= 1 && c.g >= 0 && c.g <= 1 && c.b >= 0 && c.b <= 1 && c.a >= 0 && c.a <= 1);
        }
        // Fading to transparent keeps the hue: premultiplied lanes.
        auto fade = hanim::lerp(hanim::Rgba::bytes(0, 0, 255), hanim::Rgba{0, 0, 0, 0}, 0.5);
        expect(fade.b > 0.9f && fade.r < 0.05f) << "a fade does not pass through black";
        expect(near(fade.a, 0.5, 1e-6));
    };

    "hex colours"_test = [] {
        expect(hanim::Rgba::parse_hex("#ff8000") == hanim::Rgba::bytes(255, 128, 0));
        expect(hanim::Rgba::parse_hex("0f08") == hanim::Rgba::bytes(0, 255, 0, 136));
        expect(hanim::Rgba::parse_hex("#12345678") == hanim::Rgba::bytes(0x12, 0x34, 0x56, 0x78));
        expect(!hanim::Rgba::parse_hex("#12345").has_value());
        expect(!hanim::Rgba::parse_hex("#gg0000").has_value());
    };

    "transform matrix composes scale, skew, rotate, translate"_test = [] {
        hanim::Transform2D t{.tx = 5, .ty = 7, .sx = 2, .sy = 3, .rotate = hanim::dm::half_pi};
        auto m = t.matrix();
        // (1, 0) → scale → (2, 0) → rotate 90° → (0, 2) → translate → (5, 9)
        expect(near(m[0] * 1 + m[2] * 0 + m[4], 5, 1e-12));
        expect(near(m[1] * 1 + m[3] * 0 + m[5], 9, 1e-12));
        auto mid = hanim::lerp(hanim::Transform2D{}, t, 0.5);
        expect(near(mid.rotate, hanim::dm::half_pi / 2, 1e-15));
        expect(near(mid.sx, 1.5, 1e-15));
    };

    "motion path moves by arc length"_test = [] {
        hanim::MotionPath p;
        p.move_to({0, 0}).line_to({100, 0}).line_to({100, 100});
        expect(near(p.length(), 200, 1e-9));
        expect(near(p.point(0.25).x, 50, 1e-9));
        expect(near(p.point(0.75).y, 50, 1e-9));
        expect(near(p.at(0.25).angle, 0, 1e-12));
        expect(near(p.at(0.75).angle, hanim::dm::half_pi, 1e-12));
        hanim::MotionPath circle;
        circle.move_to({1, 0}).cubic_to({1, 0.5523}, {0.5523, 1}, {0, 1});
        expect(near(circle.length(), hanim::dm::half_pi, 1e-3));
        auto q = circle.point(0.5);
        expect(near(std::hypot(q.x, q.y), 1.0, 1e-3));
    };

    "frame rates: frame times are exact and map back"_test = [] {
        mlibs::time::Rate r{30000, 1001};
        for (std::int64_t f : {0LL, 1LL, 29LL, 30LL, 1799LL, 107892LL}) expect(eq(r.frame_at(r.time_of(f)), f));
        mlibs::time::Rate sixty{60, 1};
        expect(eq(std::bit_cast<std::uint64_t>(double(sixty.time_of(30).count())), std::bit_cast<std::uint64_t>(double(0.5))));
        expect(eq(sixty.frame_at(seconds(0.5 - 1e-12)), std::int64_t{29}));
    };

    "clocks"_test = [] {
        auto v = hanim::Clock::virtual_time();
        v.advance(seconds(0.25));
        v.advance(seconds(0.25));
        expect(eq(std::bit_cast<std::uint64_t>(double(v.now().count())), std::bit_cast<std::uint64_t>(double(0.5))));
        auto r = hanim::Clock::render({60, 1});
        r.seek(120);
        expect(eq(std::bit_cast<std::uint64_t>(double(r.now().count())), std::bit_cast<std::uint64_t>(double(2.0))));
        expect(near(r.subframe(0, 4, 0.5).count(), 2.0 + (1.0 / 60) * 0.5 / 4 * 0.5, 1e-15));
        auto i = hanim::Clock::interactive();
        auto t0 = std::chrono::steady_clock::time_point{};
        i.begin_frame(t0);
        i.begin_frame(t0 + std::chrono::milliseconds(100));
        expect(near(i.now().count(), 0.1, 1e-9));
        i.pause();
        i.begin_frame(t0 + std::chrono::milliseconds(500));
        expect(near(i.now().count(), 0.1, 1e-9));
        expect(i.paused());
    };

    "motion path inspection and position-only evaluation share the arc table"_test = [] {
        hanim::MotionPath path;path.move_to({3,4}).line_to({13,4}).cubic_to({13,24},{3,24},{3,4});
        expect(path.valid() && path.continuous() && path.segment_count()==2 && path.arc_samples_per_segment==32);
        for(int i=0;i<=100;++i)expect(path.point(i/100.)==path.at(i/100.).position);
        hanim::MotionPath split;split.line_to({1,0}).move_to({4,0}).line_to({5,0});
        expect(split.valid() && !split.continuous());
        hanim::MotionPath invalid;invalid.line_to({std::numeric_limits<double>::infinity(),0});expect(!invalid.valid());
        hanim::MotionPath overflowing;overflowing.line_to({1e308,0});expect(!overflowing.valid());
        hanim::MotionPath still;still.move_to({2,3}).line_to({2,3});
        expect(still.valid() && still.continuous() && still.length()==0 && still.point(.5)==hanim::Vec2{2,3});
    };
}};
