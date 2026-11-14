// hanim — the animation tool, without a GUI: check a document, sample where
// its objects are at any moment, render its frames.
//
//   hanim check  FILE                         the plan: duration, tracks, markers — or every problem
//   hanim at     FILE --at T [--object ID]    the steps that govern that moment (of that object), innermost
//                                             first: each one's JSON pointer, timing and JSON — what to change
//   hanim vary   FILE --set POINTER --value JSON [--value JSON …] --out PREFIX [--frames N | --at T …]
//                                             alternatives to pick from by looking: the value at POINTER
//                                             set to each, every variant drawn at the same moments, a row
//                                             each (the document as written first) — PREFIX.variants.png,
//                                             PREFIX-v1.animation.omui.json …, PREFIX.variants.omui.json
//   hanim sample FILE --at T [--at T …]       every object's position, scale, rotation, fill and bounds
//   hanim render FILE --out PREFIX [--frames N | --at T …] [--webp FPS] [--svg]
//                                             PREFIX-000.png …, PREFIX.strip.png, PREFIX.report.omui.json;
//                                             with --svg each frame as vectors too: PREFIX-000.svg …
//
// Every answer is one JSON object on standard output; exit 0 done, 1 the
// document or the work failed (its problems said), 2 the command was wrong.
import std;
import hanim.studio;
import xrender.vector.svg;
import mlibs.image;
import mlibs.hash;
import mcpplibs.os;
import mcpplibs.cmdline;
import nlohmann.json;

namespace {

namespace studio = hanim::studio;
namespace os = mcpplibs::os;
namespace cmdline = mcpplibs::cmdline;
using Json = nlohmann::json;

constexpr std::string_view usage =
    "usage: hanim check FILE | hanim at FILE --at T [--object ID] | hanim vary FILE --set POINTER --value JSON … --out PREFIX [--frames N | --at T …] | hanim sample FILE --at T [--at T …] | hanim render FILE --out PREFIX [--frames N | --at T …] [--webp FPS] [--svg]";

int answer(const Json& json, int code) {
    std::println("{}", json.dump());
    return code;
}
int wrong(std::string_view text) { return answer(Json{{"ok", false}, {"error", text}, {"usage", usage}}, 2); }
int failed(const studio::Problems& problems) {
    Json list = Json::array();
    for (const auto& [where, text] : problems) list.push_back({{"where", where}, {"text", text}});
    return answer(Json{{"ok", false}, {"problems", std::move(list)}}, 1);
}

Json color(const std::optional<hanim::Rgba>& rgba) {
    if (!rgba) return nullptr;
    const auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
    return std::format("#{:02x}{:02x}{:02x}{:02x}", byte(rgba->r), byte(rgba->g), byte(rgba->b), byte(rgba->a));
}
double round3(double value) { return std::round(value * 1000) / 1000; }
Json states(const std::vector<studio::ObjectState>& objects) {
    Json list = Json::array();
    for (const auto& object : objects) {
        Json one{{"id", object.id}, {"drawn", object.drawn}};
        if (object.drawn) {
            one["position"] = {round3(object.position.x), round3(object.position.y)};
            one["scale"] = {round3(object.scale.x), round3(object.scale.y)};
            one["rotation_degrees"] = round3(object.rotation * 180 / std::numbers::pi);
            one["fill"] = color(object.fill);
            one["opacity"] = round3(object.opacity);
            one["bounds"] = {round3(object.low.x), round3(object.low.y), round3(object.high.x), round3(object.high.y)};
        }
        list.push_back(std::move(one));
    }
    return list;
}
Json operations(const studio::Studio& opened) {
    Json list = Json::array();
    for (const auto& op : opened.operations())
        list.push_back({{"operation", op.name}, {"decision", op.decision == openas::Decision::run ? "run" : op.decision == openas::Decision::ask ? "ask" : "refuse"}});
    return list;
}
Json plan(const studio::Studio& opened) {
    Json markers = Json::array();
    for (const auto& marker : opened.markers()) markers.push_back({{"name", marker.name}, {"time", round3(marker.time.count())}});
    const double length = opened.duration().count();
    return {{"duration", std::isfinite(length) ? Json(round3(length)) : Json("forever")},
            {"tracks", opened.plan().tracks().size()},
            {"segments", opened.plan().segments().size()},
            {"markers", std::move(markers)},
            {"size", {opened.document().width, opened.document().height}}};
}

// The command line (mcpplibs.cmdline): check, at, vary, sample, render — each its options.
cmdline::App command_line() {
    using cmdline::App, cmdline::Arg, cmdline::Option;
    const auto document = Arg("file").required().help("the animation document");
    const auto at = Option("at").value_type<double>().multiple().value_name("T").help("a moment in seconds");
    const auto out = Option("out").takes_value().value_name("PREFIX").help("where the pictures go");
    const auto frames = Option("frames").value_type<double>().help("evenly spaced frames, 1 … 240");
    return App("hanim")
        .description("The animation tool without a GUI: one JSON answer, exit 0 done / 1 failed / 2 a wrong command")
        .subcommand(App("check").description("the plan: duration, tracks, markers — or every problem").arg(document))
        .subcommand(App("at").description("the steps that govern a moment (of an object), innermost first").arg(document).option(at)
                        .option(Option("object").takes_value().value_name("ID").help("only this object")))
        .subcommand(App("vary").description("alternatives drawn side by side: the value at POINTER set to each").arg(document)
                        .option(Option("set").takes_value().value_name("POINTER").help("the JSON pointer varied"))
                        .option(Option("value").takes_value().multiple().value_name("JSON").help("a value it takes"))
                        .option(out).option(frames).option(at))
        .subcommand(App("sample").description("every object's position, scale, rotation, fill and bounds").arg(document).option(at))
        .subcommand(App("render").description("frames as PNG, a strip and a report").arg(document).option(out).option(frames).option(at)
                        .option(Option("webp").value_type<double>().value_name("FPS").help("also an animated WebP, 1 … 120 frames a second"))
                        .option(Option("svg").help("each frame as vectors too")));
}

}  // namespace

