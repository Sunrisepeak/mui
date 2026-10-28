module openmui.fuzzy;

import std;
import openmui.unicode;

namespace omui::fuzzy {

namespace {

namespace u = omui::unicode;

enum class Class : std::uint8_t { white, delimiter, nonword, lower, upper, digit, letter };

Class class_of(char32_t cp) {
    if (cp < 0x80) {
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') return Class::white;
        if (cp == '/' || cp == '\\' || cp == ':' || cp == ';' || cp == ',' || cp == '|') return Class::delimiter;
        if (cp >= 'a' && cp <= 'z') return Class::lower;
        if (cp >= 'A' && cp <= 'Z') return Class::upper;
        if (cp >= '0' && cp <= '9') return Class::digit;
        return Class::nonword;
    }
    switch (u::word_class(cp)) {
        case u::Wb::WSegSpace:
        case u::Wb::Newline: return Class::white;
        case u::Wb::Numeric: return Class::digit;
        case u::Wb::ALetter:
        case u::Wb::Hebrew_Letter:
            if (u::simple_lower(cp) != cp) return Class::upper;
            if (u::simple_upper(cp) != cp) return Class::lower;
            return Class::letter;
        case u::Wb::Katakana:
        case u::Wb::Other:
            // Ideographs and other scripts without case: letters, unless
            // they are punctuation the word rules set apart.
            return u::line_class(cp) == u::Lb::ID || u::line_class(cp) == u::Lb::AL ? Class::letter : Class::nonword;
        default: return Class::nonword;
    }
}

bool word(Class c) { return c == Class::lower || c == Class::upper || c == Class::digit || c == Class::letter; }

// The bonus for a match at a character after one of class `prev`.
std::int32_t bonus(Class prev, Class cur) {
    if (word(cur)) {
        if (prev == Class::white) return score::boundary_white;
        if (prev == Class::delimiter) return score::boundary_delimiter;
        if (prev == Class::nonword) return score::boundary;
        if (prev == Class::lower && cur == Class::upper) return score::camel;
        if (prev != Class::digit && cur == Class::digit) return score::camel;
        return 0;
    }
    if (cur == Class::white) return score::boundary_white;
    return score::boundary;  // punctuation in the query is meant
}

// A candidate as the matcher reads it.
struct Text {
    std::vector<char32_t>    cps;
    std::vector<char32_t>    folded;
    std::vector<std::int32_t> bonus;
    std::vector<std::size_t> offsets;  // the byte offset of each code point, then the end
};

void read(std::string_view s, Text& t) {
    t.cps.clear();
    t.folded.clear();
    t.bonus.clear();
    t.offsets.clear();
    Class prev = Class::white;  // the start of a candidate is a word start
    for (std::size_t at = 0; at < s.size();) {
        const u::Decoded d = u::decode(s, at);
        const Class c = class_of(d.cp);
        t.cps.push_back(d.cp);
        t.folded.push_back(u::simple_lower(d.cp));
        t.bonus.push_back(bonus(prev, c));
        t.offsets.push_back(at);
        prev = c;
        at += d.length;
    }
    t.offsets.push_back(s.size());
}

struct Term {
    std::vector<char32_t> cps;
    bool                  exact = false;  // smart case: it has an uppercase letter
};

std::vector<Term> terms_of(std::string_view query) {
    std::vector<Term> out;
    Term t;
    for (std::size_t at = 0; at < query.size();) {
        const u::Decoded d = u::decode(query, at);
        at += d.length;
        if (d.cp == ' ') {
            if (!t.cps.empty()) out.push_back(std::move(t));
            t = Term{};
            continue;
        }
        if (u::simple_lower(d.cp) != d.cp) t.exact = true;
        t.cps.push_back(d.cp);
    }
    if (!t.cps.empty()) out.push_back(std::move(t));
    for (Term& x : out)
        if (!x.exact)
            for (char32_t& cp : x.cps) cp = u::simple_lower(cp);
    return out;
}

constexpr std::int32_t none = std::numeric_limits<std::int32_t>::min() / 4;

// Reused per thread: the dynamic programme's tables.
struct Scratch {
    std::vector<std::int32_t> h;      // best score with term[i] at candidate[j]
    std::vector<std::int32_t> from;   // where term[i - 1] was, for that score
    std::vector<std::int32_t> chunk;  // the bonus where that run of matches began
    Text                      text;
};

// The best alignment of one term: its score, and the positions (appended).
std::optional<std::int32_t> align(const Term& term, const Text& t, Scratch& s, std::vector<std::uint32_t>& positions) {
    const std::vector<char32_t>& c = term.exact ? t.cps : t.folded;
    const std::size_t n = term.cps.size(), m = c.size();
    if (n > m) return std::nullopt;
    // A subsequence at all? (Most candidates are not; this is the fast no.)
    for (std::size_t i = 0, j = 0; i < n; ++j) {
        if (j == m) return std::nullopt;
        if (c[j] == term.cps[i]) ++i;
    }
    s.h.assign(n * m, none);
    s.from.assign(n * m, -1);
    s.chunk.assign(n * m, 0);
    for (std::size_t i = 0; i < n; ++i) {
        // carry: the best h[i - 1][k] for k ≤ j - 2, less the gap's extension
        // beyond its first skipped character.
        std::int32_t carry = none;
        std::int32_t carry_at = -1;
        for (std::size_t j = i; j < m; ++j) {
            if (i > 0 && j >= 2) {
                if (carry != none) carry -= score::gap_extension;
                const std::int32_t k = s.h[(i - 1) * m + (j - 2)];
                if (k != none && k >= carry) {  // ≥: the nearer one, the shorter gap
                    carry = k;
                    carry_at = static_cast<std::int32_t>(j - 2);
                }
            }
            if (c[j] != term.cps[i]) continue;
            const std::int32_t b = t.bonus[j];
            std::int32_t best = none, at = -1, chunk = b;
            if (i == 0) {
                best = score::match + b * score::first_multiplier;
            } else {
                // A run carries the bonus of where it began: `abc` at a word
                // start is worth its start three times, as a person reads it.
                if (const std::int32_t prev = s.h[(i - 1) * m + (j - 1)]; prev != none) {
                    const std::int32_t began = s.chunk[(i - 1) * m + (j - 1)];
                    best = prev + score::match + std::max({b, score::consecutive, began});
                    at = static_cast<std::int32_t>(j - 1);
                    chunk = std::max(b, began);
                }
                if (carry != none) {
                    const std::int32_t gapped = carry - score::gap_start + score::match + b;
                    if (gapped > best) {
                        best = gapped;
                        at = carry_at;
                        chunk = b;
                    }
                }
            }
            s.h[i * m + j] = best;
            s.from[i * m + j] = at;
            s.chunk[i * m + j] = chunk;
        }
    }
    std::int32_t best = none;
    std::size_t end = 0;
    for (std::size_t j = n - 1; j < m; ++j)
        if (s.h[(n - 1) * m + j] > best) {  // >: the earliest end of the best
            best = s.h[(n - 1) * m + j];
            end = j;
        }
    if (best == none) return std::nullopt;
    const std::size_t first = positions.size();
    std::int32_t j = static_cast<std::int32_t>(end);
    for (std::size_t i = n; i-- > 0;) {
        positions.push_back(static_cast<std::uint32_t>(j));
        j = s.from[i * m + static_cast<std::size_t>(j)];
    }
    std::reverse(positions.begin() + static_cast<std::ptrdiff_t>(first), positions.end());
    return best;
}

std::optional<Match> score_one(const std::vector<Term>& terms, std::string_view candidate, std::uint32_t index, Scratch& s, bool segments) {
    read(candidate, s.text);
    Match out;
    out.index = index;
    for (const Term& term : terms) {
        auto r = align(term, s.text, s, out.positions);
        if (!r) return std::nullopt;
        out.score += *r;
    }
    std::ranges::sort(out.positions);
    const auto [last, end] = std::ranges::unique(out.positions);
    out.positions.erase(last, end);
    if (segments) {
        std::size_t p = 0;
        for (std::size_t i = 0; i < s.text.cps.size();) {
            const bool matched = p < out.positions.size() && out.positions[p] == i;
            std::size_t j = i;
            while (j < s.text.cps.size() && (p < out.positions.size() && out.positions[p] == j) == matched) {
                if (matched) ++p;
                ++j;
            }
            out.segments.push_back({std::string(candidate.substr(s.text.offsets[i], s.text.offsets[j] - s.text.offsets[i])), matched});
            i = j;
        }
    }
    return out;
}

// Best first: score, then the shorter candidate, then the earlier.
bool before(const Match& a, const Match& b, std::span<const std::string> candidates) {
    if (a.score != b.score) return a.score > b.score;
    const std::size_t la = candidates[a.index].size(), lb = candidates[b.index].size();
    if (la != lb) return la < lb;
    return a.index < b.index;
}

}  // namespace

std::optional<Match> match(std::string_view query, std::string_view candidate) {
    Scratch s;
    return score_one(terms_of(query), candidate, 0, s, true);
}

std::vector<Match> rank(std::string_view query, std::span<const std::string> candidates, std::size_t limit) {
    const std::vector<Term> terms = terms_of(query);
    const std::size_t count = candidates.size();
    if (terms.empty()) {
        // Nothing asked: the list as it is.
        std::vector<Match> all;
        Scratch s;
        for (std::size_t i = 0; i < count && (!limit || i < limit); ++i)
            all.push_back(*score_one(terms, candidates[i], static_cast<std::uint32_t>(i), s, true));
        return all;
    }
    // Scored in chunks, on as many threads as help (one for a short list).
    constexpr std::size_t per_thread = 2048;
    const std::size_t threads = std::clamp<std::size_t>(count / per_thread, 1, std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::vector<Match>> found(threads);
    auto work = [&](std::size_t t) {
        Scratch s;
        for (std::size_t i = t * count / threads; i < (t + 1) * count / threads; ++i)
            if (auto m = score_one(terms, candidates[i], static_cast<std::uint32_t>(i), s, false)) found[t].push_back(std::move(*m));
    };
    if (threads == 1) {
        work(0);
    } else {
        std::vector<std::jthread> pool;
        for (std::size_t t = 0; t < threads; ++t) pool.emplace_back(work, t);
    }
    std::vector<Match> all;
    for (auto& f : found) std::ranges::move(f, std::back_inserter(all));
    auto order = [&](const Match& a, const Match& b) { return before(a, b, candidates); };
    if (limit && limit < all.size()) {
        std::ranges::partial_sort(all, all.begin() + static_cast<std::ptrdiff_t>(limit), order);
        all.resize(limit);
    } else {
        std::ranges::sort(all, order);
    }
    // The segments of the ones shown only.
    Scratch s;
    for (Match& m : all) m = *score_one(terms, candidates[m.index], m.index, s, true);
    return all;
}

}  // namespace omui::fuzzy
