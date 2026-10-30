// openmui.showcase.promo_video:film — the film as data: its cuts (a
// timeline each, *.timeline.omui.toml), its lines (*.subtitles.omui.toml),
// the files they name, and which clip shows at a frame.
//
// A cut's picture track holds two kinds of clip: `shot:<name>`, a live
// openmui scene drawn from its own time (src/shots), and `film`, the cut's
// own frames played back at the clip's speed (whole frames: the speed a
// whole number) — the rewind. Its subtitle track's clips are
// `line:<id>`, its sound tracks music files and `sound:<name>` cues.
export module openmui.showcase.promo_video:film;

import std;
import openmui.timeline;

export namespace omui::promo {

namespace tl = omui::timeline;

// What is wrong with a file, where.
struct Problem {
    std::string file;
    int         line = 0, column = 0;
    std::string message;
    [[nodiscard]] std::string text() const;
};

// Where the film's files come from: those the program carries (by their
// paths in showcase/promo-video), or a directory of edited copies, which
// wins for every file it has.
class Files {
public:
    Files();
    explicit Files(std::filesystem::path directory);
    [[nodiscard]] std::expected<std::string, std::string> read(std::string_view path) const;
    [[nodiscard]] std::vector<std::string> list(std::string_view directory) const;  // the carried files under a directory, by name
    // Every carried file's text as read now (an edited copy where there is
    // one), but for those whose names end in one of `leaving_out`, hashed.
    [[nodiscard]] std::uint64_t digest(std::span<const std::string_view> leaving_out) const;
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }

private:
    std::filesystem::path                                  directory_;
    std::map<std::string, std::string_view, std::less<>> carried_;
};

// A line, in both languages.
struct Line {
    std::string id, zh, en;
};
struct Subtitles {
    std::string       doc;
    std::vector<Line> lines;
    [[nodiscard]] const Line* find(std::string_view id) const noexcept;
};
[[nodiscard]] std::expected<Subtitles, std::vector<Problem>> read_subtitles(std::string_view text, std::string_view file);

// A cut: its timeline as read, and its text as written (the end card shows it).
struct Cut {
    std::string  name;  // "promo", "short"
    std::string  text;
    tl::Timeline timeline;
    // The picture track, the subtitle track, the sound effects' track (none: −1).
    int picture = -1, subtitles = -1, sfx = -1;
    [[nodiscard]] std::int64_t frames() const noexcept;
    [[nodiscard]] double seconds_of(std::int64_t frame) const noexcept;
    [[nodiscard]] tl::Flicks time_of(std::int64_t frame) const noexcept { return tl::frame_time(timeline.rate, frame); }
};

// A cut by name (`promo` reads promo.timeline.omui.toml), checked: the
// timeline's own rules, a picture track, every picture clip a shot this
// program has or `film` (a whole-number speed, its frames earlier than
// itself), every line one the subtitles have.
[[nodiscard]] std::expected<Cut, std::vector<Problem>> load_cut(const Files& files, std::string_view name, const Subtitles& subtitles);
// The cuts the program carries.
inline constexpr std::array<std::string_view, 2> cut_names{"promo", "short"};

// What the picture shows at a frame: the clip, and where in its source
// (a shot's own time; the cut's own time for `film`).
struct Shown {
    const tl::Clip* clip = nullptr;
    std::size_t     index = 0;     // in the picture track
    tl::Flicks      source = 0;
    float           level = 1;     // its fades
    bool            film = false;
    [[nodiscard]] std::string_view shot() const noexcept;  // "origin" for shot:origin
    [[nodiscard]] double seconds() const noexcept { return tl::to_seconds(source); }
};
[[nodiscard]] std::optional<Shown> shown(const Cut& cut, std::int64_t frame);

// The lines up at a frame (the subtitle track's clips that cover it).
struct Showing {
    const Line*     line = nullptr;
    const tl::Clip* clip = nullptr;
};
[[nodiscard]] std::vector<Showing> lines_at(const Cut& cut, const Subtitles& subtitles, std::int64_t frame);

// A chapter: its name and its first frame (the cut's `chapter:` markers).
struct Chapter {
    std::string  name;
    std::int64_t frame = 0;
};
[[nodiscard]] std::vector<Chapter> chapters(const Cut& cut);

// The shots this program draws.
inline constexpr std::array<std::string_view, 7> shot_names{"origin", "code", "infinite", "styles", "agent", "time", "end"};

}  // namespace omui::promo
