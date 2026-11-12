// xengine.dynamics.inspect — what a body is doing, for an agent or a person
// looking at it (D-133 contract 8): where it is, how fast its fastest point
// goes, how far it is from its rest area, and what it touches — each contact
// a particle on a half-plane or on a ledge. Read between steps (pause a
// timeline, step it once, read): nothing here changes the body.
//
//     const auto report = xengine::dynamics::report(body);   // centre, bounds, contacts…
//     std::println("{}", xengine::dynamics::explain(body));  // the same in words
export module xengine.dynamics.inspect;

import std;
import xengine.dynamics.xpbd;

export namespace xengine::dynamics {

enum class ContactKind : std::uint8_t { plane, ledge };

struct Contact {
    std::uint32_t particle = 0;
    ContactKind   kind = ContactKind::plane;
    double        x = 0, y = 0;
};

struct BodyReport {
    double centre_x = 0, centre_y = 0;
    double left = 0, top = 0, right = 0, bottom = 0;
    double area = 0, rest_area = 0;  // signed as SoftBody::area
    double fastest = 0;              // the fastest particle's speed
    std::vector<Contact> contacts;   // what the last substep touched
};

[[nodiscard]] BodyReport report(const SoftBody& body);
// The report in a few lines of words: its place, its squash (area against
// rest), its speed, its contacts by kind, and the stages a step goes through.
[[nodiscard]] std::string explain(const SoftBody& body);

}  // namespace xengine::dynamics