int main(int argc, char** argv) {
    const cmdline::App app = command_line();
    auto parsed = app.parse(argc, argv);
    if (!parsed) return parsed.error().is_error() ? wrong(parsed.error().message) : 0;
    if (!parsed->has_subcommand()) return wrong("a command and a document");
    const std::string command(parsed->subcommand_name());
    const cmdline::ParsedArgs& args = parsed->subcommand()->get();
    const std::string file = args.positional(0);
    std::vector<double> times;
    for (const std::string& value : args.option_or_empty("at").values) {
        double time = 0;
        std::from_chars(value.data(), value.data() + value.size(), time);
        if (!std::isfinite(time) || time < 0) return wrong("--at takes a time in seconds, 0 or more");
        times.push_back(time);
    }
    const std::optional<std::string> out = args.value("out");
    int frames = 0, fps = 0;
    if (auto count = args.value_as<double>("frames")) {
        if (*count < 1 || *count > 240 || *count != std::floor(*count)) return wrong("--frames takes 1 … 240");
        frames = static_cast<int>(*count);
    }
    if (auto rate = args.value_as<double>("webp")) {
        if (*rate < 1 || *rate > 120 || *rate != std::floor(*rate)) return wrong("--webp takes frames a second, 1 … 120");
        fps = static_cast<int>(*rate);
    }
    const bool svg = args.is_flag_set("svg");
    const std::string object = args.value("object").value_or(""), set = args.value("set").value_or("");
    const std::vector<std::string> values = args.option_or_empty("value").values;
    const auto text = os::fs::read(os::fs::from_utf8(file));
    if (!text) return answer(Json{{"ok", false}, {"problems", {{{"where", ""}, {"text", std::format("cannot read {}: {}", file, text.error().message())}}}}}, 1);
    const auto document = studio::read(*text);
    if (!document) return failed(document.error());
    auto opened = studio::Studio::open(*document);
    if (!opened) return failed(opened.error());

    if (command == "check") return answer(Json{{"ok", true}, {"file", file}, {"plan", plan(*opened)}, {"operations", operations(*opened)}}, 0);
    if (command == "at") {
        // What governs a moment someone points at (Part 2 H): the steps, innermost first, each with its
        // JSON pointer, its timing and its own JSON — the numbers to change.
        if (times.size() != 1) return wrong("hanim at FILE --at T [--object ID]");
        const double moment = times.front();
        const Json whole = Json::parse(*text, nullptr, false);
        Json steps = Json::array();
        for (const auto& governing : opened->at(hanim::seconds(moment), object)) {
            Json one{{"where", governing.where},
                     {"start", round3(governing.start)},
                     {"end", round3(governing.end)},
                     {"progress", round3(governing.progress)},
                     {"objects", governing.objects}};
            // Studio's pointers are well-formed (from its own reading): look the step up without throwing.
            const Json::json_pointer pointer(governing.where);
            if (!whole.is_discarded() && whole.contains(pointer)) one["step"] = whole[pointer];
            steps.push_back(std::move(one));
        }
        return answer(Json{{"ok", true}, {"file", file}, {"at", moment}, {"object", object}, {"steps", std::move(steps)}}, 0);
    }

    if (command == "vary") {
        // Alternatives a person picks from by looking (Part 2 H): one value set several ways, every
        // variant drawn at the same moments — the longest variant's course — a row each.
        if (set.empty() || values.empty() || !out) return wrong("hanim vary FILE --set POINTER --value JSON [--value JSON …] --out PREFIX");
        if (values.size() > 15) return wrong("15 values at most");
        const auto variants = studio::vary(*text, set, values);
        if (!variants) return failed(variants.error());
        std::vector<std::optional<studio::Studio>> studios;
        double longest = 0;
        for (const auto& variant : *variants) {
            std::optional<studio::Studio> one;
            if (variant.document)
                if (auto made = studio::Studio::open(*variant.document)) one.emplace(std::move(*made));
            if (one && std::isfinite(one->duration().count())) longest = std::max(longest, one->duration().count());
            studios.push_back(std::move(one));
        }
        if (times.empty()) {
            const int count = frames ? frames : 8;
            for (int index = 0; index < count; ++index) times.push_back(count == 1 ? 0 : longest * index / (count - 1));
        }
        if (times.size() > 24) return wrong("24 moments at most");
        const int width = static_cast<int>(document->width), height = static_cast<int>(document->height), gap = 4;
        const int columns = static_cast<int>(times.size()), rows = static_cast<int>(variants->size());
        xrender::raster::Surface sheet(columns * width, rows * height + (rows - 1) * gap);
        sheet.clear(xrender::raster::Rgba8{96, 96, 96, 255});
        Json list = Json::array();
        for (std::size_t row = 0; row < variants->size(); ++row) {
            const auto& variant = (*variants)[row];
            Json one{{"row", row}, {"value", variant.value.empty() ? Json(nullptr) : Json::parse(variant.value)}};
            if (row > 0) {
                const std::string name = std::format("{}-v{}.animation.omui.json", *out, row);
                if (auto saved = os::fs::write_atomic(os::fs::from_utf8(name), variant.text); !saved)
                    return failed({{"", std::format("cannot write {}: {}", name, saved.error().message())}});
                one["file"] = name;
            } else {
                one["file"] = file;
            }
            const auto problems = !variant.document ? variant.document.error()
                                  : !studios[row] ? studio::Problems{{set, "the variant reads but does not open"}}
                                                  : studio::Problems{};
            if (!problems.empty()) {
                Json listed = Json::array();
                for (const auto& [where, what] : problems) listed.push_back({{"where", where}, {"text", what}});
                one["problems"] = std::move(listed);
                list.push_back(std::move(one));
                continue;  // its row stays the sheet's grey
            }
            auto& opened_variant = *studios[row];
            one["duration"] = std::isfinite(opened_variant.duration().count()) ? Json(round3(opened_variant.duration().count())) : Json("forever");
            Json pixels = Json::array();
            for (std::size_t column = 0; column < times.size(); ++column) {
                xrender::raster::Surface surface(width, height);
                if (auto drawn = opened_variant.render(hanim::seconds(times[column]), surface); !drawn) return failed(drawn.error());
                pixels.push_back(std::format("{:016x}", surface.hash()));
                for (int y = 0; y < height; ++y)
                    std::copy_n(surface.row(y), width, sheet.row(static_cast<int>(row) * (height + gap) + y) + static_cast<int>(column) * width);
            }
            one["pixels"] = std::move(pixels);
            list.push_back(std::move(one));
        }
        Json moments = Json::array();
        for (const double time : times) moments.push_back(round3(time));
        const std::string sheet_name = *out + ".variants.png";
        const auto png = mlibs::image::encode_png({reinterpret_cast<const std::uint8_t*>(sheet.pixels().data()), sheet.width(), sheet.height(),
                                                   sheet.width() * 4, mlibs::image::Alpha::premultiplied});
        if (!png) return failed({{"", "PNG encoding failed: " + png.error()}});
        if (auto saved = os::fs::write_atomic(os::fs::from_utf8(sheet_name), std::string_view(reinterpret_cast<const char*>(png->data()), png->size()));
            !saved)
            return failed({{"", std::format("cannot write {}: {}", sheet_name, saved.error().message())}});
        Json report{{"kind", "hanim.variants"}, {"version", 1}, {"ok", true}, {"file", file}, {"set", set}, {"moments", std::move(moments)},
                    {"sheet", sheet_name}, {"variants", std::move(list)}};
        const std::string report_name = *out + ".variants.omui.json";
        if (auto saved = os::fs::write_atomic(os::fs::from_utf8(report_name), report.dump(2) + "\n"); !saved)
            return failed({{"", std::format("cannot write {}: {}", report_name, saved.error().message())}});
        report["report"] = report_name;
        return answer(report, 0);
    }

    const double length = opened->duration().count();
    if (command == "sample") {
        if (times.empty()) return wrong("sample takes --at T");
        Json samples = Json::array();
        for (const double time : times) {
            const auto at = opened->state(hanim::seconds(time));
            if (!at) return failed(at.error());
            samples.push_back({{"time", time}, {"objects", states(*at)}});
        }
        return answer(Json{{"ok", true}, {"plan", plan(*opened)}, {"samples", std::move(samples)}, {"operations", operations(*opened)}}, 0);
    }

    if (!out) return wrong("render takes --out PREFIX");
    if (times.empty()) {
        if (!std::isfinite(length)) return wrong("a program that loops forever renders --at given times");
        const int count = frames ? frames : (fps ? std::max(2, static_cast<int>(std::ceil(length * fps)) + 1) : 9);
        for (int index = 0; index < count; ++index) times.push_back(count == 1 ? 0 : length * index / (count - 1));
    }
    if (times.size() > 240) return wrong("240 frames at most");
    const int width = static_cast<int>(document->width), height = static_cast<int>(document->height);
    std::vector<xrender::raster::Surface> pictures;
    Json list = Json::array();
    const auto view = [](const xrender::raster::Surface& surface) {
        return mlibs::image::View{reinterpret_cast<const std::uint8_t*>(surface.pixels().data()), surface.width(), surface.height(), surface.width() * 4,
                                  mlibs::image::Alpha::premultiplied};
    };
    const auto save = [](const std::string& name, const std::vector<std::uint8_t>& bytes) {
        return os::fs::write_atomic(os::fs::from_utf8(name), std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    };
    for (std::size_t index = 0; index < times.size(); ++index) {
        xrender::raster::Surface surface(width, height);
        if (auto drawn = opened->render(hanim::seconds(times[index]), surface); !drawn) return failed(drawn.error());
        const auto at = opened->state(hanim::seconds(times[index]));
        if (!at) return failed(at.error());
        const auto png = mlibs::image::encode_png(view(surface));
        if (!png) return failed({{"", "PNG encoding failed: " + png.error()}});
        const std::string name = std::format("{}-{:03}.png", *out, index);
        if (auto saved = save(name, *png); !saved) return failed({{"", std::format("cannot write {}: {}", name, saved.error().message())}});
        mlibs::hash::Fnv1a hash;
        hash.bytes(std::as_bytes(surface.pixels()));
        Json frame{{"time", round3(times[index])}, {"file", name}, {"pixels", std::format("{:016x}", hash.value())}, {"objects", states(*at)}};
        if (svg) {
            // The same frame as vectors: what SVG cannot say stays the reference's pixels, listed.
            const auto drawn = opened->display(hanim::seconds(times[index]));
            if (!drawn) return failed(drawn.error());
            const auto& background = document->background;
            const auto picture = xrender::svg::write(**drawn, {}, {.view = {0, 0, static_cast<float>(document->width), static_cast<float>(document->height)},
                .background = mlibs::geom::Color{background.r, background.g, background.b, background.a},
                .encode_png = [](std::span<const std::uint8_t> rgba, int width, int height) {
                    return mlibs::image::encode_png({rgba.data(), width, height, width * 4, mlibs::image::Alpha::premultiplied});
                }});
            if (!picture) return failed({{"", "SVG: " + picture.error()}});
            const std::string svg_name = std::format("{}-{:03}.svg", *out, index);
            if (auto saved = os::fs::write_atomic(os::fs::from_utf8(svg_name), picture->text); !saved)
                return failed({{"", std::format("cannot write {}: {}", svg_name, saved.error().message())}});
            frame["svg"] = svg_name;
            frame["islands"] = picture->report.islands.size();
        }
        list.push_back(std::move(frame));
        pictures.push_back(std::move(surface));
    }
    // The frames side by side, at most eight a row: the whole course at a glance.
    const int columns = static_cast<int>(std::min<std::size_t>(8, pictures.size()));
    const int rows = static_cast<int>((pictures.size() + columns - 1) / columns);
    xrender::raster::Surface strip(columns * width, rows * height);
    for (std::size_t index = 0; index < pictures.size(); ++index)
        for (int y = 0; y < height; ++y)
            std::copy_n(pictures[index].row(y), width,
                        strip.row(static_cast<int>(index) / columns * height + y) + static_cast<int>(index) % columns * width);
    Json outputs = Json::array();
    if (const auto png = mlibs::image::encode_png(view(strip)); png && save(*out + ".strip.png", *png)) outputs.push_back(*out + ".strip.png");
    if (fps) {
        std::vector<mlibs::image::View> views;
        for (const auto& picture : pictures) views.push_back(view(picture));
        if (mlibs::image::save_animated_webp(os::fs::from_utf8(*out + ".webp"), views,
                                             {.delay_num = 1, .delay_den = static_cast<std::uint16_t>(fps), .loops = 0, .lossless = true}))
            outputs.push_back(*out + ".webp");
    }
    Json report{{"kind", "hanim.render"}, {"version", 1}, {"ok", true}, {"file", file}, {"plan", plan(*opened)},
                {"renderer", "cpu"}, {"frames", std::move(list)}, {"outputs", std::move(outputs)}, {"operations", operations(*opened)}};
    const std::string report_name = *out + ".report.omui.json";
    if (auto saved = os::fs::write_atomic(os::fs::from_utf8(report_name), report.dump(2) + "\n"); !saved)
        return failed({{"", std::format("cannot write {}: {}", report_name, saved.error().message())}});
    report["report"] = report_name;
    report.erase("operations");
    return answer(report, 0);
}
