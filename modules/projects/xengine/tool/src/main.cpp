// xengine — xengine's shared editing pieces without a GUI, for people, agents and CI.
//
//   xengine timeline check FILE                 the timeline (rate, size, tracks, markers, length),
//                                               or every problem at its line and column
//   xengine timeline at FILE --time T           what plays at T: each clip, where in its source, at what level
//   xengine timeline edit FILE EDIT --track N --clip I [--at T] [--length T] [--to-track N] [--dry-run]
//                                               EDIT: split, trim, move, remove, ripple-remove; the file
//                                               written as the edit leaves it, the text before kept
//   xengine timeline undo FILE                  the last edit taken back (the text before it restored)
//
// Times are the file's: 90f, 1.5s, 250ms, 123fl. An edit's history is a
// sidecar beside the file (.<file>.history.omui.jsonl, the last 64). One JSON
// answer on standard output; exit 0 done, 1 refused or failed (why), 2 a
// wrong command.
import std;
import xengine.timeline;
import mlibs.time;
import mcpplibs.os;
import nlohmann.json;

namespace {

namespace tl = xengine::timeline;
namespace os = mcpplibs::os;
using Json = nlohmann::json;

constexpr std::string_view usage =
    "usage: xengine timeline check FILE | timeline at FILE --time T | "
    "timeline edit FILE split|trim|move|remove|ripple-remove --track N --clip I [--at T] [--length T] [--to-track N] [--dry-run] | "
    "timeline undo FILE";
constexpr std::size_t history_kept = 64;

int answer(const Json& json, int code) {
    std::println("{}", json.dump());
    return code;
}
int wrong(std::string_view text) { return answer(Json{{"ok", false}, {"error", text}, {"usage", usage}}, 2); }
int failed(std::string_view text) { return answer(Json{{"ok", false}, {"error", text}}, 1); }

std::string utf8(const std::filesystem::path& path) {
    const std::u8string u = path.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}
std::filesystem::path history_of(const std::filesystem::path& file) {
    return file.parent_path() / os::fs::from_utf8("." + utf8(file.filename()) + ".history.omui.jsonl");
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
            problems.push_back({{"line", p.line}, {"column", p.column}, {"message", p.message}, {"hint", p.hint}});
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

struct Options {
    std::optional<std::string> time, at, length;
    std::optional<std::size_t> track, clip, to_track;
    bool dry_run = false;
};
std::expected<Options, std::string> options_of(int first, int argc, char** argv) {
    Options o;
    for (int index = first; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--dry-run") {
            o.dry_run = true;
            continue;
        }
        if (index + 1 >= argc) return std::unexpected(std::format("{} takes a value", option));
        const std::string_view value = argv[++index];
        const auto count = [&](std::optional<std::size_t>& out) -> bool {
            std::size_t n = 0;
            const auto r = std::from_chars(value.data(), value.data() + value.size(), n);
            if (r.ec != std::errc{} || r.ptr != value.data() + value.size()) return false;
            out = n;
            return true;
        };
        if (option == "--time") o.time = std::string(value);
        else if (option == "--at") o.at = std::string(value);
        else if (option == "--length") o.length = std::string(value);
        else if (option == "--track") { if (!count(o.track)) return std::unexpected("--track takes a track's index (0: the first)"); }
        else if (option == "--clip") { if (!count(o.clip)) return std::unexpected("--clip takes a clip's index on its track (0: the first)"); }
        else if (option == "--to-track") { if (!count(o.to_track)) return std::unexpected("--to-track takes a track's index"); }
        else return std::unexpected(std::format("no option `{}` here", option));
    }
    return o;
}

int check(const std::filesystem::path& file) {
    auto opened = open(file);
    if (!opened) return answer(opened.error(), 1);
    const auto broken = tl::check(opened->timeline);
    Json out = summary(opened->timeline);
    out["ok"] = broken.empty();
    if (!broken.empty()) out["problems"] = broken;
    return answer(out, broken.empty() ? 0 : 1);
}

int at(const std::filesystem::path& file, const Options& o) {
    if (!o.time) return wrong("timeline at takes --time T");
    auto opened = open(file);
    if (!opened) return answer(opened.error(), 1);
    const auto& t = opened->timeline;
    const auto time = tl::parse_time(*o.time, t.rate);
    if (!time) return wrong(std::format("`{}` is not a time (90f, 1.5s, 250ms, 123fl)", *o.time));
    std::vector<tl::Playing> now;
    tl::playing(t, *time, now);
    Json list = Json::array();
    for (const auto& p : now) {
        const auto& clip = t.tracks[p.track].clips[p.clip];
        list.push_back({{"track", t.tracks[p.track].name}, {"clip", clip.name}, {"source", clip.source}, {"source_at", tl::time_text(p.source, t.rate)},
                        {"level", p.level}});
    }
    return answer(Json{{"ok", true}, {"time", tl::time_text(*time, t.rate)}, {"playing", std::move(list)}}, 0);
}

int edit(const std::filesystem::path& file, std::string_view kind, const Options& o) {
    auto opened = open(file);
    if (!opened) return answer(opened.error(), 1);
    auto& t = opened->timeline;
    if (!o.track || !o.clip) return wrong("an edit names its --track and --clip");
    if (*o.track >= t.tracks.size() || *o.clip >= t.tracks[*o.track].clips.size())
        return failed(std::format("no clip {} on track {}", *o.clip, *o.track));
    const auto time = [&](const std::optional<std::string>& text, std::string_view what) -> std::expected<mlibs::time::Flicks, std::string> {
        if (!text) return std::unexpected(std::format("{} takes --{}", kind, what));
        const auto parsed = tl::parse_time(*text, t.rate);
        if (!parsed) return std::unexpected(std::format("`{}` is not a time (90f, 1.5s, 250ms, 123fl)", *text));
        return *parsed;
    };
    std::expected<tl::Change, std::string> change = std::unexpected(std::string("no edit"));
    if (kind == "split") {
        const auto when = time(o.at, "at");
        if (!when) return wrong(when.error());
        change = tl::split(t, *o.track, *o.clip, *when);
    } else if (kind == "trim") {
        const auto when = time(o.at, "at");
        const auto length = time(o.length, "length");
        if (!when) return wrong(when.error());
        if (!length) return wrong(length.error());
        change = tl::trim(t, *o.track, *o.clip, *when, *length);
    } else if (kind == "move") {
        const auto when = time(o.at, "at");
        if (!when) return wrong(when.error());
        change = tl::move(t, *o.track, *o.clip, o.to_track.value_or(*o.track), *when);
    } else if (kind == "remove") {
        change = tl::remove(t, *o.track, *o.clip);
    } else if (kind == "ripple-remove") {
        change = tl::ripple_remove(t, *o.track, *o.clip);
    } else {
        return wrong(std::format("no edit `{}` (split, trim, move, remove, ripple-remove)", kind));
    }
    if (!change) return failed(change.error());
    auto undone = tl::apply(t, *change);
    if (!undone) return failed(undone.error());
    const std::string after = tl::write_timeline(t);
    Json out{{"ok", true}, {"edit", change->what}, {"dry_run", o.dry_run}, {"timeline", summary(t)}};
    if (o.dry_run) return answer(out, 0);
    // The text before, kept: `undo` puts it back.
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
    if (!os::fs::write_atomic(history, kept)) return failed(std::format("cannot keep the history in {}", utf8(history)));
    if (!os::fs::write_atomic(file, after)) return failed(std::format("cannot write {}", utf8(file)));
    out["undo"] = std::format("xengine timeline undo {}", utf8(file));
    return answer(out, 0);
}

int undo(const std::filesystem::path& file) {
    const auto history = history_of(file);
    auto old = os::fs::read(history);
    std::vector<std::string> lines;
    if (old) {
        std::istringstream in(*old);
        for (std::string line; std::getline(in, line);)
            if (!line.empty()) lines.push_back(line);
    }
    if (lines.empty()) return failed("nothing to take back: no edit made by this command is kept");
    const Json last = Json::parse(lines.back(), nullptr, false);
    if (last.is_discarded() || !last.contains("before") || !last["before"].is_string()) return failed(std::format("{} is damaged", utf8(history)));
    lines.pop_back();
    std::string kept;
    for (const auto& line : lines) kept += line + "\n";
    if (!os::fs::write_atomic(file, last["before"].get<std::string>()) || !os::fs::write_atomic(history, kept))
        return failed(std::format("cannot write {}", utf8(file)));
    return answer(Json{{"ok", true}, {"undone", last.value("edit", std::string())}, {"left", lines.size()}}, 0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4 || std::string_view(argv[1]) != "timeline") return wrong("xengine timeline <command> FILE");
    const std::string_view command = argv[2];
    const auto file = os::fs::from_utf8(argv[3]);
    if (command == "check" && argc == 4) return check(file);
    if (command == "undo" && argc == 4) return undo(file);
    if (command == "at") {
        auto o = options_of(4, argc, argv);
        return o ? at(file, *o) : wrong(o.error());
    }
    if (command == "edit" && argc >= 5) {
        auto o = options_of(5, argc, argv);
        return o ? edit(file, argv[4], *o) : wrong(o.error());
    }
    return wrong(std::format("no command `{}`", command));
}
