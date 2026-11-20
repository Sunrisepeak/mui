// hanim.studio's subject `document`: the studio's operations (studio.ops.xdef.ts) answered from a document
// read from a file — what the `hanim` command runs through the tool kernel's entries (mlibs.agent:host).
module hanim.studio;

import std;
import openas;
import mlibs.agent;
import mlibs.image;
import mlibs.hash;
import mlibs.json;
import mcpplibs.os;
import nlohmann.json;
import xrender.vector.svg;

namespace hanim::studio {

namespace {

namespace os = mcpplibs::os;
using Json = nlohmann::json;
using mlibs::agent::Answer;
using mlibs::agent::Request;

Json color(const std::optional<hanim::Rgba>& rgba) {
    if (!rgba) return nullptr;
    const auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
    return std::format("#{:02x}{:02x}{:02x}{:02x}", byte(rgba->r), byte(rgba->g), byte(rgba->b), byte(rgba->a));
}
double round3(double value) { return std::round(value * 1000) / 1000; }
Json states(const std::vector<ObjectState>& objects) {
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
Json operations_of(const Studio& opened) {
    Json list = Json::array();
    for (const auto& op : opened.operations())
        list.push_back({{"operation", op.name}, {"decision", op.decision == openas::Decision::run ? "run" : op.decision == openas::Decision::ask ? "ask" : "refuse"}});
    return list;
}
Json plan(const Studio& opened) {
    Json markers = Json::array();
    for (const auto& marker : opened.markers()) markers.push_back({{"name", marker.name}, {"time", round3(marker.time.count())}});
    const double length = opened.duration().count();
    return {{"duration", std::isfinite(length) ? Json(round3(length)) : Json("forever")},
            {"tracks", opened.plan().tracks().size()},
            {"segments", opened.plan().segments().size()},
            {"markers", std::move(markers)},
            {"size", {opened.document().width, opened.document().height}}};
}

// An argument that is wrong: said, with the operation's ways out (the session's).
void wrong(Answer& answer, std::string_view text) { answer.fail(openas::error_code::invalid_argument, text); }
// The document's or the work's problems: listed as data, then the failure.
void failed(Answer& answer, const Problems& problems) {
    Json list = Json::array();
    for (const auto& [where, text] : problems) list.push_back({{"where", where}, {"text", text}});
    answer.data(Json{{"ok", false}, {"problems", std::move(list)}}.dump());
    answer.fail(openas::error_code::invalid_argument, to_text(problems));
}

// One number, or an array of them.
std::optional<std::vector<double>> numbers(const mlibs::json::Node& node) {
    std::vector<double> out;
    if (!node) return out;
    for (const mlibs::json::Node& each : node.is_array() ? node.items() : std::vector{node}) {
        const auto value = each.number();
        if (!value || !std::isfinite(*value) || *value < 0) return std::nullopt;
        out.push_back(*value);
    }
    return out;
}

bool written(Answer& answer, const std::string& name, std::string_view bytes) {
    if (auto saved = os::fs::write_atomic(os::fs::from_utf8(name), bytes); !saved) {
        failed(answer, {{"", std::format("cannot write {}: {}", name, saved.error().message())}});
        return false;
    }
    return true;
}

mlibs::image::View view(const xrender::raster::Surface& surface) {
    return {reinterpret_cast<const std::uint8_t*>(surface.pixels().data()), surface.width(), surface.height(), surface.width() * 4, mlibs::image::Alpha::premultiplied};
}

// document.vary: alternatives a person picks from by looking — one value set several ways, every variant drawn at
// the same moments (the longest variant's course), a row each.
void vary(Answer& answer, const std::string& file, const std::string& text, const Document& document, const Request& request, std::vector<double> times, int frames) {
    const std::string set = request["set"].text().value_or(""), out = request["out"].text().value_or("");
    std::vector<std::string> values;
    for (const mlibs::json::Node& each : request["value"].is_array() ? request["value"].items() : std::vector{request["value"]})
        if (each) values.push_back(each.dump());
    if (set.empty() || values.empty() || out.empty()) return wrong(answer, "document.vary takes --set POINTER, --value JSON (one each) and --out PREFIX");
    if (values.size() > 15) return wrong(answer, "15 values at most");
    const auto variants = studio::vary(text, set, values);
    if (!variants) return failed(answer, variants.error());
    std::vector<std::optional<Studio>> studios;
    double longest = 0;
    for (const auto& variant : *variants) {
        std::optional<Studio> one;
        if (variant.document)
            if (auto made = Studio::open(*variant.document)) one.emplace(std::move(*made));
        if (one && std::isfinite(one->duration().count())) longest = std::max(longest, one->duration().count());
        studios.push_back(std::move(one));
    }
    if (times.empty()) {
        const int count = frames ? frames : 8;
        for (int index = 0; index < count; ++index) times.push_back(count == 1 ? 0 : longest * index / (count - 1));
    }
    if (times.size() > 24) return wrong(answer, "24 moments at most");
    const int width = static_cast<int>(document.width), height = static_cast<int>(document.height), gap = 4;
    const int columns = static_cast<int>(times.size()), rows = static_cast<int>(variants->size());
    xrender::raster::Surface sheet(columns * width, rows * height + (rows - 1) * gap);
    sheet.clear(xrender::raster::Rgba8{96, 96, 96, 255});
    Json list = Json::array();
    for (std::size_t row = 0; row < variants->size(); ++row) {
        const auto& variant = (*variants)[row];
        Json one{{"row", row}, {"value", variant.value.empty() ? Json(nullptr) : Json::parse(variant.value)}};
        if (row > 0) {
            const std::string name = std::format("{}-v{}.animation.xdef.json", out, row);
            if (!written(answer, name, variant.text)) return;
            one["file"] = name;
        } else {
            one["file"] = file;
        }
        const auto problems = !variant.document ? variant.document.error() : !studios[row] ? Problems{{set, "the variant reads but does not open"}} : Problems{};
        if (!problems.empty()) {
            Json listed = Json::array();
            for (const auto& [where, what] : problems) listed.push_back({{"where", where}, {"text", what}});
            one["problems"] = std::move(listed);
            list.push_back(std::move(one));
            continue;  // its row stays the sheet's grey
        }
        auto& opened = *studios[row];
        one["duration"] = std::isfinite(opened.duration().count()) ? Json(round3(opened.duration().count())) : Json("forever");
        Json pixels = Json::array();
        for (std::size_t column = 0; column < times.size(); ++column) {
            xrender::raster::Surface surface(width, height);
            if (auto drawn = opened.render(hanim::seconds(times[column]), surface); !drawn) return failed(answer, drawn.error());
            pixels.push_back(std::format("{:016x}", surface.hash()));
            for (int y = 0; y < height; ++y) std::copy_n(surface.row(y), width, sheet.row(static_cast<int>(row) * (height + gap) + y) + static_cast<int>(column) * width);
        }
        one["pixels"] = std::move(pixels);
        list.push_back(std::move(one));
    }
    Json moments = Json::array();
    for (const double time : times) moments.push_back(round3(time));
    const std::string sheet_name = out + ".variants.png";
    const auto png = mlibs::image::encode_png(view(sheet));
    if (!png) return failed(answer, {{"", "PNG encoding failed: " + png.error()}});
    if (!written(answer, sheet_name, std::string_view(reinterpret_cast<const char*>(png->data()), png->size()))) return;
    Json report{{"kind", "hanim.variants"}, {"version", 1}, {"ok", true}, {"file", file}, {"set", set}, {"moments", std::move(moments)}, {"sheet", sheet_name}, {"variants", std::move(list)}};
    const std::string report_name = out + ".variants.xdef.json";
    if (!written(answer, report_name, report.dump(2) + "\n")) return;
    report["report"] = report_name;
    answer.data(report.dump());
}

// document.render: frames as PNG (and SVG), a strip of them, an animated WebP, and a report.
void render(Answer& answer, const std::string& file, Studio& opened, const Document& document, const Request& request, std::vector<double> times, int frames, int fps) {
    const std::string out = request["out"].text().value_or("");
    if (out.empty()) return wrong(answer, "document.render takes --out PREFIX");
    const bool svg = request["svg"].boolean().value_or(false);
    const double length = opened.duration().count();
    if (times.empty()) {
        if (!std::isfinite(length)) return wrong(answer, "a program that loops forever renders --at given times");
        const int count = frames ? frames : (fps ? std::max(2, static_cast<int>(std::ceil(length * fps)) + 1) : 9);
        for (int index = 0; index < count; ++index) times.push_back(count == 1 ? 0 : length * index / (count - 1));
    }
    if (times.size() > 240) return wrong(answer, "240 frames at most");
    const int width = static_cast<int>(document.width), height = static_cast<int>(document.height);
    std::vector<xrender::raster::Surface> pictures;
    Json list = Json::array();
    for (std::size_t index = 0; index < times.size(); ++index) {
        xrender::raster::Surface surface(width, height);
        if (auto drawn = opened.render(hanim::seconds(times[index]), surface); !drawn) return failed(answer, drawn.error());
        const auto at = opened.state(hanim::seconds(times[index]));
        if (!at) return failed(answer, at.error());
        const auto png = mlibs::image::encode_png(view(surface));
        if (!png) return failed(answer, {{"", "PNG encoding failed: " + png.error()}});
        const std::string name = std::format("{}-{:03}.png", out, index);
        if (!written(answer, name, std::string_view(reinterpret_cast<const char*>(png->data()), png->size()))) return;
        mlibs::hash::Fnv1a hash;
        hash.bytes(std::as_bytes(surface.pixels()));
        Json frame{{"time", round3(times[index])}, {"file", name}, {"pixels", std::format("{:016x}", hash.value())}, {"objects", states(*at)}};
        if (svg) {
            // The same frame as vectors: what SVG cannot say stays the reference's pixels, listed.
            const auto drawn = opened.display(hanim::seconds(times[index]));
            if (!drawn) return failed(answer, drawn.error());
            const auto& background = document.background;
            const auto picture = xrender::svg::write(**drawn, {}, {.view = {0, 0, static_cast<float>(document.width), static_cast<float>(document.height)},
                .background = mlibs::geom::Color{background.r, background.g, background.b, background.a},
                .encode_png = [](std::span<const std::uint8_t> rgba, int across, int down) {
                    return mlibs::image::encode_png({rgba.data(), across, down, across * 4, mlibs::image::Alpha::premultiplied});
                }});
            if (!picture) return failed(answer, {{"", "SVG: " + picture.error()}});
            const std::string svg_name = std::format("{}-{:03}.svg", out, index);
            if (!written(answer, svg_name, picture->text)) return;
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
            std::copy_n(pictures[index].row(y), width, strip.row(static_cast<int>(index) / columns * height + y) + static_cast<int>(index) % columns * width);
    Json outputs = Json::array();
    if (const auto png = mlibs::image::encode_png(view(strip));
        png && os::fs::write_atomic(os::fs::from_utf8(out + ".strip.png"), std::string_view(reinterpret_cast<const char*>(png->data()), png->size())))
        outputs.push_back(out + ".strip.png");
    if (fps) {
        std::vector<mlibs::image::View> views;
        for (const auto& picture : pictures) views.push_back(view(picture));
        if (mlibs::image::save_animated_webp(os::fs::from_utf8(out + ".webp"), views, {.delay_num = 1, .delay_den = static_cast<std::uint16_t>(fps), .loops = 0, .lossless = true}))
            outputs.push_back(out + ".webp");
    }
    Json report{{"kind", "hanim.render"}, {"version", 1}, {"ok", true}, {"file", file}, {"plan", plan(opened)},
                {"renderer", "cpu"}, {"frames", std::move(list)}, {"outputs", std::move(outputs)}, {"operations", operations_of(opened)}};
    const std::string report_name = out + ".report.xdef.json";
    if (!written(answer, report_name, report.dump(2) + "\n")) return;
    report["report"] = report_name;
    report.erase("operations");
    answer.data(report.dump());
}

}  // namespace

mlibs::control::Guide Documents::guide() const noexcept {
    static constexpr std::array<std::string_view, 2> look{R"({"op":"document.check","file":"studio/examples/01-rise.animation.xdef.json"})",
                                                          R"({"op":"document.render","file":"studio/examples/01-rise.animation.xdef.json","out":"rise","frames":5})"};
    static constexpr std::array<mlibs::control::Task, 1> tasks{{{"check-and-look", "Check a document, then look at five of its frames", look}}};
    return {"hanim animation documents (<name>.animation.xdef.json), checked, read at moments, varied and rendered without a GUI.", tasks};
}

void Documents::operate(std::string_view operation, const Request& request, Answer& answer) {
    const std::string file = request["file"].text().value_or("");
    const auto times = numbers(request["at"]);
    if (!times) return wrong(answer, "`at` is a time in seconds, 0 or more, or an array of them");
    const auto frames = request["frames"].integer().value_or(0), fps = request["webp"].integer().value_or(0);
    if (request["frames"] && (frames < 1 || frames > 240)) return wrong(answer, "`frames` takes 1 … 240");
    if (request["webp"] && (fps < 1 || fps > 120)) return wrong(answer, "`webp` takes frames a second, 1 … 120");
    const auto text = os::fs::read(os::fs::from_utf8(file));
    if (!text) return failed(answer, {{"", std::format("cannot read {}: {}", file, text.error().message())}});
    const auto document = read(*text);
    if (!document) return failed(answer, document.error());
    auto opened = Studio::open(*document);
    if (!opened) return failed(answer, opened.error());

    if (operation == "document.check") return answer.data(Json{{"ok", true}, {"file", file}, {"plan", plan(*opened)}, {"operations", operations_of(*opened)}}.dump());
    if (operation == "document.at") {
        // What governs a moment someone points at: the steps, innermost first, each with its JSON pointer, its
        // timing and its own JSON — the numbers to change.
        if (times->size() != 1) return wrong(answer, "document.at takes one moment: --at T");
        const double moment = times->front();
        const Json whole = Json::parse(*text, nullptr, false);
        Json steps = Json::array();
        for (const auto& governing : opened->at(hanim::seconds(moment), request["object"].text().value_or(""))) {
            Json one{{"where", governing.where}, {"start", round3(governing.start)}, {"end", round3(governing.end)}, {"progress", round3(governing.progress)}, {"objects", governing.objects}};
            // Studio's pointers are well-formed (from its own reading): look the step up without throwing.
            const Json::json_pointer pointer(governing.where);
            if (!whole.is_discarded() && whole.contains(pointer)) one["step"] = whole[pointer];
            steps.push_back(std::move(one));
        }
        return answer.data(Json{{"ok", true}, {"file", file}, {"at", moment}, {"object", request["object"].text().value_or("")}, {"steps", std::move(steps)}}.dump());
    }
    if (operation == "document.sample") {
        if (times->empty()) return wrong(answer, "document.sample takes --at T (one each)");
        Json samples = Json::array();
        for (const double time : *times) {
            const auto at = opened->state(hanim::seconds(time));
            if (!at) return failed(answer, at.error());
            samples.push_back({{"time", time}, {"objects", states(*at)}});
        }
        return answer.data(Json{{"ok", true}, {"plan", plan(*opened)}, {"samples", std::move(samples)}, {"operations", operations_of(*opened)}}.dump());
    }
    if (operation == "document.vary") return vary(answer, file, *text, *document, request, *times, static_cast<int>(frames));
    if (operation == "document.render") return render(answer, file, *opened, *document, request, *times, static_cast<int>(frames), static_cast<int>(fps));
}

}  // namespace hanim::studio
