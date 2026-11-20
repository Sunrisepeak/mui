// hanim.timeline:edit — changes made of three steps (insert a clip, remove
// one, replace one), so that applying a change gives back its exact inverse:
// undo and redo are just more changes, and a change that cannot be made is
// not made at all.
export module hanim.timeline:edit;

import std;
import mlibs.error;
import :model;

export namespace hanim::timeline {

struct Step {
    enum class Kind : std::uint8_t { insert, remove, replace };
    Kind        kind = Kind::insert;
    std::size_t track = 0, index = 0;  // which track, and where on it
    Clip        clip;                  // insert, replace: the clip put there (remove: none needed)
};

// Why an edit cannot be made (the error contract, mlibs.error): its code, where (the change's `what`, or the
// track), and what was found.
enum class EditError : std::uint8_t { no_track, no_clip, no_place, no_length, before_start, no_speed, overlap, other_kind, outside, unknown_step };
[[nodiscard]] constexpr mlibs::error::Description describe(EditError error) noexcept {
    using Kind = mlibs::error::Kind;
    switch (error) {
        case EditError::no_track: return {"hanim.timeline.no-track", Kind::not_found, "there is no such track", "use a track the timeline has, or add the track first"};
        case EditError::no_clip: return {"hanim.timeline.no-clip", Kind::not_found, "the track has no such clip", "use a clip of that track (the track lists its clips)"};
        case EditError::no_place: return {"hanim.timeline.no-place", Kind::invalid_argument, "the track has no such place", "give a place within the track: from 0 to its clip count"};
        case EditError::no_length: return {"hanim.timeline.no-length", Kind::invalid_argument, "a clip lasts longer than 0", "give the clip a length above 0"};
        case EditError::before_start: return {"hanim.timeline.before-start", Kind::invalid_argument, "a clip starts at 0 or later", "start the clip at 0 or later"};
        case EditError::no_speed: return {"hanim.timeline.no-speed", Kind::invalid_argument, "a clip's speed is not 0", "give a speed other than 0 (negative plays it backwards)"};
        case EditError::overlap: return {"hanim.timeline.overlap", Kind::refused, "clips on a track do not overlap", "move the clip to a free span of the track, or shorten or move the clip in the way"};
        case EditError::other_kind: return {"hanim.timeline.other-kind", Kind::refused, "a clip moves only to a track of its kind", "move the clip to a track of its kind, or make one"};
        case EditError::outside: return {"hanim.timeline.outside", Kind::invalid_argument, "the time is not inside the clip", "give a time inside the clip: from its start to its end"};
        case EditError::unknown_step: return {"hanim.timeline.unknown-step", Kind::internal, "a step of no known kind", "report it with the edit that was applied: the editor met a step it does not know"};
    }
    return {"hanim.timeline.?", Kind::internal};
}
using EditFailure = mlibs::error::Error<EditError>;

struct Change {
    std::string       what;  // for a history: "split `title` at 2s"
    std::vector<Step> steps;
};

// Makes a change on `timeline`: its steps in order, each keeping its track in
// order and without overlap. Answers the change that undoes it, or why it
// cannot be made — and then the timeline is as it was.
[[nodiscard]] std::expected<Change, EditFailure> apply(Timeline& timeline, const Change& change);

// The usual edits, read from the timeline as it is (apply makes them):
// a clip put on a track where it fits;
[[nodiscard]] std::expected<Change, EditFailure> insert(const Timeline& timeline, std::size_t track, Clip clip);
// a clip taken off;
[[nodiscard]] std::expected<Change, EditFailure> remove(const Timeline& timeline, std::size_t track, std::size_t index);
// a clip taken off and every later clip on its track moved back by its length;
[[nodiscard]] std::expected<Change, EditFailure> ripple_remove(const Timeline& timeline, std::size_t track, std::size_t index);
// a clip moved to `at` on `to_track` (the same track or another of its kind);
[[nodiscard]] std::expected<Change, EditFailure> move(const Timeline& timeline, std::size_t track, std::size_t index, std::size_t to_track, Flicks at);
// a clip's ends moved: it starts at `at` and lasts `length`, the same source
// staying under the same time (its `in` and its keys follow its start);
[[nodiscard]] std::expected<Change, EditFailure> trim(const Timeline& timeline, std::size_t track, std::size_t index, Flicks at, Flicks length);
// a clip cut in two at `at`: the second half goes on from where the first stopped.
[[nodiscard]] std::expected<Change, EditFailure> split(const Timeline& timeline, std::size_t track, std::size_t index, Flicks at);

}  // namespace hanim::timeline
