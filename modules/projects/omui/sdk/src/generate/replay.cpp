// The design shown in place (omui.generate:replay; G8, D-253).
module omui.generate;

import std;
import omui.core;
import omui.view;
import omui.ui.base;
import mlibs.agent;

namespace omui::generate {

namespace {

// "agent:designer" → "designer · agent"; "person" → "person".
std::string who_of(std::string_view actor) {
    const auto colon = actor.find(':');
    if (colon == std::string_view::npos) return std::string(actor);
    return std::format("{} · {}", actor.substr(colon + 1), actor.substr(0, colon));
}

// What the step did, as its maker says it beside the node: the caption without its count ("4/9 · automatic renewal"
// → "automatic renewal"; the count is the line's at the bottom).
std::string said_of(std::string_view caption) {
    const auto dot = caption.find(" · ");
    const std::string_view count = caption.substr(0, dot);
    const bool counted = dot != std::string_view::npos && count.contains('/') &&
                         std::ranges::all_of(count, [](char c) { return c == '/' || (c >= '0' && c <= '9'); });
    return std::string(counted ? caption.substr(dot + std::string_view(" · ").size()) : caption);
}

}  // namespace

Replayer::Replayer(Runtime& runtime, mlibs::agent::Replay replay, ShowTextsFn show, std::function<void()> restore)
    : runtime_(runtime), replay_(std::move(replay)), show_(std::move(show)), restore_(std::move(restore)) {}

Replayer::~Replayer() {
    if (timer_) runtime_.cancel(timer_);
    for (const std::string& maker : noted_) runtime_.note({.actor = maker});
}

std::expected<void, std::string> Replayer::play(std::size_t from, double speed) {
    if (from == 0 || from > replay_.steps.size())
        return std::unexpected(std::format("step {} is not in the replay (1 … {})", from, replay_.steps.size()));
    speed_ = std::clamp(std::isfinite(speed) ? speed : 1.0, 0.25, 4.0);
    pause();
    if (auto shown = show(from); !shown) return shown;
    schedule();
    return {};
}

void Replayer::pause() {
    if (timer_) runtime_.cancel(timer_);
    timer_ = 0;
}

std::expected<void, std::string> Replayer::at(std::size_t step) {
    if (step == 0 || step > replay_.steps.size())
        return std::unexpected(std::format("step {} is not in the replay (1 … {})", step, replay_.steps.size()));
    pause();
    return show(step);
}

void Replayer::stop() {
    pause();
    ended_ = false;
    if (at_ != 0 && restore_) restore_();
    at_ = 0;
    for (const std::string& maker : noted_) runtime_.note({.actor = maker});  // what they said withdrawn
    noted_.clear();
    runtime_.unmount(slot);
}

std::expected<void, std::string> Replayer::show(std::size_t step) {
    const mlibs::agent::ReplayStep& shown = replay_.steps[step - 1];
    if (auto ran = show_ ? show_(shown.texts) : std::expected<void, std::string>{}; !ran) {
        problem_ = std::format("step {}: {}", step, ran.error());
        stop();
        return std::unexpected(problem_);
    }
    problem_.clear();
    ended_ = false;
    const bool first = at_ == 0;
    at_ = step;
    caption_.set(shown.caption);
    who_.set(who_of(shown.actor));
    if (first) present();
    played_.set(static_cast<double>(step) / static_cast<double>(replay_.steps.size()));
    // Its maker at what the step built: a note there (the presence shows it); a step that names no node withdraws
    // the last one's.
    if (!shown.actor.empty()) {
        for (const std::string& maker : noted_)
            if (maker != shown.actor) runtime_.note({.actor = maker});
        noted_ = {shown.actor};
        runtime_.note({.actor = shown.actor, .text = said_of(shown.caption), .node = shown.node});
    }
    return {};
}

void Replayer::schedule() {
    const double on_screen = replay_.steps[at_ - 1].seconds / speed_;
    timer_ = runtime_.after(seconds(on_screen), [this] {
        timer_ = 0;
        if (at_ < replay_.steps.size()) {
            if (show(at_ + 1)) schedule();
            return;
        }
        // The last step held its time: the application as it ships (the design's last step, when the replay is
        // taken from what it ships), the caption gone.
        stop();
        ended_ = true;
    });
}

// Over the application: the step's maker at what it built (the agent's presence, D-160: its colour, its frame and
// what it said), and at the bottom who did the step and what it did, over how far the design has come. Its colours
// are the theme's (a tooltip's ink on its pill); it takes no pointer, so the page under it is used as it plays.
void Replayer::present() {
    const Size window = runtime_.size();
    const float width = std::clamp(window.width - 48.0f, 120.0f, 420.0f), inner = width - 28.0f;
    played_.set(0.0);
    Element over = stack({.width = window.width, .height = window.height},
                         {agent_presence({.id = "presence", .actors = "agent:", .trail = 0, .width = window.width, .height = window.height}),
                          column({.width = window.width, .height = window.height, .padding = 14, .justify = "end", .align = "center"},
                                 {column({.id = "replay", .width = width, .padding = Edges(10.0f, 14.0f), .gap = 6, .background = "tooltip", .radius = 12,
                                          .shadow = "md", .label = "The design, step by step"},
                                         {text({.id = "who", .text = who_, .style = "caption", .color = "on_tooltip", .opacity = 0.72f}),
                                          text({.id = "caption", .text = caption_, .style = "label", .color = "on_tooltip", .max_lines = 2, .max_width = inner}),
                                          progress({.id = "played", .label = "How far the design has come", .value = played_, .width = inner})})})});
    over.prop("pointer_events", Prop<std::string>(std::string("none")));
    runtime_.mount(slot, std::move(over));
}

Value Replayer::state() const {
    Value::List steps;
    for (const mlibs::agent::ReplayStep& step : replay_.steps)
        steps.push_back(Value{}.set("caption", step.caption).set("actor", step.actor).set("seconds", step.seconds));
    Value out = Value{}
                    .set("doc", replay_.doc)
                    .set("steps", Value(std::move(steps)))
                    .set("at", static_cast<std::int64_t>(at_))
                    .set("playing", timer_ != 0)
                    .set("ended", ended_)
                    .set("speed", speed_);
    if (!problem_.empty()) out.set("problem", problem_);
    return out;
}

}  // namespace omui::generate
