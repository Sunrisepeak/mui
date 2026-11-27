// hanim.timeline.tool: the subject `timeline` — what the `hanim-timeline` command runs.
module hanim.timeline.tool;

import std;
import openas;
import hanim.timeline;
import mlibs.time;
import mlibs.json;
import mcpplibs.os;
import nlohmann.json;

namespace hanim::timeline::tool {

namespace {

namespace tl = hanim::timeline;
namespace os = mcpplibs::os;
using Json = nlohmann::json;
using mlibs::agent::Answer;
using mlibs::agent::Request;

constexpr std::size_t history_kept = 64;

// An argument that is wrong, or what was asked refused or failed: said, with the operation's ways out.
void wrong(Answer& answer, std::string_view text) { answer.fail(openas::error_code::invalid_argument, text); }
void failed(Answer& answer, std::string_view text) { answer.fail("failed", text); }
// What could not be opened: its problems as data, then the failure.
void unopened(Answer& answer, const Json& why) {
    answer.data(why.dump());
    answer.fail(openas::error_code::invalid_argument, why.contains("error") ? why["error"].get<std::string>() : "the timeline does not read: its problems are listed");
}

std::string utf8(const std::filesystem::path& path) {
    const std::u8string u = path.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}
std::filesystem::path history_of(const std::filesystem::path& file) {
    return file.parent_path() / os::fs::from_utf8("." + utf8(file.filename()) + ".history.xdef.jsonl");
}

struct Opened {
    std::string text;
    tl::Timeline timeline;
};
std::expected<Opened, Json> open(const std::filesystem::path& file) {
    auto text = os::fs::read(file);
    if (!text) return std::unexpected(Json{{"ok", false}, {"error", std::format("cannot read {}: {}", utf8(file), text.error().message())}});
    auto read = tl::read_timeline(*text, utf8(file.filename()));
    if (!read) {
        Json problems = Json::array();
        for (const auto& p : read.error())
            problems.push_back({{"line", p.line}, {"column", p.column}, {"at", p.at}, {"message", p.message}, {"hint", p.hint}});
        return std::unexpected(Json{{"ok", false}, {"problems", std::move(problems)}});
    }
    return Opened{std::move(*text), std::move(*read)};
}

std::string_view kind_name(tl::Kind kind) {
    switch (kind) {
    case tl::Kind::video: return "video";
    case tl::Kind::audio: return "audio";
    case tl::Kind::animation: return "animation";
    case tl::Kind::events: return "events";
    }
    return "?";
}

Json summary(const tl::Timeline& t) {
    Json tracks = Json::array();
    for (const auto& track : t.tracks) {
        Json clips = Json::array();
        for (const auto& clip : track.clips)
            clips.push_back({{"name", clip.name}, {"source", clip.source}, {"at", tl::time_text(clip.at, t.rate)}, {"length", tl::time_text(clip.length, t.rate)}});
        tracks.push_back({{"name", track.name}, {"kind", kind_name(track.kind)}, {"muted", track.muted}, {"clips", std::move(clips)}});
    }
    Json markers = Json::array();
    for (const auto& marker : t.markers) markers.push_back({{"name", marker.name}, {"at", tl::time_text(marker.at, t.rate)}});
    return {{"name", t.name},
            {"rate", std::format("{}/{}", t.rate.num, t.rate.den)},
            {"size", {t.width, t.height}},
            {"length", tl::time_text(t.duration(), t.rate)},
            {"tracks", std::move(tracks)},
            {"markers", std::move(markers)}};
}

std::optional<std::size_t> index_of(const mlibs::json::Node& node) {
    const auto value = node.integer();
    if (!value || *value < 0) return std::nullopt;
    return static_cast<std::size_t>(*value);
}

void check(Answer& answer, const std::filesystem::path& file) {
    auto opened = open(file);
    if (!opened) return unopened(answer, opened.error());
    const auto broken = tl::check(opened->timeline);
    Json out = summary(opened->timeline);
    out["ok"] = broken.empty();
    if (!broken.empty()) out["problems"] = broken;
    answer.data(out.dump());
    if (!broken.empty()) answer.fail(openas::error_code::invalid_argument, "the timeline reads but does not hold together: its problems are listed");
}

void at(Answer& answer, const std::filesystem::path& file, const Request& request) {
    const auto when = request["time"].text();
    if (!when) return wrong(answer, "timeline.at takes --time T");
    auto opened = open(file);
    if (!opened) return unopened(answer, opened.error());
    const auto& t = opened->timeline;
    const auto time = tl::parse_time(*when, t.rate);
    if (!time) return wrong(answer, std::format("`{}` is not a time (90f, 1.5s, 250ms, 123fl)", *when));
    std::vector<tl::Playing> now;
    tl::playing(t, *time, now);
    Json list = Json::array();
    for (const auto& p : now) {
        const auto& clip = t.tracks[p.track].clips[p.clip];
        list.push_back({{"track", t.tracks[p.track].name}, {"clip", clip.name}, {"source", clip.source}, {"source_at", tl::time_text(p.source, t.rate)}, {"level", p.level}});
    }
    answer.data(Json{{"ok", true}, {"time", tl::time_text(*time, t.rate)}, {"playing", std::move(list)}}.dump());
}

void edit(Answer& answer, const std::filesystem::path& file, const Request& request) {
    auto opened = open(file);
    if (!opened) return unopened(answer, opened.error());
    auto& t = opened->timeline;
    const std::string kind = request["edit"].text().value_or("");
    const auto track = index_of(request["track"]), clip = index_of(request["clip"]), to_track = index_of(request["to_track"]);
    const bool dry_run = request["dry_run"].boolean().value_or(false);
    if (!track || !clip) return wrong(answer, "an edit names its --track and --clip (indices from 0)");
    if (*track >= t.tracks.size() || *clip >= t.tracks[*track].clips.size()) return failed(answer, std::format("no clip {} on track {}", *clip, *track));
    const auto time = [&](std::string_view key) -> std::expected<mlibs::time::Flicks, std::string> {
        const auto text = request[key].text();
        if (!text) return std::unexpected(std::format("{} takes --{}", kind, key));
        const auto parsed = tl::parse_time(*text, t.rate);
        if (!parsed) return std::unexpected(std::format("`{}` is not a time (90f, 1.5s, 250ms, 123fl)", *text));
        return *parsed;
    };
    std::expected<tl::Change, tl::EditFailure> change = std::unexpected(tl::EditFailure{tl::EditError::unknown_step, "no edit"});
    if (kind == "split") {
        const auto when = time("at");
        if (!when) return wrong(answer, when.error());
        change = tl::split(t, *track, *clip, *when);
    } else if (kind == "trim") {
        const auto when = time("at");
        const auto length = time("length");
        if (!when) return wrong(answer, when.error());
        if (!length) return wrong(answer, length.error());
        change = tl::trim(t, *track, *clip, *when, *length);
    } else if (kind == "move") {
        const auto when = time("at");
        if (!when) return wrong(answer, when.error());
        change = tl::move(t, *track, *clip, to_track.value_or(*track), *when);
    } else if (kind == "remove") {
        change = tl::remove(t, *track, *clip);
    } else if (kind == "ripple-remove") {
        change = tl::ripple_remove(t, *track, *clip);
    } else {
        return wrong(answer, std::format("no edit `{}` (split, trim, move, remove, ripple-remove)", kind));
    }
    if (!change) return failed(answer, change.error().text());
    auto undone = tl::apply(t, *change);
    if (!undone) return failed(answer, undone.error().text());
    const std::string after = tl::write_timeline(t);
    Json out{{"ok", true}, {"edit", change->what}, {"dry_run", dry_run}, {"timeline", summary(t)}};
    if (dry_run) return answer.data(out.dump());
    // The text before, kept: `timeline.undo` puts it back.
    const auto history = history_of(file);
    std::vector<std::string> lines;
    if (auto old = os::fs::read(history)) {
        std::istringstream in(*old);
        for (std::string line; std::getline(in, line);)
            if (!line.empty()) lines.push_back(line);
    }
    lines.push_back(Json{{"edit", change->what}, {"before", opened->text}}.dump());
    if (lines.size() > history_kept) lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(history_kept));
    std::string kept;
    for (const auto& line : lines) kept += line + "\n";
    if (!os::fs::write_atomic(history, kept)) return failed(answer, std::format("cannot keep the history in {}", utf8(history)));
    if (!os::fs::write_atomic(file, after)) return failed(answer, std::format("cannot write {}", utf8(file)));
    out["undo"] = Json{{"op", "timeline.undo"}, {"file", utf8(file)}};
    answer.data(out.dump());
}

void undo(Answer& answer, const std::filesystem::path& file) {
    const auto history = history_of(file);
    auto old = os::fs::read(history);
    std::vector<std::string> lines;
    if (old) {
        std::istringstream in(*old);
        for (std::string line; std::getline(in, line);)
            if (!line.empty()) lines.push_back(line);
    }
    if (lines.empty()) return failed(answer, "nothing to take back: no edit made by this command is kept");
    const Json last = Json::parse(lines.back(), nullptr, false);
    if (last.is_discarded() || !last.contains("before") || !last["before"].is_string()) return failed(answer, std::format("{} is damaged", utf8(history)));
    lines.pop_back();
    std::string kept;
    for (const auto& line : lines) kept += line + "\n";
    if (!os::fs::write_atomic(file, last["before"].get<std::string>()) || !os::fs::write_atomic(history, kept)) return failed(answer, std::format("cannot write {}", utf8(file)));
    answer.data(Json{{"ok", true}, {"undone", last.value("edit", std::string())}, {"left", lines.size()}}.dump());
}

}  // namespace

