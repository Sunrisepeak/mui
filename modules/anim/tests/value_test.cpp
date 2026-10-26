import std;
import openmui.anim;
import openmui.testing;

using namespace omui::testing;
namespace anim = omui::anim;
using anim::seconds;

int main(int argc, char** argv) {
    Suite s{"anim.value"};

    s.test("OKLab of reference colours", [] {
        auto white = anim::to_oklab(anim::Rgba{1, 1, 1, 1});
        check_near(white.l, 1.0, 1e-7);
        check_near(white.a, 0.0, 1e-7);
        check_near(white.b, 0.0, 1e-7);
        auto red = anim::to_oklab(anim::Rgba{1, 0, 0, 1});
        check_near(red.l, 0.627955, 1e-5);
        check_near(red.a, 0.224863, 1e-5);
        check_near(red.b, 0.125846, 1e-5);
        auto back = anim::from_oklab(red, 1.0f);
        check_near(back.r, 1.0, 1e-5);
        check_near(back.g, 0.0, 1e-5);
    });

    s.test("colour interpolation ends exactly and stays in gamut", [] {
        auto a = anim::Rgba::bytes(255, 0, 0);
        auto b = anim::Rgba::bytes(0, 128, 255, 128);
        check(anim::lerp(a, b, 0.0) == a);
        check(anim::lerp(a, b, 1.0) == b);
        for (int i = 0; i <= 20; ++i) {
            auto c = anim::lerp(a, b, i / 20.0);
            check(c.r >= 0 && c.r <= 1 && c.g >= 0 && c.g <= 1 && c.b >= 0 && c.b <= 1 && c.a >= 0 && c.a <= 1);
        }
        // Fading to transparent keeps the hue: premultiplied lanes.
        auto fade = anim::lerp(anim::Rgba::bytes(0, 0, 255), anim::Rgba{0, 0, 0, 0}, 0.5);
        check(fade.b > 0.9f && fade.r < 0.05f, "a fade does not pass through black");
        check_near(fade.a, 0.5, 1e-6);
    });

    s.test("hex colours", [] {
        check(anim::Rgba::parse_hex("#ff8000") == anim::Rgba::bytes(255, 128, 0));
        check(anim::Rgba::parse_hex("0f08") == anim::Rgba::bytes(0, 255, 0, 136));
        check(anim::Rgba::parse_hex("#12345678") == anim::Rgba::bytes(0x12, 0x34, 0x56, 0x78));
        check(!anim::Rgba::parse_hex("#12345").has_value());
        check(!anim::Rgba::parse_hex("#gg0000").has_value());
    });

    s.test("transform matrix composes scale, skew, rotate, translate", [] {
        anim::Transform2D t{.tx = 5, .ty = 7, .sx = 2, .sy = 3, .rotate = anim::dm::half_pi};
        auto m = t.matrix();
        // (1, 0) → scale → (2, 0) → rotate 90° → (0, 2) → translate → (5, 9)
        check_near(m[0] * 1 + m[2] * 0 + m[4], 5, 1e-12);
        check_near(m[1] * 1 + m[3] * 0 + m[5], 9, 1e-12);
        auto mid = anim::lerp(anim::Transform2D{}, t, 0.5);
        check_near(mid.rotate, anim::dm::half_pi / 2, 1e-15);
        check_near(mid.sx, 1.5, 1e-15);
    });

    s.test("motion path moves by arc length", [] {
        anim::MotionPath p;
        p.move_to({0, 0}).line_to({100, 0}).line_to({100, 100});
        check_near(p.length(), 200, 1e-9);
        check_near(p.point(0.25).x, 50, 1e-9);
        check_near(p.point(0.75).y, 50, 1e-9);
        check_near(p.at(0.25).angle, 0, 1e-12);
        check_near(p.at(0.75).angle, anim::dm::half_pi, 1e-12);
        anim::MotionPath circle;
        circle.move_to({1, 0}).cubic_to({1, 0.5523}, {0.5523, 1}, {0, 1});
        check_near(circle.length(), anim::dm::half_pi, 1e-3);
        auto q = circle.point(0.5);
        check_near(std::hypot(q.x, q.y), 1.0, 1e-3);
    });

    s.test("frame rates: frame times are exact and map back", [] {
        anim::FrameRate r{30000, 1001};
        for (std::int64_t f : {0LL, 1LL, 29LL, 30LL, 1799LL, 107892LL}) check_eq(r.frame_at(r.time_of(f)), f);
        anim::FrameRate sixty{60, 1};
        check_bits(sixty.time_of(30).count(), 0.5);
        check_eq(sixty.frame_at(seconds(0.5 - 1e-12)), std::int64_t{29});
    });

    s.test("clocks", [] {
        auto v = anim::Clock::virtual_time();
        v.advance(seconds(0.25));
        v.advance(seconds(0.25));
        check_bits(v.now().count(), 0.5);
        auto r = anim::Clock::render({60, 1});
        r.seek(120);
        check_bits(r.now().count(), 2.0);
        check_near(r.subframe(0, 4, 0.5).count(), 2.0 + (1.0 / 60) * 0.5 / 4 * 0.5, 1e-15);
        auto i = anim::Clock::interactive();
        auto t0 = std::chrono::steady_clock::time_point{};
        i.begin_frame(t0);
        i.begin_frame(t0 + std::chrono::milliseconds(100));
        check_near(i.now().count(), 0.1, 1e-9);
        i.pause();
        i.begin_frame(t0 + std::chrono::milliseconds(500));
        check_near(i.now().count(), 0.1, 1e-9);
        check(i.paused());
    });

    return s.run(argc, argv);
}
