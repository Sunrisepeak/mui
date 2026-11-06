module openmui.semantic;

import std;
import openmui.core;
import openmui.view;
import mlibs.session;

namespace omui::semantic {

namespace {

std::string_view clock_name(hanim::ClockKind k) {
    switch (k) {
        case hanim::ClockKind::interactive: return "interactive";
        case hanim::ClockKind::render: return "render";
        case hanim::ClockKind::virtual_time: return "virtual";
    }
    return "virtual";
}

std::string digest_text(std::uint64_t d) { return std::format("{:016x}", d); }

}  // namespace

namespace {

// How the frame is drawn (performance tiers, D-94; the person's motion chosen, universal motion §5.2), as a session
// keeps it: an input a replay applies.
std::string adaptation_json(const Adaptation& a, std::string_view choice) {
    static constexpr std::array<std::string_view, 3> levels{"full", "reduced", "off"};
    Value v;
    v.set("tier", mlibs::pipeline::name(a.tier)).set("motion", levels[static_cast<std::size_t>(a.motion)]).set("effects", mlibs::pipeline::name(a.effects)).set("reason", a.reason);
    if (!choice.empty()) v.set("choice", choice);
    return v.json();
}

std::optional<Adaptation> adaptation_from(const Value& v) {
    auto tier = [](std::string_view t) -> std::optional<mlibs::pipeline::Tier> {
        for (int i = 0; i <= 4; ++i)
            if (mlibs::pipeline::name(static_cast<mlibs::pipeline::Tier>(i)) == t) return static_cast<mlibs::pipeline::Tier>(i);
        return std::nullopt;
    };
    const auto t = tier(v.get("tier") ? v.get("tier")->as_string() : "");
    const auto e = tier(v.get("effects") ? v.get("effects")->as_string() : "");
    const std::string_view m = v.get("motion") ? v.get("motion")->as_string() : "";
    if (!t || !e || (m != "full" && m != "reduced" && m != "off")) return std::nullopt;
    return Adaptation{*t, m == "full" ? MotionLevel::full : m == "reduced" ? MotionLevel::reduced : MotionLevel::off, *e,
                      std::string(v.get("reason") ? v.get("reason")->as_string() : "")};
}

}  // namespace

Recorder::Recorder(Tree& tree, mlibs::session::Session& session) : tree_(tree), session_(session) {
    Runtime& rt = tree.runtime();
    mlibs::session::Header& h = session_.header;
    h.spec = std::string(spec::version);
    h.width = rt.size().width;
    h.height = rt.size().height;
    h.scale = rt.scale();
    h.clock = std::string(clock_name(rt.clock().kind()));
    if (rt.clock().kind() == hanim::ClockKind::render) h.fps = rt.clock().frame_rate().fps();
    // How it is drawn from the start (its tier, its levels): applied before the first frame is replayed.
    adapted_ = rt.adaptation();
    chosen_ = rt.motion_choice();
    (void)session_.add(rt.now().count(), "adapt", adaptation_json(adapted_, chosen_));
}

FrameOutput Recorder::frame(const FrameInput& input) {
    Runtime& rt = tree_.runtime();
    FrameOutput out = rt.frame(input);
    // The governor changed how it is drawn: recorded after the frame that did it, applied before the next.
    if (!(rt.adaptation() == adapted_) || rt.motion_choice() != chosen_) {
        adapted_ = rt.adaptation();
        chosen_ = rt.motion_choice();
        (void)session_.add(rt.now().count(), "adapt", adaptation_json(adapted_, chosen_));
    }
    Value body = Value(Value::Map{});
    if (!input.events.empty()) {
        Value::List events;
        for (const InputEvent& e : input.events) events.push_back(to_value(e));
        body.set("events", Value(std::move(events)));
    }
    (void)session_.add(rt.now().count(), "frame", body.json());
    return out;
}

Envelope Recorder::act(std::string_view id, std::string_view action, const Invocation& call) {
    Envelope e = tree_.act(id, action, call);
    Value body;
    body.set("node", id).set("action", action);
    if (!call.args.is_null()) body.set("args", call.args);
    if (!call.answer.empty()) body.set("answer", call.answer);
    body.set("actor", call.actor.text());
    body.set("exit", static_cast<std::int64_t>(e.exit_code().value_or(-1)));
    (void)session_.add(tree_.runtime().now().count(), "act", body.json());
    return e;
}

void Recorder::checkpoint(std::string_view label) {
    Value body;
    body.set("digest", digest_text(tree_.digest()));
    if (!label.empty()) body.set("label", label);
    (void)session_.add(tree_.runtime().now().count(), "check", body.json());
}

void Recorder::note(std::string_view text) {
    (void)session_.add(tree_.runtime().now().count(), "note", Value{}.set("text", text).json());
}

Player::Player(Tree& tree, const mlibs::session::Session& session) : tree_(tree), session_(session) {}

bool Player::done() const noexcept { return next_ >= session_.size(); }

const ReplayReport& Player::advance_to(double time) {
    const auto entries = session_.entries();
    while (next_ < entries.size() && entries[next_].time <= time) run(entries[next_++]);
    position_ = std::isfinite(time) ? std::max(position_, time) : std::max(position_, session_.duration());
    report_.digest = tree_.digest();
    return report_;
}

void Player::run(const mlibs::session::Entry& entry) {
    ReplayReport& r = report_;
    Runtime& rt = tree_.runtime();
    Tree& tree = tree_;
    auto body = parse_json(entry.body);
    if (!body) {
        r.mismatches.push_back(std::format("t={}: unreadable {} entry: {}", entry.time, entry.kind, body.error().text()));
        return;
    }
    if (entry.kind == "frame") {
        FrameInput in;
        in.time = seconds(entry.time);
        if (const Value* events = body->get("events"); events && events->list())
            for (const Value& v : *events->list()) {
                if (auto e = event_from_value(v)) in.events.push_back(std::move(*e));
                else r.mismatches.push_back(std::format("t={}: an event this runtime cannot read: {}", entry.time, v.json()));
            }
        (void)rt.frame(in);
        ++r.frames;
    } else if (entry.kind == "adapt") {
        if (auto a = adaptation_from(*body)) {
            rt.set_adaptation(*a);
            rt.set_motion_choice(std::string(body->get("choice") ? body->get("choice")->as_string() : ""));
        } else {
            r.mismatches.push_back(std::format("t={}: an adaptation this runtime cannot read: {}", entry.time, entry.body));
        }
    } else if (entry.kind == "act") {
        Invocation call;
        if (const Value* a = body->get("args")) call.args = *a;
        if (const Value* a = body->get("answer")) call.answer = std::string(a->as_string());
        if (const Value* a = body->get("actor"))
            if (auto actor = Actor::parse(a->as_string())) call.actor = std::move(*actor);
        const std::string node(body->get("node") ? body->get("node")->as_string() : "");
        const std::string action(body->get("action") ? body->get("action")->as_string() : "");
        Envelope e = tree.act(node, action, call);
        const std::int64_t expected = body->get("exit") ? body->get("exit")->as_int(-1) : -1;
        const int got = e.exit_code().value_or(-1);
        if (expected != got)
            r.mismatches.push_back(std::format("t={}: {} on '{}' ended with {} (recorded: {})", entry.time, action, node, got, expected));
        ++r.actions;
    } else if (entry.kind == "check") {
        const std::string want(body->get("digest") ? body->get("digest")->as_string() : "");
        const std::string got = digest_text(tree.digest());
        if (want != got) {
            const std::string label(body->get("label") ? body->get("label")->as_string() : "");
            r.mismatches.push_back(std::format("t={}: checkpoint {}: tree {} (recorded: {})", entry.time,
                                               label.empty() ? std::string("") : std::format("'{}'", label), got, want));
        }
        ++r.checkpoints;
    }
    // Other kinds (notes, a host's own) are not this runtime's to replay.
}

ReplayReport replay(Tree& tree, const mlibs::session::Session& session, double until) {
    Player p(tree, session);
    return p.advance_to(until);
}

}  // namespace omui::semantic
