module mlibs.geom.api;

import std;
import mlibs.math;

namespace mlibs::geom {

namespace {

enum Kind : std::uint32_t { positions = 1, normals = 2, tangents = 3, uvs = 4, indices = 5, clusters = 6 };
constexpr std::size_t header_bytes = 16, entry_bytes = 24, cluster_bytes = 52;

struct Writer {
    std::vector<std::uint8_t> bytes;
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void vec3(Vec3 value) { f32(value.x), f32(value.y), f32(value.z); }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t at = 0;
    std::uint32_t u32() noexcept {
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index) value |= static_cast<std::uint32_t>(bytes[at++]) << (8 * index);
        return value;
    }
    std::uint64_t u64() noexcept {
        std::uint64_t value = 0;
        for (int index = 0; index < 8; ++index) value |= static_cast<std::uint64_t>(bytes[at++]) << (8 * index);
        return value;
    }
    float f32() noexcept { return std::bit_cast<float>(u32()); }
    Vec3 vec3() noexcept {
        const float x = f32(), y = f32(), z = f32();
        return {x, y, z};
    }
};
std::string_view name_of(std::uint32_t kind) noexcept {
    switch (kind) {
        case positions: return "positions";
        case normals: return "normals";
        case tangents: return "tangents";
        case uvs: return "uvs";
        case indices: return "indices";
        case clusters: return "clusters";
    }
    return "";
}
std::uint32_t element_of(std::uint32_t kind) noexcept {
    switch (kind) {
        case positions:
        case normals: return 12;
        case tangents: return 16;
        case uvs: return 8;
        case indices: return 4;
        case clusters: return cluster_bytes;
    }
    return 0;
}

}  // namespace

std::vector<std::uint8_t> stored(const ClusteredMesh& mesh) {
    struct Section {
        std::uint32_t kind;
        std::uint64_t count;
    };
    std::vector<Section> sections;
    const auto add = [&](std::uint32_t kind, std::size_t count) {
        if (count) sections.push_back({kind, count});
    };
    add(positions, mesh.mesh.positions.size()), add(normals, mesh.mesh.normals.size()), add(tangents, mesh.mesh.tangents.size()), add(uvs, mesh.mesh.uvs.size());
    add(indices, mesh.mesh.indices.size()), add(clusters, mesh.clusters.size());
    Writer out;
    out.bytes.insert(out.bytes.end(), stored_magic.begin(), stored_magic.end());
    out.u32(ClusteredMesh::version), out.u32(static_cast<std::uint32_t>(sections.size())), out.u32(0);
    std::uint64_t place = header_bytes + sections.size() * entry_bytes;
    for (const Section& section : sections) {
        out.u32(section.kind), out.u32(element_of(section.kind)), out.u64(place), out.u64(section.count);
        place += section.count * element_of(section.kind);
    }
    out.bytes.reserve(static_cast<std::size_t>(place));
    for (const Section& section : sections) switch (section.kind) {
            case positions:
                for (const Vec3 value : mesh.mesh.positions) out.vec3(value);
                break;
            case normals:
                for (const Vec3 value : mesh.mesh.normals) out.vec3(value);
                break;
            case tangents:
                for (const Vec4& value : mesh.mesh.tangents) out.f32(value.x), out.f32(value.y), out.f32(value.z), out.f32(value.w);
                break;
            case uvs:
                for (const Vec2 value : mesh.mesh.uvs) out.f32(value.x), out.f32(value.y);
                break;
            case indices:
                for (const std::uint32_t value : mesh.mesh.indices) out.u32(value);
                break;
            case clusters:
                for (const Cluster& cluster : mesh.clusters) {
                    out.u32(cluster.first_index), out.u32(cluster.triangle_count), out.vec3(cluster.bounds.centre), out.f32(cluster.bounds.radius);
                    out.vec3(cluster.cone_axis), out.f32(cluster.cone_cutoff), out.f32(cluster.error), out.f32(cluster.parent_error), out.u32(cluster.level);
                }
                break;
        }
    return std::move(out.bytes);
}

