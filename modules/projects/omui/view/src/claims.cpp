// Claims (D-160): an actor holding a part of the interface while it designs
// it — a node, one of its properties, or a subtree — so that another agent's
// change there waits. The book is the agent kernel's (mlibs::agent::Claims);
// here: the runtime's clock, the record of what was asked, and the refusal of
// an agent's change to a part another holds. A person is never held.
module omui.view;

import std;
import omui.core;
import mlibs.agent;
import :impl;

namespace omui {

namespace {

bool person(const Actor& actor) { return actor.kind == "person"; }

}  // namespace

std::expected<mlibs::agent::Claim, mlibs::agent::Claim> Runtime::claim(const mlibs::agent::ClaimRequest& request, const Actor& by) {
    Impl& d = *impl_;
    const std::string holder = by.text();
    const double now = d.now().count();
    auto made = d.held_claims.claim(request, holder, now);
    if (!made) {
        const mlibs::agent::Claim& held = made.error();
        d.record(RecordLevel::interactions, "claim", find(request.part),
                 std::format("{} asked for {}{}: {} holds it ({})", holder, request.part, request.property.empty() ? "" : "." + request.property, held.actor, held.intent));
        return made;
    }
    // Made anew: on record. A renewal changes its time, and its intent when one is said.
    if (made->since == now)
        d.record(RecordLevel::interactions, "claim", find(request.part),
                 std::format("{} holds {}{}{} for {:.0f} s: {}", holder, request.part, request.property.empty() ? "" : "." + request.property,
                             request.subtree ? " and inside it" : "", made->until - now, request.intent));
    d.actions_changed();  // what shows the agents shows it
    return made;
}

bool Runtime::release_claim(std::string_view node, std::string_view property, const Actor& by) {
    Impl& d = *impl_;
    const std::string holder = by.text();
    if (!d.held_claims.release(node, property, holder, person(by))) return false;
    d.record(RecordLevel::interactions, "claim", find(node), std::format("{} let go of {}{}", holder, node, property.empty() ? "" : "." + std::string(property)));
    d.actions_changed();
    return true;
}

std::vector<mlibs::agent::Claim> Runtime::claims() const { return impl_->held_claims.held(impl_->now().count()); }

std::optional<mlibs::agent::Claim> Runtime::claim_over(NodeId node, std::string_view actor) const {
    if (!valid(node)) return std::nullopt;
    return impl_->held_claims.over(path(node), actor, impl_->now().count());
}

std::optional<Envelope> Runtime::Impl::held_by_claim(const Invocation& call, Effect effect, NodeId node, std::string_view what) const {
    if (effect == Effect::read || replaying != 0 || person(call.actor) || held_claims.empty()) return std::nullopt;
    const auto held = self.claim_over(node, call.actor.text());
    if (!held) return std::nullopt;
    const double left = std::max(0.0, held->until - now().count());
    Envelope env;
    env.error(spec::error_code::conflict,
              std::format("{} holds '{}'{}: {} waits (it said: {}) — another part, or after its claim (D-160)", held->actor, held->part,
                          held->property.empty() ? "" : std::format(" ({})", held->property), what, held->intent.empty() ? "nothing" : held->intent),
              Value{}.set("retry_after", left).set("holder", held->actor).set("claim", held->part).set("intent", held->intent))
        .result(spec::exit_code::failure);
    return env;
}

}  // namespace omui
