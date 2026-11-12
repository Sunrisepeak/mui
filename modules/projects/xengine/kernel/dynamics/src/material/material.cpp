module xengine.dynamics.material;

import std;

namespace xengine::dynamics {

std::expected<MaterialId, MaterialError> MaterialTable::add(PhysicalMaterial material) {
    const auto unit = [](double value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    if (!(std::isfinite(material.density) && material.density > 0) || !unit(material.friction) || !unit(material.restitution) ||
        !unit(material.shape_stiffness) || !(std::isfinite(material.compliance) && material.compliance >= 0))
        return std::unexpected(MaterialError::invalid_value);
    if (find(material.name)) return std::unexpected(MaterialError::duplicate_name);
    if (materials_.size() >= std::numeric_limits<MaterialId>::max()) return std::unexpected(MaterialError::full);
    materials_.push_back(std::move(material));
    return static_cast<MaterialId>(materials_.size() - 1);
}

const PhysicalMaterial* MaterialTable::at(MaterialId id) const noexcept { return id < materials_.size() ? &materials_[id] : nullptr; }

const PhysicalMaterial* MaterialTable::find(std::string_view name) const noexcept {
    const auto it = std::ranges::find(materials_, name, &PhysicalMaterial::name);
    return it == materials_.end() ? nullptr : &*it;
}

std::optional<MaterialId> MaterialTable::id_of(std::string_view name) const noexcept {
    const auto it = std::ranges::find(materials_, name, &PhysicalMaterial::name);
    if (it == materials_.end()) return std::nullopt;
    return static_cast<MaterialId>(it - materials_.begin());
}

MaterialTable MaterialTable::defaults() {
    MaterialTable table;
    for (PhysicalMaterial m : {
             PhysicalMaterial{"rubber", Phase::soft, 1.1, 0.8, 0.75, 0.0001, 0.2},
             PhysicalMaterial{"wood", Phase::solid, 0.6, 0.45, 0.35, 0, 1},
             PhysicalMaterial{"stone", Phase::solid, 2.5, 0.6, 0.15, 0, 1},
             PhysicalMaterial{"jelly", Phase::soft, 1.0, 0.4, 0.2, 0.0005, 0.012},
             PhysicalMaterial{"cloth", Phase::soft, 0.3, 0.5, 0.05, 0.001, 0},
             PhysicalMaterial{"sand", Phase::granular, 1.6, 0.7, 0.05, 0, 0},
             PhysicalMaterial{"water", Phase::liquid, 1.0, 0.02, 0.0, 0, 0},
         })
        (void)table.add(std::move(m));
    return table;
}

}  // namespace xengine::dynamics
