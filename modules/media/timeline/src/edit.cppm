// openmui.timeline:edit — changes made of three steps (insert a clip, remove
// one, replace one), so that applying a change gives back its exact inverse:
// undo and redo are just more changes, and a change that cannot be made is
// not made at all.
export module openmui.timeline:edit;

import std;
import :model;

export namespace omui::timeline {

struct Step {
    enum class Kind : std::uint8_t { insert, remove, replace };
    Kind        kind = Kind::insert;
    std::size_t track = 0, index = 0;  // which track, and where on it
    Clip        clip;                  // insert, replace: the clip put there (remove: none needed)
};

struct Change {
    std::string       what;  // for a history: "split `title` at 2s"
    std::vector<Step> steps;
};

// Makes a change on `timeline`: its steps in order, each keeping its track in
// order and without overlap. Answers the change that undoes it, or why it
// cannot be made — and then the timeline is as it was.
[[nodiscard]] std::expected<Change, std::string> apply(Timeline& timeline, const Change& change);

// The usual edits, read from the timeline as it is (apply makes them):
// a clip put on a track where it fits;
[[nodiscard]] std::expected<Change, std::string> insert(const Timeline& timeline, std::size_t track, Clip clip);
// a clip taken off;
[[nodiscard]] std::expected<Change, std::string> remove(const Timeline& timeline, std::size_t track, std::size_t index);
// a clip taken off and every later clip on its track moved back by its length;
[[nodiscard]] std::expected<Change, std::string> ripple_remove(const Timeline& timeline, std::size_t track, std::size_t index);
// a clip moved to `at` on `to_track` (the same track or another of its kind);
[[nodiscard]] std::expected<Change, std::string> move(const Timeline& timeline, std::size_t track, std::size_t index, std::size_t to_track, Flicks at);
// a clip's ends moved: it starts at `at` and lasts `length`, the same source
// staying under the same time (its `in` and its keys follow its start);
[[nodiscard]] std::expected<Change, std::string> trim(const Timeline& timeline, std::size_t track, std::size_t index, Flicks at, Flicks length);
// a clip cut in two at `at`: the second half goes on from where the first stopped.
[[nodiscard]] std::expected<Change, std::string> split(const Timeline& timeline, std::size_t track, std::size_t index, Flicks at);

}  // namespace omui::timeline
