// eval animate (convergence plan §10.3; moved from tools/eval/animate.py): a real LLM does animation work without a
// GUI. Given only hanim's skill, the document reference and the `hanim` command, it writes animation.xdef.json in an
// empty directory of its own for each brief in tools/eval/animation.briefs.xdef.json and verifies it with the
// command. The grader checks each document through `hanim document.check` and `hanim document.sample` (the numbers at 601 moments,
// never the agent's word): where things start and end, bounces counted, overshoot, order and timing. Every trial,
// and each run's success rate, go to animation.eval.xdef.jsonl; an agent that cannot run is recorded as unavailable.
module omui.devtools.eval;

import std;
import mcpplibs.os;
import nlohmann.json;
import omui.devtools.run;

namespace omui::devtools::eval {

namespace {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;
using nlohmann::json;
using nlohmann::ordered_json;

std::pair<int, json> hanim_says(const fs::path& hanim, std::vector<std::string> args) {
    const auto done = run::program(fs::current_path(), os::fs::utf8(hanim), std::move(args), {.timeout = std::chrono::seconds(120)});
    if (!done) return {127, json{{"ok", false}, {"raw", done.error()}}};
    // Its answer's records, a line each: the first data record is what it says.
    for (auto line : std::views::split(std::string_view(done->out), '\n')) {
        auto record = json::parse(std::string_view(line.begin(), line.end()), nullptr, false);
        if (!record.is_discarded() && record.value("kind", "") == "data") return {done->status, std::move(record["data"])};
    }
    return {done->status, json{{"ok", false}, {"raw", done->out}}};
}

// The document sampled: its plan, and each object's state at many moments.
struct Course {
    bool ok = false;
    json problems = json::array(), plan, document;
    double duration = 10;
    std::vector<double> times;
    std::vector<json> samples;

    Course(const fs::path& hanim, const fs::path& file, int steps = 600) {
        const auto [code, checked] = hanim_says(hanim, {"document.check", "--file", os::fs::utf8(file)});
        ok = code == 0 && checked.value("ok", false);
        problems = checked.value("problems", json::array());
        if (!ok) return;
        plan = checked["plan"];
        if (plan["duration"].is_number()) duration = plan["duration"].get<double>();
        std::vector<std::string> args{"document.sample", "--file", os::fs::utf8(file)};
        for (int index = 0; index <= steps; ++index) {
            times.push_back(duration * index / steps);
            args.insert(args.end(), {"--at", std::format("{:.6f}", times.back())});
        }
        const auto [sampled_code, sampled] = hanim_says(hanim, std::move(args));
        ok = sampled_code == 0 && sampled.value("ok", false);
        if (ok)
            for (const auto& sample : sampled.value("samples", json::array())) samples.push_back(sample);
        document = json::parse(os::fs::read(file).value_or(""), nullptr, false);
    }

