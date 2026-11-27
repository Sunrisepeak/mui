// The design shown in place (omui.generate:replay; G8, D-253).
module omui.generate;

import std;
import omui.core;
import omui.view;
import omui.ui.base;
import mlibs.agent;
import hanim.timeline;

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

// The first node under `at`, in the tree's order, whose own id is `id` (the replay's own slot left out).
NodeId named(const Runtime& runtime, NodeId at, std::string_view id) {
    for (const NodeId child : runtime.children(at)) {
        const std::string path = runtime.path(child);
        if (path == Replayer::slot) continue;
        const auto slash = path.rfind('/');
        if (std::string_view(path).substr(slash == std::string::npos ? 0 : slash + 1) == id) return child;
        if (const NodeId deeper = named(runtime, child, id); deeper) return deeper;
    }
    return {};
}

// How many times a step's interface is looked at again for the course's node, a tenth of a second apart: a page
// shows a step when its evaluation settles (a frame or two), not as its texts are given.
constexpr int looks = 10;

}  // namespace

Replayer::Replayer(Runtime& runtime, mlibs::agent::Replay replay, ShowTextsFn show, std::function<void()> restore, std::string course,
                   std::optional<hanim::timeline::Timeline> show_timeline)
    : runtime_(runtime), replay_(std::move(replay)), show_(std::move(show)), restore_(std::move(restore)), course_(std::move(course)) {
    // The show's beats, a clip of its `states` track a step (the director makes one a step, in order).
    if (show_timeline)
        if (const auto states = std::ranges::find(show_timeline->tracks, "states", &hanim::timeline::Track::name); states != show_timeline->tracks.end())
            for (const hanim::timeline::Clip& clip : states->clips) beats_.push_back(mlibs::time::to_seconds(clip.length));
}

double Replayer::seconds_of(std::size_t step) const noexcept {
    if (step == 0 || step > replay_.steps.size()) return 0;
    return step <= beats_.size() ? beats_[step - 1] : replay_.steps[step - 1].seconds;
}

Replayer::~Replayer() {
    if (timer_) runtime_.cancel(timer_);
    if (looking_) runtime_.cancel(looking_);
    for (const std::string& maker : noted_) runtime_.note({.actor = maker});
    if (mounted_) runtime_.unmount(slot);  // its controls call back here
}

void Replayer::said_playing() { toggle_.set(timer_ ? "Pause" : "Play"); }

std::expected<void, std::string> Replayer::play(std::size_t from, double speed) {
    if (from == 0 || from > replay_.steps.size())
        return std::unexpected(std::format("step {} is not in the replay (1 … {})", from, replay_.steps.size()));
    speed_ = std::clamp(std::isfinite(speed) ? speed : 1.0, 0.25, 4.0);
    pause();
    if (auto shown = show(from, true); !shown) return shown;
    schedule();
    said_playing();
    return {};
}

void Replayer::pause() {
    if (timer_) runtime_.cancel(timer_);
    timer_ = 0;
    said_playing();
}

std::expected<void, std::string> Replayer::at(std::size_t step) {
    if (step == 0 || step > replay_.steps.size())
        return std::unexpected(std::format("step {} is not in the replay (1 … {})", step, replay_.steps.size()));
    pause();
    return show(step);
}

void Replayer::stop() {
    pause();
    if (looking_) runtime_.cancel(looking_);
    looking_ = 0;
    on_node_.set(false);
    ended_ = false;
    if (at_ != 0 && restore_) restore_();
    at_ = 0;
    for (const std::string& maker : noted_) runtime_.note({.actor = maker});  // what they said withdrawn
    noted_.clear();
    if (mounted_) runtime_.unmount(slot);
    mounted_ = false;
}

