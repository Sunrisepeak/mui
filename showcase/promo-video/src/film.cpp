// The film as data: the files, the cuts, what shows at a frame.
module openmui.showcase.promo_video;

import std;
import openmui.hash;
import openmui.timeline;

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
    for (const Table table : {Table(files::ts_files), Table(files::lua_files), Table(files::py_files), Table(files::cpp_files)})
        for (const auto& [name, text] : table) carried_.emplace(std::format("code/{}", name), text);
}

Files::Files(std::filesystem::path directory) : Files() { directory_ = std::move(directory); }

std::uint64_t Files::digest(std::span<const std::string_view> leaving_out) const {
    hash::Fnv1a h;
    for (const auto& [name, carried] : carried_) {
        if (std::ranges::any_of(leaving_out, [&](std::string_view end) { return std::string_view(name).ends_with(end); })) continue;
        h.str(name);
        if (auto text = read(name)) h.str(*text);
    }
    return h.value();
}

std::expected<std::string, std::string> Files::read(std::string_view path) const {
    if (!directory_.empty()) {
        const auto file = directory_ / std::filesystem::path(std::u8string(path.begin(), path.end()));
        std::ifstream in(file, std::ios::binary);
        if (in) return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (const auto it = carried_.find(path); it != carried_.end()) return std::string(it->second);
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

std::int64_t Cut::frames() const noexcept { return tl::frame_at(timeline.rate, timeline.duration() + tl::frame_length(timeline.rate) - 1); }

double Cut::seconds_of(std::int64_t frame) const noexcept { return tl::to_seconds(tl::frame_time(timeline.rate, frame)); }

std::string_view Shown::shot() const noexcept {
    if (!clip) return {};
    std::string_view s = clip->source;
    return s.starts_with("shot:") ? s.substr(5) : std::string_view{};
}

std::expected<Cut, std::vector<Problem>> load_cut(const Files& files, std::string_view name, const Subtitles& subtitles) {
    const std::string file = std::format("{}.timeline.omui.toml", name);
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
    const tl::Flicks frame = tl::frame_length(c.timeline.rate);
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
                    const tl::Flicks first = clip.source_at(clip.at), last = clip.source_at(clip.end() - frame);
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
    const tl::Flicks t = cut.time_of(frame);
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
    const tl::Flicks t = cut.time_of(frame);
    for (const tl::Clip& c : cut.timeline.tracks[static_cast<std::size_t>(cut.subtitles)].clips)
        if (c.covers(t) && c.source.starts_with("line:"))
            if (const Line* l = subtitles.find(std::string_view(c.source).substr(5))) out.push_back({l, &c});
    return out;
}

std::vector<Chapter> chapters(const Cut& cut) {
    std::vector<Chapter> out;
    for (const tl::Marker& m : cut.timeline.markers)
        if (m.name.starts_with("chapter:")) out.push_back({m.name.substr(8), tl::frame_at(cut.timeline.rate, m.at)});
    return out;
}

}  // namespace omui::promo
