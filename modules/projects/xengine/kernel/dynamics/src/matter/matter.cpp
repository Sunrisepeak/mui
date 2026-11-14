module xengine.dynamics.matter;

import std;

namespace xengine::dynamics {

BodyParams params_of(const xengine::matter::Program& matter, xengine::matter::MaterialId material, double area, BodyParams base) noexcept {
    if (material >= matter.materials().size()) return base;
    const auto read = [&](std::string_view column, double otherwise) {
        const auto index = matter.column(column);
        return index ? matter.number(material, *index) : otherwise;
    };
    base.mass = read("density", 1000) / 1000 * (std::isfinite(area) && area > 0 ? area : 1);
    base.friction = read("friction", base.friction);
    base.restitution = read("restitution", base.restitution);
    base.edge_compliance = read("compliance", base.edge_compliance);
    base.shape_stiffness = read("shape_stiffness", base.shape_stiffness);
    return base;
}

}  // namespace xengine::dynamics
