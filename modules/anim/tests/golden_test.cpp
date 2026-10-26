// A-level conformance of the animation results: every curve family sampled on
// a fixed grid, hashed, and compared with the value pinned in this file. The
// same hash is required on every platform and toolchain the CI runs; a change
// of it is a change of animation results and needs a spec decision.
import std;
import openmui.anim;
import openmui.testing;

using namespace omui::testing;
namespace anim = omui::anim;
using anim::seconds;

int main(int argc, char** argv) {
    Suite s{"anim.golden"};

    s.test("curves", [] {
        Hash h;
        for (int k = 0; k <= static_cast<int>(anim::EaseKind::in_out_bounce); ++k) {
            anim::Ease e{static_cast<anim::EaseKind>(k)};
            for (int i = 0; i <= 256; ++i) h.f64(e(i / 256.0));
        }
        for (auto e : {anim::ease::css_ease, anim::ease::standard, anim::Ease::bezier(0.68, -0.6, 0.32, 1.6)})
            for (int i = 0; i <= 256; ++i) h.f64(e(i / 256.0));
        std::println("    curves {}", h.hex());
        check_eq(h.hex(), std::string("ab1e08bd389e7baa"));
    });

    s.test("springs and decay", [] {
        Hash h;
        for (auto p : {anim::springs::standard, anim::springs::wobbly, anim::springs::molasses,
                       anim::SpringParams{100, 20, 1}, anim::SpringParams::response(0.35, 0.25)}) {
            anim::Oscillator o(0, 12, 1, p, {.delta = 1e-4, .speed = 1e-4});
            h.f64(o.settle_time());
            for (int i = 0; i <= 600; ++i) {
                h.f64(o.position(i / 240.0));
                h.f64(o.velocity(i / 240.0));
            }
        }
        anim::Glide g(3, -1700, {});
        h.f64(g.settle_time());
        for (int i = 0; i <= 600; ++i) h.f64(g.position(i / 240.0));
        std::println("    springs {}", h.hex());
        check_eq(h.hex(), std::string("0d1c041e44ae6388"));
    });

    s.test("colours and motion", [] {
        Hash h;
        auto a = anim::Rgba::bytes(255, 64, 0);
        auto b = anim::Rgba::bytes(20, 120, 255, 200);
        for (int i = 0; i <= 256; ++i) {
            auto c = anim::lerp(a, b, i / 256.0);
            h.f32(c.r);
            h.f32(c.g);
            h.f32(c.b);
            h.f32(c.a);
        }
        anim::Motion<anim::Vec2> m({0, 0});
        m.spring_to(seconds(0), {300, 200}, anim::springs::wobbly)
            .tween_to(seconds(0.4), {-100, 50}, seconds(0.5), anim::ease::in_out_cubic)
            .decay(seconds(1.1), {800, -300})
            .spring_to(seconds(2.0), {0, 0}, anim::springs::gentle);
        anim::FrameRate rate{60, 1};
        for (std::int64_t f = 0; f < 360; ++f) {
            auto v = m.value(rate.time_of(f));
            h.f64(v.x);
            h.f64(v.y);
        }
        anim::MotionPath p;
        p.move_to({0, 0}).cubic_to({50, -80}, {150, 80}, {200, 0});
        for (int i = 0; i <= 128; ++i) {
            auto pose = p.at(i / 128.0);
            h.f64(pose.position.x);
            h.f64(pose.position.y);
            h.f64(pose.angle);
        }
        std::println("    colours/motion {}", h.hex());
        check_eq(h.hex(), std::string("9a82a28e165d19fe"));
    });

    return s.run(argc, argv);
}
