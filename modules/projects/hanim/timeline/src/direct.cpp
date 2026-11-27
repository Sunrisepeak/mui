// The director (FE4): a show's timeline from the beats of a history, and its rules read as data.
module hanim.timeline;

import std;
import hanim.core;
import mlibs.json;
import mlibs.xdef;
import mlibs.names;

namespace hanim::timeline {

namespace {

struct Camera {
    double x = 0, y = 0, zoom = 1;
};

// Where a beat changed the frame: whether anything shows, the share of the frame, the bounds of its rectangles and
// their centre by weight.
struct Place {
    bool   visible = false;
    double share = 0;
    double left = 0, top = 0, right = 0, bottom = 0;
    double centre_x = 0, centre_y = 0;
};

Place place_of(const Beat& beat, double width, double height) {
    Place place;
    double area = 0, weights = 0, weighted_x = 0, weighted_y = 0;
    for (const Region& region : beat.regions) {
        const double left = std::clamp(region.x, 0.0, width), right = std::clamp(region.x + region.width, 0.0, width);
        const double top = std::clamp(region.y, 0.0, height), bottom = std::clamp(region.y + region.height, 0.0, height);
        if (right <= left || bottom <= top) continue;
        const double size = (right - left) * (bottom - top);
        if (!place.visible) place.left = left, place.top = top, place.right = right, place.bottom = bottom;
        place.left = std::min(place.left, left), place.top = std::min(place.top, top);
        place.right = std::max(place.right, right), place.bottom = std::max(place.bottom, bottom);
        place.visible = true;
        area += size;
        // A rectangle weighs what it is said to; one said to weigh nothing (or less) weighs its area.
        const double weight = region.weight > 0 ? region.weight : size;
        weights += weight;
        weighted_x += weight * (left + right) / 2;
        weighted_y += weight * (top + bottom) / 2;
    }
    if (!place.visible) return place;
    place.share = std::min(1.0, area / (width * height));
    place.centre_x = weighted_x / weights;
    place.centre_y = weighted_y / weights;
    return place;
}

// The camera that shows a visible change: the whole frame, or pushed in on a small change so that its longer side
// fills the share of the frame's shorter side the rules ask, never showing outside the frame.
Camera camera_for(const Place& place, double width, double height, const Direction& rules) {
    Camera camera{width / 2, height / 2, 1};
    if (place.share >= rules.near) return camera;
    const double side = std::max(place.right - place.left, place.bottom - place.top);
    if (side <= 0) return camera;
    camera.zoom = std::clamp(rules.fill * std::min(width, height) / side, 1.0, std::max(1.0, rules.deepest));
    if (camera.zoom <= 1) return camera;
    const double half_width = width / (2 * camera.zoom), half_height = height / (2 * camera.zoom);
    camera.x = std::clamp(place.centre_x, half_width, width - half_width);
    camera.y = std::clamp(place.centre_y, half_height, height - half_height);
    return camera;
}

// A next place close to where the camera is, at about its depth: the camera stays, not pulled back and forth.
bool stays(const Camera& from, const Camera& to, double shorter, const Direction& rules) {
    const double across = to.x - from.x, down = to.y - from.y, reach = rules.stay * shorter;
    const double ratio = to.zoom / from.zoom;
    return across * across + down * down <= reach * reach && ratio >= 0.8 && ratio <= 1.25;
}

// The transition into a beat: what most of its nodes had happen; a tie in the order the list is in.
std::string transition_of(const Beat& beat, const Direction& rules) {
    const std::array<std::pair<std::uint32_t, const std::string*>, 5> kinds{{
        {beat.appeared, &rules.appear},
        {beat.retexted, &rules.retext},
        {beat.moved, &rules.relocate},
        {beat.removed, &rules.vanish},
        {beat.restyled, &rules.otherwise},
    }};
    const auto most = std::ranges::max_element(kinds, std::ranges::less{}, [](const auto& kind) { return kind.first; });
    return most->first == 0 ? rules.otherwise : *most->second;
}

std::size_t characters(std::string_view text) {
    return static_cast<std::size_t>(std::ranges::count_if(text, [](char byte) { return (static_cast<unsigned char>(byte) & 0xC0) != 0x80; }));
}

// Whole frames of `rate` nearest to `seconds`, at least one.
std::int64_t frames_of(double seconds, Rate rate) {
    const double frames = seconds * static_cast<double>(rate.num) / static_cast<double>(rate.den);
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::round(frames)));
}

}  // namespace

