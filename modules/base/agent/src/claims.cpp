// The book of claims (mlibs.agent:claims; D-160).
module mlibs.agent;

import std;

namespace mlibs::agent {

bool within(std::string_view inner, std::string_view outer) noexcept {
    return inner == outer || (inner.size() > outer.size() && inner.starts_with(outer) && inner[outer.size()] == '/');
}

namespace {

// Two claims meet: the same part (itself, or the same property), or one's subtree holding the other's part.
bool meet(const Claim& one, const ClaimRequest& other) {
    if (one.part == other.part) return one.property.empty() || other.property.empty() || one.property == other.property || one.subtree || other.subtree;
    return (one.subtree && within(other.part, one.part)) || (other.subtree && within(one.part, other.part));
}

}  // namespace

std::expected<Claim, Claim> Claims::claim(const ClaimRequest& request, std::string_view actor, double now) {
    std::erase_if(held_, [&](const Claim& held) { return held.until <= now; });
    for (const Claim& held : held_)
        if (held.actor != actor && meet(held, request)) return std::unexpected(held);
    const double seconds = std::clamp(request.seconds, 1.0, 600.0);
    for (Claim& held : held_)
        if (held.actor == actor && held.part == request.part && held.property == request.property && held.subtree == request.subtree) {
            held.until = now + seconds;
            if (!request.intent.empty()) held.intent = request.intent;
            return held;
        }
    held_.push_back({.part = request.part, .property = request.property, .subtree = request.subtree, .actor = std::string(actor), .intent = request.intent,
                     .since = now, .until = now + seconds});
    return held_.back();
}

bool Claims::release(std::string_view part, std::string_view property, std::string_view actor, bool anyones) {
    return std::erase_if(held_, [&](const Claim& held) { return held.part == part && held.property == property && (anyones || held.actor == actor); }) != 0;
}

std::vector<Claim> Claims::held(double now) const {
    std::vector<Claim> out;
    for (const Claim& held : held_)
        if (held.until > now) out.push_back(held);
    return out;
}

std::optional<Claim> Claims::over(std::string_view part, std::string_view actor, double now) const {
    for (const Claim& held : held_)
        if (held.until > now && held.actor != actor && (held.part == part || (held.subtree && within(part, held.part)))) return held;
    return std::nullopt;
}

}  // namespace mlibs::agent
