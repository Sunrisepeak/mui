// openmui.showcase.promo_video:choreography — a shot's timing, declared: the
// spans over its own time in which something comes in, goes out or moves
// (a label fades in at 0.45 s, the card rises when the agent asks), each a
// hanim tween from 0 to 1 on a named target of one hanim::Timeline, sampled
// once a frame. Fixed moments are written when the shot is made; moments
// found while it plays (an answer, the end of the typing) when they are
// known. `omui-promo choreography CUT` lists every shot's.
//
//     Choreography c;
//     c.span("label.in", 0.45, 0.6, hanim::ease::smoothstep);
//     c.sample(t);
//     const double on = c["label.in"];          // 0 before, 1 after
export module omui.showcase.promo_video:choreography;

import std;
import hanim.core;

export namespace omui::promo {

struct TimedSpan {
    std::string target;
    double      from = 0, to = 0;  // shot seconds
    hanim::Ease ease;
};

class Choreography {
public:
    // `target` goes from 0 to 1 between `from` and `to` (to > from), eased;
    // 0 before, 1 after — what hanim::window(t, from, to, ease) gives.
    Choreography& span(std::string target, double from, double to, const hanim::Ease& ease = {});
    // Every target at `t` (shot seconds).
    void sample(double t);
    // A target's value at the time sampled last; 0 for one with no span yet.
    [[nodiscard]] double operator[](std::string_view target) const;
    [[nodiscard]] std::span<const TimedSpan> spans() const noexcept { return spans_; }
    [[nodiscard]] const hanim::Timeline& timeline() const noexcept { return timeline_; }

private:
    hanim::Timeline                               timeline_;
    std::vector<TimedSpan>                           spans_;
    std::vector<std::pair<std::string, double>>   values_;
};

}  // namespace omui::promo