bool Direction::operator==(const Direction& other) const {
    return rate == other.rate && shortest == other.shortest && longest == other.longest && full == other.full && still == other.still &&
           reading == other.reading && glance == other.glance && near == other.near && fill == other.fill && deepest == other.deepest &&
           stay == other.stay && move == other.move && appear == other.appear && vanish == other.vanish && retext == other.retext &&
           relocate == other.relocate && otherwise == other.otherwise && score == other.score && cue == other.cue;
}

Timeline direct(std::span<const Beat> beats, std::int32_t width, std::int32_t height, const Direction& rules) {
    Timeline show;
    show.name = "show";
    show.rate = rules.rate;
    show.width = width;
    show.height = height;
    Track states{.name = "states", .kind = Kind::video};
    Track camera_track{.name = "camera", .kind = Kind::animation};
    Track captions{.name = "captions", .kind = Kind::events};
    const double frame_width = std::max(1, width), frame_height = std::max(1, height);

    std::vector<Place> places;
    places.reserve(beats.size());
    for (const Beat& beat : beats) places.push_back(place_of(beat, frame_width, frame_height));

    // Each beat's length by what it changed: a visible change between the shortest and the longest by its share of
    // the frame; nothing visible, `still`.
    std::vector<std::int64_t> frames(beats.size());
    for (std::size_t at = 0; at < beats.size(); ++at) {
        const Place& place = places[at];
        const double seconds = place.visible ? rules.shortest + (rules.longest - rules.shortest) * std::min(1.0, place.share / rules.full) : rules.still;
        frames[at] = frames_of(seconds, rules.rate);
    }

    // Groups of words: a run of beats with nothing visible and the visible beat after it; a run at the end joins the
    // group before it. A group lasts long enough to read its words, lengthening its visible beat (never a still one).
    struct Group {
        std::size_t first = 0, last = 0, anchor = 0;  // its beats, and the one lengthened to read it
    };
    std::vector<Group> groups;
    std::size_t run = 0;  // the first beat of the run not yet in a group
    for (std::size_t at = 0; at < beats.size(); ++at)
        if (places[at].visible) groups.push_back({run, at, at}), run = at + 1;
    if (run < beats.size()) {
        if (groups.empty()) groups.push_back({run, beats.size() - 1, beats.size() - 1});
        else groups.back().last = beats.size() - 1;
    }
    std::vector<std::string> words(groups.size());
    for (std::size_t index = 0; index < groups.size(); ++index) {
        Group& group = groups[index];
        std::size_t count = 0;
        for (std::size_t at = group.first; at <= group.last; ++at) {
            if (beats[at].caption.empty()) continue;
            if (!words[index].empty()) words[index] += '\n';
            words[index] += beats[at].caption;
            count += characters(beats[at].caption);
        }
        if (count == 0) continue;
        const std::int64_t needed = frames_of(std::max(rules.glance, static_cast<double>(count) / rules.reading), rules.rate);
        std::int64_t has = 0;
        for (std::size_t at = group.first; at <= group.last; ++at) has += frames[at];
        if (has < needed) frames[group.anchor] += needed - has;
    }

    // The beats in order, each starting on a frame; a marker at each start.
    std::vector<std::int64_t> starts(beats.size() + 1, 0);
    for (std::size_t at = 0; at < beats.size(); ++at) starts[at + 1] = starts[at] + frames[at];
    for (std::size_t at = 0; at < beats.size(); ++at) {
        states.clips.push_back({.name = transition_of(beats[at], rules),
                                .source = beats[at].source,
                                .at = rules.rate.frame_time(starts[at]),
                                .length = rules.rate.frame_time(frames[at])});
        // How long its change is seen to move at its own pace, when measured (to the millisecond): the player's.
        if (beats[at].moving > 0) {
            Property moving{.name = "moving"};
            moving.keys.key(hanim::seconds{0}, std::round(beats[at].moving * 1000) / 1000);
            states.clips.back().properties.push_back(std::move(moving));
        }
        show.markers.push_back({rules.rate.frame_time(starts[at]), std::format("beat {}", at + 1)});
    }
    for (std::size_t index = 0; index < groups.size(); ++index) {
        if (words[index].empty()) continue;
        const std::int64_t from = starts[groups[index].first], to = starts[groups[index].last + 1];
        captions.clips.push_back({.name = words[index], .source = "caption", .at = rules.rate.frame_time(from), .length = rules.rate.frame_time(to - from)});
    }

    // The camera: where each visible beat is, kept where it is for a place close by and for a beat with nothing
    // visible; a move starts with its beat and takes `move` (within the beat), eased as navigation.
    if (!beats.empty()) {
        Property x{.name = "x"}, y{.name = "y"}, zoom{.name = "zoom"};
        const double shorter = std::min(frame_width, frame_height);
        Camera now{frame_width / 2, frame_height / 2, 1};
        bool placed = false;
        for (std::size_t at = 0; at < beats.size(); ++at) {
            if (!places[at].visible) continue;
            const Camera next = camera_for(places[at], frame_width, frame_height, rules);
            if (!placed) {
                now = next, placed = true;
                continue;
            }
            if (stays(now, next, shorter, rules)) continue;
            const hanim::seconds start = rules.rate.time_of(starts[at]);
            const hanim::seconds arrive = rules.rate.time_of(starts[at] + std::min(frames[at], frames_of(rules.move, rules.rate)));
            x.keys.key(start, now.x, hanim::ease::standard).key(arrive, next.x);
            y.keys.key(start, now.y, hanim::ease::standard).key(arrive, next.y);
            zoom.keys.key(start, now.zoom, hanim::ease::standard).key(arrive, next.zoom);
            now = next;
        }
        // The first place from the start (the whole frame when nothing ever shows).
        Camera first{frame_width / 2, frame_height / 2, 1};
        for (std::size_t at = 0; at < beats.size(); ++at)
            if (places[at].visible) {
                first = camera_for(places[at], frame_width, frame_height, rules);
                break;
            }
        x.keys.key(hanim::seconds{0}, first.x);
        y.keys.key(hanim::seconds{0}, first.y);
        zoom.keys.key(hanim::seconds{0}, first.zoom);
        camera_track.clips.push_back({.name = "camera",
                                      .source = "camera",
                                      .at = 0,
                                      .length = rules.rate.frame_time(starts.back()),
                                      .properties = {std::move(x), std::move(y), std::move(zoom)}});
    }

    show.tracks.push_back(std::move(states));
    show.tracks.push_back(std::move(camera_track));
    show.tracks.push_back(std::move(captions));
    // Its sound, when the rules give one: the score under the whole show, a cue on each visible beat's first frame.
    if (!rules.score.empty() && !beats.empty())
        show.tracks.push_back({.name = "music",
                               .kind = Kind::audio,
                               .clips = {{.name = "score", .source = rules.score, .at = 0, .length = rules.rate.frame_time(starts.back())}}});
    if (!rules.cue.empty()) {
        Track sfx{.name = "sfx", .kind = Kind::events};
        for (std::size_t at = 0; at < beats.size(); ++at)
            if (places[at].visible)
                sfx.clips.push_back({.name = std::format("beat {}", at + 1), .source = std::format("sound:{}", rules.cue), .at = rules.rate.frame_time(starts[at]),
                                     .length = rules.rate.frame_time(1)});
        if (!sfx.clips.empty()) show.tracks.push_back(std::move(sfx));
    }
    return show;
}

