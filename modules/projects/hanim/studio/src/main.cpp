// hanim — the animation tool, without a GUI: check a document, sample where
// its objects are at any moment, render its frames.
//
//   hanim check  FILE                         the plan: duration, tracks, markers — or every problem
//   hanim sample FILE --at T [--at T …]       every object's position, scale, rotation, fill and bounds
//   hanim render FILE --out PREFIX [--frames N | --at T …] [--webp FPS]
//                                             PREFIX-000.png …, PREFIX.strip.png, PREFIX.report.omui.json
//
// Every answer is one JSON object on standard output; exit 0 done, 1 the
// document or the work failed (its problems said), 2 the command was wrong.
import std;
import hanim.studio;
import mlibs.image;
import mlibs.hash;
import mcpplibs.os;
import nlohmann.json;

namespace {

namespace studio = hanim::studio;
namespace os = mcpplibs::os;
using Json = nlohmann::json;

constexpr std::string_view usage =
    "usage: hanim check FILE | hanim sample FILE --at T [--at T …] | hanim render FILE --out PREFIX [--frames N | --at T …] [--webp FPS]";

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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return wrong("a command and a document");
    const std::string_view command = argv[1];
    if (command != "check" && command != "sample" && command != "render") return wrong(std::format("no command `{}`", command));
    const std::string file = argv[2];
    std::vector<double> times;
    std::optional<std::string> out;
    int frames = 0, fps = 0;
    for (int index = 3; index < argc; ++index) {
        const std::string_view option = argv[index];
        const bool has_value = index + 1 < argc;
        const auto number = [&](double& value) {
            const std::string_view text = argv[++index];
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value);
        };
        if (option == "--at" && has_value) {
            double time = 0;
            if (!number(time) || time < 0) return wrong("--at takes a time in seconds, 0 or more");
            times.push_back(time);
        } else if (option == "--out" && has_value) {
            out = argv[++index];
        } else if (option == "--frames" && has_value) {
            double count = 0;
            if (!number(count) || count < 1 || count > 240 || count != std::floor(count)) return wrong("--frames takes 1 … 240");
            frames = static_cast<int>(count);
        } else if (option == "--webp" && has_value) {
            double rate = 0;
            if (!number(rate) || rate < 1 || rate > 120 || rate != std::floor(rate)) return wrong("--webp takes frames a second, 1 … 120");
            fps = static_cast<int>(rate);
        } else {
            return wrong(std::format("no option `{}` here", option));
        }
    }
    const auto text = os::fs::read(os::fs::from_utf8(file));
    if (!text) return answer(Json{{"ok", false}, {"problems", {{{"where", ""}, {"text", std::format("cannot read {}: {}", file, text.error().message())}}}}}, 1);
    const auto document = studio::read(*text);
    if (!document) return failed(document.error());
    auto opened = studio::Studio::open(*document);
    if (!opened) return failed(opened.error());

    if (command == "check") return answer(Json{{"ok", true}, {"file", file}, {"plan", plan(*opened)}, {"operations", operations(*opened)}}, 0);

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
        list.push_back({{"time", round3(times[index])}, {"file", name}, {"pixels", std::format("{:016x}", hash.value())}, {"objects", states(*at)}});
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
