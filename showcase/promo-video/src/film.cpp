// The film as data: the files, the cuts, what shows at a frame.
module omui.showcase.promo_video;

import std;
import mlibs.hash;
import hanim.timeline;
import omui.lang.ts;

namespace omui::promo {

std::string Problem::text() const {
    if (line > 0) return std::format("{}:{}:{}: {}", file, line, column, message);
    return std::format("{}: {}", file, message);
}

// ── files ───────────────────────────────────────────────────────────────

Files::Files() {
    for (const auto& [name, text] : files::timeline_files) carried_.emplace(std::string(name), text);
    for (const auto& [name, text] : files::subtitles_files) carried_.emplace(std::string(name), text);
    for (const auto& [name, text] : files::style_files) carried_.emplace(std::string(name), text);
    for (const auto& [name, text] : files::music_files) carried_.emplace(std::format("music/{}", name), text);
    for (const auto& [name, text] : files::sound_files) carried_.emplace(std::format("sounds/{}", name), text);
    for (const auto& [name, text] : files::page_files) carried_.emplace(std::format("script/{}", name), text);
    using Table = std::span<const std::pair<std::string_view, std::string_view>>;
    for (const Table table : {Table(files::ts_files), Table(files::cpp_files)})
        for (const auto& [name, text] : table) carried_.emplace(std::format("code/{}", name), text);
}

Files::Files(std::filesystem::path directory) : Files() { directory_ = std::move(directory); }

std::uint64_t Files::digest(std::span<const std::string_view> leaving_out) const {
    mlibs::hash::Fnv1a h;
    for (const auto& [name, carried] : carried_) {
        if (std::ranges::any_of(leaving_out, [&](std::string_view end) { return std::string_view(name).ends_with(end); })) continue;
        h.str(name);
        if (auto text = read(name)) h.str(*text);
    }
    return h.value();
}

std::string snapshot_name(std::string_view path) {
    std::string name(path);
    if (name.ends_with(".omui.ts")) name = name.substr(0, name.size() - std::string_view(".ts").size()) + ".json";
    return name;
}

std::expected<std::string, std::string> Files::read(std::string_view path) const {
    // A data module (`music/promo.music.omui.ts`) is its JSON snapshot: carried as
    // `….omui.json`; in a directory of edited copies, the snapshot there, else
    // the module there evaluated (D-139).
    const std::string name = snapshot_name(path);
    if (!directory_.empty()) {
        const auto at = [&](std::string_view relative) { return directory_ / std::filesystem::path(std::u8string(relative.begin(), relative.end())); };
        std::ifstream in(at(name), std::ios::binary);
        if (in) return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (name.ends_with(".omui.json")) {
            const auto module = at(name.substr(0, name.size() - std::string_view(".json").size()) + ".ts");
            std::error_code error;
            if (std::filesystem::is_regular_file(module, error)) {
                auto data = lang::ts::evaluate(module);
                if (!data) return std::unexpected(data.error());
                return std::format("{{\"{}\":{}}}", data->kind, data->json);
            }
        }
    }
    if (const auto it = carried_.find(name); it != carried_.end()) return std::string(it->second);
    return std::unexpected(std::format("no file `{}`", path));
}

std::vector<std::string> Files::list(std::string_view directory) const {
    std::vector<std::string> out;
    const std::string prefix = directory.empty() ? std::string() : std::format("{}/", directory);
    for (const auto& [name, text] : carried_)
        if (name.starts_with(prefix) && name.find('/', prefix.size()) == std::string::npos) out.push_back(name.substr(prefix.size()));
    return out;
}

const Line* Subtitles::find(std::string_view id) const noexcept {
    for (const Line& l : lines)
        if (l.id == id) return &l;
    return nullptr;
}

// ── cuts ────────────────────────────────────────────────────────────────

std::int64_t Cut::frames() const noexcept { return timeline.rate.frame_at(timeline.duration() + timeline.rate.frame_length() - 1); }

double Cut::seconds_of(std::int64_t frame) const noexcept { return mlibs::time::to_seconds(timeline.rate.frame_time(frame)); }

std::string_view Shown::shot() const noexcept {
    if (!clip) return {};
    std::string_view s = clip->source;
    return s.starts_with("shot:") ? s.substr(5) : std::string_view{};
}

std::expected<Cut, std::vector<Problem>> load_cut(const Files& files, std::string_view name, const Subtitles& subtitles) {
    const std::string file = std::format("{}.timeline.omui.json", name);  // what promo.timeline.omui.ts evaluates to
    auto text = files.read(file);
    if (!text) return std::unexpected(std::vector<Problem>{{file, 0, 0, text.error()}});
    auto t = tl::read_timeline(*text, file);
    if (!t) {
        std::vector<Problem> out;
        for (const auto& p : t.error()) out.push_back({p.file, p.line, p.column, p.hint.empty() ? p.message : std::format("{} ({})", p.message, p.hint)});
        return std::unexpected(std::move(out));
    }
    Cut c;
    c.name = std::string(name);
    c.text = std::move(*text);
    c.timeline = std::move(*t);
    std::vector<Problem> out;
    for (const std::string& s : tl::check(c.timeline)) out.push_back({file, 0, 0, s});
    for (std::size_t i = 0; i < c.timeline.tracks.size(); ++i) {
        const tl::Track& tr = c.timeline.tracks[i];
        if (tr.kind == tl::Kind::video && c.picture < 0) c.picture = static_cast<int>(i);
        if (tr.kind == tl::Kind::events && tr.name == "subtitles") c.subtitles = static_cast<int>(i);
        if (tr.kind == tl::Kind::events && tr.name == "sfx") c.sfx = static_cast<int>(i);
    }
    if (c.picture < 0) out.push_back({file, 0, 0, "the cut has no picture track (a track of kind `video`)"});
    const mlibs::time::Flicks frame = c.timeline.rate.frame_length();
    for (const tl::Track& tr : c.timeline.tracks)
        for (const tl::Clip& clip : tr.clips) {
            const std::string where = std::format("track `{}`, clip `{}`", tr.name, clip.name);
            if (clip.at % frame != 0 || clip.length % frame != 0 || clip.in % frame != 0)
                out.push_back({file, 0, 0, std::format("{}: starts, lasts and begins its source on whole frames", where)});
            if (tr.kind == tl::Kind::video) {
                if (clip.source == "film") {
                    if (clip.speed != std::round(clip.speed))
                        out.push_back({file, 0, 0, std::format("{}: the film plays back at a whole number of frames a frame (speed {})", where, clip.speed)});
                    // Every frame it plays back lies before it, so drawing it never comes round to itself.
                    const mlibs::time::Flicks first = clip.source_at(clip.at), last = clip.source_at(clip.end() - frame);
                    if (std::max(first, last) >= clip.at || std::min(first, last) < 0)
                        out.push_back({file, 0, 0, std::format("{}: plays back frames before itself, from the cut's start", where)});
                } else if (clip.source.starts_with("shot:")) {
                    const std::string_view shot = std::string_view(clip.source).substr(5);
                    if (std::ranges::find(shot_names, shot) == shot_names.end())
                        out.push_back({file, 0, 0, std::format("{}: no shot is named `{}`", where, shot)});
                    if (clip.speed != 1) out.push_back({file, 0, 0, std::format("{}: a shot plays at its own pace (speed 1)", where)});
                } else {
                    out.push_back({file, 0, 0, std::format("{}: a picture is `shot:<name>` or `film`, not `{}`", where, clip.source)});
                }
            } else if (static_cast<int>(&tr - c.timeline.tracks.data()) == c.subtitles) {
                if (!clip.source.starts_with("line:") || !subtitles.find(std::string_view(clip.source).substr(5)))
                    out.push_back({file, 0, 0, std::format("{}: `{}` is no line of the subtitles", where, clip.source)});
            } else if (static_cast<int>(&tr - c.timeline.tracks.data()) == c.sfx) {
                if (!clip.source.starts_with("sound:")) out.push_back({file, 0, 0, std::format("{}: a sound cue is `sound:<name>`", where)});
            } else if (tr.kind == tl::Kind::audio) {
                if (!files.read(clip.source)) out.push_back({file, 0, 0, std::format("{}: no file `{}`", where, clip.source)});
            }
        }
    if (!out.empty()) return std::unexpected(std::move(out));
    return c;
}

std::optional<Shown> shown(const Cut& cut, std::int64_t frame) {
    if (cut.picture < 0) return std::nullopt;
    const mlibs::time::Flicks t = cut.time_of(frame);
    const tl::Track& tr = cut.timeline.tracks[static_cast<std::size_t>(cut.picture)];
    for (std::size_t i = 0; i < tr.clips.size(); ++i) {
        const tl::Clip& c = tr.clips[i];
        if (!c.covers(t)) continue;
        Shown s;
        s.clip = &c;
        s.index = i;
        s.source = c.source_at(t);
        s.level = c.level_at(t);
        s.film = c.source == "film";
        return s;
    }
    return std::nullopt;
}

std::vector<Showing> lines_at(const Cut& cut, const Subtitles& subtitles, std::int64_t frame) {
    std::vector<Showing> out;
    if (cut.subtitles < 0) return out;
    const mlibs::time::Flicks t = cut.time_of(frame);
    for (const tl::Clip& c : cut.timeline.tracks[static_cast<std::size_t>(cut.subtitles)].clips)
        if (c.covers(t) && c.source.starts_with("line:"))
            if (const Line* l = subtitles.find(std::string_view(c.source).substr(5))) out.push_back({l, &c});
    return out;
}

std::vector<Chapter> chapters(const Cut& cut) {
    std::vector<Chapter> out;
    for (const tl::Marker& m : cut.timeline.markers)
        if (m.name.starts_with("chapter:")) out.push_back({m.name.substr(8), cut.timeline.rate.frame_at(m.at)});
    return out;
}

}  // namespace omui::promo
