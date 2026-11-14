// mlibs.names — the nearest name for a typo (the one implementation): what every reader of keys, columns,
// channels or operations says when a name is not one it knows.
//
//     problems.push_back(std::format("`{}` is not a key ({})", key, mlibs::names::nearest(key, keys)));
//     // → `temop` is not a key (did you mean `tempo`?)
export module mlibs.names;

import std;

export namespace mlibs::names {

// The number of edits (insert, delete, change, swap two neighbours) from a to b.
[[nodiscard]] std::size_t distance(std::string_view a, std::string_view b);
// "did you mean `x`?" for the nearest of `options` within a third of the
// name (never a whole one-letter name), else "one of `a`, `b` …".
[[nodiscard]] std::string nearest(std::string_view name, std::span<const std::string_view> options);

}  // namespace mlibs::names
