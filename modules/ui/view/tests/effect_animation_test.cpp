// Arbitrary registered effect parameters use hanim, with real offscreen pixels.
import std;
import openmui.view;
import openmui.raster;
import openmui.image;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

namespace {
Value ends(double from, double to) { return Value(Value::List{from, to}); }
Value animation() {
    return Value{}.set("name", "pixelate").set("size", 8).set("duration", 1.).set("ease", "linear")
        .set("animate", Value{}.set("amount", ends(0, 1)).set("origin_x", ends(0, 3)));
}
struct Probe {
    Runtime runtime{{.size = {120, 80}}};
    Probe() {
        runtime.mount("main", row({.radius = 9, .clip = true}, {painter({.id = "canvas", .width = 80, .height = 70, .focusable = true,
            .paint = [](PaintContext& ctx) {
                for (int i = 0; i < 20; ++i) ctx.list.fill_rect({static_cast<float>(i) * 4, 0, 2, 70}, Color::hex(0x236ddf));
                ctx.list.fill_rect({8, 17, 31, 19}, Color::hex(0xe0a34e));
            }}), box({.id = "outside", .width = 40, .height = 80, .background = "#ef1234"})}));
        frame(0);
    }
    NodeId node() const { return runtime.find("main/canvas"); }
    void frame(double t) { check(runtime.frame({seconds(t), {}}).failure.empty()); }
    void seek(double t) { runtime.seek_time(seconds(t)); frame(10); }
    void effect(Value fx) { runtime.set(node(), "pixel_effect", std::move(fx)); frame(0); }
    std::vector<float> params(std::string_view name = "pixelate") const {
        const auto ref = *render::effects().find(name);
        for (const auto& command : runtime.display_list().commands())
            if (const auto* layer = std::get_if<render::PushLayer>(&command); layer && layer->effect.id == ref.id)
                return {layer->params.begin(), layer->params.end()};
        return {};
    }
    raster::Surface pixels(float scale = 1) {
        raster::Surface out(static_cast<int>(120 * scale), static_cast<int>(80 * scale));
        render::CpuRenderer renderer;
        const auto report = renderer.render(runtime.display_list(), out, {}, {.scale = scale, .background = Color::hex(0xf8fafc)});
        check(report.effects_skipped == 0 && report.invalid_paints == 0);
        return out;
    }
};
}

