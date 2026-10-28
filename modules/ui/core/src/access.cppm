// openmui.core:access — what an agent may do over the bus (spec S-11), decided
// as openas decides it (openas §5): the application's cap, set when it is
// built; the grants the person gave; the person's own verbs, which no grant
// reaches. The program's own requests (its scripts, its host) are not the bus
// and are not decided here.
export module openmui.core:access;

import std;
export import openas;
import :base;
import :verb;

export namespace omui {

// "none", or an effect: the highest a request over the bus may have.
[[nodiscard]] std::optional<openas::Cap> parse_cap(std::string_view text) noexcept;
[[nodiscard]] std::string cap_name(const openas::Cap& cap);

// The process's cap and grants. Grants are given by the person, present:
// the program's own code calls `give` from what the person did (a setting
// turned on, a dialog answered) — nothing that arrives over the bus can.
class AgentAccess {
public:
    void set_cap(openas::Cap cap);
    [[nodiscard]] openas::Cap cap() const;

    void give(openas::Grant grant);
    // Every grant to `actor`; how many there were.
    std::size_t revoke(const openas::Actor& actor);
    void revoke_all();
    [[nodiscard]] std::vector<openas::Grant> grants() const;

    // A request for `name` (a verb, or a node's action without one), of
    // `effect`, from `actor` over the bus (openas §5.3).
    [[nodiscard]] openas::Decision decide(std::string_view name, Effect effect, bool person, const Actor& actor) const;

private:
    mutable std::mutex         mutex_;
    openas::Cap                cap_;
    std::vector<openas::Grant> grants_;
};

[[nodiscard]] AgentAccess& agent_access();

// The request's clearance: `refuse` (above the cap), `run` (within a grant:
// no answer is needed) or `ask` (S-1 as usual). A request that is not remote
// is the program's own: `ask`.
[[nodiscard]] openas::Decision clearance(std::string_view name, Effect effect, bool person, const Invocation& call);

// The refusal of a request above the application's cap (S-11).
[[nodiscard]] Envelope above_cap(std::string_view what);

// openmui's actor as openas reads it (a kind openas does not know is a program).
[[nodiscard]] openas::Actor to_openas(const Actor& actor);

}  // namespace omui
