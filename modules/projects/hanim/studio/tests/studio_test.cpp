// The animation tool as an Agent uses it: a document read whole (every
// problem where it is), opened through hanim.control, sampled at moments in
// any order the same each time, and drawn — the ball's whole course checked
// in its numbers and its pixels, not only its ends.
import std;
import hanim.studio;
import xrender.vector.svg;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace studio = hanim::studio;

namespace {
std::string example() {
    std::ifstream in(std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "examples" /
                         "bounce.animation.omui.json",
                     std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
const studio::ObjectState& find(const std::vector<studio::ObjectState>& states, std::string_view id) {
    return *std::ranges::find(states, id, &studio::ObjectState::id);
}
bool says(const studio::Problems& problems, std::string_view where, std::string_view text) {
    return std::ranges::any_of(problems, [&](const auto& p) { return p.where == where && p.text.find(text) != std::string::npos; });
}
}  // namespace

UTest cases{"hanim.studio", [] {

    "the example reads, opens through hanim.control and has its plan"_test = [] {
        const auto document = studio::read(example());
        expect(document.has_value()) << document ? "" : studio::to_text(document.error());
        if (!document) return;
        expect(eq(document->objects.size(), std::size_t{4}));
        auto opened = studio::Studio::open(*document);
        expect(opened.has_value()) << opened ? "" : studio::to_text(opened.error());
        if (!opened) return;
        // The drop's course and its squash settling, the travel with the star's spring, the morph, the wobble.
        expect(opened->duration().count() > 3 && opened->duration().count() < 8) << std::format("{} s", opened->duration().count());
        expect(opened->markers().size() == 2 && opened->markers()[0].name == "landed" && opened->markers()[1].name == "arrived") << "the markers";
        const auto ops = opened->operations();
        expect(ops.size() == 3 && ops[0].name == "animation.prepare" && ops[1].name == "animation.play" && ops[2].name == "animation.pause" &&
                  std::ranges::all_of(ops, [](const auto& op) { return op.decision == openas::Decision::run; })) << "prepared, played, paused — each admitted";
    };

    "the ball's course: it falls, bounces lower each time, travels, becomes the box; the star springs"_test = [] {
        auto opened = studio::Studio::open(*studio::read(example()));
        if (!opened) { expect(false) << studio::to_text(opened.error()); return; };
        const double landed = opened->markers()[0].time.count(), arrived = opened->markers()[1].time.count();
        std::vector<double> heights;
        for (int step = 0; step <= 120; ++step) heights.push_back(find(*opened->state(hanim::seconds(1.2 * step / 120)), "ball").high.y);
        expect(std::abs(heights.front() - 98) < 0.5) << std::format("starts with its bottom at 98: {}", heights.front());
        expect(std::abs(heights.back() - 280) < 0.5) << std::format("lands for good on the ground: {}", heights.back());
        // Its bounces: the heights it rises back to, lower each time.
        std::vector<double> peaks;
        for (std::size_t index = 1; index + 1 < heights.size(); ++index)
            if (heights[index] < heights[index - 1] && heights[index] <= heights[index + 1] && heights[index] < 279) peaks.push_back(heights[index]);
        expect(peaks.size() == 2 && peaks[0] < peaks[1]) << std::format("{} bounces", peaks.size());
        const auto ground = find(*opened->state(hanim::seconds(0.6)), "ground");
        expect(ground.drawn && std::abs(ground.position.y - 283) < 1e-9) << "the ground stands still, drawn";
        expect(std::abs(find(*opened->state(hanim::seconds(landed)), "ball").position.x - 120) < 1e-9) << "it lands where it fell";
        const auto travelled = *opened->state(hanim::seconds(arrived));
        expect(std::abs(find(travelled, "ball").position.x - 360) < 1e-6) << std::format("travelled to the box: {}", find(travelled, "ball").position.x);
        // The star's spring overshoots before it settles at 1.
        double most = 0;
        for (int step = 0; step <= 60; ++step) most = std::max(most, find(*opened->state(hanim::seconds(landed + (arrived - landed) * step / 60)), "star").scale.x);
        expect(most > 1.02) << std::format("the star overshoots: {}", most);
        expect(std::abs(find(travelled, "star").scale.x - 1) < 1e-3) << "and settles";
        const auto end = *opened->state(opened->duration());
        const auto& ball = find(end, "ball");
        expect(std::abs((ball.high.x - ball.low.x) - 64) < 0.5 && std::abs((ball.high.y - ball.low.y) - 64) < 0.5) << std::format("the ball is the box at the end: {} × {}", ball.high.x - ball.low.x, ball.high.y - ball.low.y);
        expect(!find(end, "box").drawn) << "the hidden box is never drawn itself";
        // Any order, the same answers.
        const auto again = find(*opened->state(hanim::seconds(0.6)), "ball").high.y;
        expect(eq(again, heights[60]));
    };

    "drawn: the frames differ through the course and repeat exactly"_test = [] {
        auto opened = studio::Studio::open(*studio::read(example()));
        if (!opened) { expect(false) << studio::to_text(opened.error()); return; };
        const auto draw = [&](double time) {
            xrender::raster::Surface surface(480, 320);
            const auto drawn = opened->render(hanim::seconds(time), surface);
            expect(drawn.has_value()) << drawn ? "" : studio::to_text(drawn.error());
            return std::vector<xrender::raster::Rgba8>(surface.pixels().begin(), surface.pixels().end());
        };
        const double end_time = opened->duration().count();
        const auto start = draw(0), middle = draw(1.0), end = draw(end_time), repeated = draw(1.0);
        expect(start != middle && middle != end) << "the picture changes";
        expect(middle == repeated) << "the same moment, the same pixels";
        // The ball's colour at its start, under its centre.
        const auto& pixel = start[70 * 480 + 120];
        expect(pixel.r > 200 && pixel.g < 140 && pixel.b < 100) << std::format("orange under the ball: {} {} {}", pixel.r, pixel.g, pixel.b);
    };

    "hanim's recipes as steps: entrances fade in, exits fade out, cues return, loops count, moves arrive"_test = [] {
        const auto document = studio::read(R"({"version": 1, "size": [480, 320], "objects": [
            {"id": "a", "shape": {"rect": [40, 40]}, "fill": "#e4572e", "at": [100, 100]},
            {"id": "b", "shape": {"circle": 20}, "fill": "#29335c", "at": [300, 100]},
            {"id": "c", "shape": {"star": {"points": 5, "outer": 24, "inner": 10}}, "stroke": {"color": "#ffffff", "width": 2}, "at": [200, 220]},
            {"id": "d", "shape": {"circle": 10}, "fill": "#2a9d8f", "at": [400, 250]}
        ], "program": {"do": "sequence", "items": [
            {"do": "parallel", "items": [{"do": "pop_in", "object": "a"}, {"do": "rise_in", "object": "b", "distance": 30}, {"do": "write_in", "object": "c"}]},
            {"do": "marker", "name": "in"},
            {"do": "heartbeat", "object": "a", "scale": 1.2},
            {"do": "swap", "object": "a", "with": "b", "by": [200, 0]},
            {"do": "marker", "name": "swapped"},
            {"do": "bob", "object": "d", "amplitude": 8, "period": 1, "count": 2},
            {"do": "fade_out", "object": "c"}
        ]}})");
        expect(document.has_value()) << document ? "" : studio::to_text(document.error());
        if (!document) return;
        auto opened = studio::Studio::open(*document);
        expect(opened.has_value()) << opened ? "" : studio::to_text(opened.error());
        if (!opened) return;
        const auto start = *opened->state(hanim::seconds(0));
        expect(find(start, "a").opacity < .01 && std::abs(find(start, "a").scale.x - .3) < 1e-6) << "a pops in from small and unseen";
        expect(std::abs(find(start, "b").position.y - 130) < 1e-6) << "b rises from 30 below";
        const double in = opened->markers()[0].time.count(), swapped = opened->markers()[1].time.count();
        const auto shown = *opened->state(hanim::seconds(in));
        expect(std::abs(find(shown, "a").opacity - 1) < 1e-6 && std::abs(find(shown, "a").scale.x - 1) < 1e-6 && std::abs(find(shown, "b").position.y - 100) < 1e-6) << "both in place, seen";
        const auto after = *opened->state(hanim::seconds(swapped));
        expect(std::abs(find(after, "a").position.x - 300) < 1e-6 && std::abs(find(after, "b").position.x - 100) < 1e-6) << "a and b exchanged places";
        double highest = 1e9;
        for (int k = 0; k <= 40; ++k) highest = std::min(highest, find(*opened->state(hanim::seconds(swapped + 2.0 * k / 40)), "d").position.y);
        expect(std::abs(highest - 242) < .01) << std::format("d bobs 8 up: {}", highest);
        const auto end = *opened->state(opened->duration());
        expect(find(end, "c").opacity < .01 && std::abs(find(end, "d").position.y - 250) < 1e-6) << "c faded out; d back where it was";
        xrender::raster::Surface surface(480, 320);
        for (const double t : {0.1, in, swapped, opened->duration().count()}) expect(opened->render(hanim::seconds(t), surface).has_value()) << "drawn";
        // An object whose first step is an entrance is not on the stage before it; one standing still is.
        const auto later = studio::read(R"({"version": 1, "objects": [{"id": "a", "shape": {"circle": 5}, "fill": "#fff", "at": [10, 10]},
            {"id": "b", "shape": {"circle": 5}, "fill": "#fff", "at": [30, 10]}],
            "program": {"do": "sequence", "items": [{"do": "wait", "duration": 0.5}, {"do": "fade_in", "object": "a"}]}})");
        auto waiting = studio::Studio::open(*later);
        expect(waiting && !find(*waiting->state(hanim::seconds(.2)), "a").drawn && find(*waiting->state(hanim::seconds(.2)), "b").drawn &&
                  find(*waiting->state(hanim::seconds(.6)), "a").drawn) << "on the stage from its entrance";
        // A path shape: SVG path data, arcs and all; bad data said where it stops.
        const auto drawn = studio::read(R"({"version": 1, "objects": [{"id": "ring", "shape": {"path": "M-10 0 A10 10 0 1 1 10 0 A10 10 0 1 1 -10 0 Z M-5 0 A5 5 0 1 0 5 0 A5 5 0 1 0 -5 0 Z"}, "fill": "#fff", "at": [50, 50]}],
            "program": {"do": "pulse", "object": "ring"}})");
        auto ring = drawn ? studio::Studio::open(*drawn) : std::unexpected(drawn.error());
        expect(ring.has_value()) << ring ? "" : studio::to_text(ring.error());
        if (ring) {
            const auto& state = find(*ring->state(hanim::seconds(0)), "ring");
            expect(std::abs(state.high.x - state.low.x - 20) < .01) << std::format("a ring 20 across: {}", state.high.x - state.low.x);
        }
        const auto broken_path = studio::read(R"({"version": 1, "objects": [{"id": "a", "shape": {"path": "M0 0 L5"}, "fill": "#fff"}], "program": {"do": "pulse", "object": "a"}})");
        expect(!broken_path && says(broken_path.error(), "/objects/0/shape/path", "stops at character")) << broken_path ? "accepted" : studio::to_text(broken_path.error());
        // A recipe's fields are its own.
        const auto wrong = studio::read(R"({"version": 1, "objects": [{"id": "a", "shape": {"circle": 5}, "fill": "#fff"}],
            "program": {"do": "rise_in", "object": "a", "distnace": 10}})");
        expect(!wrong && says(wrong.error(), "/program/distnace", "not a field of `rise_in`")) << wrong ? "accepted" : studio::to_text(wrong.error());
    };

    "the examples: each reads, opens, has an end and moves through it — every object drawn where it should be; each frame as SVG is vectors alone"_test = [] {
        const auto dir = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "examples";
        std::size_t count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.path().string().ends_with(".animation.omui.json")) continue;
            ++count;
            const auto name = entry.path().filename().string();
            std::ifstream in(entry.path(), std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            const auto document = studio::read(text);
            expect(document.has_value()) << std::format("{}: {}", name, document ? "" : studio::to_text(document.error()));
            if (!document) continue;
            auto opened = studio::Studio::open(*document);
            expect(opened.has_value()) << std::format("{}: {}", name, opened ? "" : studio::to_text(opened.error()));
            if (!opened) continue;
            const double length = opened->duration().count();
            expect(std::isfinite(length) && length > .3 && length < 10) << std::format("{} lasts {} s", name, length);
            xrender::raster::Surface a(static_cast<int>(document->width), static_cast<int>(document->height)), b = a, c = a;
            expect(opened->render(hanim::seconds(length * .1), a).has_value() && opened->render(hanim::seconds(length * .5), b).has_value() &&
                      opened->render(hanim::seconds(length), c).has_value()) << std::format("{} draws", name);
            expect(!std::ranges::equal(a.bytes(), b.bytes()) || !std::ranges::equal(b.bytes(), c.bytes())) << std::format("{} moves", name);
            // Every frame of it as SVG is vectors alone: shapes, paths, fades (the opacity
            // effect), trimmed strokes — nothing left as pixels.
            for (const double at : {0.0, length * .25, length * .5, length * .75, length}) {
                const auto list = opened->display(hanim::seconds(at));
                const auto picture = list ? xrender::svg::write(**list, {}, {.view = {0, 0, static_cast<float>(document->width), static_cast<float>(document->height)}})
                                          : std::expected<xrender::svg::Picture, std::string>(std::unexpected("no list"));
                expect(picture && picture->report.islands.empty() && picture->report.unencoded == 0) << std::format("{} at {:.2f} s as SVG: {}", name, at, picture ? std::format("{} islands", picture->report.islands.size()) : picture.error());
            }
        }
        expect(count >= 20) << std::format("{} examples", count);
    };

    "every problem, where it is"_test = [] {
        const auto broken = studio::read(R"({"version": 1, "objects": [
            {"id": "a", "shape": {"circle": 10}, "fill": "#ff0000"},
            {"id": "a", "shape": {"hexagon": 3}},
            {"id": "b", "shape": {"rect": [10, 10]}, "fill": "red", "colour": "#fff"}
        ], "program": {"do": "sequence", "items": [
            {"do": "move_to", "object": "c", "to": [1, 2]},
            {"do": "twirl", "object": "a"},
            {"do": "move_by", "object": "a", "by": [1, 2], "ease": "wobbly"}
        ]}})");
        expect(!broken) << "refused";
        if (broken) return;
        const auto& problems = broken.error();
        expect(says(problems, "/objects/1/id", "twice")) << studio::to_text(problems);
        expect(says(problems, "/objects/1/shape/hexagon", "not a shape")) << "the unknown shape";
        expect(says(problems, "/objects/1", "a fill, a stroke or both")) << "nothing to draw";
        expect(says(problems, "/objects/2/fill", "not a colour")) << "the colour";
        expect(says(problems, "/objects/2/colour", "not a field")) << "the misspelt field";
        expect(says(problems, "/program/items/0/object", "no object `c`")) << "the missing object";
        expect(says(problems, "/program/items/1/do", "not a step")) << "the unknown step";
        expect(says(problems, "/program/items/2/ease", "not an ease")) << "the unknown ease";
        const auto syntax = studio::read("{\n  \"version\": 1,\n  \"objects\": [ }");
        expect(!syntax && syntax.error().size() == 1 && syntax.error()[0].where.starts_with("line 3")) << syntax ? "" : syntax.error()[0].where;
    };

    "pointing at a moment: the steps that govern it, innermost first, each by its JSON pointer"_test = [] {
        auto opened = studio::Studio::open(*studio::read(example()));
        if (!opened) return;
        // 0.3 s: the ball is dropping (the first step), inside the whole sequence.
        const auto dropping = opened->at(hanim::seconds(0.3));
        expect(dropping.size() == 2 && dropping[0].where == "/program/items/0" && dropping[1].where == "/program") << "the drop, then the sequence";
        if (dropping.empty()) return;
        expect(dropping[0].start == 0 && dropping[0].progress > 0.1 && dropping[0].progress < 0.3 && dropping[0].objects == std::vector<std::string>{"ball"}) << std::format("the drop's timing {} → {}, {}", dropping[0].start, dropping[0].end, dropping[0].progress);
        // While the ball travels the star springs: asking for the star names its step, not the ball's.
        const double travel = opened->markers()[0].time.count() + 0.2;
        const auto star = opened->at(hanim::seconds(travel), "star");
        expect(!star.empty() && star[0].where == "/program/items/2/items/1") << star.empty() ? "nothing for the star" : star[0].where;
        const auto ball = opened->at(hanim::seconds(travel), "ball");
        expect(!ball.empty() && ball[0].where == "/program/items/2/items/0") << ball.empty() ? "nothing for the ball" : ball[0].where;
        expect(opened->at(hanim::seconds(travel), "nobody").empty()) << "an object no step moves: nothing governs it";
    };

    "variants: one value set several ways, each read again; a wrong one carries its problem, a pointer to nowhere refuses all"_test = [] {
        const std::string text = example();
        const std::vector<std::string> values{"0", "4", "\"x\""};
        const auto variants = studio::vary(text, "/program/items/0/bounces", values);
        expect(variants.has_value() && variants->size() == 4) << "as written, then three";
        if (!variants || variants->size() != 4) return;
        expect((*variants)[0].value.empty() && (*variants)[0].text == text && (*variants)[0].document.has_value()) << "the first is the document as written";
        expect((*variants)[1].document && (*variants)[2].document) << "0 and 4 bounces read";
        expect(!(*variants)[3].document && says((*variants)[3].document.error(), "/program/items/0/bounces", "number")) << "\"x\" is not a count";
        // The variants differ where the value acts: the bounces drawn differ part-way and agree at the start.
        auto fewer = studio::Studio::open(*(*variants)[1].document);
        auto more = studio::Studio::open(*(*variants)[2].document);
        if (!fewer || !more) return;
        xrender::raster::Surface first(static_cast<int>(fewer->document().width), static_cast<int>(fewer->document().height)), second(first.width(), first.height());
        expect(fewer->render(hanim::seconds(0), first) && more->render(hanim::seconds(0), second) && first.hash() == second.hash()) << "the same start";
        expect(fewer->render(hanim::seconds(0.7), first) && more->render(hanim::seconds(0.7), second) && first.hash() != second.hash()) << "different bounces";
        // A key the step does not have yet is added; a pointer to nowhere, or not JSON, refuses them all.
        const auto eased = studio::vary(text, "/program/items/0/ease", std::vector<std::string>{"\"out_cubic\""});
        expect(eased.has_value() && eased->size() == 2) << "a new key of an existing step";
        expect(!studio::vary(text, "/program/items/9/x", values).has_value()) << "nowhere";
        expect(!studio::vary(text, "program", values).has_value()) << "not a pointer";
        expect(!studio::vary(text, "/program/items/0/bounces", std::vector<std::string>{"four"}).has_value()) << "not JSON";
    };
}};
