module xrender.asset;

import std;

namespace xrender::asset {

namespace {

// A path as UTF-8 text (`string()` narrows through the code page on Windows).
std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

}  // namespace

std::size_t Imported::triangles() const noexcept {
    std::size_t total = 0;
    for (const mlibs::geom::ClusteredMesh& each : meshes) total += each.mesh.triangles();
    return total;
}

mlibs::geom::Box3 Imported::bounds() const noexcept {
    mlibs::geom::Box3 all;
    for (const scene::Instance& instance : instances) all.join(mlibs::geom::moved(instance.world, instance.bounds));
    return all;
}

std::expected<void, std::string> place(const Imported& imported, scene::Scene& scene, Base base) {
    std::size_t added = 0, lit = 0;
    std::string why;
    for (const scene::Instance& each : imported.instances) {
        scene::Instance instance = each;
        instance.mesh += base.mesh, instance.material += base.material;
        if (const auto done = scene.add(instance); !done) {
            why = std::format("instance {}: {}", each.id, scene::text(done.error()));
            break;
        }
        ++added;
    }
    if (why.empty())
        for (const scene::Light& light : imported.lights) {
            if (const auto done = scene.add(light); !done) {
                why = std::format("light {}: {}", light.id, scene::text(done.error()));
                break;
            }
            ++lit;
        }
    if (why.empty()) return {};
    // The scene as it was: what was added is taken out again.
    for (std::size_t index = 0; index < lit; ++index) (void)scene.remove_light(imported.lights[index].id);
    for (std::size_t index = 0; index < added; ++index) (void)scene.remove(imported.instances[index].id);
    return std::unexpected(std::move(why));
}

std::expected<Imported, std::string> gltf_file(const std::filesystem::path& file, Options options) {
    const auto read = [](const std::filesystem::path& path) -> std::expected<std::vector<std::uint8_t>, std::string> {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::unexpected(std::format("{} cannot be opened", utf8(path)));
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return bytes;
    };
    const auto bytes = read(file);
    if (!bytes) return std::unexpected(bytes.error());
    if (!options.read) {
        // What it names beside itself is under its own directory, and nowhere else: a name that climbs out is refused.
        const std::filesystem::path beside = file.parent_path();
        options.read = [beside, read](std::string_view name) -> std::expected<std::vector<std::uint8_t>, std::string> {
            const std::filesystem::path named(name);
            if (named.is_absolute() || std::ranges::contains(named, std::filesystem::path(".."))) return std::unexpected(std::string("a name outside the document's directory"));
            return read(beside / named);
        };
    }
    return gltf(*bytes, options);
}

}  // namespace xrender::asset