int main(int argc, char** argv) {
    Suite suite{"view.effect-animation"};
    suite.test("named parameters have exact endpoints actual middle pixels and repeatable seek", [] {
        Probe probe; const auto id = probe.node();
        const auto plain = probe.pixels(2);
        probe.effect(animation());
        check(probe.params().empty(), "an off UI filter adds no isolated group or duplicate ancestor clip");
        check(probe.pixels(2).hash() == plain.hash(), "zero is a real high-DPI bypass");
        probe.frame(.5); const auto middle = probe.pixels(2);
        check(probe.params() == std::vector<float>{8, .5f, 0, 1.5f, 0});
        check(middle.hash() != plain.hash());
        probe.frame(1.2); const auto end = probe.pixels(2);
        check(end.hash() != middle.hash());
        check(!probe.runtime.semantic_node(id).states.has(State::animating));
        probe.seek(.5); check(probe.pixels(2).hash() == middle.hash());
        probe.seek(0); check(probe.pixels(2).hash() == plain.hash());
        probe.seek(1); check(probe.pixels(2).hash() == end.hash());
        check(probe.node() == id && probe.runtime.hit_test({30, 30}) == id);
        probe.runtime.focus(id); probe.seek(.5); check(probe.runtime.focused() == id);
        for (int y = 0; y < end.height(); ++y) for (int x = 164; x < end.width(); ++x) check(end.at(x, y) == plain.at(x, y));
    });
    suite.test("delay reverse zero duration and motion preferences preserve the declared material endpoint", [] {
        Probe reverse; reverse.effect(animation().set("delay", .25).set("reverse", true));
        reverse.frame(.1); check(reverse.params()[1] == 1);
        reverse.frame(.75); check(reverse.params()[1] == .5f);
        reverse.frame(2); check(reverse.params().empty());
        Probe instant; instant.effect(animation().set("duration", 0.)); check(instant.params()[1] == 1);
        for (auto level : {MotionLevel::reduced, MotionLevel::off}) {
            Probe probe; probe.effect(animation()); probe.frame(.2); probe.runtime.set_motion_level(level); probe.frame(.3);
            check(probe.params()[1] == 1, "disable time variation, keep the final appearance");
        }
        Probe reduced; reduced.runtime.set_reduce_motion(true); reduced.effect(animation()); check(reduced.params()[1] == 1);
    });
    suite.test("the entire effect process is sampled rendered replayed and exported without a GUI", [] {
        Probe probe; const auto id = probe.node();
        probe.runtime.focus(id); probe.frame(0); const auto plain = probe.pixels(2);
        probe.effect(animation());
        std::vector<raster::Surface> frames; frames.reserve(61);
        Value::List observations;
        for (int step = 0; step <= 60; ++step) {
            const double time = static_cast<double>(step) / 60.;
            probe.seek(time); frames.push_back(probe.pixels(2));
            const auto p = probe.params();
            if (step == 0) check(p.empty() && frames.back().hash() == plain.hash());
            else {
                check(p.size() == 5); if (p.size() != 5) return;
                check_near(p[1], time, 1e-6); check_near(p[3], 3 * time, 1e-6);
                check(frames.back().hash() != frames[frames.size() - 2].hash(), "each sampled interval has actual visual evolution");
            }
            check(probe.node() == id && probe.runtime.hit_test({30, 30}) == id && probe.runtime.focused() == id);
            for (int y = 0; y < plain.height(); ++y) for (int x = 164; x < plain.width(); ++x)
                check(frames.back().at(x, y) == plain.at(x, y), "outside pixels remain unchanged at every sample");
            observations.push_back(Value{}.set("seconds", time).set("hash", std::format("{:016x}", frames.back().hash())));
        }
        for (int step = 60; step >= 0; --step) {
            probe.seek(static_cast<double>(step) / 60.);
            check(probe.pixels(2).hash() == frames[static_cast<std::size_t>(step)].hash(), "reverse seeking reproduces the full sampled process");
        }
        const auto view = [](const raster::Surface& f) {
            return imageio::View{reinterpret_cast<const std::uint8_t*>(f.pixels().data()), f.width(), f.height(), f.width() * 4};
        };
        const auto out = scratch_dir("effect-animation");
        std::vector<imageio::View> views; views.reserve(frames.size());
        for (std::size_t i = 0; i < frames.size(); ++i) {
            views.push_back(view(frames[i]));
            check(imageio::save_png(out / std::format("frame-{:02}.png", i), views.back()).has_value());
        }
        check(imageio::save_animated_webp(out / "process.webp", views, {.delay_num = 1, .delay_den = 60, .loops = 1, .lossless = true}).has_value());
        const std::array indices{0, 3, 9, 18, 30, 42, 54, 60};
        raster::Surface sheet(4 * plain.width(), 2 * plain.height());
        for (std::size_t k = 0; k < indices.size(); ++k) {
            const auto& f = frames[static_cast<std::size_t>(indices[k])];
            const int x0 = static_cast<int>(k % 4) * f.width(), y0 = static_cast<int>(k / 4) * f.height();
            for (int y = 0; y < f.height(); ++y) std::copy_n(f.row(y), f.width(), sheet.row(y0 + y) + x0);
        }
        check(imageio::save_png(out / "process-sheet.png", view(sheet)).has_value());
        std::println("motion-sampling {}", Value{}.set("renderer", "cpu").set("dpi", 2).set("fps", 60).set("background", "#f8fafc")
            .set("duration_seconds", 1).set("samples", Value(std::move(observations))).json());
    });
    suite.test("legacy progress and multiple effects share the same timeline", [] {
        Probe probe;
        const auto blur = Value{}.set("name", "blur").set("duration", 1.).set("ease", "linear").set("animate", Value{}.set("radius", ends(0, 6)));
        const auto dissolve = Value{}.set("name", "dissolve").set("duration", 1.).set("ease", "linear");
        probe.effect(Value(Value::List{blur, dissolve})); probe.frame(.5);
        check(probe.params("blur") == std::vector<float>{3});
        const auto p = probe.params("dissolve"); check(!p.empty() && p[0] == .5f);
        const auto mid = probe.pixels().hash(); probe.frame(2); probe.seek(.5); check(probe.pixels().hash() == mid);
    });
    suite.test("invalid channels and timing are diagnosed instead of creating partial animations", [] {
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        for (auto bad : {animation().set("duration", -1.), animation().set("duration", nan), animation().set("delay", -1.),
            animation().set("ease", "missing"), animation().set("animate", Value{}),
            animation().set("animate", Value{}.set("missing", ends(0, 1))),
            animation().set("animate", Value{}.set("amount", ends(0, 2))),
            animation().set("animate", Value{}.set("amount", ends(nan, 1))),
            animation().set("animate", Value{}.set("amount", Value(Value::List{0.})))}) {
            Probe probe; probe.effect(std::move(bad)); check(probe.params().empty());
        }
    });
    suite.test("appearance remains on a disabled node while legacy feedback is suppressed", [] {
        Probe probe;
        auto base = Value{}.set("name", "pixelate").set("size", 8).set("usage", "appearance");
        probe.runtime.set_effect_rules({{"paint", base}, {"paint:hovered", Value{}.set("name", "pixelate").set("size", 3)}});
        probe.frame(.1); check(!probe.params().empty());
        probe.runtime.set(probe.node(), "disabled", true); probe.frame(.2);
        const auto disabled = probe.params(); check(!disabled.empty() && disabled[0] == 8);
        probe.runtime.set_effect_rules({{"paint", base.set("usage", "feedback")}}); probe.frame(.3);
        check(probe.params().empty());
    });
    return suite.run(argc, argv);
}
