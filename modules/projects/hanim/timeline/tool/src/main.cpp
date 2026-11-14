// hanim-timeline — the timeline every editor shares (hanim.timeline) without a GUI, for people, agents and CI.
//
//   hanim-timeline check FILE                   the timeline (rate, size, tracks, markers, length),
//                                               or every problem at its line and column
//   hanim-timeline at FILE --time T             what plays at T: each clip, where in its source, at what level
//   hanim-timeline edit FILE EDIT --track N --clip I [--at T] [--length T] [--to-track N] [--dry-run]
//                                               EDIT: split, trim, move, remove, ripple-remove; the file
//                                               written as the edit leaves it, the text before kept
//   hanim-timeline undo FILE                    the last edit taken back (the text before it restored)
//
// Times are the file's: 90f, 1.5s, 250ms, 123fl. An edit's history is a
// sidecar beside the file (.<file>.history.omui.jsonl, the last 64). One JSON
// answer on standard output; exit 0 done, 1 refused or failed (why), 2 a
// wrong command.
import std;
import hanim.timeline;
import mlibs.time;
import mcpplibs.os;
import mcpplibs.cmdline;
import nlohmann.json;

namespace {

namespace tl = hanim::timeline;
namespace cmdline = mcpplibs::cmdline;
namespace os = mcpplibs::os;
using Json = nlohmann::json;

constexpr std::string_view usage =
    "usage: hanim-timeline check FILE | at FILE --time T | "
    "edit FILE split|trim|move|remove|ripple-remove --track N --clip I [--at T] [--length T] [--to-track N] [--dry-run] | "
    "undo FILE";
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

struct Options {
    std::optional<std::string> time, at, length;
    std::optional<std::size_t> track, clip, to_track;
    bool dry_run = false;
};
Options options_of(const cmdline::ParsedArgs& args) {
    Options o;
    o.dry_run = args.is_flag_set("dry-run");
    o.time = args.value("time");
    o.at = args.value("at");
    o.length = args.value("length");
    o.track = args.value_as<std::size_t>("track");
    o.clip = args.value_as<std::size_t>("clip");
    o.to_track = args.value_as<std::size_t>("to-track");
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
    out["undo"] = std::format("hanim-timeline undo {}", utf8(file));
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

// The command line (mcpplibs.cmdline): check | at | edit | undo.
cmdline::App command_line() {
    using cmdline::App, cmdline::Arg, cmdline::Option;
    const auto file = Arg("file").required().help("the timeline file");
    return App("hanim-timeline")
        .description("A timeline without a GUI — checked, sampled, edited, an edit taken back: one JSON answer, exit 0 done / 1 refused or failed / 2 a wrong command")
        .subcommand(App("check").description("the timeline, or every problem at its line and column").arg(file))
        .subcommand(App("at")
                        .description("what plays at a time: each clip, where in its source, at what level")
                        .arg(file)
                        .option(Option("time").takes_value().value_name("T").help("90f, 1.5s, 250ms, 123fl")))
        .subcommand(App("edit")
                        .description("an edit, the file written as it leaves it, the text before kept")
                        .arg(file)
                        .arg(Arg("edit").required().help("split, trim, move, remove or ripple-remove"))
                        .option(Option("track").value_type<std::size_t>().help("a track's index (0: the first)"))
                        .option(Option("clip").value_type<std::size_t>().help("a clip's index on its track"))
                        .option(Option("at").takes_value().value_name("T").help("where the edit is"))
                        .option(Option("length").takes_value().value_name("T").help("a trim's length"))
                        .option(Option("to-track").value_type<std::size_t>().help("a move's track"))
                        .option(Option("dry-run").help("say what it would do, write nothing")))
        .subcommand(App("undo").description("the last edit taken back").arg(file));
}

}  // namespace

int main(int argc, char** argv) {
    const cmdline::App app = command_line();
    auto parsed = app.parse(argc, argv);
    if (!parsed) return parsed.error().is_error() ? wrong(parsed.error().message) : 0;
    if (!parsed->has_subcommand()) return wrong("hanim-timeline <command> FILE");
    const std::string command(parsed->subcommand_name());
    const cmdline::ParsedArgs& args = parsed->subcommand()->get();
    const auto file = os::fs::from_utf8(args.positional(0));
    if (command == "check") return check(file);
    if (command == "undo") return undo(file);
    if (command == "at") return at(file, options_of(args));
    return edit(file, args.positional(1), options_of(args));
}