mlibs::control::Guide Timelines::guide() const noexcept {
    static constexpr std::array<std::string_view, 3> cut{R"({"op":"timeline.at","file":"short.timeline.xdef.json","time":"1s"})",
                                                         R"({"op":"timeline.edit","file":"short.timeline.xdef.json","edit":"split","track":0,"clip":0,"at":"15f","dry_run":true})",
                                                         R"({"op":"timeline.edit","file":"short.timeline.xdef.json","edit":"split","track":0,"clip":0,"at":"15f"})"};
    static constexpr std::array<mlibs::control::Task, 1> tasks{{{"cut-a-clip", "See what plays, try a split, then make it (timeline.undo takes it back)", cut}}};
    return {"A timeline every editor shares (*.timeline.xdef.json; xdef snapshot makes one from its .ts): checked, read at a time, edited, an edit taken back.", tasks};
}

void Timelines::operate(std::string_view operation, const Request& request, Answer& answer) {
    const auto file = os::fs::from_utf8(request["file"].text().value_or(""));
    if (operation == "timeline.check") return check(answer, file);
    if (operation == "timeline.at") return at(answer, file, request);
    if (operation == "timeline.edit") return edit(answer, file, request);
    if (operation == "timeline.undo") return undo(answer, file);
}

}  // namespace hanim::timeline::tool
