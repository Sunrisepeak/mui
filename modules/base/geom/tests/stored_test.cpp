// A mesh in clusters as bytes (mlibs.geom.api:stored; the foundation plan's §9.4): written and read back it is the
// same mesh bit for bit; the bytes of a mesh are pinned (a file written today is read tomorrow); a section of a
// kind not known is passed over; and bytes that are not a mesh — cut anywhere, or saying what is not so — are
// refused with the reason, never read past.
import std;
import mlibs.geom;
import mlibs.math;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace geom = mlibs::geom;

namespace {

std::uint64_t bits(std::span<const std::uint8_t> bytes) {
    mlibs::hash::Fnv1a hash;
    for (const std::uint8_t byte : bytes) hash.le(byte, 1);
    return hash.value();
}
void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) bytes[at + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(value >> (8 * index));
}
void put64(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint64_t value) {
    for (int index = 0; index < 8; ++index) bytes[at + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(value >> (8 * index));
}
// Where section `number`'s entry is in the table.
constexpr std::size_t entry(std::size_t number) { return 16 + number * 24; }

}  // namespace

static UTest cases{"geom.stored", [] {
    "written and read back it is the same mesh, bit for bit — every shape made by code, and one with positions alone"_test = [] {
        geom::Mesh bare;
        bare.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0.5f}};
        bare.indices = {0, 1, 2, 2, 1, 3};
        // And a box with a second set of coordinates and a colour at each corner.
        geom::Mesh coloured = geom::box({1, 1, 1});
        for (std::size_t corner = 0; corner < coloured.positions.size(); ++corner) {
            const float share = static_cast<float>(corner) / static_cast<float>(coloured.positions.size());
            coloured.second_uvs.push_back({share, 1 - share}), coloured.colours.push_back({share, 0.5f, 1 - share, 0.25f + share * 0.5f});
        }
        const std::array<geom::ClusteredMesh, 6> meshes{*geom::clustered(geom::box({1, 2, 3})), *geom::clustered(geom::sheet(4, 4, 8, 8), 16), *geom::clustered(geom::sphere(1, 16, 32), 32),
                                                        *geom::clustered(geom::cylinder(0.5f, 2, 24)), *geom::clustered(bare), *geom::clustered(coloured)};
        for (const geom::ClusteredMesh& mesh : meshes) {
            const std::vector<std::uint8_t> bytes = geom::stored(mesh);
            const auto back = geom::read_stored(bytes);
            expect(fatal(back.has_value())) << (back ? "" : back.error());
            // Written again: the same bytes — so every number of it came back as it went.
            expect(geom::stored(*back) == bytes);
            expect(eq(back->mesh.positions.size(), mesh.mesh.positions.size()) && eq(back->mesh.indices.size(), mesh.mesh.indices.size()) && eq(back->clusters.size(), mesh.clusters.size()));
            expect(eq(back->mesh.normals.size(), mesh.mesh.normals.size()) && eq(back->mesh.tangents.size(), mesh.mesh.tangents.size()) && eq(back->mesh.uvs.size(), mesh.mesh.uvs.size()));
            expect(eq(back->mesh.second_uvs.size(), mesh.mesh.second_uvs.size()) && eq(back->mesh.colours.size(), mesh.mesh.colours.size()));
            expect(std::isinf(back->clusters[0].parent_error)) << "what a number is, it stays: one level has no parent";
        }
    };
    "a mesh's bytes are pinned: a file written today is one read tomorrow"_test = [] {
        const std::vector<std::uint8_t> bytes = geom::stored(*geom::clustered(geom::sphere(1, 8, 16), 32));
        std::println("a sphere of 8 × 16: {} bytes, bits {:016x}", bytes.size(), bits(bytes));
        expect(bytes[0] == 'O' && bytes[1] == 'M' && bytes[2] == 'S' && bytes[3] == 'H' && bytes[4] == 1 && bytes[8] == 6);
        expect(eq(bits(bytes), std::uint64_t{0x05f3f67e6163ddf6})) << std::format("{:016x}", bits(bytes));
    };
    "a section of a kind this does not know is passed over: the rest is read"_test = [] {
        const geom::ClusteredMesh mesh = *geom::clustered(geom::box({1, 1, 1}));
        std::vector<std::uint8_t> bytes = geom::stored(mesh);
        // The third section is the tangents: called something from later.
        put32(bytes, entry(2), 99);
        const auto back = geom::read_stored(bytes);
        expect(fatal(back.has_value())) << (back ? "" : back.error());
        expect(back->mesh.tangents.empty() && eq(back->mesh.positions.size(), mesh.mesh.positions.size()) && eq(back->mesh.uvs.size(), mesh.mesh.uvs.size()) && eq(back->clusters.size(), mesh.clusters.size()));
    };
    "bytes that are not a mesh are refused with the reason, and never read past: cut at every length, and each thing a file can say that is not so"_test = [] {
        const geom::ClusteredMesh mesh = *geom::clustered(geom::box({1, 1, 1}));
        const std::vector<std::uint8_t> bytes = geom::stored(mesh);
        std::size_t read = 0;
        for (std::size_t length = 0; length < bytes.size(); ++length) read += geom::read_stored(std::span(bytes).first(length)).has_value();
        expect(eq(read, std::size_t{0})) << "no shorter run of its bytes is a mesh";
        const auto refused = [&](std::string_view what, auto&& change) {
            std::vector<std::uint8_t> changed = bytes;
            change(changed);
            const auto back = geom::read_stored(changed);
            expect(!back.has_value() && back.error().contains(what)) << what << ": " << (back ? "read" : back.error());
        };
        refused("OMSH", [](auto& b) { b[0] = 'X'; });
        refused("version 2", [](auto& b) { put32(b, 4, 2); });
        refused("no room for their table", [](auto& b) { put32(b, 8, 100000); });
        refused("runs past the end", [](auto& b) { put64(b, entry(0) + 16, 1u << 30); });
        refused("runs past the end", [](auto& b) { put64(b, entry(4) + 8, std::uint64_t{1} << 60); });
        refused("bytes an element", [](auto& b) { put32(b, entry(0) + 4, 16); });
        refused("there twice", [](auto& b) { put32(b, entry(1), 1); });
        // What the numbers say: an index that names no position; clusters that do not hold the indices.
        const std::size_t indices_at = static_cast<std::size_t>(bytes[entry(4) + 8]) | static_cast<std::size_t>(bytes[entry(4) + 9]) << 8;
        refused("names position", [&](auto& b) { put32(b, indices_at, 1000); });
        const std::size_t clusters_at = static_cast<std::size_t>(bytes[entry(5) + 8]) | static_cast<std::size_t>(bytes[entry(5) + 9]) << 8;
        refused("triangles", [&](auto& b) { put32(b, clusters_at + 4, 0); });
        refused("runs past", [&](auto& b) { put32(b, clusters_at + 4, 1000); });
        refused("clusters hold", [&](auto& b) { put32(b, clusters_at + 4, 11); });
        std::println("a box: {} bytes; cut at each of {} lengths it is refused", bytes.size(), bytes.size());
    };

    "a mesh put into another at a place: moved, its normals turned as a surface is, its triangles after; kinds that differ refused"_test = [] {
        namespace math = mlibs::math;
        geom::Mesh level;
        expect(geom::append(level, geom::box({1, 1, 1}), math::Mat4{}).has_value());
        const std::size_t first = level.positions.size(), triangles = level.triangles();
        // Twice as wide along x, turned a quarter about y, 5 m along z.
        const math::Mat4 place = math::translation({0, 0, 5}) * math::rotation(math::Quat::about({0, 1, 0}, 1.5707964f)) * math::scaling({2, 1, 1});
        expect(geom::append(level, geom::box({1, 1, 1}), place).has_value());
        expect(eq(level.positions.size(), 2 * first) && eq(level.triangles(), 2 * triangles) && geom::check(level).has_value());
        const geom::Box3 bounds = geom::bounds(level);
        expect(lt(std::abs(bounds.high.z - 7.0f), 1e-5f) && lt(std::abs(bounds.low.z + 1.0f), 1e-5f)) << "the second box 2 m deep along z (its x turned onto z), at 5";
        for (std::size_t corner = first; corner < level.positions.size(); ++corner) expect(lt(std::abs(math::length(level.normals[corner]) - 1), 1e-5f));
        for (std::size_t index = triangles * 3; index < level.indices.size(); ++index) expect(ge(level.indices[index], static_cast<std::uint32_t>(first)));
        geom::Mesh bare;
        bare.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, bare.indices = {0, 1, 2};
        const std::size_t before = level.positions.size();
        expect(!geom::append(level, bare, math::Mat4{}).has_value() && eq(level.positions.size(), before)) << "no normals against normals";
    };
}};