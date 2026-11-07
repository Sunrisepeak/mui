// The animation tool as an Agent uses it: a document read whole (every
// problem where it is), opened through hanim.control, sampled at moments in
// any order the same each time, and drawn — the ball's whole course checked
// in its numbers and its pixels, not only its ends.
import std;
import hanim.studio;
import mlibs.testing;

using namespace mlibs::testing;
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

int main(int argc, char** argv) {
    Suite s{"hanim.studio"};

    s.test("the example reads, opens through hanim.control and has its plan", [] {
        const auto document = studio::read(example());
        check(document.has_value(), document ? "" : studio::to_text(document.error()));
        if (!document) return;
        check_eq(document->objects.size(), std::size_t{4});
        auto opened = studio::Studio::open(*document);
        check(opened.has_value(), opened ? "" : studio::to_text(opened.error()));
        if (!opened) return;
        // The drop's course and its squash settling, the travel with the star's spring, the morph, the wobble.
        check(opened->duration().count() > 3 && opened->duration().count() < 8, std::format("{} s", opened->duration().count()));
        check(opened->markers().size() == 2 && opened->markers()[0].name == "landed" && opened->markers()[1].name == "arrived", "the markers");
        const auto ops = opened->operations();
        check(ops.size() == 3 && ops[0].name == "animation.prepare" && ops[1].name == "animation.play" && ops[2].name == "animation.pause" &&
                  std::ranges::all_of(ops, [](const auto& op) { return op.decision == openas::Decision::run; }),
              "prepared, played, paused — each admitted");
    });

    s.test("the ball's course: it falls, bounces lower each time, travels, becomes the box; the star springs", [] {
        auto opened = studio::Studio::open(*studio::read(example()));
        if (!opened) return check(false, studio::to_text(opened.error()));
        const double landed = opened->markers()[0].time.count(), arrived = opened->markers()[1].time.count();
        std::vector<double> heights;
        for (int step = 0; step <= 120; ++step) heights.push_back(find(*opened->state(hanim::seconds(1.2 * step / 120)), "ball").high.y);
        check(std::abs(heights.front() - 98) < 0.5, std::format("starts with its bottom at 98: {}", heights.front()));
        check(std::abs(heights.back() - 280) < 0.5, std::format("lands for good on the ground: {}", heights.back()));
        // Its bounces: the heights it rises back to, lower each time.
        std::vector<double> peaks;
        for (std::size_t index = 1; index + 1 < heights.size(); ++index)
            if (heights[index] < heights[index - 1] && heights[index] <= heights[index + 1] && heights[index] < 279) peaks.push_back(heights[index]);
        check(peaks.size() == 2 && peaks[0] < peaks[1], std::format("{} bounces", peaks.size()));
        const auto ground = find(*opened->state(hanim::seconds(0.6)), "ground");
        check(ground.drawn && std::abs(ground.position.y - 283) < 1e-9, "the ground stands still, drawn");
        check(std::abs(find(*opened->state(hanim::seconds(landed)), "ball").position.x - 120) < 1e-9, "it lands where it fell");
        const auto travelled = *opened->state(hanim::seconds(arrived));
        check(std::abs(find(travelled, "ball").position.x - 360) < 1e-6, std::format("travelled to the box: {}", find(travelled, "ball").position.x));
        // The star's spring overshoots before it settles at 1.
        double most = 0;
        for (int step = 0; step <= 60; ++step) most = std::max(most, find(*opened->state(hanim::seconds(landed + (arrived - landed) * step / 60)), "star").scale.x);
        check(most > 1.02, std::format("the star overshoots: {}", most));
        check(std::abs(find(travelled, "star").scale.x - 1) < 1e-3, "and settles");
        const auto end = *opened->state(opened->duration());
        const auto& ball = find(end, "ball");
        check(std::abs((ball.high.x - ball.low.x) - 64) < 0.5 && std::abs((ball.high.y - ball.low.y) - 64) < 0.5,
              std::format("the ball is the box at the end: {} × {}", ball.high.x - ball.low.x, ball.high.y - ball.low.y));
        check(!find(end, "box").drawn, "the hidden box is never drawn itself");
        // Any order, the same answers.
        const auto again = find(*opened->state(hanim::seconds(0.6)), "ball").high.y;
        check_eq(again, heights[60]);
    });

    s.test("drawn: the frames differ through the course and repeat exactly", [] {
        auto opened = studio::Studio::open(*studio::read(example()));
        if (!opened) return check(false, studio::to_text(opened.error()));
        const auto draw = [&](double time) {
            xrender::raster::Surface surface(480, 320);
            const auto drawn = opened->render(hanim::seconds(time), surface);
            check(drawn.has_value(), drawn ? "" : studio::to_text(drawn.error()));
            return std::vector<xrender::raster::Rgba8>(surface.pixels().begin(), surface.pixels().end());
        };
        const double end_time = opened->duration().count();
        const auto start = draw(0), middle = draw(1.0), end = draw(end_time), repeated = draw(1.0);
        check(start != middle && middle != end, "the picture changes");
        check(middle == repeated, "the same moment, the same pixels");
        // The ball's colour at its start, under its centre.
        const auto& pixel = start[70 * 480 + 120];
        check(pixel.r > 200 && pixel.g < 140 && pixel.b < 100, std::format("orange under the ball: {} {} {}", pixel.r, pixel.g, pixel.b));
    });

    s.test("every problem, where it is", [] {
        const auto broken = studio::read(R"({"version": 1, "objects": [
            {"id": "a", "shape": {"circle": 10}, "fill": "#ff0000"},
            {"id": "a", "shape": {"hexagon": 3}},
            {"id": "b", "shape": {"rect": [10, 10]}, "fill": "red", "colour": "#fff"}
        ], "program": {"do": "sequence", "items": [
            {"do": "move_to", "object": "c", "to": [1, 2]},
            {"do": "spin", "object": "a"},
            {"do": "move_by", "object": "a", "by": [1, 2], "ease": "wobbly"}
        ]}})");
        check(!broken, "refused");
        if (broken) return;
        const auto& problems = broken.error();
        check(says(problems, "/objects/1/id", "twice"), studio::to_text(problems));
        check(says(problems, "/objects/1/shape/hexagon", "not a shape"), "the unknown shape");
        check(says(problems, "/objects/1", "a fill, a stroke or both"), "nothing to draw");
        check(says(problems, "/objects/2/fill", "not a colour"), "the colour");
        check(says(problems, "/objects/2/colour", "not a field"), "the misspelt field");
        check(says(problems, "/program/items/0/object", "no object `c`"), "the missing object");
        check(says(problems, "/program/items/1/do", "not a step"), "the unknown step");
        check(says(problems, "/program/items/2/ease", "not an ease"), "the unknown ease");
        const auto syntax = studio::read("{\n  \"version\": 1,\n  \"objects\": [ }");
        check(!syntax && syntax.error().size() == 1 && syntax.error()[0].where.starts_with("line 3"), syntax ? "" : syntax.error()[0].where);
    });

    return s.run(argc, argv);
}