    json object(std::size_t index, std::string_view name) const {
        for (const auto& candidate : samples[index].value("objects", json::array()))
            if (candidate.value("id", "") == name) return candidate;
        return json::object();
    }
    std::vector<std::pair<double, json>> series(std::string_view name, std::string_view key) const {
        std::vector<std::pair<double, json>> out;
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const json found = object(index, name);
            out.emplace_back(samples[index].value("time", 0.0), found.contains(key) ? found[std::string(key)] : json());
        }
        return out;
    }
    json at_start(std::string_view name) const { return samples.empty() ? json::object() : object(0, name); }
    json at_end(std::string_view name) const { return samples.empty() ? json::object() : object(samples.size() - 1, name); }
    json at_time(double moment, std::string_view name) const {
        std::size_t best = 0;
        for (std::size_t index = 0; index < times.size(); ++index)
            if (std::abs(times[index] - moment) < std::abs(times[best] - moment)) best = index;
        return samples.empty() ? json::object() : object(best, name);
    }
};

std::optional<double> number(const json& value) { return value.is_number() ? std::optional(value.get<double>()) : std::nullopt; }
std::optional<double> item(const json& value, std::size_t index) {
    return value.is_array() && index < value.size() ? number(value[index]) : std::nullopt;
}
bool near(std::optional<double> a, std::optional<double> b, double tolerance) { return a && b && std::abs(*a - *b) <= tolerance; }

std::array<int, 4> rgba(const json& value) {
    std::string text = value.is_string() ? value.get<std::string>() : "#00000000";
    if (text.starts_with('#')) text.erase(0, 1);
    std::array<int, 4> out{};
    for (std::size_t index = 0; index < 4 && index * 2 + 2 <= text.size(); ++index)
        std::from_chars(text.data() + index * 2, text.data() + index * 2 + 2, out[index], 16);
    return out;
}

// How many times it leaves the ground again after touching it (its bottom edge's samples).
int bounces(const std::vector<std::optional<double>>& bottoms, double ground = 280) {
    int count = 0;
    bool down = false;
    for (const auto& value : bottoms) {
        if (!value) continue;
        if (*value >= ground - 0.5) down = true;
        else if (down && *value < ground - 1) {
            ++count;
            down = false;
        }
    }
    return count;
}

std::string fixed(double value, int digits = 3) { return std::format("{:.{}f}", value, digits); }

struct Check {
    std::string name;
    bool passed = false;
    std::string detail;
};

std::vector<Check> grade(const Course& course, std::string_view kind) {
    std::vector<Check> results;
    const auto check = [&](std::string name, bool passed, std::string detail = {}) { results.push_back({std::move(name), passed, std::move(detail)}); };
    check("the document checks", course.ok, course.problems.dump().substr(0, 300));
    if (!course.ok) return results;
    const auto values = [](const std::vector<std::pair<double, json>>& series, std::size_t index) {
        std::vector<std::optional<double>> out;
        for (const auto& [time, value] : series) out.push_back(item(value, index));
        return out;
    };
    const auto present = [](const std::vector<std::optional<double>>& series) {
        std::vector<double> out;
        for (const auto& value : series)
            if (value) out.push_back(*value);
        return out;
    };
    if (kind == "bounce") {
        const json start = course.at_start("ball"), end = course.at_end("ball");
        check("the ball starts at (100, 60)", near(item(start.value("position", json()), 0), 100, 1) && near(item(start.value("position", json()), 1), 60, 1),
              start.value("position", json()).dump());
        const auto bottoms = values(course.series("ball", "bounds"), 3);
        check("it bounces at least twice", bounces(bottoms) >= 2, std::format("{} bounces", bounces(bottoms)));
        check("it rests on the ground (bottom at 280) at the end", near(item(end.value("bounds", json()), 3), 280, 2), end.value("bounds", json()).dump());
        check("it ends at x = 380", near(item(end.value("position", json()), 0), 380, 2), end.value("position", json()).dump());
        const auto xs = present(values(course.series("ball", "position"), 0));
        std::optional<std::size_t> moves;
        for (std::size_t index = 0; index < xs.size() && !moves; ++index)
            if (std::abs(xs[index] - xs.front()) > 0.5) moves = index;
        bool landed = moves.has_value();
        for (std::size_t index = moves.value_or(bottoms.size()); landed && index < bottoms.size(); ++index) landed = bottoms[index] && *bottoms[index] >= 279.5;
        check("it rolls only after it has landed for good", landed, moves ? std::format("moves at sample {}", *moves) : "moves at sample None");
    } else if (kind == "morph") {
        const json start = course.at_start("square"), end = course.at_end("square");
        const json begin_bounds = start.value("bounds", json::array({0, 0, 0, 0})), end_bounds = end.value("bounds", json::array({0, 0, 0, 0}));
        const auto width = [](const json& bounds) { return item(bounds, 2).value_or(0) - item(bounds, 0).value_or(0); };
        const auto height = [](const json& bounds) { return item(bounds, 3).value_or(0) - item(bounds, 1).value_or(0); };
        check("a blue 80 × 80 square at the centre to begin", near(width(begin_bounds), 80, 1) && near(height(begin_bounds), 80, 1) && rgba(start.value("fill", json()))[2] > 150,
              start.dump());
        const double middle_x = (item(end_bounds, 0).value_or(0) + item(end_bounds, 2).value_or(0)) / 2, middle_y = (item(end_bounds, 1).value_or(0) + item(end_bounds, 3).value_or(0)) / 2;
        check("it ends as an 80-wide shape at the same place", near(width(end_bounds), 80, 2) && near(middle_x, 240, 2) && near(middle_y, 160, 2), end_bounds.dump());
        const auto [red, green, blue, alpha] = rgba(end.value("fill", json()));
        check("it ends yellow", red > 200 && green > 170 && blue < 120, end.value("fill", json()).dump());
        const std::string steps = course.document.value("program", json()).dump();
        bool circle = false;
        for (const auto& candidate : course.document.value("objects", json::array()))
            circle = circle || (candidate.contains("shape") && candidate["shape"].is_object() && candidate["shape"].contains("circle"));
        check("it morphs (a morph step into a circle)", steps.find("\"morph\"") != std::string::npos && circle);
        const auto scales = present(values(course.series("square", "scale"), 0));
        const double most = scales.empty() ? 0 : *std::ranges::max_element(scales);
        check("it pulses after the morph", !scales.empty() && most > 1.02 && near(scales.back(), 1, 0.01), std::format("max scale {:.3f}", most));
    } else if (kind == "stagger") {
        std::vector<std::optional<double>> rises;
        for (int dot = 1; dot <= 5; ++dot) {
            const std::string name = std::format("dot{}", dot);
            const auto ys = course.series(name, "position");
            if (std::ranges::any_of(ys, [](const auto& entry) { return !entry.second.is_array() || entry.second.empty(); })) {
                check(std::format("{} is drawn throughout", name), false);
                return results;
            }
            std::optional<double> first;
            double lowest = std::numeric_limits<double>::infinity();
            for (const auto& [time, position] : ys) {
                const double y = item(position, 1).value_or(0);
                if (!first && y < 155) first = time;
                lowest = std::min(lowest, y);
            }
            rises.push_back(first);
            check(std::format("{} starts at ({}, 160)", name, 80 * dot), near(item(ys.front().second, 0), 80 * dot, 1) && near(item(ys.front().second, 1), 160, 1),
                  ys.front().second.dump());
            check(std::format("{} rises to 100", name), lowest < 101, fixed(lowest));
            check(std::format("{} returns to 160", name), near(item(ys.back().second, 1), 160, 1), ys.back().second.dump());
        }
        bool ordered = std::ranges::all_of(rises, [](const auto& rise) { return rise.has_value(); });
        bool spaced = ordered;
        for (std::size_t index = 1; ordered && index < rises.size(); ++index) {
            ordered = *rises[index] - *rises[index - 1] > 0.05;
            spaced = spaced && near(*rises[index] - *rises[index - 1], 0.1, 0.03);
        }
        std::string said;
        for (const auto& rise : rises) said += (said.empty() ? "" : ", ") + (rise ? fixed(*rise) : std::string("None"));
        check("they rise one after another, about 0.1 s apart", ordered && spaced, "[" + said + "]");
    } else if (kind == "spring") {
        const auto scales = present(values(course.series("card", "scale"), 0));
        std::vector<double> rotations;
        for (const auto& [time, rotation] : course.series("card", "rotation_degrees"))
            if (const auto value = number(rotation)) rotations.push_back(*value);
        if (scales.empty() || rotations.empty()) {
            check("the card is drawn throughout", false);
            return results;
        }
        const json start = course.at_start("card");
        const json bounds = start.value("bounds", json::array({0, 0, 0, 0}));
        const auto colour = rgba(start.value("fill", json()));
        check("a white card, 160 × 100 at scale 0.5 to begin",
              near(scales.front(), 0.5, 0.01) && near(item(bounds, 2).value_or(0) - item(bounds, 0).value_or(0), 80, 1.5) && colour[0] == 255 && colour[1] == 255 && colour[2] == 255,
              start.dump());
        const auto top = std::ranges::max_element(scales);
        check("it overshoots 1 on a spring", *top > 1.02, std::format("max {:.3f}", *top));
        check("it settles at 1", near(scales.back(), 1, 0.005), fixed(scales.back()));
        check("it turns 10 degrees", near(*std::ranges::max_element(rotations), 10, 0.5), fixed(*std::ranges::max_element(rotations)));
        check("and back", near(rotations.back(), 0, 0.5), fixed(rotations.back()));
        const auto peak = static_cast<std::size_t>(top - scales.begin());
        std::size_t turned = rotations.size();
        for (std::size_t index = 0; index < rotations.size() && turned == rotations.size(); ++index)
            if (std::abs(rotations[index]) > 0.5) turned = index;
        check("the turn comes after the spring", turned > peak, std::format("turn at sample {}, peak at {}", turned, peak));
    } else if (kind == "fade") {
        std::vector<int> alphas;
        for (const auto& [time, fill] : course.series("bar", "fill")) alphas.push_back(rgba(fill)[3]);
        const auto ys = values(course.series("bar", "position"), 1);
        check("it starts invisible", alphas.front() == 0, std::to_string(alphas.front()));
        check("it ends opaque", alphas.back() == 255, std::to_string(alphas.back()));
        check("it rises from y = 200 to y = 160", near(ys.front(), 200, 1) && near(ys.back(), 160, 1),
              std::format("({}, {})", ys.front() ? fixed(*ys.front()) : "None", ys.back() ? fixed(*ys.back()) : "None"));
        std::size_t half_faded = 0;
        bool together = false;
        for (std::size_t index = 0; index < alphas.size(); ++index)
            if (alphas[index] > 40 && alphas[index] < 215) {
                ++half_faded;
                together = together || (ys[index] && *ys[index] > 170 && *ys[index] < 195);
            }
        check("it fades and rises at the same time", together, std::format("{} half-faded samples", half_faded));
        const auto fading = std::ranges::count_if(alphas, [](int alpha) { return alpha > 0 && alpha < 255; });
        check("the fade takes about 0.5 s", near(static_cast<double>(fading) * course.duration / static_cast<double>(alphas.size() - 1), 0.5, 0.12));
    } else if (kind == "fan") {
        constexpr std::array<double, 6> places{65, 135, 205, 275, 345, 415};
        std::vector<std::optional<double>> appear;
        for (int card = 1; card <= 6; ++card) {
            const std::string name = std::format("card{}", card);
            const json start = course.at_start(name), end = course.at_end(name);
            check(std::format("{} starts invisible at the centre, scale 0.6", name),
                  rgba(start.value("fill", json()))[3] == 0 && near(item(start.value("position", json::array({0, 0})), 0), 240, 1) &&
                      near(item(start.value("scale", json::array({0})), 0), 0.6, 0.01),
                  start.dump());
            check(std::format("{} ends opaque on its place at scale 1", name),
                  rgba(end.value("fill", json()))[3] == 255 && near(item(end.value("position", json::array({0, 0})), 0), places[card - 1], 1.5) &&
                      near(item(end.value("position", json::array({0, 0})), 1), 160, 1.5) && near(item(end.value("scale", json::array({0})), 0), 1, 0.01),
                  end.dump());
            std::optional<double> shown;
            for (const auto& [time, fill] : course.series(name, "fill"))
                if (!shown && rgba(fill)[3] > 0) shown = time;
            appear.push_back(shown);
            std::vector<double> rotations;
            for (const auto& [time, rotation] : course.series(name, "rotation_degrees"))
                if (const auto value = number(rotation)) rotations.push_back(*value);
            const double last = rotations.empty() ? 0 : rotations.back();
            check(std::format("{} turns 8 degrees at the end", name), near(last, 8, 0.3) || near(last, -8, 0.3), fixed(last));
        }
        bool spaced = std::ranges::all_of(appear, [](const auto& when) { return when.has_value(); });
        for (std::size_t index = 1; spaced && index < appear.size(); ++index) spaced = near(*appear[index] - *appear[index - 1], 0.12, 0.04);
        std::string said;
        for (const auto& when : appear) said += (said.empty() ? "" : ", ") + (when ? fixed(*when) : std::string("None"));
        check("they appear one after another, about 0.12 s apart", spaced, "[" + said + "]");
        double last_arrival = 0, first_turn = 99;
        for (int card = 1; card <= 6; ++card) {
            const std::string name = std::format("card{}", card);
            double arrived = 99, turned = 99;
            for (const auto& [time, position] : course.series(name, "position"))
                if (arrived == 99 && position.is_array() && near(item(position, 0), places[card - 1], 0.5)) arrived = time;
            for (const auto& [time, rotation] : course.series(name, "rotation_degrees"))
                if (turned == 99 && number(rotation) && std::abs(*number(rotation)) > 0.2) turned = time;
            last_arrival = std::max(last_arrival, arrived);
            first_turn = std::min(first_turn, turned);
        }
        check("the turn begins after every card has arrived", first_turn >= last_arrival - 0.02, std::format("({}, {})", fixed(last_arrival), fixed(first_turn)));
    } else if (kind == "marker") {
        std::map<std::string, double> markers;
        for (const auto& marker : course.plan.value("markers", json::array())) markers[marker.value("name", "")] = marker.value("time", 0.0);
        const auto marker = [&](const std::string& name) { return markers.contains(name) ? std::optional(markers[name]) : std::nullopt; };
        check("the program is 2 s long", near(course.duration, 2, 0.01), fixed(course.duration));
        check("a marker `halfway` at 1 s", near(marker("halfway"), 1.0, 0.01), ordered_json(markers).dump());
        check("a marker `end` at 2 s", near(marker("end"), 2.0, 0.01), ordered_json(markers).dump());
        const json halfway = course.at_time(marker("halfway").value_or(0), "ball");
        check("the ball is at x = 240 at `halfway`", near(item(halfway.value("position", json()), 0), 240, 2), halfway.value("position", json()).dump());
        const auto xs = present(values(course.series("ball", "position"), 0));
        if (xs.size() < 2) {
            check("the ball is drawn throughout", false);
            return results;
        }
        std::vector<double> speeds;
        for (std::size_t index = 1; index < xs.size(); ++index) speeds.push_back(xs[index] - xs[index - 1]);
        const double fastest = *std::ranges::max_element(speeds), slowest = *std::ranges::min_element(speeds);
        check("it rolls at constant speed", fastest - slowest < 0.05 * fastest, std::format("({}, {})", fixed(slowest), fixed(fastest)));
        check("from (60, 160) to (420, 160)", near(xs.front(), 60, 1) && near(xs.back(), 420, 1), std::format("({}, {})", fixed(xs.front()), fixed(xs.back())));
    } else if (kind == "inside") {
        std::vector<json> positions, bounds;
        for (const auto& [time, position] : course.series("star", "position"))
            if (position.is_array() && !position.empty()) positions.push_back(position);
        for (const auto& [time, box] : course.series("star", "bounds"))
            if (box.is_array() && !box.empty()) bounds.push_back(box);
        if (positions.empty() || bounds.empty()) {
            check("the star is drawn throughout", false);
            return results;
        }
        double farthest = -std::numeric_limits<double>::infinity(), worst = -std::numeric_limits<double>::infinity();
        for (const auto& position : positions) farthest = std::max(farthest, item(position, 0).value_or(0));
        for (const auto& box : bounds)
            worst = std::max({worst, -item(box, 0).value_or(0), -item(box, 1).value_or(0), item(box, 2).value_or(0) - 480, item(box, 3).value_or(0) - 320});
        check("it overshoots its target", farthest > 401, fixed(farthest));
        check("every part stays inside the stage at every moment", worst <= 0.5, std::format("{:.1f} px outside at worst", worst));
        const json end = course.at_end("star");
        check("it ends at (400, 160) at scale 1.5",
              near(item(end.value("position", json()), 0), 400, 1) && near(item(end.value("position", json()), 1), 160, 1) && near(item(end.value("scale", json()), 0), 1.5, 0.01),
              end.dump());
        check("it starts at (80, 160)", near(item(positions.front(), 0), 80, 1), positions.front().dump());
    } else {
        check("a known brief", false, std::string(kind));
    }
    return results;
}

std::string prompt(const fs::path& root, const json& brief, const fs::path& hanim) {
    return std::format(
        "You are making an animation without a GUI, with hanim's animation tool.\n\nWrite `animation.xdef.json` in the current directory for this "
        "brief, then verify it with the tool until it does what the brief asks. Use the tool's own answers (document.check, document.sample, "
        "document.render) as evidence; "
        "look at a rendered strip if you can. Do not ask questions; finish the work.\n\nThe tool is `{}` (document.check --file FILE, document.sample --file FILE --at T "
        "…, document.render --file FILE --out PREFIX --frames N; `subjects` and `describe` say the rest).\n\nTHE BRIEF ({}):\n{}\n\nHANIM'S SKILL:\n{}\n\nTHE DOCUMENT REFERENCE:\n{}\n\nAN EXAMPLE "
        "DOCUMENT:\n{}\n\nWhen done, reply with one line: done, or what you could not do and why.",
        os::fs::utf8(hanim), brief.value("id", ""), brief.value("text", ""), os::fs::read(root / "modules/projects/hanim/skill.omui.md").value_or(""),
        os::fs::read(root / "modules/projects/hanim/studio/README.md").value_or(""), os::fs::read(root / "modules/projects/hanim/studio/examples/bounce.animation.xdef.json").value_or(""));
}

ordered_json trial(const fs::path& root, const json& brief, int number_of_trial, const fs::path& hanim, const Animate& options) {
    const std::string id = brief.value("id", "");
    const fs::path work = options.keep / std::format("animate-{}-{}-{}", id, number_of_trial, std::random_device{}());
    std::error_code error;
    fs::create_directories(work, error);
    const auto started = std::chrono::steady_clock::now();
    // claude headless in the trial's directory (its edits allowed there, nothing from the web), or codex in a writable sandbox.
    std::vector<std::string> args;
    std::string program = options.agent;
    if (options.agent == "codex") {
        args = {"exec", "--sandbox", "workspace-write", "--skip-git-repo-check", "-C", os::fs::utf8(work), "-c", "approval_policy=\"never\"", "-c",
                std::format("model_reasoning_effort=\"{}\"", options.effort), "-o", os::fs::utf8(work / "answer.md")};
        if (!options.model.empty()) args.insert(args.end(), {"-m", options.model});
        args.push_back(prompt(root, brief, hanim));
    } else {
        args = {"-p", prompt(root, brief, hanim), "--output-format", "text", "--permission-mode", "bypassPermissions", "--disallowedTools", "WebFetch", "WebSearch",
                "Agent", "--model", options.model.empty() ? "sonnet" : options.model};
    }
    const auto done = run::program(work, program, std::move(args), {.cwd = work, .merged = true, .timeout = options.timeout});
    const std::string log = !done ? done.error() : done->timed_out ? std::string("timeout") : done->out;
    if (options.agent == "claude" && done && !done->timed_out) std::ofstream(work / "answer.md", std::ios::binary) << done->out;
    std::ofstream(work / std::format("{}.log", options.agent), std::ios::binary) << log;
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
    const fs::path document = work / "animation.xdef.json";
    std::string answer = fs::exists(work / "answer.md") ? os::fs::read(work / "answer.md").value_or("") : std::string{};
    while (!answer.empty() && std::isspace(static_cast<unsigned char>(answer.back()))) answer.pop_back();
    answer = answer.substr(0, 500);
    ordered_json record{{"brief", id}, {"trial", number_of_trial}};
    if (!fs::exists(document))
        for (const char* marker : {"usage limit", "rate limit", "Invalid API key", "Please run /login", "not logged in", "credit balance is too low"})
            if (log.find(marker) != std::string::npos) {
                record.update(ordered_json{{"passed", nullptr}, {"unavailable", marker}, {"seconds", seconds}, {"checks", ordered_json::array()}, {"answer", answer},
                                           {"directory", os::fs::utf8(work)}});
                return record;
            }
    const auto checks = fs::exists(document) ? grade(Course(hanim, document), id) : std::vector<Check>{{"a document was written", false, ""}};
    ordered_json listed = ordered_json::array();
    bool passed = true;
    for (const auto& check : checks) {
        passed = passed && check.passed;
        listed.push_back({{"check", check.name}, {"passed", check.passed}, {"detail", check.detail.substr(0, 300)}});
    }
    record.update(ordered_json{{"passed", passed}, {"seconds", seconds}, {"checks", listed}, {"answer", answer}, {"directory", os::fs::utf8(work)}});
    return record;
}

}  // namespace

