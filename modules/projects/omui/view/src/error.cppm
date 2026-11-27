// omui.view:error — why the view's own operations fail (mlibs.error): a model's rows refused, an input's
// attribution malformed, a capture or a reload this host cannot make or that failed — and the hosts' (headless,
// desktop, Web), which answer the same contract: a window, a device, a region, a file.
export module omui.view:error;

import std;
import mlibs.error;

export namespace omui {

enum class ViewError : std::uint8_t {
    bad_key, duplicate_key, key_taken, past_end,              // a model's rows
    empty_range, range_past_input, unordered_ranges,          // an input's attribution
    no_vector_capture, no_pixel_capture, capture_failed,      // what is drawn, captured
    no_reloader, reload_failed,                               // sources reloaded
    empty_region, nothing_shown, below_level,                 // a host's capture
    window_failed, device_failed, not_open,                   // a host's window and device
    write_failed, nothing_recorded                            // what a host writes
};
[[nodiscard]] constexpr mlibs::error::Description describe(ViewError error) noexcept {
    using Kind = mlibs::error::Kind;
    switch (error) {
        case ViewError::bad_key: return {"omui.view.bad-key", Kind::invalid_argument, "a key is 1-128 of [A-Za-z0-9_.-] (spec S-8)", "give a key of 1–128 letters, digits, `_`, `.` or `-`"};
        case ViewError::duplicate_key: return {"omui.view.duplicate-key", Kind::invalid_argument, "a key once in a batch", "give each row of the batch its own key"};
        case ViewError::key_taken: return {"omui.view.key-taken", Kind::refused, "the model has a row of that key", "update the row of that key instead, or give the new row another key"};
        case ViewError::past_end: return {"omui.view.past-end", Kind::invalid_argument, "past the model's rows", "give a position from 0 to the model's row count"};
        case ViewError::empty_range: return {"omui.view.empty-range", Kind::invalid_argument, "an attribution range is not empty", "give the range at least one event, or leave it out"};
        case ViewError::range_past_input: return {"omui.view.range-past-input", Kind::invalid_argument, "an attribution range within the frame's events", "keep the range within the frame's events: 0 to their count"};
        case ViewError::unordered_ranges: return {"omui.view.unordered-ranges", Kind::invalid_argument, "attribution ranges ordered, not overlapping", "sort the ranges by their start and merge the ones that overlap"};
        case ViewError::no_vector_capture: return {"omui.view.no-vector-capture", Kind::refused, "this host does not draw in vectors", "capture pixels instead (`ui.capture` as a PNG), or run on a host that draws vectors"};
        case ViewError::no_pixel_capture: return {"omui.view.no-pixel-capture", Kind::refused, "this host draws no pixels", "read the semantic tree (`ui.snapshot`) or vectors instead, or run a host that draws pixels"};
        case ViewError::capture_failed: return {"omui.view.capture-failed", Kind::failed, "the capture could not be made", "read the message for the cause; capture a smaller region, or try again after the next frame"};
        case ViewError::no_reloader: return {"omui.view.no-reloader", Kind::refused, "the application is not run from sources", "run the application from its sources (`omui dev`) to reload it"};
        case ViewError::reload_failed: return {"omui.view.reload-failed", Kind::failed, "the sources could not be reloaded", "read the message (it names the file and line), correct the source and reload again"};
        case ViewError::empty_region: return {"omui.view.empty-region", Kind::invalid_argument, "the region is empty or outside what is shown", "give a region with a width and a height that overlaps what is shown (`ui.snapshot` gives the bounds)"};
        case ViewError::nothing_shown: return {"omui.view.nothing-shown", Kind::refused, "nothing is shown yet", "wait for the first frame (`ui.wait` with `settled`), then ask again"};
        case ViewError::below_level: return {"omui.view.below-level", Kind::refused, "a headless host below H2 (level pixels) draws no pixels", "run the headless host at H2 (pixels) or above, or read the semantic tree instead"};
        case ViewError::window_failed: return {"omui.view.window-failed", Kind::failed, "the window could not be opened", "read the message for the cause; run headless (`--headless`), or with SDL_VIDEO_DRIVER=offscreen where there is no display"};
        case ViewError::device_failed: return {"omui.view.device-failed", Kind::failed, "no device or renderer for the window", "run with the software renderer, or headless; report the device in the message"};
        case ViewError::not_open: return {"omui.view.not-open", Kind::refused, "the window is not open", "open the window first, or act on one that is open"};
        case ViewError::write_failed: return {"omui.view.write-failed", Kind::failed, "the file could not be written", "check that the directory exists and can be written, then write again"};
        case ViewError::nothing_recorded: return {"omui.view.nothing-recorded", Kind::refused, "nothing has been recorded", "run in development (the recorder keeps every frame) and act before asking for the recording"};
    }
    return {"omui.view.?", Kind::internal};
}
using ViewFailure = mlibs::error::Error<ViewError>;

}  // namespace omui
