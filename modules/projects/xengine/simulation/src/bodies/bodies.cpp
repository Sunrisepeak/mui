module xengine.simulation.bodies;

import std;

namespace xengine::simulation {
namespace {
double signed_area(std::span<const double> x, std::span<const double> y) noexcept {
    double twice = 0;
    for (std::size_t index = 0; index < x.size(); ++index) {
        const auto next = (index + 1) % x.size();
        twice += x[index] * y[next] - x[next] * y[index];
    }
    return twice / 2;
}
bool finite(double value) noexcept {
    return std::isfinite(value);
}
}  // namespace

std::expected<SoftBody, BodyError> SoftBody::ring(std::span<const std::array<double, 2>> points,
                                                  BodyParams params,
                                                  std::span<const Plane> planes) {
    if (points.size() < 3)
        return std::unexpected(BodyError::too_few_points);
    if (points.size() > 4096)
        return std::unexpected(BodyError::too_many_points);
    for (const auto& point : points)
        if (!finite(point[0]) || !finite(point[1]))
            return std::unexpected(BodyError::nonfinite);
    for (const auto& plane : planes)
        if (!finite(plane.normal_x) || !finite(plane.normal_y) || !finite(plane.offset) ||
            !(plane.normal_x * plane.normal_x + plane.normal_y * plane.normal_y > 0))
            return std::unexpected(BodyError::invalid_params);
    if (!(params.mass > 0) || !finite(params.mass) || !(params.step > 0) || !finite(params.step) ||
        params.substeps < 1 || params.substeps > 64 || !(params.edge_compliance >= 0) ||
        !(params.area_compliance >= 0) || !(params.shape_stiffness >= 0) ||
        !(params.shape_stiffness <= 1) || !(params.restitution >= 0) ||
        !(params.restitution <= 1) || !(params.friction >= 0) || !(params.friction <= 1) ||
        !(params.damping >= 0) || !finite(params.gravity_x) || !finite(params.gravity_y))
        return std::unexpected(BodyError::invalid_params);
    SoftBody body;
    body.params = params;
    for (const auto& plane : planes) {
        const double length =
            std::sqrt(plane.normal_x * plane.normal_x + plane.normal_y * plane.normal_y);
        body.planes.push_back(
            {plane.normal_x / length, plane.normal_y / length, plane.offset / length});
    }
    const auto count = points.size();
    for (const auto& point : points) {
        body.x.push_back(point[0]);
        body.y.push_back(point[1]);
    }
    body.velocity_x.assign(count, 0);
    body.velocity_y.assign(count, 0);
    body.rest_area = signed_area(body.x, body.y);
    if (!(std::abs(body.rest_area) > 0))
        return std::unexpected(BodyError::degenerate);
    const auto [centre_x, centre_y] = body.centre();
    for (std::size_t index = 0; index < count; ++index) {
        body.rest_x.push_back(body.x[index] - centre_x);
        body.rest_y.push_back(body.y[index] - centre_y);
        const auto next = (index + 1) % count;
        body.edge_rest.push_back(
            std::sqrt((body.x[next] - body.x[index]) * (body.x[next] - body.x[index]) +
                      (body.y[next] - body.y[index]) * (body.y[next] - body.y[index])));
    }
    body.inverse_mass = static_cast<double>(count) / params.mass;
    body.scratch_x.assign(count, 0);
    body.scratch_y.assign(count, 0);
    body.moved_x.assign(count, 0);
    body.moved_y.assign(count, 0);
    body.touching.assign(count, 0);
    return body;
}

double SoftBody::area() const noexcept {
    return signed_area(x, y);
}

std::array<double, 2> SoftBody::centre() const noexcept {
    double sum_x = 0, sum_y = 0;
    for (std::size_t index = 0; index < x.size(); ++index)
        sum_x += x[index], sum_y += y[index];
    const auto count = static_cast<double>(x.size());
    return {sum_x / count, sum_y / count};
}

void SoftBodyModel::step(State& body, const Input& input) noexcept {
    const auto count = body.size();
    if (!count)
        return;
    const auto& params = body.params;
    const double substep = params.step / params.substeps;
    const double weight = body.inverse_mass;
    if (body.scratch_x.size() != count || body.moved_x.size() != count ||
        body.touching.size() != count)
        return;
    auto& before_x = body.scratch_x;
    auto& before_y = body.scratch_y;
    for (int pass = 0; pass < params.substeps; ++pass) {
        // Predict: gravity, then move.
        for (std::size_t index = 0; index < count; ++index) {
            body.velocity_x[index] += substep * params.gravity_x;
            body.velocity_y[index] += substep * params.gravity_y;
            before_x[index] = body.x[index];
            before_y[index] = body.y[index];
            body.x[index] += substep * body.velocity_x[index];
            body.y[index] += substep * body.velocity_y[index];
        }
        // Edges keep their lengths (XPBD, one iteration a substep).
        const double edge_alpha = params.edge_compliance / (substep * substep);
        for (std::size_t index = 0; index < count; ++index) {
            const auto next = (index + 1) % count;
            const double across_x = body.x[next] - body.x[index],
                         across_y = body.y[next] - body.y[index];
            const double length = std::sqrt(across_x * across_x + across_y * across_y);
            if (!(length > 0))
                continue;
            const double correction = -(length - body.edge_rest[index]) / (2 * weight + edge_alpha);
            const double unit_x = across_x / length, unit_y = across_y / length;
            body.x[index] -= weight * correction * unit_x;
            body.y[index] -= weight * correction * unit_y;
            body.x[next] += weight * correction * unit_x;
            body.y[next] += weight * correction * unit_y;
        }
        // The enclosed area holds (a pressure along each particle's area gradient).
        {
            const double error = body.area() - body.rest_area;
            double gradient_sum = 0;
            for (std::size_t index = 0; index < count; ++index) {
                const auto next = (index + 1) % count, previous = (index + count - 1) % count;
                const double gradient_x = (body.y[next] - body.y[previous]) / 2,
                             gradient_y = (body.x[previous] - body.x[next]) / 2;
                gradient_sum += weight * (gradient_x * gradient_x + gradient_y * gradient_y);
            }
            const double area_alpha = params.area_compliance / (substep * substep);
            if (gradient_sum + area_alpha > 0) {
                const double correction = -error / (gradient_sum + area_alpha);
                auto& moved_x = body.moved_x;
                auto& moved_y = body.moved_y;
                for (std::size_t index = 0; index < count; ++index) {
                    const auto next = (index + 1) % count, previous = (index + count - 1) % count;
                    moved_x[index] = weight * correction * (body.y[next] - body.y[previous]) / 2;
                    moved_y[index] = weight * correction * (body.x[previous] - body.x[next]) / 2;
                }
                for (std::size_t index = 0; index < count; ++index)
                    body.x[index] += moved_x[index], body.y[index] += moved_y[index];
            }
        }
        // Shape matching: towards the made shape, turned as the body is (2D polar decomposition).
        if (params.shape_stiffness > 0) {
            const auto [centre_x, centre_y] = body.centre();
            double cosine_part = 0, sine_part = 0;
            for (std::size_t index = 0; index < count; ++index) {
                const double offset_x = body.x[index] - centre_x,
                             offset_y = body.y[index] - centre_y;
                cosine_part += offset_x * body.rest_x[index] + offset_y * body.rest_y[index];
                sine_part += offset_y * body.rest_x[index] - offset_x * body.rest_y[index];
            }
            const double norm = std::sqrt(cosine_part * cosine_part + sine_part * sine_part);
            if (norm > 0) {
                const double cosine = cosine_part / norm, sine = sine_part / norm;
                for (std::size_t index = 0; index < count; ++index) {
                    const double goal_x =
                        centre_x + cosine * body.rest_x[index] - sine * body.rest_y[index];
                    const double goal_y =
                        centre_y + sine * body.rest_x[index] + cosine * body.rest_y[index];
                    body.x[index] += params.shape_stiffness * (goal_x - body.x[index]);
                    body.y[index] += params.shape_stiffness * (goal_y - body.y[index]);
                }
            }
        }
        // The hand.
        if (input.pin.active && input.pin.particle < count && finite(input.pin.x) &&
            finite(input.pin.y)) {
            body.x[input.pin.particle] = input.pin.x;
            body.y[input.pin.particle] = input.pin.y;
        }
        // Colliders: out of each half-plane, then the velocity from the move.
        auto& touching = body.touching;
        std::ranges::fill(touching, std::uint8_t{0});
        for (const auto& plane : body.planes)
            for (std::size_t index = 0; index < count; ++index) {
                const double depth = plane.offset - (plane.normal_x * body.x[index] +
                                                     plane.normal_y * body.y[index]);
                if (depth <= 0)
                    continue;
                body.x[index] += depth * plane.normal_x;
                body.y[index] += depth * plane.normal_y;
                touching[index] = 1;
            }
        const double keep = std::max(0.0, 1 - params.damping * substep);
        for (std::size_t index = 0; index < count; ++index) {
            const double incoming_x = body.velocity_x[index], incoming_y = body.velocity_y[index];
            body.velocity_x[index] = (body.x[index] - before_x[index]) / substep * keep;
            body.velocity_y[index] = (body.y[index] - before_y[index]) / substep * keep;
            if (!touching[index])
                continue;
            for (const auto& plane : body.planes) {
                const double gap =
                    plane.normal_x * body.x[index] + plane.normal_y * body.y[index] - plane.offset;
                if (gap > 1e-9)
                    continue;
                // Off the collider at restitution of the speed it came in with; friction on the
                // rest.
                const double normal_in = incoming_x * plane.normal_x + incoming_y * plane.normal_y;
                const double normal_now = body.velocity_x[index] * plane.normal_x +
                                          body.velocity_y[index] * plane.normal_y;
                const double tangent_x = body.velocity_x[index] - normal_now * plane.normal_x;
                const double tangent_y = body.velocity_y[index] - normal_now * plane.normal_y;
                const double bounce = normal_in < 0 ? -params.restitution * normal_in : normal_now;
                body.velocity_x[index] =
                    (1 - params.friction) * tangent_x + bounce * plane.normal_x;
                body.velocity_y[index] =
                    (1 - params.friction) * tangent_y + bounce * plane.normal_y;
            }
        }
    }
}

}  // namespace xengine::simulation