int animate(const fs::path& root, const Animate& options) {
    const auto briefs_document = json::parse(os::fs::read(root / "tools/eval/animation.briefs.xdef.json").value_or(""), nullptr, false);
    if (briefs_document.is_discarded()) {
        std::cerr << "omui-devtools eval animate: tools/eval/animation.briefs.xdef.json is not JSON\n";
        return 2;
    }
    const auto built = run::build(root, {"-p", "hanim.studio"});
    fs::path hanim;
    if (built)
        for (const auto& artifact : *built)
            if (artifact.name == "hanim") hanim = artifact.path;
    if (hanim.empty()) {
        std::cerr << "omui-devtools eval animate: hanim did not build" << (built ? std::string() : ": " + built.error()) << '\n';
        return 1;
    }
    std::vector<json> briefs;
    for (const auto& brief : briefs_document.value("briefs", json::array()))
        if (options.only.empty() || std::ranges::contains(options.only, brief.value("id", ""))) briefs.push_back(brief);
    if (!options.grade.empty()) {
        const std::string name = os::fs::utf8(options.grade.filename());
        for (const auto& brief : briefs_document.value("briefs", json::array()))
            if (name.starts_with(std::format("animate-{}-", brief.value("id", "")))) {
                for (const auto& check : grade(Course(hanim, options.grade / "animation.xdef.json"), brief.value("id", "")))
                    std::cout << std::format("{} {}  {}\n", check.passed ? "ok  " : "FAIL", check.name, check.detail);
                return 0;
            }
        std::cerr << std::format("omui-devtools eval animate: {} names no brief (animate-<brief>-…)\n", name);
        return 2;
    }
    std::error_code error;
    fs::create_directories(options.keep, error);
    // The trials, `parallel` at a time.
    std::vector<std::pair<json, int>> jobs;
    for (const auto& brief : briefs)
        for (int index = 1; index <= options.trials; ++index) jobs.emplace_back(brief, index);
    std::vector<ordered_json> results(jobs.size());
    std::atomic<std::size_t> next{0};
    {
        std::vector<std::jthread> workers;
        for (int worker = 0; worker < std::max(1, options.parallel); ++worker)
            workers.emplace_back([&] {
                for (std::size_t index; (index = next++) < jobs.size();) results[index] = trial(root, jobs[index].first, jobs[index].second, hanim, options);
            });
    }
    const std::string commit_id = run::commit(root);
    const auto version = run::output(root, options.agent, {"--version"});
    const std::string model = options.model.empty() ? (options.agent == "claude" ? "sonnet" : "default") : options.model;
    const std::string date = run::today();
    std::size_t ran = 0, passed = 0;
    std::set<std::string> brief_ids;
    for (const auto& result : results) {
        const bool available = !result["passed"].is_null();
        brief_ids.insert(result.value("brief", ""));
        const std::string state = !available ? "unavailable" : result["passed"].get<bool>() ? "pass" : "FAIL";
        std::cout << std::format("{}  {} #{}  {} s  {}{}\n", state, result.value("brief", ""), result.value("trial", 0), result.value("seconds", 0), result.value("directory", ""),
                                 available ? std::string() : std::format("  ({})", result.value("unavailable", "")));
        for (const auto& check : result["checks"])
            if (!check.value("passed", false)) std::cout << std::format("        ✗ {}: {}\n", check.value("check", ""), check.value("detail", ""));
        if (available) {
            ++ran;
            passed += result["passed"].get<bool>();
        }
    }
    std::cout << std::format("animate: {} of {} passed ({:.0f}%){}\n", passed, ran, 100.0 * passed / std::max<std::size_t>(1, ran),
                             ran < results.size() ? std::format("; {} unavailable (not counted)", results.size() - ran) : std::string());
    if (!ran) {
        std::cout << "animate: the agent could not run; nothing recorded\n";
        return 1;
    }
    if (!options.no_ledger) {
        std::ofstream ledger(root / ".agents/docs/evaluation/animation.eval.xdef.jsonl", std::ios::binary | std::ios::app);
        for (const auto& result : results) {
            ordered_json line{{"date", date}, {"commit", commit_id}, {"agent", options.agent}, {"agent_version", version.value_or("")}, {"model", model}};
            for (const auto& entry : result.items())
                if (entry.key() != "directory") line[entry.key()] = entry.value();
            ledger << ledger_line(line) << '\n';
        }
        ledger << ledger_line(ordered_json{{"date", date}, {"commit", commit_id}, {"agent", options.agent}, {"model", model}, {"summary", true}, {"trials", ran},
                                           {"passed", passed}, {"unavailable", results.size() - ran}, {"briefs", brief_ids}})
               << '\n';
    }
    return 0;
}

}  // namespace omui::devtools::eval
