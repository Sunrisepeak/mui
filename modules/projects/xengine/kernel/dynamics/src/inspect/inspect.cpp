module xengine.dynamics.inspect;

import std;

namespace xengine::dynamics {

BodyReport report(const SoftBody& body) {
    BodyReport out;
    if (body.size() == 0) return out;
    const auto [centre_x, centre_y] = body.centre();
    out.centre_x = centre_x, out.centre_y = centre_y;
    const auto [left, right] = std::ranges::minmax(body.x);
    const auto [top, bottom] = std::ranges::minmax(body.y);
    out.left = left, out.right = right, out.top = top, out.bottom = bottom;
    out.area = body.area();
    out.rest_area = body.rest_area;
    for (std::size_t index = 0; index < body.size(); ++index) {
        const double speed = std::sqrt(body.velocity_x[index] * body.velocity_x[index] + body.velocity_y[index] * body.velocity_y[index]);
        out.fastest = std::max(out.fastest, speed);
        if (index < body.touching.size() && body.touching[index])
            out.contacts.push_back({static_cast<std::uint32_t>(index), body.touching[index] == 2 ? ContactKind::ledge : ContactKind::plane,
                                    body.x[index], body.y[index]});
    }
    return out;
}

std::string explain(const SoftBody& body) {
    const BodyReport r = report(body);
    const auto count = [&](ContactKind kind) { return std::ranges::count(r.contacts, kind, &Contact::kind); };
    std::string text = std::format("a soft body of {} particles at ({:.1f}, {:.1f}), {:.1f} wide and {:.1f} tall\n", body.size(), r.centre_x, r.centre_y,
                                   r.right - r.left, r.bottom - r.top);
    text += std::format("its area {:.1f}% of its rest; its fastest point at {:.1f} a second\n", r.rest_area != 0 ? 100 * r.area / r.rest_area : 0.0, r.fastest);
    if (r.contacts.empty()) text += "it touches nothing (in the air)\n";
    else text += std::format("it touches: {} particles on a half-plane, {} on a ledge\n", count(ContactKind::plane), count(ContactKind::ledge));
    text += "a step:";
    for (const Stage& stage : stages()) text += std::format(" {}", stage.name);
    text += "\n";
    return text;
}

}  // namespace xengine::dynamics
