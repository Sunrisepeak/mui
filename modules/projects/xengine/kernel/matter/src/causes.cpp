// What happened and why, kept for an agent's `explain` (matter.cppm: Causes).
module xengine.matter;

import std;

namespace xengine::matter {

void Causes::keep(Who who, Cause cause) {
    std::vector<Cause>& causes = by_[who];
    if (causes.size() == kept) causes.erase(causes.begin());
    causes.push_back(cause);
}

std::span<const Cause> Causes::of(Who who) const noexcept {
    const auto found = by_.find(who);
    return found == by_.end() ? std::span<const Cause>() : std::span<const Cause>(found->second);
}

Cause Causes::judged(const Program& program, const Event& event, const std::optional<Why>& why, std::uint64_t step, const std::function<CameFrom(std::uint32_t instance)>& source) {
    Cause cause{.step = step, .kind = event.kind == EventKind::became ? CauseKind::became : event.kind == EventKind::rose ? CauseKind::rose : CauseKind::fell,
                .channel = event.channel, .event = event.name, .inflow = no_carrier};
    if (!why) return cause;
    if (event.kind == EventKind::became && why->became_by) cause.law = *why->became_by;
    else {
        // The held law that flags the channel (a fire's `burns` raising `burning`), else the first that held.
        const auto flags = std::ranges::find_if(why->laws, [&](const Judged& judged) { return judged.holds && program.laws()[judged.law].flag == event.channel; });
        const auto held = std::ranges::find_if(why->laws, [](const Judged& judged) { return judged.holds; });
        cause.law = flags != why->laws.end() ? flags->law : held != why->laws.end() ? held->law : no_carrier;
    }
    // The most that came in, and from whom.
    const Flowed* most = nullptr;
    for (const Flowed& flow : why->flows)
        if (flow.in && (!most || flow.amount > most->amount)) most = &flow;
    if (most) {
        cause.inflow = most->law;
        if (source)
            if (const CameFrom from = source(most->beside)) cause.from = from->first, cause.through = from->second;
    }
    return cause;
}

std::vector<Explained> Causes::explain(Who who, const std::function<std::span<const Cause>(Who)>& of, std::size_t depth, std::optional<std::uint32_t> about) {
    std::vector<Explained> out;
    std::uint64_t before = std::numeric_limits<std::uint64_t>::max();
    bool same = false;  // the next link is the same thing's, strictly earlier
    while (who && out.size() < depth) {
        const std::span<const Cause> causes = of(who);
        // Its latest cause at or before the step the one after it was caused at (before it, for its own earlier one).
        const bool first = out.empty() && about.has_value();
        const auto latest = std::ranges::find_if(causes.rbegin(), causes.rend(), [&](const Cause& cause) {
            if (first) return cause.kind == CauseKind::became || (cause.kind == CauseKind::rose && cause.channel == *about);
            return (same ? cause.step < before : cause.step <= before) && cause.kind != CauseKind::fell;
        });
        if (latest == causes.rend()) break;
        if (std::ranges::any_of(out, [&](const Explained& link) { return link.who == who && link.cause.step == latest->step; })) break;
        out.push_back({who, *latest});
        before = latest->step;
        // What it came from; or, where nothing came in (it burned out, it was applied), what it was before.
        if (latest->from) who = latest->from, same = false;
        else if (latest->kind == CauseKind::applied || latest->kind == CauseKind::struck) break;
        else same = true;
    }
    return out;
}

std::vector<Explained> Causes::explain(Who who, std::size_t depth, std::optional<std::uint32_t> about) const {
    return explain(who, [this](Who one) { return of(one); }, depth, about);
}

}  // namespace xengine::matter
