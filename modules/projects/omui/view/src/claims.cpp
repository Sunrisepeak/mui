// Claims (D-160): an actor holding a part of the interface while it designs
// it — a node, one of its properties, or a subtree — so that another agent's
// change there waits. Leases end unless renewed; a person is never held.
module omui.view;

import std;
import omui.core;
import :impl;

namespace omui {

namespace {

// `inner` is `outer` or inside it, by path ("main/a" holds "main/a/b", not "main/ab").
bool within(std::string_view inner, std::string_view outer) {
    return inner == outer || (inner.size() > outer.size() && inner.starts_with(outer) && inner[outer.size()] == '/');
}

// Two claims meet: the same node (itself, or the same property), or one's subtree holding the other's node.
bool meet(const Claim& one, const ClaimRequest& other) {
    if (one.node == other.node) return one.property.empty() || other.property.empty() || one.property == other.property || one.subtree || other.subtree;
    return (one.subtree && within(other.node, one.node)) || (other.subtree && within(one.node, other.node));
}

bool person(const Actor& actor) { return actor.kind == "person"; }

}  // namespace

std::expected<Claim, Claim> Runtime::claim(const ClaimRequest& request, const Actor& by) {
    Impl& d = *impl_;
    const double now = d.now().count();
    std::erase_if(d.held_claims, [&](const Claim& held) { return held.until <= now; });
    const std::string holder = by.text();
    for (const Claim& held : d.held_claims)
        if (held.actor != holder && meet(held, request)) {
            d.record(RecordLevel::interactions, "claim", find(request.node),
                     std::format("{} asked for {}{}: {} holds it ({})", holder, request.node, request.property.empty() ? "" : "." + request.property,
                                 held.actor, held.intent));
            return std::unexpected(held);
        }
    const double seconds = std::clamp(request.seconds, 1.0, 600.0);
    // The same part again: renewed (and its intent said anew).
    for (Claim& held : d.held_claims)
        if (held.actor == holder && held.node == request.node && held.property == request.property && held.subtree == request.subtree) {
            held.until = now + seconds;
            if (!request.intent.empty()) held.intent = request.intent;
            d.actions_changed();
            return held;
        }
    Claim made{.node = request.node, .property = request.property, .subtree = request.subtree, .actor = holder, .intent = request.intent,
               .since = now, .until = now + seconds};
    d.held_claims.push_back(made);
    d.record(RecordLevel::interactions, "claim", find(request.node),
             std::format("{} holds {}{}{} for {:.0f} s: {}", holder, request.node, request.property.empty() ? "" : "." + request.property,
                         request.subtree ? " and inside it" : "", seconds, request.intent));
    d.actions_changed();  // what shows the agents shows it
    return made;
}

bool Runtime::release_claim(std::string_view node, std::string_view property, const Actor& by) {
    Impl& d = *impl_;
    const std::string holder = by.text();
    const auto before = d.held_claims.size();
    std::erase_if(d.held_claims, [&](const Claim& held) {
        return held.node == node && held.property == property && (held.actor == holder || person(by));
    });
    if (d.held_claims.size() == before) return false;
    d.record(RecordLevel::interactions, "claim", find(node), std::format("{} let go of {}{}", holder, node, property.empty() ? "" : "." + std::string(property)));
    d.actions_changed();
    return true;
}

std::vector<Claim> Runtime::claims() const {
    const double now = impl_->now().count();
    std::vector<Claim> out;
    for (const Claim& held : impl_->held_claims)
        if (held.until > now) out.push_back(held);
    return out;
}

std::optional<Claim> Runtime::claim_over(NodeId node, std::string_view actor) const {
    if (!valid(node)) return std::nullopt;
    const std::string where = path(node);
    const double now = impl_->now().count();
    for (const Claim& held : impl_->held_claims)
        if (held.until > now && held.actor != actor && (held.node == where || (held.subtree && within(where, held.node)))) return held;
    return std::nullopt;
}

std::optional<Envelope> Runtime::Impl::held_by_claim(const Invocation& call, Effect effect, NodeId node, std::string_view what) const {
    if (effect == Effect::read || replaying != 0 || person(call.actor) || held_claims.empty()) return std::nullopt;
    const auto held = self.claim_over(node, call.actor.text());
    if (!held) return std::nullopt;
    const double left = std::max(0.0, held->until - now().count());
    Envelope env;
    env.error(spec::error_code::conflict,
              std::format("{} holds '{}'{}: {} waits (it said: {}) — another part, or after its claim (D-160)", held->actor, held->node,
                          held->property.empty() ? "" : std::format(" ({})", held->property), what, held->intent.empty() ? "nothing" : held->intent),
              Value{}.set("retry_after", left).set("holder", held->actor).set("claim", held->node).set("intent", held->intent))
        .result(spec::exit_code::failure);
    return env;
}

}  // namespace omui
