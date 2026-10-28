// openmui.fuzzy — fuzzy matching as a finder does it.
//
//     auto best = fuzzy::rank("mcfg", {"main.cpp", "my_config.toml", "src/mcpp/config.cppm"}, 10);
//     // my_config.toml (m·c at word starts), then src/mcpp/config.cppm …
//
// A query matches a candidate when its characters appear in it in order. The
// best alignment is found by dynamic programming (as fzf's), scoring a match
// higher at the start of a word, a camel hump or a path segment, and in runs,
// and lower across gaps. Lowercase queries match either case; a query with an
// uppercase letter matches case exactly (smart case). Words of the query,
// separated by spaces, must all match. Everything is integer arithmetic over
// code points: the same input gives the same output on every platform.
export module openmui.fuzzy;

import std;

export namespace omui::fuzzy {

// A run of the candidate, matched or not: what a view highlights.
struct Segment {
    std::string text;
    bool        matched = false;
};

struct Match {
    std::uint32_t              index = 0;  // the candidate's position in the list
    std::int32_t               score = 0;  // higher is better
    std::vector<std::uint32_t> positions;  // the matched characters (code point indices), ascending
    std::vector<Segment>       segments;   // the candidate as runs of matched and unmatched text
};

// How a query lines up with one candidate; nothing when it does not match.
// An empty query matches everything with score 0.
[[nodiscard]] std::optional<Match> match(std::string_view query, std::string_view candidate);

// The candidates the query matches, best first: by score, then the shorter,
// then the earlier; for an empty query, all of them in the list's order. At
// most `limit` of them (0: all). Large lists are scored on several threads;
// the answer is the same.
[[nodiscard]] std::vector<Match> rank(std::string_view query, std::span<const std::string> candidates, std::size_t limit);

// The scoring, for tests and documentation.
namespace score {
inline constexpr std::int32_t match = 16;           // each matched character
inline constexpr std::int32_t gap_start = 3;        // a gap's first skipped character
inline constexpr std::int32_t gap_extension = 1;    // each further one
inline constexpr std::int32_t boundary_white = 10;  // after a space, or at the start
inline constexpr std::int32_t boundary_delimiter = 9;  // after / \ : ; , |
inline constexpr std::int32_t boundary = 8;         // after other punctuation (_ - .)
inline constexpr std::int32_t camel = 7;            // a lower-to-upper hump, or a letter-to-digit one
inline constexpr std::int32_t consecutive = 4;      // a run continues: at least this, or the bonus where the run began
inline constexpr std::int32_t first_multiplier = 2; // the query's first character weighs its bonus twice
}  // namespace score

}  // namespace omui::fuzzy
