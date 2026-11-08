// xengine.dynamics.material — physical materials (D-133 §1): how a thing
// behaves, not how it looks (a render material is xrender's; the two meet by
// a surface type's id). One table every solver family reads; the cellular
// rules' material presets read it too.
//
//     MaterialTable table = MaterialTable::defaults();
//     const PhysicalMaterial& jelly = *table.find("jelly");
//     auto params = xengine::dynamics::params_of(jelly);   // a soft body's
export module xengine.dynamics.material;

import std;

export namespace xengine::dynamics {

enum class Phase : std::uint8_t { solid, soft, granular, liquid, gas };

struct PhysicalMaterial {
    std::string name;
    Phase  phase = Phase::solid;
    double density = 1;        // per unit of area (2D) or volume (3D), the simulation's units
    double friction = 0.3;     // of the tangential speed lost on contact, 0 to 1
    double restitution = 0.4;  // of the normal speed kept off a collider, 0 to 1
    double compliance = 0;     // XPBD compliance of its stretch (0: rigid)
    double shape_stiffness = 0.05;  // a soft solid's spring back to its shape, 0 to 1 a substep
};

using MaterialId = std::uint16_t;

enum class MaterialError : std::uint8_t { duplicate_name, invalid_value, full };

[[nodiscard]] constexpr std::string_view describe(MaterialError e) noexcept {
    switch (e) {
        case MaterialError::duplicate_name: return "a material of that name is in the table";
        case MaterialError::invalid_value: return "a material's value is out of range (friction and restitution 0 to 1, density above 0)";
        case MaterialError::full: return "the table holds 65535 materials";
    }
    return "?";
}

// Materials by id and by name; ids are stable for the table's life (a
// surface type in a scene names one).
class MaterialTable {
public:
    [[nodiscard]] std::expected<MaterialId, MaterialError> add(PhysicalMaterial material);
    [[nodiscard]] const PhysicalMaterial* at(MaterialId id) const noexcept;
    [[nodiscard]] const PhysicalMaterial* find(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<MaterialId> id_of(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return materials_.size(); }
    // A table to start from: rubber, wood, stone, jelly, cloth, sand, water.
    [[nodiscard]] static MaterialTable defaults();

private:
    std::vector<PhysicalMaterial> materials_;
};

}  // namespace xengine::dynamics
