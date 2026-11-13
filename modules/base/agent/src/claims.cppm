// mlibs.agent:claims — who holds which part of a subject while working on it (D-160; D-173: the kernel's, for
// any domain). A part is addressed by a path of words ("main/settings/notifications" in an interface,
// "materials/wood" or "laws/burns" in a definition of matter): the part itself, one property of it, or it and
// everything inside it. Claims stand together unless they meet; one that meets another's waits. A claim is a
// lease: it ends unless renewed. What a claim keeps others from doing — and that a person is never held — is
// the subject's to enforce where its operations are decided; this is the book.
export module mlibs.agent:claims;

import std;

export namespace mlibs::agent {

struct Claim {
    std::string part;             // the part's path
    std::string property;         // "": the part; a property's name: that alone
    bool        subtree = false;  // and everything inside the part
    std::string actor;            // who holds it ("agent:a;via=session:1")
    std::string intent;           // what it is doing there, as it said
    double      since = 0, until = 0;  // the lease, in seconds of the owner's clock
};

// What a claim asks for: the part, what for, and how long (renewed by asking again).
struct ClaimRequest {
    std::string part;
    std::string property;
    bool        subtree = false;
    std::string intent;
    double      seconds = 30;
};

// `inner` is `outer` or inside it, by path ("main/a" holds "main/a/b", not "main/ab").
[[nodiscard]] bool within(std::string_view inner, std::string_view outer) noexcept;

class Claims {
public:
    // The part for `actor`, from `now` for the seconds asked (1 … 600). Two claims meet when they name the same
    // part (itself, or the same property), or one's subtree holds the other's part. The same actor asking for
    // the same part again renews it (and says its intent anew). Error: the claim in the way.
    std::expected<Claim, Claim> claim(const ClaimRequest& request, std::string_view actor, double now);
    // Lets go of the part as `actor` holds it — or as anyone does, when `anyones` (a person's word). Whether
    // one was held.
    bool release(std::string_view part, std::string_view property, std::string_view actor, bool anyones = false);
    // Every claim let go: a person took over.
    void clear() noexcept { held_.clear(); }
    // The claims that hold at `now`, oldest first.
    [[nodiscard]] std::vector<Claim> held(double now) const;
    // The claim another than `actor` holds over a part at `now` — on the part itself or a property of it, or a
    // subtree around it — if one.
    [[nodiscard]] std::optional<Claim> over(std::string_view part, std::string_view actor, double now) const;
    [[nodiscard]] bool empty() const noexcept { return held_.empty(); }

private:
    std::vector<Claim> held_;  // oldest first; those whose lease ended are dropped as claims are made
};

}  // namespace mlibs::agent