namespace {

using mlibs::json::Node;

constexpr std::array<std::string_view, 20> direction_keys{"rate",  "shortest", "longest", "full",   "still",  "reading",  "glance",   "near",      "fill",
                                                          "deepest", "stay",   "move",    "appear", "vanish", "retext",   "relocate", "otherwise", "score",
                                                          "cue",     "doc"};

}  // namespace

std::expected<Direction, std::vector<Problem>> read_direction(std::string_view text, std::string_view file) {
    auto doc = mlibs::json::Document::parse(text, std::string(file));
    if (!doc) {
        const auto& problem = doc.error();
        return std::unexpected(std::vector<Problem>{{problem.file, static_cast<int>(problem.line), static_cast<int>(problem.column), problem.message, problem.hint, problem.at}});
    }
    std::vector<Problem> problems;
    const auto said = [&](const Node& node, std::string message, std::string hint = {}) {
        const auto problem = doc->problem(node, std::move(message), std::move(hint));
        problems.push_back({problem.file, static_cast<int>(problem.line), static_cast<int>(problem.column), problem.message, problem.hint, problem.at});
    };
    constexpr std::string_view shape = "{ \"$format\": \"direction@1\", \"direction\": { shortest, longest, still, reading, … } }";
    if (const auto wrong = mlibs::xdef::format_problem(doc->root()["$format"].text(), "direction", 1)) {
        said(doc->root()["$format"] ? doc->root()["$format"] : doc->root(), *wrong, std::string(shape));
        return std::unexpected(std::move(problems));
    }
    for (const auto& [key, value] : doc->root().members())
        if (key != "direction" && key != "$format") said(value, std::format("a direction file has no key `{}`", key), std::string(shape));
    const Node rules_node = doc->root()["direction"];
    if (!rules_node.is_object()) {
        said(rules_node ? rules_node : doc->root(), "a direction file's `direction` is an object of rules", std::string(shape));
        return std::unexpected(std::move(problems));
    }
    for (const auto& [key, value] : rules_node.members())
        if (std::ranges::find(direction_keys, key) == direction_keys.end())
            said(value, std::format("a direction has no rule `{}`", key), mlibs::names::nearest(key, direction_keys));

    Direction rules;
    if (const Node rate = rules_node["rate"]) {
        if (const auto frames = rate.integer(); frames && *frames > 0) rules.rate = {*frames, 1};
        else said(rate, "a rate is whole frames a second", "rate: 60");
        if (!rules.rate.exact()) said(rate, std::format("a frame at {} a second is not a whole number of flicks", rules.rate.num), "24, 25, 30, 48, 50, 60, 120");
    }
    // A number at least `low` (and below `high` where there is one).
    const auto number = [&](std::string_view key, double& into, double low, std::optional<double> high, std::string_view what) {
        const Node node = rules_node[key];
        if (!node) return;
        const auto value = node.number();
        if (!value) return said(node, std::format("`{}` is a number: {}", key, what));
        if (*value < low || (high && *value > *high))
            return said(node, std::format("`{}` is {}: {}", key, *value, what), high ? std::format("from {} to {}", low, *high) : std::format("at least {}", low));
        into = *value;
    };
    number("shortest", rules.shortest, 1.0 / 120, std::nullopt, "seconds a beat with a visible change lasts at the least");
    number("longest", rules.longest, 1.0 / 120, std::nullopt, "seconds it lasts at the most");
    number("full", rules.full, 1e-6, 1.0, "the share of the frame changed that lasts the longest");
    number("still", rules.still, 1.0 / 120, std::nullopt, "seconds a beat with no visible change lasts");
    number("reading", rules.reading, 1e-6, std::nullopt, "characters read a second");
    number("glance", rules.glance, 0.0, std::nullopt, "seconds to read any caption at the least");
    number("near", rules.near, 0.0, 1.0, "the share of the frame below which a change is pushed in on");
    number("fill", rules.fill, 1e-6, 1.0, "the share of the frame's shorter side a pushed-in change fills");
    number("deepest", rules.deepest, 1.0, std::nullopt, "how far the camera pushes in at the most");
    number("stay", rules.stay, 0.0, 1.0, "the share of the shorter side within which the camera stays");
    number("move", rules.move, 0.0, std::nullopt, "seconds the camera takes to a new place");
    if (rules.longest < rules.shortest)
        said(rules_node["longest"] ? rules_node["longest"] : rules_node, std::format("`longest` ({}) is shorter than `shortest` ({})", rules.longest, rules.shortest));
    for (const auto& [key, into] : {std::pair<std::string_view, std::string*>{"appear", &rules.appear},
                                    {"vanish", &rules.vanish},
                                    {"retext", &rules.retext},
                                    {"relocate", &rules.relocate},
                                    {"otherwise", &rules.otherwise}}) {
        const Node node = rules_node[key];
        if (!node) continue;
        const auto name = node.text();
        if (!name || name->empty()) said(node, std::format("`{}` is a recipe's registered name", key), "settle_in, fade_out, morph …");
        else *into = *name;
    }
    for (const auto& [key, into, what] : {std::tuple<std::string_view, std::string*, std::string_view>{"score", &rules.score, "a music file (\"main.music.xdef.ts\")"},
                                          {"cue", &rules.cue, "a sound's name in the bank (\"step\")"}}) {
        const Node node = rules_node[key];
        if (!node) continue;
        const auto name = node.text();
        if (!name) said(node, std::format("`{}` is {}", key, what), "");
        else *into = *name;
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return rules;
}

}  // namespace hanim::timeline
