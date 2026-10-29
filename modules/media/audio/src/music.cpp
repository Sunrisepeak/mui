// A piece's patterns read from their text: notes (`D5:2 F5 G5 | A5>C6:4!`)
// and drum lanes (`x..x..x.|X...`), into hits in ticks, each problem at its
// offset in the text (the file's reader turns that into a line and column).
module openmui.audio;

import std;
import :detail;

namespace omui::audio {

namespace {

constexpr std::array<std::int32_t, 7> semitone{9, 11, 0, 2, 4, 5, 7};  // A … G from C
constexpr std::array<std::string_view, 12> names{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

bool space(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// "1/8 steps", "1/16 steps": what a step is, said in a problem.
std::string steps_of(std::int32_t step) { return std::format("1/{} steps", whole_ticks / step); }

// The tick a step begins on, swung (every other step late).
std::int32_t swung(std::int32_t tick, std::int32_t step, float swing) noexcept {
    if (swing <= 0 || (tick / step) % 2 == 0) return tick;
    return tick + static_cast<std::int32_t>(std::lround(static_cast<double>(swing) * step));
}

// A stretch between bar lines (or the whole text) must be whole bars.
void whole_bars(std::int32_t ticks, std::int32_t bar, std::int32_t step, std::size_t at, std::string_view what, std::vector<detail::TextProblem>& problems) {
    if (ticks > 0 && ticks % bar == 0) return;
    problems.push_back({at, std::format("{} {} steps, not whole bars", what, ticks / step), std::format("a bar is {} {}", bar / step, steps_of(step))});
}

}  // namespace

std::int32_t detail::note_key(std::string_view s) noexcept {
    if (s.size() < 2 || s[0] < 'A' || s[0] > 'G') return -1;
    std::int32_t key = semitone[static_cast<std::size_t>(s[0] - 'A')];
    std::size_t i = 1;
    if (s[i] == '#') ++key, ++i;
    else if (s[i] == 'b') --key, ++i;
    bool negative = false;
    if (i < s.size() && s[i] == '-') negative = true, ++i;
    if (i + 1 != s.size() || s[i] < '0' || s[i] > '9') return -1;
    const std::int32_t octave = negative ? -(s[i] - '0') : s[i] - '0';
    if (octave < -1) return -1;
    key += 12 * (octave + 1);
    return key < 0 || key > 127 ? -1 : key;
}

std::string detail::note_name(std::int32_t key) {
    return std::format("{}{}", names[static_cast<std::size_t>(key % 12)], key / 12 - 1);
}

std::int32_t detail::step_ticks(const Pattern& p) noexcept {
    if (p.step > 0) return whole_ticks / p.step;
    return p.lanes.empty() ? whole_ticks / 8 : whole_ticks / 16;
}

std::int32_t detail::read_notes(std::string_view text, std::int32_t step, std::int32_t bar, float swing, std::vector<Hit>& out, std::vector<TextProblem>& problems) {
    const std::size_t before = problems.size();
    std::int32_t tick = 0, stretch = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        if (space(text[i])) {
            ++i;
            continue;
        }
        if (text[i] == '|') {
            whole_bars(tick - stretch, bar, step, i, "this stretch is", problems);
            stretch = tick;
            ++i;
            continue;
        }
        const std::size_t from = i;
        while (i < text.size() && !space(text[i]) && text[i] != '|') ++i;
        std::string_view token = text.substr(from, i - from);
        // Its loudness, its length, then what sounds.
        float velocity = 0.8f;
        if (token.ends_with('!')) velocity = 1.0f, token.remove_suffix(1);
        else if (token.ends_with('?')) velocity = 0.5f, token.remove_suffix(1);
        std::int32_t steps = 1;
        if (const std::size_t colon = token.find(':'); colon != std::string_view::npos) {
            const std::string_view n = token.substr(colon + 1);
            std::int32_t v = 0;
            const auto [end, error] = std::from_chars(n.data(), n.data() + n.size(), v);
            if (error != std::errc{} || end != n.data() + n.size() || v < 1 || v > 1024) {
                problems.push_back({from + colon + 1, std::format("`{}` is not a length", n), "a whole number of steps: `C4:3`"});
                v = 1;
            }
            steps = v;
            token = token.substr(0, colon);
        }
        if (token != ".") {
            // Notes joined by `+`, each maybe sliding (`>`) to another.
            std::size_t at = 0;
            while (at <= token.size()) {
                const std::size_t plus = std::min(token.find('+', at), token.size());
                const std::string_view member = token.substr(at, plus - at);
                const std::size_t arrow = member.find('>');
                const std::string_view played = member.substr(0, arrow);
                const std::int32_t key = note_key(played);
                std::int32_t to = -1;
                bool good = key >= 0;
                if (!good)
                    problems.push_back({from + at, std::format("`{}` is not a note", played.empty() ? token : played),
                                        "a note is a letter A–G, maybe # or b, and an octave: `C4`, `F#3`, `Bb5`; `.` is a rest"});
                if (arrow != std::string_view::npos) {
                    const std::string_view target = member.substr(arrow + 1);
                    to = note_key(target);
                    if (to < 0) {
                        problems.push_back({from + at + arrow + 1, std::format("`{}` is not a note to slide to", target), "`A4>C5` slides from A4 up to C5"});
                        good = false;
                    }
                }
                if (good) out.push_back({swung(tick, step, swing), steps * step, key, to, velocity, -1});
                at = plus + 1;
            }
        }
        tick += steps * step;
    }
    if (stretch > 0 && tick == stretch) return problems.size() == before ? tick / bar : 0;  // a bar line at the end
    whole_bars(tick - stretch, bar, step, text.size(), stretch > 0 ? "the last stretch is" : "the notes are", problems);
    return problems.size() == before ? tick / bar : 0;
}

std::int32_t detail::read_lane(std::string_view text, std::int32_t step, std::int32_t bar, float swing, std::int32_t lane, std::vector<Hit>& out,
                               std::vector<TextProblem>& problems) {
    const std::size_t before = problems.size();
    std::int32_t tick = 0, stretch = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (space(c)) continue;
        if (c == '|') {
            whole_bars(tick - stretch, bar, step, i, "this stretch is", problems);
            stretch = tick;
            continue;
        }
        float velocity = 0;
        switch (c) {
            case 'x': velocity = 0.8f; break;
            case 'X': velocity = 1.0f; break;
            case 'o': velocity = 0.45f; break;
            case '.': break;
            default:
                problems.push_back({i, std::format("`{}` is not a step", std::string_view(&text[i], 1)), "`x` a hit, `X` an accent, `o` a ghost, `.` none, `|` a bar line"});
        }
        if (velocity > 0) out.push_back({swung(tick, step, swing), step, -1, -1, velocity, lane});
        tick += step;
    }
    if (!(stretch > 0 && tick == stretch)) whole_bars(tick - stretch, bar, step, text.size(), stretch > 0 ? "the last stretch is" : "the lane is", problems);
    return problems.size() == before ? tick / bar : 0;
}

std::expected<Hits, std::vector<Problem>> hits(const Pattern& pattern, const Music& music) {
    std::vector<detail::TextProblem> problems;
    Hits h;
    const std::int32_t step = detail::step_ticks(pattern), bar = music.bar_ticks();
    std::vector<Problem> out;
    auto report = [&](std::string_view what) {
        for (const auto& p : problems) out.push_back({std::format("pattern `{}`{}", pattern.name, what), 0, static_cast<int>(p.offset) + 1, p.message, p.hint});
        problems.clear();
    };
    if (bar % step != 0) {
        out.push_back({std::format("pattern `{}`", pattern.name), 0, 0, std::format("{} do not fill a bar of {}/{}", steps_of(step), music.meter[0], music.meter[1]), {}});
        return std::unexpected(std::move(out));
    }
    if (!pattern.notes.empty()) {
        h.bars = detail::read_notes(pattern.notes, step, bar, pattern.swing, h.hits, problems);
        report("");
    }
    // Lanes: each repeated to the longest, when it divides it.
    std::vector<std::int32_t> lengths;
    std::vector<std::vector<Hit>> lanes(pattern.lanes.size());
    for (std::size_t l = 0; l < pattern.lanes.size(); ++l) {
        lengths.push_back(detail::read_lane(pattern.lanes[l].steps, step, bar, pattern.swing, static_cast<std::int32_t>(l), lanes[l], problems));
        report(std::format(" lane `{}`", pattern.lanes[l].instrument));
        h.bars = std::max(h.bars, lengths.back());
    }
    for (std::size_t l = 0; l < lanes.size(); ++l) {
        if (lengths[l] == 0) continue;
        if (h.bars % lengths[l] != 0) {
            out.push_back({std::format("pattern `{}` lane `{}`", pattern.name, pattern.lanes[l].instrument), 0, 0,
                           std::format("its {} bars do not repeat into the pattern's {}", lengths[l], h.bars), "make the longest lane a multiple of the others"});
            continue;
        }
        for (std::int32_t r = 0; r < h.bars / lengths[l]; ++r)
            for (Hit hit : lanes[l]) {
                hit.tick += r * lengths[l] * bar;
                h.hits.push_back(hit);
            }
    }
    if (!out.empty()) return std::unexpected(std::move(out));
    std::ranges::stable_sort(h.hits, {}, &Hit::tick);
    return h;
}

std::int32_t Music::bars(std::span<const std::string> list) const noexcept {
    std::int32_t n = 0;
    for (const std::string& s : list)
        if (const auto it = std::ranges::find(sections, s, &Section::name); it != sections.end()) n += it->bars;
    return n;
}

}  // namespace omui::audio
