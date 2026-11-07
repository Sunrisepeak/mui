// Reading a document: the JSON's syntax (where it breaks, by line and
// column), then every field checked against what it may be — every problem,
// each at its JSON pointer, before anything is built.
module hanim.studio;

import std;
import nlohmann.json;

namespace hanim::studio {

namespace {

using Json = nlohmann::json;

// A SAX reader that only notes where the syntax breaks.
struct SyntaxProbe {
    std::optional<std::size_t> at;
    bool null() { return true; }
    bool boolean(bool) { return true; }
    bool number_integer(Json::number_integer_t) { return true; }
    bool number_unsigned(Json::number_unsigned_t) { return true; }
    bool number_float(Json::number_float_t, const std::string&) { return true; }
    bool string(std::string&) { return true; }
    bool binary(Json::binary_t&) { return true; }
    bool start_object(std::size_t) { return true; }
    bool key(std::string&) { return true; }
    bool end_object() { return true; }
    bool start_array(std::size_t) { return true; }
    bool end_array() { return true; }
    template <class Exception>
    bool parse_error(std::size_t position, const std::string&, const Exception&) {
        at = position;
        return false;
    }
};

std::string line_and_column(std::string_view text, std::size_t offset) {
    std::size_t line = 1, column = 1;
    for (std::size_t index = 0; index < std::min(offset, text.size()); ++index) {
        if (text[index] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return std::format("line {}, column {}", line, column);
}

constexpr std::array step_names{
    std::pair{StepKind::sequence, std::string_view("sequence")}, std::pair{StepKind::parallel, std::string_view("parallel")},
    std::pair{StepKind::stagger, std::string_view("stagger")},   std::pair{StepKind::wait, std::string_view("wait")},
    std::pair{StepKind::loop, std::string_view("loop")},         std::pair{StepKind::marker, std::string_view("marker")},
    std::pair{StepKind::move_to, std::string_view("move_to")},   std::pair{StepKind::move_by, std::string_view("move_by")},
    std::pair{StepKind::rotate_to, std::string_view("rotate_to")}, std::pair{StepKind::rotate_by, std::string_view("rotate_by")},
    std::pair{StepKind::scale_to, std::string_view("scale_to")}, std::pair{StepKind::scale_by, std::string_view("scale_by")},
    std::pair{StepKind::fill_to, std::string_view("fill_to")},   std::pair{StepKind::fade_to, std::string_view("fade_to")},
    std::pair{StepKind::morph, std::string_view("morph")},       std::pair{StepKind::drop, std::string_view("drop")},
    std::pair{StepKind::pulse, std::string_view("pulse")},       std::pair{StepKind::shake, std::string_view("shake")},
    std::pair{StepKind::wobble, std::string_view("wobble")},
    std::pair{StepKind::fade_in, std::string_view("fade_in")},   std::pair{StepKind::rise_in, std::string_view("rise_in")},
    std::pair{StepKind::sink_in, std::string_view("sink_in")},   std::pair{StepKind::slide_in, std::string_view("slide_in")},
    std::pair{StepKind::pop_in, std::string_view("pop_in")},     std::pair{StepKind::zoom_in, std::string_view("zoom_in")},
    std::pair{StepKind::spin_in, std::string_view("spin_in")},   std::pair{StepKind::unfold_in, std::string_view("unfold_in")},
    std::pair{StepKind::write_in, std::string_view("write_in")}, std::pair{StepKind::drop_in, std::string_view("drop_in")},
    std::pair{StepKind::fade_out, std::string_view("fade_out")}, std::pair{StepKind::rise_out, std::string_view("rise_out")},
    std::pair{StepKind::sink_out, std::string_view("sink_out")}, std::pair{StepKind::slide_out, std::string_view("slide_out")},
    std::pair{StepKind::pop_out, std::string_view("pop_out")},   std::pair{StepKind::zoom_out, std::string_view("zoom_out")},
    std::pair{StepKind::spin_out, std::string_view("spin_out")}, std::pair{StepKind::fold_out, std::string_view("fold_out")},
    std::pair{StepKind::unwrite, std::string_view("unwrite")},   std::pair{StepKind::bounce, std::string_view("bounce")},
    std::pair{StepKind::flash, std::string_view("flash")},       std::pair{StepKind::heartbeat, std::string_view("heartbeat")},
    std::pair{StepKind::rubber_band, std::string_view("rubber_band")}, std::pair{StepKind::swing, std::string_view("swing")},
    std::pair{StepKind::tada, std::string_view("tada")},         std::pair{StepKind::nudge, std::string_view("nudge")},
    std::pair{StepKind::bob, std::string_view("bob")},           std::pair{StepKind::breathe, std::string_view("breathe")},
    std::pair{StepKind::spin, std::string_view("spin")},         std::pair{StepKind::sway, std::string_view("sway")},
    std::pair{StepKind::orbit, std::string_view("orbit")},       std::pair{StepKind::blink, std::string_view("blink")},
    std::pair{StepKind::arc_to, std::string_view("arc_to")},     std::pair{StepKind::swap, std::string_view("swap")},
    std::pair{StepKind::reorder, std::string_view("reorder")},
};

// The recipes' one parameter each (besides object, duration and ease): its
// name, what it is, its default; and for idle loops their period's default.
enum class Form : std::uint8_t { none, number, pair, degrees, times };
struct Recipe {
    StepKind kind;
    Form form = Form::none;
    std::string_view param;
    double number = 0;
    Vec2 vector{};
    double period = 0;  // an idle loop's default course (0: not a loop)
};
constexpr std::array recipes{
    Recipe{StepKind::fade_in},  Recipe{StepKind::rise_in, Form::number, "distance", 24},  Recipe{StepKind::sink_in, Form::number, "distance", 24},
    Recipe{StepKind::slide_in, Form::pair, "from", 0, {-40, 0}}, Recipe{StepKind::pop_in}, Recipe{StepKind::zoom_in},
    Recipe{StepKind::spin_in, Form::degrees, "degrees", -69},  Recipe{StepKind::unfold_in}, Recipe{StepKind::write_in},
    Recipe{StepKind::drop_in, Form::number, "height", 160},
    Recipe{StepKind::fade_out}, Recipe{StepKind::rise_out, Form::number, "distance", 24}, Recipe{StepKind::sink_out, Form::number, "distance", 24},
    Recipe{StepKind::slide_out, Form::pair, "to", 0, {40, 0}}, Recipe{StepKind::pop_out}, Recipe{StepKind::zoom_out},
    Recipe{StepKind::spin_out, Form::degrees, "degrees", 69}, Recipe{StepKind::fold_out}, Recipe{StepKind::unwrite},
    Recipe{StepKind::bounce, Form::number, "height", 28},     Recipe{StepKind::flash, Form::times, "times", 2},
    Recipe{StepKind::heartbeat, Form::number, "scale", 1.15}, Recipe{StepKind::rubber_band}, Recipe{StepKind::swing, Form::degrees, "degrees", 15},
    Recipe{StepKind::tada}, Recipe{StepKind::nudge, Form::pair, "by", 0, {8, 0}},
    Recipe{StepKind::bob, Form::number, "amplitude", 6, {}, 2.4},  Recipe{StepKind::breathe, Form::number, "scale", 1.04, {}, 3},
    Recipe{StepKind::spin, Form::none, {}, 0, {}, 1.2},             Recipe{StepKind::sway, Form::degrees, "degrees", 5, {}, 2.4},
    Recipe{StepKind::orbit, Form::number, "radius", 16, {}, 2},     Recipe{StepKind::blink, Form::none, {}, 0, {}, 1},
};

constexpr double degrees = std::numbers::pi / 180;

// The document's checker: each read notes what is wrong where it is.
class Reader {
public:
    Problems problems;

    void problem(std::string where, std::string text) { problems.push_back({std::move(where), std::move(text)}); }

    // Only the keys a thing may have; a misspelt one is named with what it may be.
    void keys(const Json& value, const std::string& where, std::initializer_list<std::string_view> allowed) {
        for (auto item = value.begin(); item != value.end(); ++item)
            if (const std::string& key = item.key(); std::ranges::find(allowed, std::string_view(key)) == allowed.end()) {
                std::string names;
                for (const auto name : allowed) names += std::format("{}{}", names.empty() ? "" : ", ", name);
                problem(where + "/" + key, std::format("`{}` is not a field here (fields: {})", key, names));
            }
    }
    std::optional<double> number(const Json& value, const std::string& where, double low, double high) {
        if (!value.is_number()) {
            problem(where, "a number");
            return std::nullopt;
        }
        const double result = value.get<double>();
        if (!std::isfinite(result) || result < low || result > high) {
            problem(where, std::format("{} is outside {} … {}", result, low, high));
            return std::nullopt;
        }
        return result;
    }
    std::optional<Vec2> pair(const Json& value, const std::string& where, double low, double high) {
        if (!value.is_array() || value.size() != 2) {
            problem(where, "a pair [x, y]");
            return std::nullopt;
        }
        const auto x = number(value[0], where + "/0", low, high), y = number(value[1], where + "/1", low, high);
        if (!x || !y) return std::nullopt;
        return Vec2{*x, *y};
    }
    std::optional<std::string> text(const Json& value, const std::string& where) {
        if (!value.is_string() || value.get<std::string>().empty()) {
            problem(where, "a nonempty string");
            return std::nullopt;
        }
        return value.get<std::string>();
    }
    std::optional<Rgba> color(const Json& value, const std::string& where) {
        if (!value.is_string()) {
            problem(where, "a colour: \"#rrggbb\" or \"#rrggbbaa\"");
            return std::nullopt;
        }
        const auto parsed = Rgba::parse_hex(value.get<std::string>());
        if (!parsed) problem(where, std::format("`{}` is not a colour (#rgb, #rgba, #rrggbb, #rrggbbaa)", value.get<std::string>()));
        return parsed;
    }

    ShapeSpec shape(const Json& value, const std::string& where) {
        ShapeSpec result;
        if (!value.is_object() || value.size() != 1) {
            problem(where, "one shape: {\"rect\": [w, h]}, {\"circle\": r}, {\"ellipse\": [rx, ry]}, {\"polygon\": [[x, y], …]} or {\"star\": {…}}");
            return result;
        }
        const auto first = value.begin();
        const std::string kind = first.key();
        const Json& body = first.value();
        const std::string at = where + "/" + kind;
        if (kind == "rect") {
            result.kind = ShapeKind::rect;
            if (body.is_object()) {
                keys(body, at, {"size", "radius"});
                if (body.contains("size")) result.size = pair(body["size"], at + "/size", 0.01, 1e5).value_or(Vec2{});
                else problem(at, "a rect has a size [w, h]");
                if (body.contains("radius")) result.radius = number(body["radius"], at + "/radius", 0, 1e5).value_or(0);
            } else {
                result.size = pair(body, at, 0.01, 1e5).value_or(Vec2{});
            }
            if (result.radius * 2 > std::min(result.size.x, result.size.y))
                problem(at + "/radius", "a corner radius is at most half the shorter side");
        } else if (kind == "circle") {
            result.kind = ShapeKind::circle;
            result.radius = number(body, at, 0.01, 1e5).value_or(1);
        } else if (kind == "ellipse") {
            result.kind = ShapeKind::ellipse;
            result.size = pair(body, at, 0.01, 1e5).value_or(Vec2{1, 1});
        } else if (kind == "polygon") {
            result.kind = ShapeKind::polygon;
            if (!body.is_array() || body.size() < 3) {
                problem(at, "a polygon has three points or more: [[x, y], …]");
            } else {
                for (std::size_t index = 0; index < body.size(); ++index)
                    if (auto point = pair(body[index], std::format("{}/{}", at, index), -1e5, 1e5)) result.points.push_back(*point);
            }
        } else if (kind == "star") {
            result.kind = ShapeKind::star;
            if (!body.is_object()) {
                problem(at, "a star is {\"points\": 5, \"outer\": r, \"inner\": r}");
            } else {
                keys(body, at, {"points", "outer", "inner"});
                result.count = static_cast<int>(body.contains("points") ? number(body["points"], at + "/points", 3, 64).value_or(5) : 5);
                result.radius = body.contains("outer") ? number(body["outer"], at + "/outer", 0.01, 1e5).value_or(1) : 1;
                result.inner = body.contains("inner") ? number(body["inner"], at + "/inner", 0.01, 1e5).value_or(result.radius / 2) : result.radius / 2;
                if (result.inner >= result.radius) problem(at + "/inner", "a star's inner radius is less than its outer");
            }
        } else {
            problem(at, std::format("`{}` is not a shape (rect, circle, ellipse, polygon, star)", kind));
        }
        return result;
    }

    ObjectSpec object(const Json& value, const std::string& where) {
        ObjectSpec result;
        if (!value.is_object()) {
            problem(where, "an object: {\"id\": …, \"shape\": …, \"fill\": …}");
            return result;
        }
        keys(value, where, {"id", "shape", "fill", "stroke", "at", "rotation", "scale", "hidden"});
        if (value.contains("id")) {
            result.id = text(value["id"], where + "/id").value_or("");
            if (!result.id.empty() && !std::ranges::all_of(result.id, [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
                }))
                problem(where + "/id", "an id is lowercase letters, digits, - and _");
        } else {
            problem(where, "an object has an id");
        }
        if (value.contains("shape")) result.shape = shape(value["shape"], where + "/shape");
        else problem(where, "an object has a shape");
        if (value.contains("fill")) result.fill = color(value["fill"], where + "/fill");
        if (value.contains("stroke")) {
            const auto& stroke = value["stroke"];
            if (!stroke.is_object() || !stroke.contains("color")) {
                problem(where + "/stroke", "a stroke is {\"color\": \"#rrggbb\", \"width\": w}");
            } else {
                keys(stroke, where + "/stroke", {"color", "width"});
                PartStroke made;
                if (auto c = color(stroke["color"], where + "/stroke/color")) made.color = *c;
                if (stroke.contains("width")) made.width = number(stroke["width"], where + "/stroke/width", 0.01, 1e4).value_or(1);
                result.stroke = made;
            }
        }
        if (!result.fill && !result.stroke) problem(where, "an object has a fill, a stroke or both");
        if (value.contains("at")) result.at = pair(value["at"], where + "/at", -1e5, 1e5).value_or(Vec2{});
        if (value.contains("rotation")) result.rotation = number(value["rotation"], where + "/rotation", -36000, 36000).value_or(0) * degrees;
        if (value.contains("scale")) result.scale = scale(value["scale"], where + "/scale").value_or(Vec2{1, 1});
        if (value.contains("hidden")) {
            if (!value["hidden"].is_boolean()) problem(where + "/hidden", "true or false");
            else result.hidden = value["hidden"].get<bool>();
        }
        return result;
    }
    // A scale: one factor for both axes, or [x, y].
    std::optional<Vec2> scale(const Json& value, const std::string& where) {
        if (value.is_number()) {
            const auto factor = number(value, where, 1e-4, 1e4);
            return factor ? std::optional<Vec2>(Vec2{*factor, *factor}) : std::nullopt;
        }
        return pair(value, where, 1e-4, 1e4);
    }

    StepSpec step(const Json& value, const std::string& where, const std::set<std::string>& objects, int depth) {
        StepSpec result;
        result.where = where;
        if (depth > 32) {
            problem(where, "steps nest 32 deep at most");
            return result;
        }
        if (!value.is_object() || !value.contains("do") || !value["do"].is_string()) {
            problem(where, "a step: {\"do\": \"move_to\", \"object\": …} (or sequence, parallel, stagger, wait, loop, marker)");
            return result;
        }
        const std::string name = value["do"].get<std::string>();
        const auto found = std::ranges::find(step_names, std::string_view(name), &std::pair<StepKind, std::string_view>::second);
        if (found == step_names.end()) {
            std::string all;
            for (const auto& [kind, text] : step_names) all += std::format("{}{}", all.empty() ? "" : ", ", text);
            problem(where + "/do", std::format("`{}` is not a step ({})", name, all));
            return result;
        }
        result.kind = found->first;
        const auto object_of = [&](std::string_view key) {
            if (!value.contains(key)) {
                problem(where, std::format("`{}` names its {}", name, key));
                return std::string();
            }
            auto id = text(value[key], std::format("{}/{}", where, key)).value_or("");
            if (!id.empty() && !objects.contains(id)) problem(std::format("{}/{}", where, key), std::format("no object `{}`", id));
            return id;
        };
        const auto timing = [&] {
            if (value.contains("duration"))
                if (auto d = number(value["duration"], where + "/duration", 0, 3600)) result.duration = seconds(*d);
            if (value.contains("ease")) {
                if (!value["ease"].is_string()) problem(where + "/ease", "an ease: \"out_cubic\", \"cubic-bezier(.2, 0, 0, 1)\", \"spring(170, 26, 1)\" …");
                else if (!(result.ease = parse_ease(value["ease"].get<std::string>())))
                    problem(where + "/ease", std::format("`{}` is not an ease (linear, in_out_cubic, out_back, cubic-bezier(…), steps(…), spring(…) …)",
                                                         value["ease"].get<std::string>()));
            }
            if (value.contains("spring")) {
                const auto& spring = value["spring"];
                if (!spring.is_object()) {
                    problem(where + "/spring", "a spring: {\"stiffness\": 170, \"damping\": 26, \"mass\": 1}");
                } else {
                    keys(spring, where + "/spring", {"stiffness", "damping", "mass"});
                    SpringParams params;
                    if (spring.contains("stiffness")) params.stiffness = number(spring["stiffness"], where + "/spring/stiffness", 1, 1e5).value_or(170);
                    if (spring.contains("damping")) params.damping = number(spring["damping"], where + "/spring/damping", 0.01, 1e4).value_or(26);
                    if (spring.contains("mass")) params.mass = number(spring["mass"], where + "/spring/mass", 0.01, 1e3).value_or(1);
                    result.spring = params;
                }
                if (result.duration || result.ease) problem(where + "/spring", "a spring takes as long as it settles: no duration or ease with it");
            }
        };
        const auto items = [&] {
            if (!value.contains("items") || !value["items"].is_array() || value["items"].empty()) {
                problem(where, std::format("`{}` has its steps in `items`: [ … ]", name));
                return;
            }
            for (std::size_t index = 0; index < value["items"].size(); ++index)
                result.items.push_back(step(value["items"][index], std::format("{}/items/{}", where, index), objects, depth + 1));
        };
        switch (result.kind) {
        case StepKind::sequence:
        case StepKind::parallel:
            keys(value, where, {"do", "items"});
            items();
            break;
        case StepKind::stagger:
            keys(value, where, {"do", "items", "step"});
            items();
            if (value.contains("step")) result.step = seconds(number(value["step"], where + "/step", 0, 60).value_or(0.1));
            break;
        case StepKind::wait:
            keys(value, where, {"do", "duration"});
            if (!value.contains("duration")) problem(where, "a wait has a duration");
            timing();
            break;
        case StepKind::loop:
            keys(value, where, {"do", "item", "count", "alternate"});
            if (!value.contains("item")) problem(where, "a loop repeats one step, its `item`");
            else result.items.push_back(step(value["item"], where + "/item", objects, depth + 1));
            if (value.contains("count")) result.count = number(value["count"], where + "/count", 1, 1e6).value_or(1);
            if (value.contains("alternate")) {
                if (!value["alternate"].is_boolean()) problem(where + "/alternate", "true or false");
                else result.alternate = value["alternate"].get<bool>();
            }
            break;
        case StepKind::marker:
            keys(value, where, {"do", "name"});
            if (!value.contains("name")) problem(where, "a marker has a name");
            else result.name = text(value["name"], where + "/name").value_or("");
            break;
        case StepKind::move_to:
        case StepKind::move_by:
            keys(value, where, {"do", "object", "to", "by", "duration", "ease", "spring"});
            result.object = object_of("object");
            if (const char* key = result.kind == StepKind::move_to ? "to" : "by"; value.contains(key))
                result.vector = pair(value[key], std::format("{}/{}", where, key), -1e5, 1e5).value_or(Vec2{});
            else
                problem(where, std::format("`{}` says {} [x, y]", name, result.kind == StepKind::move_to ? "to" : "by"));
            timing();
            break;
        case StepKind::rotate_to:
        case StepKind::rotate_by:
            keys(value, where, {"do", "object", "degrees", "duration", "ease", "spring"});
            result.object = object_of("object");
            if (!value.contains("degrees")) problem(where, std::format("`{}` says its degrees", name));
            else result.number = number(value["degrees"], where + "/degrees", -36000, 36000).value_or(0) * degrees;
            timing();
            break;
        case StepKind::scale_to:
        case StepKind::scale_by:
            keys(value, where, {"do", "object", "to", "by", "duration", "ease", "spring"});
            result.object = object_of("object");
            if (const char* key = result.kind == StepKind::scale_to ? "to" : "by"; value.contains(key))
                result.vector = scale(value[key], std::format("{}/{}", where, key)).value_or(Vec2{1, 1});
            else
                problem(where, std::format("`{}` says {} (a factor, or [x, y])", name, result.kind == StepKind::scale_to ? "to" : "by"));
            timing();
            break;
        case StepKind::fill_to:
            keys(value, where, {"do", "object", "color", "duration", "ease", "spring"});
            result.object = object_of("object");
            if (!value.contains("color")) problem(where, "`fill_to` says its color");
            else result.color = color(value["color"], where + "/color");
            timing();
            break;
        case StepKind::fade_to:
            keys(value, where, {"do", "object", "opacity", "duration", "ease", "spring"});
            result.object = object_of("object");
            if (!value.contains("opacity")) problem(where, "`fade_to` says its opacity (0 … 1)");
            else result.number = number(value["opacity"], where + "/opacity", 0, 1).value_or(1);
            timing();
            break;
        case StepKind::morph:
            keys(value, where, {"do", "object", "into", "duration", "ease", "spring"});
            result.object = object_of("object");
            result.into = object_of("into");
            if (!result.object.empty() && result.object == result.into) problem(where + "/into", "an object morphs into another");
            timing();
            break;
        case StepKind::drop:
            keys(value, where, {"do", "object", "by", "duration", "bounces", "restitution", "squash", "stretch"});
            result.object = object_of("object");
            if (!value.contains("by")) problem(where, "`drop` says by [x, y]: how far it falls");
            else result.vector = pair(value["by"], where + "/by", -1e5, 1e5).value_or(Vec2{});
            if (value.contains("duration")) result.drop.duration = seconds(number(value["duration"], where + "/duration", 0.05, 600).value_or(1));
            if (value.contains("bounces")) result.drop.bounces = static_cast<int>(number(value["bounces"], where + "/bounces", 0, 12).value_or(2));
            if (value.contains("restitution")) result.drop.restitution = number(value["restitution"], where + "/restitution", 0, 0.95).value_or(0.45);
            if (value.contains("squash")) result.drop.squash = number(value["squash"], where + "/squash", 0, 0.9).value_or(0.2);
            if (value.contains("stretch")) result.drop.stretch = number(value["stretch"], where + "/stretch", 0, 0.9).value_or(0.08);
            break;
        case StepKind::pulse:
            keys(value, where, {"do", "object", "scale", "duration", "ease"});
            result.object = object_of("object");
            result.vector = value.contains("scale") ? scale(value["scale"], where + "/scale").value_or(Vec2{1.08, 1.08}) : Vec2{1.08, 1.08};
            timing();
            break;
        case StepKind::shake:
            keys(value, where, {"do", "object", "amplitude", "duration", "ease"});
            result.object = object_of("object");
            result.vector = value.contains("amplitude") ? pair(value["amplitude"], where + "/amplitude", -1e4, 1e4).value_or(Vec2{8, 0}) : Vec2{8, 0};
            timing();
            break;
        case StepKind::wobble:
            keys(value, where, {"do", "object", "degrees", "duration", "ease"});
            result.object = object_of("object");
            result.number = (value.contains("degrees") ? number(value["degrees"], where + "/degrees", -360, 360).value_or(7) : 7) * degrees;
            timing();
            break;
        case StepKind::arc_to:
            keys(value, where, {"do", "object", "to", "bend", "duration", "ease"});
            result.object = object_of("object");
            if (!value.contains("to")) problem(where, "`arc_to` says to [x, y]");
            else result.vector = pair(value["to"], where + "/to", -1e5, 1e5).value_or(Vec2{});
            result.number = value.contains("bend") ? number(value["bend"], where + "/bend", -2, 2).value_or(.3) : .3;
            timing();
            break;
        case StepKind::swap:
            keys(value, where, {"do", "object", "with", "by", "bend", "duration", "ease"});
            result.object = object_of("object");
            result.with = object_of("with");
            if (!result.with.empty() && result.with == result.object) problem(where + "/with", "an object swaps with another");
            if (!value.contains("by")) problem(where, "`swap` says by [x, y]: from the object to the other");
            else result.vector = pair(value["by"], where + "/by", -1e5, 1e5).value_or(Vec2{});
            result.number = value.contains("bend") ? number(value["bend"], where + "/bend", -2, 2).value_or(.4) : .4;
            timing();
            break;
        case StepKind::reorder:
            keys(value, where, {"do", "objects", "by", "step", "duration", "ease"});
            if (!value.contains("objects") || !value["objects"].is_array() || value["objects"].empty() || !value.contains("by") || !value["by"].is_array() ||
                value["by"].size() != value["objects"].size()) {
                problem(where, "`reorder` has `objects` and as many offsets `by`: [[dx, dy], …]");
            } else {
                for (std::size_t index = 0; index < value["objects"].size(); ++index) {
                    auto id = text(value["objects"][index], std::format("{}/objects/{}", where, index)).value_or("");
                    if (!id.empty() && !objects.contains(id)) problem(std::format("{}/objects/{}", where, index), std::format("no object `{}`", id));
                    result.objects.push_back(id);
                    result.offsets.push_back(pair(value["by"][index], std::format("{}/by/{}", where, index), -1e5, 1e5).value_or(Vec2{}));
                }
            }
            if (value.contains("step")) result.step = seconds(number(value["step"], where + "/step", 0, 60).value_or(.05));
            else result.step = seconds(.05);
            timing();
            break;
        default: {
            // A recipe of hanim's library: its object, its one parameter, its timing (and a loop's period and count).
            const auto recipe = std::ranges::find(recipes, result.kind, &Recipe::kind);
            if (recipe == recipes.end()) break;
            const bool looping = recipe->period > 0;
            std::vector<std::string_view> allowed{"do", "object", "duration", "ease"};
            if (!recipe->param.empty()) allowed.push_back(recipe->param);
            if (looping) allowed.insert(allowed.end(), {"period", "count"});
            for (auto item = value.begin(); item != value.end(); ++item)
                if (std::ranges::find(allowed, std::string_view(item.key())) == allowed.end()) {
                    std::string names;
                    for (const auto field : allowed) names += std::format("{}{}", names.empty() ? "" : ", ", field);
                    problem(where + "/" + item.key(), std::format("`{}` is not a field of `{}` (fields: {})", item.key(), name, names));
                }
            result.object = object_of("object");
            const std::string at = std::format("{}/{}", where, recipe->param);
            const bool given = !recipe->param.empty() && value.contains(recipe->param);
            switch (recipe->form) {
            case Form::none: break;
            case Form::number: result.number = given ? number(value[recipe->param], at, -1e5, 1e5).value_or(recipe->number) : recipe->number; break;
            case Form::degrees: result.number = (given ? number(value[recipe->param], at, -36000, 36000).value_or(recipe->number) : recipe->number) * degrees; break;
            case Form::times: result.number = given ? number(value[recipe->param], at, 1, 100).value_or(recipe->number) : recipe->number; break;
            case Form::pair: result.vector = given ? pair(value[recipe->param], at, -1e5, 1e5).value_or(recipe->vector) : recipe->vector; break;
            }
            if (looping) {
                result.period = seconds(value.contains("period") ? number(value["period"], where + "/period", 0.01, 600).value_or(recipe->period) : recipe->period);
                result.count = value.contains("count") ? number(value["count"], where + "/count", 1, 1e6).value_or(1) : 1;
            }
            timing();
            if (result.spring) problem(where + "/spring", "a recipe takes a duration and an ease, not a spring");
            break;
        }
        }
        return result;
    }
};

}  // namespace

std::string_view step_name(StepKind kind) noexcept {
    for (const auto& [each, name] : step_names)
        if (each == kind) return name;
    return "?";
}

std::string to_text(const Problems& problems) {
    std::string out;
    for (const auto& [where, text] : problems) out += std::format("{}{}: {}", out.empty() ? "" : "\n", where.empty() ? "/" : where, text);
    return out;
}

std::expected<Document, Problems> read(std::string_view text) {
    SyntaxProbe probe;
    if (!Json::sax_parse(text, &probe) || probe.at) {
        return std::unexpected(Problems{{line_and_column(text, probe.at.value_or(text.size())), "the JSON breaks here"}});
    }
    const Json root = Json::parse(text, nullptr, false);
    Reader reader;
    Document document;
    if (!root.is_object()) return std::unexpected(Problems{{"", "a document is an object: {\"version\": 1, \"objects\": […], \"program\": {…}}"}});
    reader.keys(root, "", {"$schema", "version", "size", "background", "objects", "program"});
    if (!root.contains("version") || !root["version"].is_number_integer() || root["version"].get<int>() != 1)
        reader.problem("/version", "this reads version 1");
    if (root.contains("size")) {
        const auto size = reader.pair(root["size"], "/size", 1, 4096);
        if (size) document.width = size->x, document.height = size->y;
    }
    if (root.contains("background"))
        if (auto c = reader.color(root["background"], "/background")) document.background = *c;
    std::set<std::string> ids;
    if (!root.contains("objects") || !root["objects"].is_array() || root["objects"].empty()) {
        reader.problem("/objects", "a document has its objects: [ {\"id\": …, \"shape\": …}, … ]");
    } else if (root["objects"].size() > 256) {
        reader.problem("/objects", "256 objects at most");
    } else {
        for (std::size_t index = 0; index < root["objects"].size(); ++index) {
            auto object = reader.object(root["objects"][index], std::format("/objects/{}", index));
            if (!object.id.empty() && !ids.insert(object.id).second) reader.problem(std::format("/objects/{}/id", index), std::format("`{}` twice", object.id));
            document.objects.push_back(std::move(object));
        }
    }
    if (!root.contains("program")) reader.problem("/program", "a document has its program: one step");
    else document.program = reader.step(root["program"], "/program", ids, 0);
    if (!reader.problems.empty()) return std::unexpected(std::move(reader.problems));
    return document;
}

}  // namespace hanim::studio