std::expected<void, std::string> Replayer::show(std::size_t step, bool playing) {
    const mlibs::agent::ReplayStep& shown = replay_.steps[step - 1];
    // Played: what the step changes presented as a design edit is, its time scaled to fill most of the step's time on
    // screen (the presentation is 0.9 s at its own pace, ¼ to 4 times as fast) — in a release too, where an edit is
    // otherwise shown at once. Gone to (`at`, the person scrubbing): shown as an edit is where it is shown at all.
    const Runtime::DesignPresentation before = runtime_.design_presentation();
    if (playing) {
        const double on_screen = std::max(0.05, seconds_of(step) / speed_);
        runtime_.set_design_presentation({Runtime::DesignPresentation::Kind::edit, 0.9 / (presented * on_screen)});
        runtime_.present_design();
    }
    auto ran = show_ ? show_(shown.texts) : std::expected<void, std::string>{};
    if (playing) runtime_.set_design_presentation(before);
    if (!ran) {
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
    shown_.set(static_cast<double>(step));
    looks_ = 0;
    place_course();
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

void Replayer::place_course() {
    if (looking_) runtime_.cancel(looking_);
    looking_ = 0;
    if (course_.empty()) return;
    const NodeId node = named(runtime_, runtime_.root(), course_);
    const Path* shape = node ? runtime_.component<Path>(node) : nullptr;
    if (shape) {
        over_.set(runtime_.path(node));
        course_shape_.set(shape->props.d);
        course_box_.set(shape->props.view_box);
    }
    on_node_.set(shape != nullptr);
    // Looked for again until it is there, a few times: the step's page may not have settled yet.
    if (!shape && ++looks_ < looks) looking_ = runtime_.after(seconds(0.1), [this] {
        looking_ = 0;
        place_course();
    });
}

void Replayer::schedule() {
    const double on_screen = seconds_of(at_) / speed_;
    timer_ = runtime_.after(seconds(on_screen), [this] {
        timer_ = 0;
        if (at_ < replay_.steps.size()) {
            if (show(at_ + 1, true)) schedule();
            said_playing();
            return;
        }
        // The last step held its time: the application as it ships (the design's last step, when the replay is
        // taken from what it ships), the caption gone.
        stop();
        ended_ = true;
    });
}

// Over the application: the step's maker at what it built (the agent's presence, D-160: its colour, its frame and
// what it said), and at the bottom a card — who did the step and what it did; Pause or Play, Close; a timeline of the
// steps the person scrubs, each step shown as it is reached. Only the card takes the pointer: the page around it is
// used as it plays. Its colours and controls are the theme's.
void Replayer::present() {
    const Size window = runtime_.size();
    const float width = std::clamp(window.width - 32.0f, 160.0f, 440.0f), inner = width - 28.0f;
    const auto steps = static_cast<double>(replay_.steps.size());
    const auto through = [](std::string_view way) { return Prop<std::string>(std::string(way)); };
    // The design's course: each step's caption is what the person or a reader hears at it. Scrubbed, the step reached.
    std::vector<std::string> captions;
    for (const mlibs::agent::ReplayStep& step : replay_.steps) captions.push_back(step.caption);
    const auto scrubbed = [this](double step) {
        const auto reached = static_cast<std::size_t>(std::lround(step));
        if (reached != at_) (void)at(reached);
    };
    Element card = column({.id = "replay", .width = width, .padding = Edges(10.0f, 14.0f), .gap = 6, .background = "surface", .border_color = "border",
                           .border_width = 1, .radius = 12, .shadow = "md", .label = "The design, step by step"},
                          {row({.id = "controls", .gap = 8, .align = "center"},
                               {column({.id = "words", .gap = 2, .grow = 1},
                                       {text({.id = "who", .text = who_, .style = "caption", .color = "text_muted"}),
                                        text({.id = "caption", .text = caption_, .style = "label", .max_lines = 2, .max_width = inner - 150})}),
                                button({.id = "play", .text = toggle_, .variant = ButtonVariant::ghost, .on_activated = [this] {
                                            if (timer_) pause();
                                            else (void)play(at_ == 0 ? 1 : at_, speed_);  // on from the step shown
                                        }}),
                                button({.id = "close", .text = "Close", .variant = ButtonVariant::ghost, .on_activated = [this] { stop(); }})}),
                           // Its line, unless the course lies on the application's own node.
                           when([this] { return !on_node_(); },
                                [=, this] {
                                    return timeline({.id = "step", .captions = captions, .label = "Step", .steps = static_cast<std::int64_t>(steps),
                                                     .value = shown_, .on_changed = scrubbed, .width = inner});
                                })});
    Element bottom = column({.id = "dock", .width = window.width, .height = window.height, .padding = 14, .justify = "end", .align = "center"}, {std::move(card)});
    bottom.prop("pointer_events", through("children"));
    // The step's maker: the host's presence where a window has one (its default, D-160), else its own.
    std::vector<Element> layers;
    if (!runtime_.valid(runtime_.find(agent_presence_slot)))
        layers.push_back(agent_presence({.id = "presence", .actors = "agent:", .trail = 0, .width = window.width, .height = window.height}));
    // The course on the application's node (the site's O): the steps on its shape where it is drawn, the node's own
    // stroke the line; only a press near it is the course's.
    layers.push_back(when([this] { return on_node_(); },
                          [=, this] {
                              return timeline({.id = "course", .captions = captions, .label = "Step", .line = false, .over = over_, .shape = course_shape_,
                                               .steps = static_cast<std::int64_t>(steps), .value = shown_, .view_box = course_box_, .on_changed = scrubbed,
                                               .width = window.width, .height = window.height});
                          }));
    layers.push_back(std::move(bottom));
    Element over = stack({.width = window.width, .height = window.height}, std::move(layers));
    over.prop("pointer_events", through("children"));
    runtime_.mount(slot, std::move(over));
    mounted_ = true;
}

Value Replayer::state() const {
    Value::List steps;
    for (const mlibs::agent::ReplayStep& step : replay_.steps)
        steps.push_back(Value{}.set("caption", step.caption).set("actor", step.actor).set("seconds", seconds_of(steps.size() + 1)));
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
