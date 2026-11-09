// Pixel pictures (the site plan's C1): one picture as a part a pixel, as an
// outline an ink and as that outline smoothed with the same segments; its
// pixels flying apart and coming back together, sampled over the whole
// course — the start, the middle, the end, played backwards — and the
// staircase morphing into the curve.
import std;
import hanim.engine;
import mcpplibs.mtest;

using namespace hanim;
// mtest by name: some of its names (tag, suite, body …) are this test's own too.
using mcpplibs::mtest::UTest, mcpplibs::mtest::expect, mcpplibs::mtest::eq, mcpplibs::mtest::operator""_test;

namespace {
template <class T, class E>
T take(std::expected<T, E> result) {
    if constexpr (requires { result.error().text(); })
        expect(bool(result)) << (result ? std::string() : result.error().text());
    else
        expect(bool(result));
    if (!result) std::abort();
    if constexpr (!std::is_void_v<T>) return std::move(*result);
}
bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }

// A small face: an outline (o), a body (b) with a hole in it (the eye, w),
// and a pixel that touches the body only at a corner (the ear, o).
const PixelGrid face{{{'o', Rgba{.1f, .1f, .3f, 1}}, {'b', Rgba{.3f, .3f, .9f, 1}}, {'w', Rgba{1, 1, 1, 1}}},
                     {"o.....",
                      ".oooo.",
                      ".obbo.",
                      ".obwo.",
                      ".oooo."},
                     10};

// The area a contour of corners-and-middles encloses (its segments' ends: the
// staircase's are exact).
double area(const Contour& contour) {
    const ShapeContour view = take(contour.view());
    double twice = 0;
    Vec2 at = view.start;
    for (const ShapeSegment& segment : view.segments) {
        twice += at.x * segment.end.y - segment.end.x * at.y;
        at = segment.end;
    }
    return twice / 2;
}

struct Flight {
    Engine engine = take(Engine::make());
    ObjectRef picture;
    PlaybackRef playback;
    double length = 0;
    explicit Flight(const std::function<Program(ObjectRef)>& recipe) {
        picture = take(engine.create_object(take(pixel_object("face", face, {.form = PixelForm::pixels}))));
        playback = take(engine.play(recipe(picture)));
        take(engine.pause(playback));
        length = take(engine.inspect(playback)).duration.count();
    }
    // Each pixel's centre and opacity at `t`, by its id.
    std::map<std::string, std::pair<Vec2, double>> at(double t) {
        take(engine.seek(playback, seconds(t)));
        const auto frame = take(engine.frame(playback, 0));
        std::map<std::string, std::pair<Vec2, double>> out;
        for (const auto& part : frame.geometry.parts)
            if (part.info.id.starts_with("px_")) out[std::string(part.info.id)] = {{part.local.tx, part.local.ty}, part.fill ? part.fill->a : 1.0};
        return out;
    }
};
Vec2 home(std::string_view id) {
    int x = 0, y = 0;
    std::sscanf(std::string(id).c_str(), "px_%d_%d", &x, &y);
    return {(x + .5) * face.cell, (y + .5) * face.cell};
}
double moved(const std::pair<Vec2, double>& pose, std::string_view id) { return std::hypot(pose.first.x - home(id).x, pose.first.y - home(id).y); }
}  // namespace

