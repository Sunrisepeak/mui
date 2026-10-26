// openmui.anim:timeline — clips placed in time, for UI choreography and video.
//
// A timeline holds clips, each a primitive (tween, track, spring, motion, or a
// function of time) bound to a target name and placed at a start time through
// a time map (offset, rate, trim, repeat, alternate, reverse, fill). Timelines
// nest, so a scene is a timeline and a film is a timeline of scenes.
//
// The target is an opaque name ("title.opacity", "cards/3.scale"): the
// timeline does not know what it animates. The consumer — openmui's view
// layer, a video composition, a game — receives (target, value) pairs and
// decides what they mean, which is what keeps this package free of UI.
export module openmui.anim:timeline;

import std;
import :math;
import :time;
import :ease;
import :value;
import :motion;

export namespace omui::anim {

namespace detail {
template <class T>
inline constexpr char type_tag = 0;
}

// A sampled value with its type erased: the object's bytes and a tag naming
// its type. The bytes are the value itself (not its lanes), so what a clip
// produced is exactly what `as<T>()` returns.
struct Value {
    static constexpr std::size_t capacity = 64;

    const void*                           type = nullptr;
    alignas(8) std::array<std::byte, capacity> bytes{};

    template <Animatable T>
        requires std::is_trivially_copyable_v<T>
    [[nodiscard]] static Value of(const T& v) {
        static_assert(sizeof(T) <= capacity, "a timeline value is at most 64 bytes");
        Value out;
        out.type = &detail::type_tag<T>;
        std::memcpy(out.bytes.data(), &v, sizeof(T));
        return out;
    }

    template <Animatable T>
    [[nodiscard]] bool is() const noexcept {
        return type == &detail::type_tag<T>;
    }

    template <Animatable T>
        requires std::is_trivially_copyable_v<T>
    [[nodiscard]] std::optional<T> as() const {
        if (!is<T>()) return std::nullopt;
        T v;
        std::memcpy(&v, bytes.data(), sizeof(T));
        return v;
    }
};

// Anything that yields a value at a local time, with a natural length.
class Clip {
public:
    template <Animatable T>
    Clip(Tween<T> a) : length_(a.total()), fn_([a](seconds t) { return Value::of(a.value(t)); }) {}

    template <Animatable T>
    Clip(Track<T> a) : length_(a.end()), fn_([a = std::move(a)](seconds t) { return Value::of(a.value(t)); }) {}

    template <Animatable T>
    Clip(Spring<T> a) : length_(a.settle_time()), fn_([a](seconds t) { return Value::of(a.value(t)); }) {}

    template <Animatable T>
    Clip(Decay<T> a) : length_(seconds{0}), fn_([a](seconds t) { return Value::of(a.value(t)); }) {
        // A decay's length is when it comes to rest; it is computed lane by lane.
        auto from = lanes<T>::to(a.from);
        double end = 0;
        for (std::size_t i = 0; i < from.size(); ++i)
            end = std::max(end, Glide(from[i], a.velocity[i], a.params).settle_time());
        length_ = a.start + seconds(end);
    }

    // A function of local time with an explicit length.
    template <class F>
        requires std::invocable<const F&, seconds> && Animatable<std::invoke_result_t<const F&, seconds>>
    static Clip function(seconds length, F f) {
        return Clip(length, [f = std::move(f)](seconds t) { return Value::of(f(t)); });
    }

    [[nodiscard]] seconds length() const noexcept { return length_; }
    [[nodiscard]] Value sample(seconds local) const { return fn_(local); }

private:
    Clip(seconds length, std::function<Value(seconds)> fn) : length_(length), fn_(std::move(fn)) {}
    seconds                       length_{0};
    std::function<Value(seconds)> fn_;
};

// Which side of its active interval a clip keeps applying on.
enum class Fill : std::uint8_t { none, forwards, backwards, both };

// How parent time maps to a clip's local time. With the defaults, local time
// is parent time minus the clip's start, and the clip keeps its last value
// after it ends.
struct TimeMap {
    double                 rate      = 1.0;      // local seconds per parent second
    seconds                trim{0};              // local time skipped at the start
    std::optional<seconds> length;               // local length (default: the clip's own, minus trim)
    double                 repeat    = 1.0;      // iterations; may be fractional or infinite
    bool                   alternate = false;    // every other iteration plays backwards
    bool                   reverse   = false;    // every iteration plays backwards
    Fill                   fill      = Fill::forwards;
};

// The local time of a clip at parent time t, or nullopt when the clip does not
// apply at t. `natural` is the clip's own length.
[[nodiscard]] std::optional<seconds> map_time(const TimeMap& map, seconds start, seconds natural, seconds t) noexcept;

// The parent-time length of a clip placed with `map` (infinite when it repeats
// forever).
[[nodiscard]] seconds mapped_length(const TimeMap& map, seconds natural) noexcept;

struct Marker {
    seconds     time{0};
    std::string name;
};

class Timeline {
public:
    Timeline() = default;

    // Places a clip at `start`.
    Timeline& at(seconds start, std::string target, Clip clip, TimeMap map = {});

    // Places a clip `gap` after the end of the clip placed last (a series).
    Timeline& then(std::string target, Clip clip, seconds gap = seconds{0}, TimeMap map = {});

    // Places a clip at the start of the clip placed last, plus `offset`.
    Timeline& with(std::string target, Clip clip, seconds offset = seconds{0}, TimeMap map = {});

    // The same clip on each target, the i-th starting at start + i·step.
    Timeline& stagger(seconds start, seconds step, std::span<const std::string> targets, const Clip& clip,
                      TimeMap map = {});

    // A whole timeline as one clip; its targets are prefixed with `prefix`.
    Timeline& nest(seconds start, Timeline child, std::string prefix = {}, TimeMap map = {});

    // A named instant (a beat, a cut, a chapter).
    Timeline& marker(seconds t, std::string name);

    [[nodiscard]] seconds duration() const noexcept;
    [[nodiscard]] std::span<const Marker> markers() const noexcept { return markers_; }
    [[nodiscard]] std::optional<seconds> marker_time(std::string_view name) const noexcept;
    // Markers of nested timelines too, in parent time, sorted by time.
    [[nodiscard]] std::vector<Marker> all_markers() const;

    // Calls sink(target, value) for every clip that applies at t, in placement
    // order — so of two clips on one target, the later-placed wins when the
    // consumer applies them in turn.
    template <class Sink>
        requires std::invocable<Sink&, std::string_view, const Value&>
    void sample(seconds t, Sink&& sink) const {
        auto thunk = [](void* ctx, std::string_view target, const Value& v) { (*static_cast<Sink*>(ctx))(target, v); };
        visit(t, thunk, &sink);
    }

    [[nodiscard]] std::vector<std::pair<std::string, Value>> sample(seconds t) const;

private:
    using Thunk = void (*)(void*, std::string_view, const Value&);
    void visit(seconds t, Thunk fn, void* ctx) const;
    void visit_prefixed(seconds t, Thunk fn, void* ctx, std::string& prefix) const;

    struct Entry {
        std::string                      target;
        seconds                          start{0};
        TimeMap                          map;
        std::optional<Clip>              clip;
        std::shared_ptr<const Timeline>  child;  // set for a nested timeline
        seconds                          natural{0};
    };
    std::vector<Entry>  entries_;
    std::vector<Marker> markers_;
};

}  // namespace omui::anim