std::expected<ClusteredMesh, std::string> read_stored(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < header_bytes || !std::ranges::equal(bytes.first(4), stored_magic)) return std::unexpected(std::string("not a stored mesh: it does not begin with OMSH"));
    Reader header{bytes, 4};
    const std::uint32_t version = header.u32(), count = header.u32();
    (void)header.u32();
    if (version == 0 || version > ClusteredMesh::version)
        return std::unexpected(std::format("a stored mesh of version {}, and this reads up to {}", version, ClusteredMesh::version));
    if (count > (bytes.size() - header_bytes) / entry_bytes) return std::unexpected(std::format("{} sections said, and the bytes have no room for their table", count));
    ClusteredMesh out;
    std::uint32_t seen = 0;
    for (std::uint32_t section = 0; section < count; ++section) {
        const std::uint32_t kind = header.u32(), element = header.u32();
        const std::uint64_t place = header.u64(), elements = header.u64();
        const std::uint32_t known = element_of(kind);
        if (!known) continue;  // a kind added later: passed over
        const std::string_view name = name_of(kind);
        if (seen >> kind & 1) return std::unexpected(std::format("the section `{}` is there twice", name));
        seen |= 1u << kind;
        if (element != known) return std::unexpected(std::format("the section `{}` says {} bytes an element, and one is {}", name, element, known));
        if (place > bytes.size() || elements > (bytes.size() - place) / known)
            return std::unexpected(std::format("the section `{}` ({} elements from byte {}) runs past the end of {} bytes", name, elements, place, bytes.size()));
        Reader in{bytes, static_cast<std::size_t>(place)};
        const std::size_t many = static_cast<std::size_t>(elements);
        switch (kind) {
            case positions:
                out.mesh.positions.resize(many);
                for (Vec3& value : out.mesh.positions) value = in.vec3();
                break;
            case normals:
                out.mesh.normals.resize(many);
                for (Vec3& value : out.mesh.normals) value = in.vec3();
                break;
            case tangents:
                out.mesh.tangents.resize(many);
                for (Vec4& value : out.mesh.tangents) value.x = in.f32(), value.y = in.f32(), value.z = in.f32(), value.w = in.f32();
                break;
            case uvs:
                out.mesh.uvs.resize(many);
                for (Vec2& value : out.mesh.uvs) value.x = in.f32(), value.y = in.f32();
                break;
            case indices:
                out.mesh.indices.resize(many);
                for (std::uint32_t& value : out.mesh.indices) value = in.u32();
                break;
            case clusters:
                out.clusters.resize(many);
                for (Cluster& cluster : out.clusters) {
                    cluster.first_index = in.u32(), cluster.triangle_count = in.u32(), cluster.bounds.centre = in.vec3(), cluster.bounds.radius = in.f32();
                    cluster.cone_axis = in.vec3(), cluster.cone_cutoff = in.f32(), cluster.error = in.f32(), cluster.parent_error = in.f32(), cluster.level = in.u32();
                }
                break;
        }
    }
    if (auto sound = check(out.mesh); !sound) return std::unexpected(std::format("the mesh stored is not one: {}", sound.error()));
    // The clusters: each a run of the indices, one after another, all of them and no more.
    std::uint64_t next = 0;
    for (std::size_t index = 0; index < out.clusters.size(); ++index) {
        const Cluster& cluster = out.clusters[index];
        if (cluster.first_index != next || cluster.triangle_count == 0)
            return std::unexpected(std::format("cluster {} begins at index {} with {} triangles, where the one before it ended at {}", index, cluster.first_index, cluster.triangle_count, next));
        next += std::uint64_t{cluster.triangle_count} * 3;
        if (next > out.mesh.indices.size()) return std::unexpected(std::format("cluster {} runs past the mesh's {} indices", index, out.mesh.indices.size()));
        if (!std::isfinite(cluster.bounds.radius) || !std::isfinite(cluster.bounds.centre.x) || !std::isfinite(cluster.bounds.centre.y) || !std::isfinite(cluster.bounds.centre.z) || cluster.bounds.radius < 0)
            return std::unexpected(std::format("cluster {} has no bounds", index));
    }
    if (next != out.mesh.indices.size()) return std::unexpected(std::format("the clusters hold {} indices of the mesh's {}", next, out.mesh.indices.size()));
    return out;
}

}  // namespace mlibs::geom
