// The interaction recorder (D-148): each input event numbered as it is
// dispatched (its cause), what came of it recorded with that number at the
// level set, and a press explained — from its press to its release, what it
// hit, what stopped it, what it activated.
module omui.view;

import std;
import omui.core;
import :impl;

namespace omui {

void Runtime::Impl::record(RecordLevel least, std::string_view kind, NodeId node, std::string text, std::optional<Point> position) {
    if (record_level < least || record_level == RecordLevel::off || previews > 0) return;
    recorded.push_back({cause, clock.now().count(), std::string(kind), alive(node) ? self.path(node) : std::string(), std::move(text),
                        input_actor ? input_actor->text() : std::string("person:user"), position});
    while (recorded.size() > record_capacity) recorded.pop_front();
}

void Runtime::Impl::record_input(const InputEvent& event) {
    std::visit(
        [&](const auto& x) {
            using T = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::same_as<T, PointerEvent>) {
                constexpr std::array kinds{"down", "up", "move", "enter", "leave", "cancel"};
                const auto kind = kinds[static_cast<std::size_t>(x.kind)];
                record(x.kind == PointerKind::down || x.kind == PointerKind::up ? RecordLevel::interactions : RecordLevel::full, "input", {},
                       std::format("pointer {} at {},{}", kind, x.position.x, x.position.y), x.position);
            } else if constexpr (std::same_as<T, WheelEvent>) {
                record(RecordLevel::interactions, "input", {}, std::format("wheel {},{} at {},{}", x.delta.x, x.delta.y, x.position.x, x.position.y),
                       x.position);
            } else if constexpr (std::same_as<T, KeyEvent>) {
                if (x.kind == KeyKind::down) record(RecordLevel::interactions, "input", {}, std::format("key {}", name(x.key)));
            } else if constexpr (std::same_as<T, TextEvent>) {
                // What a person types is theirs: how much, never what (D-148).
                record(RecordLevel::interactions, "input", {}, std::format("text: {} bytes", x.text.size()));
            }
        },
        event);
}

void Runtime::set_record_level(RecordLevel level) { impl_->record_level = level; }

RecordLevel Runtime::record_level() const noexcept { return impl_->record_level; }

Value Runtime::input_scenario() const {
    Value::List frames;
    for (const Impl::RecordedFrame& frame : impl_->input_log) {
        Value one;
        one.set("t", frame.at);
        if (!frame.events.empty()) {
            Value::List events;
            for (const InputEvent& event : frame.events) events.push_back(to_value(event));
            one.set("events", Value(std::move(events)));
        }
        frames.push_back(std::move(one));
    }
    return Value{}
        .set("omui", "0.1")
        .set("kind", "input")
        .set("size", Value(Value::List{Value(size().width), Value(size().height)}))
        .set("frames", Value(std::move(frames)));
}

std::vector<InteractionRecord> Runtime::interactions(std::uint64_t since) const {
    std::vector<InteractionRecord> out;
    for (const auto& r : impl_->recorded)
        if (r.cause > since) out.push_back(r);
    return out;
}

std::optional<Runtime::Explanation> Runtime::explain(std::optional<Point> at, std::uint64_t cause) const {
    const Impl& d = *impl_;
    const auto is_press = [](const InteractionRecord& r) { return r.kind == "input" && r.text.starts_with("pointer down"); };
    std::uint64_t press = 0;
    for (auto it = d.recorded.rbegin(); it != d.recorded.rend() && !press; ++it) {
        if (!is_press(*it)) continue;
        if (cause ? it->cause == cause : at && it->position && std::abs(it->position->x - at->x) <= 8 && std::abs(it->position->y - at->y) <= 8)
            press = it->cause;
    }
    if (!press) return std::nullopt;
    // To its release: the first pointer up after it, or what is recorded.
    std::uint64_t last = press;
    for (const auto& r : d.recorded)
        if (r.cause > press && r.kind == "input" && r.text.starts_with("pointer up")) {
            last = r.cause;
            break;
        }
    Explanation out;
    out.cause = press;
    for (const auto& r : d.recorded)
        if (r.cause >= press && r.cause <= last) out.records.push_back(r);
    for (const auto& a : d.journal)
        if (a.cause >= press && a.cause <= last) out.actions.push_back(a);
    const auto done = std::ranges::find_if(out.actions, [](const ActionEntry& a) { return a.exit_code == 0 && a.action != "input"; });
    const auto stopped = std::ranges::find_if(out.records, [](const InteractionRecord& r) { return r.kind == "missed" || r.kind == "refused"; });
    const auto hit = std::ranges::find_if(out.records, [](const InteractionRecord& r) { return r.kind == "hit"; });
    if (done != out.actions.end())
        out.summary = std::format("activated {}: {} ran", done->path, done->action);
    else if (stopped != out.records.end())
        out.summary = std::format("{}{}", stopped->node.empty() ? std::string() : stopped->node + ": ", stopped->text);
    else if (const auto failed = std::ranges::find_if(out.actions, [](const ActionEntry& a) { return a.exit_code != 0; }); failed != out.actions.end())
        out.summary = std::format("{} on {} failed (exit {})", failed->action, failed->path, failed->exit_code);
    else
        out.summary = hit != out.records.end() ? std::format("the press reached {}, which did nothing with it", hit->node)
                                               : std::string("the press reached nothing that took it");
    return out;
}

}  // namespace omui