UTest cases{"hanim.pixels", [] {

    "one picture three ways: a part a pixel; an outline an ink, its holes and corners its own; smoothed with the same segments"_test = [] {
        const auto cells = take(pixel_cells(face));
        expect(eq(cells.size(), std::size_t{17}));
        const auto pixels = take(pixel_object("face", face, {.form = PixelForm::pixels}));
        expect(eq(pixels.parts.size(), std::size_t{18}));  // the root and a part a pixel
        expect(pixels.parts[1].id == pixel_id(0, 0) && near(pixels.parts[1].local.tx, 5) && near(pixels.parts[1].local.ty, 5));

        const auto outline = take(pixel_object("face", face));
        expect(eq(outline.parts.size(), std::size_t{4}));  // the root and an ink each
        const auto& ink_o = outline.parts[1];
        expect(ink_o.id == ink_id('o') && ink_o.fill == face.inks[0].color);
        // The outline ink's region covers every ink after it: the ear, and the
        // face of 4 × 4 — two loops (they touch only at a corner).
        expect(eq(ink_o.contours.size(), std::size_t{2}));
        double covered = 0;
        for (const Contour& c : ink_o.contours) covered += area(c);
        expect(near(covered, 17 * 100)) << std::format("the outline ink's region: 17 cells, {} by area", covered / 100);
        // The body's region is the body and the eye; the eye's, one cell.
        const auto& ink_b = outline.parts[2];
        expect(ink_b.contours.size() == 1 && near(area(ink_b.contours[0]), 4 * 100));
        expect(near(area(outline.parts[3].contours[0]), 100));

        const auto smooth = take(pixel_object("face", face, {.form = PixelForm::smooth}));
        expect(eq(smooth.parts.size(), outline.parts.size()));
        for (std::size_t i = 1; i < smooth.parts.size(); ++i)
            for (std::size_t c = 0; c < smooth.parts[i].contours.size(); ++c)
                expect(eq(smooth.parts[i].contours[c].segment_count(), outline.parts[i].contours[c].segment_count()));
        expect(area(smooth.parts[3].contours[0]) < 100) << "rounded off: the smooth eye is smaller than its square";
        // A loop's start, the middle of an edge, is the same in both.
        expect(take(smooth.parts[3].contours[0].view()).start == take(outline.parts[3].contours[0].view()).start);
    };

    "what a picture cannot be is refused"_test = [] {
        expect(pixel_object("face", {{{'o', {}}}, {"ox"}, 10}).error() == ObjectError::invalid_color) << "an unknown ink";
        expect(pixel_object("face", {{{'o', {}}, {'o', {}}}, {"o"}, 10}).error() == ObjectError::duplicate_id) << "an ink twice";
        expect(pixel_object("face", {{{'o', {}}}, {"o"}, 0}).error() == ObjectError::invalid_coordinate) << "no size";
        expect(pixel_object("face", face, {.form = PixelForm::smooth, .smoothing = 2}).error() == ObjectError::invalid_coordinate);
        expect(pixel_object("", face).error() == ObjectError::invalid_id);
    };

    "scattered: at rest at the start; the nearest gone first; all far and clear at the end; the same seed the same flight"_test = [] {
        Flight scatter([](ObjectRef o) { return anim::scatter_out(o, face, {.seed = 7}); });
        expect(near(scatter.length, .9, 1e-9)) << std::format("0.9 s by default: {}", scatter.length);
        const auto start = scatter.at(0);
        expect(eq(start.size(), std::size_t{17}));
        for (const auto& [id, pose] : start) expect(near(moved(pose, id), 0) && near(pose.second, 1)) << id;
        // A fifth in: the middle of the body (it starts at once) has gone further than the ear (the farthest, last).
        const auto early = scatter.at(.2);
        expect(moved(early.at(pixel_id(2, 2)), pixel_id(2, 2)) > 1 && near(moved(early.at(pixel_id(0, 0)), pixel_id(0, 0)), 0)) << "the nearest first";
        const auto middle = scatter.at(.45);
        int on_the_way = 0;
        for (const auto& [id, pose] : middle) on_the_way += pose.second > .01 && pose.second < .99 ? 1 : 0;
        expect(on_the_way > 8) << std::format("halfway, most are fading: {}", on_the_way);
        const auto end = scatter.at(scatter.length);
        for (const auto& [id, pose] : end) expect(moved(pose, id) > 48 * .6 - 1e-6 && near(pose.second, 0)) << id;
        Flight again([](ObjectRef o) { return anim::scatter_out(o, face, {.seed = 7}); });
        Flight other([](ObjectRef o) { return anim::scatter_out(o, face, {.seed = 8}); });
        expect(again.at(.45) == middle) << "the same seed, the same flight";
        expect(other.at(.45) != middle) << "another seed, another";
    };

    "coming together: from where the scatter ends, the farthest first, at rest at the end; the scatter played backwards ends at rest too"_test = [] {
        Flight scatter([](ObjectRef o) { return anim::scatter_out(o, face, {.seed = 3}); });
        Flight assemble([](ObjectRef o) { return anim::assemble_in(o, face, {.seed = 3}); });
        const auto scattered = scatter.at(scatter.length), from = assemble.at(0);
        for (const auto& [id, pose] : from) {
            expect(near(pose.first.x, scattered.at(id).first.x, 1e-9) && near(pose.first.y, scattered.at(id).first.y, 1e-9)) << id;
            expect(near(pose.second, 0)) << id;
        }
        const auto early = assemble.at(.2);
        expect(moved(early.at(pixel_id(0, 0)), pixel_id(0, 0)) < moved(from.at(pixel_id(0, 0)), pixel_id(0, 0)) &&
                  near(moved(early.at(pixel_id(3, 3)), pixel_id(3, 3)), moved(from.at(pixel_id(3, 3)), pixel_id(3, 3)), 1e-9)) << "the ear (farthest) is on its way while the eye waits";
        for (const auto& [id, pose] : assemble.at(assemble.length)) expect(near(moved(pose, id), 0, 1e-9) && near(pose.second, 1)) << id;
        Flight back([](ObjectRef o) { return anim::scatter_out(o, face, {.seed = 3}).reverse(); });
        for (const auto& [id, pose] : back.at(0)) expect(near(pose.first.x, scattered.at(id).first.x, 1e-9)) << id;
        for (const auto& [id, pose] : back.at(back.length)) expect(near(moved(pose, id), 0, 1e-9) && near(pose.second, 1)) << id;
    };

    "the staircase into the curve: the outline morphs into the smooth form, every corner on its way, and back"_test = [] {
        Engine engine = take(Engine::make());
        const auto sharp = take(engine.create_object(take(pixel_object("face", face))));
        const auto round = take(engine.create_object(take(pixel_object("face", face, {.form = PixelForm::smooth}))));
        const auto played = take(engine.play(anim::morph(sharp, round).duration(seconds(1)).easing(ease::linear)));
        take(engine.pause(played));
        // The eye's area over the morph (its cubics' ends): the square's at the
        // start, the rounded loop's at the end, between them in the middle.
        const auto eye_area = [&](double t) {
            take(engine.seek(played, seconds(t)));
            const auto frame = take(engine.frame(played, 0));
            for (const auto& part : frame.geometry.parts) {
                if (part.info.id != ink_id('w')) continue;
                double twice = 0;
                for (std::size_t c = part.info.first_contour; c < part.info.first_contour + part.info.contour_count; ++c) {
                    const auto& contour = frame.geometry.contours[c];
                    for (std::size_t k = contour.first_cubic; k < contour.first_cubic + contour.cubic_count; ++k) {
                        const ShapeCubic& cubic = frame.geometry.cubics[k];
                        twice += cubic.start.x * cubic.end.y - cubic.end.x * cubic.start.y;
                    }
                }
                return twice / 2;
            }
            return 0.0;
        };
        const auto smooth = take(pixel_object("face", face, {.form = PixelForm::smooth}));
        const double rounded = area(smooth.parts[3].contours[0]);
        const double first = eye_area(0), middle = eye_area(.5), last = eye_area(1);
        expect(near(first, 100, 1e-6)) << std::format("square at the start: {}", first);
        expect(near(last, rounded, 1e-6)) << std::format("rounded at the end: {} (the curve's {})", last, rounded);
        expect(middle < first - 1e-3 && middle > last + 1e-3) << std::format("between halfway: {}", middle);
        expect(near(eye_area(.25), eye_area(.25))) << "a sample is a function of time";
    };
}};
