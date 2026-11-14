// xrender.asset (the foundation plan's §5.5, M6.3; D-171): a glTF document becomes what the scene and the mesh
// pass are given — meshes in clusters, materials, a texture, instances and lights under ids that are the file's
// places; a .glb is the same thing as the .gltf; what the document says that is not carried is a note with where
// it is; what is not a document is refused with its reason; and a mesh ordered for drawing is the same triangles.
import std;
import xrender.asset;
import mlibs.base64;
import mlibs.image;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace asset = xrender::asset;
namespace scene = xrender::scene;
namespace geom = mlibs::geom;
namespace math = mlibs::math;

namespace {

template <typename Number>
void put(std::vector<std::uint8_t>& bytes, Number value) {
    const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(Number));
}
// A square in the x–y plane facing +z: four positions, four normals, four texture coordinates (132 bytes of
// floats… 48 + 48 + 32 = 128), then six 16-bit indices (12 bytes): 140 bytes.
std::vector<std::uint8_t> square_bytes() {
    std::vector<std::uint8_t> bytes;
    for (const auto& [x, y] : {std::pair{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}}) put(bytes, x), put(bytes, y), put(bytes, 0.f);
    for (int corner = 0; corner < 4; ++corner) put(bytes, 0.f), put(bytes, 0.f), put(bytes, 1.f);
    for (const auto& [across, down] : {std::pair{0.f, 1.f}, {1.f, 1.f}, {1.f, 0.f}, {0.f, 0.f}}) put(bytes, across), put(bytes, down);
    for (const std::uint16_t index : {0, 1, 2, 0, 2, 3}) put(bytes, index);
    return bytes;
}
std::string uri_of(std::span<const std::uint8_t> bytes, std::string_view type = "application/octet-stream") {
    return std::format("data:{};base64,{}", type, mlibs::base64::encode(bytes));
}
// A 2 × 2 picture: red, green, blue, white.
std::vector<std::uint8_t> picture_png() {
    mlibs::image::Image image{.width = 2, .height = 2, .rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255}};
    return *mlibs::image::encode_png(mlibs::image::view(image));
}
// What every document here shares: the square's views and accessors over buffer 0.
constexpr std::string_view square_parts = R"(
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 48}, {"buffer": 0, "byteOffset": 48, "byteLength": 48},
                  {"buffer": 0, "byteOffset": 96, "byteLength": 32}, {"buffer": 0, "byteOffset": 128, "byteLength": 12}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
                {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
                {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
                {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}])";
// A document of the square: `buffer` is its buffer's "uri" member (or nothing, for a .glb), `rest` further members.
std::string document(std::string_view buffer, std::string_view rest) {
    return std::format(R"({{"asset": {{"version": "2.0"}}, "buffers": [{{"byteLength": 140{}}}],{}{}}})", buffer, square_parts, rest);
}
std::string with_uri(std::string_view rest) { return document(std::format(R"(, "uri": "{}")", uri_of(square_bytes())), rest); }
std::span<const std::uint8_t> bytes_of(std::string_view text) { return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}; }

// Two nodes of one mesh — the second the first's child, twice the size — a material with a picture, a sun.
std::string whole() {
    return with_uri(std::format(R"(,
  "scene": 0, "scenes": [{{"nodes": [0, 2]}}],
  "nodes": [{{"name": "first", "mesh": 0, "translation": [1, 2, 3], "children": [1]}},
            {{"name": "second", "mesh": 0, "translation": [0, 0, 1], "scale": [2, 2, 2]}},
            {{"name": "the sun", "rotation": [-0.7071068, 0, 0, 0.7071068], "extensions": {{"KHR_lights_punctual": {{"light": 0}}}}}}],
  "meshes": [{{"name": "square", "primitives": [{{"attributes": {{"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}}, "indices": 3, "material": 0}}]}}],
  "materials": [{{"name": "painted", "pbrMetallicRoughness": {{"baseColorFactor": [0.5, 0.25, 1, 1], "metallicFactor": 0.25, "roughnessFactor": 0.5,
                 "baseColorTexture": {{"index": 0}}, "metallicRoughnessTexture": {{"index": 1}}}}, "emissiveFactor": [0.1, 0.2, 0.3],
                 "normalTexture": {{"index": 1, "scale": 0.5}}, "occlusionTexture": {{"index": 1, "strength": 0.75}}, "emissiveTexture": {{"index": 0}}}}],
  "textures": [{{"source": 0}}, {{"source": 1}}],
  "images": [{{"uri": "{0}"}}, {{"uri": "{0}"}}],
  "extensionsUsed": ["KHR_lights_punctual"],
  "extensions": {{"KHR_lights_punctual": {{"lights": [{{"type": "directional", "color": [1, 0.5, 0.25], "intensity": 3}}]}}}})",
                                uri_of(picture_png(), "image/png")));
}

// A .glb of a document and its buffer.
std::vector<std::uint8_t> glb(std::string json, std::vector<std::uint8_t> buffer) {
    while (json.size() % 4) json += ' ';
    while (buffer.size() % 4) buffer.push_back(0);
    std::vector<std::uint8_t> bytes;
    put(bytes, std::uint32_t{0x46546C67}), put(bytes, std::uint32_t{2}), put(bytes, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + buffer.size()));
    put(bytes, static_cast<std::uint32_t>(json.size())), put(bytes, std::uint32_t{0x4E4F534A});
    bytes.insert(bytes.end(), json.begin(), json.end());
    put(bytes, static_cast<std::uint32_t>(buffer.size())), put(bytes, std::uint32_t{0x004E4942});
    bytes.insert(bytes.end(), buffer.begin(), buffer.end());
    return bytes;
}

bool noted(const asset::Imported& imported, std::string_view where) {
    return std::ranges::any_of(imported.notes, [&](const asset::Note& note) { return note.where == where && !note.what.empty() && !note.instead.empty(); });
}
// A mesh's triangles, each from its least corner's place, in order: the same for two meshes of the same triangles
// whatever order they and their corners are in.
std::vector<std::array<float, 9>> triangles_of(const geom::Mesh& mesh) {
    std::vector<std::array<float, 9>> out;
    for (std::size_t at = 0; at + 2 < mesh.indices.size(); at += 3) {
        std::array<math::Vec3, 3> corners{mesh.positions[mesh.indices[at]], mesh.positions[mesh.indices[at + 1]], mesh.positions[mesh.indices[at + 2]]};
        const auto key = [](math::Vec3 corner) { return std::tuple{corner.x, corner.y, corner.z}; };
        std::size_t least = 0;
        for (std::size_t corner = 1; corner < 3; ++corner)
            if (key(corners[corner]) < key(corners[least])) least = corner;
        std::ranges::rotate(corners, corners.begin() + static_cast<std::ptrdiff_t>(least));
        out.push_back({corners[0].x, corners[0].y, corners[0].z, corners[1].x, corners[1].y, corners[1].z, corners[2].x, corners[2].y, corners[2].z});
    }
    std::ranges::sort(out);
    return out;
}

}  // namespace

static UTest cases{"xrender.asset", [] {
    "a document becomes meshes in clusters, materials, a texture, instances and a light under the file's places"_test = [] {
        const std::string text = whole();
        const auto model = asset::gltf(bytes_of(text));
        expect(fatal(model.has_value())) << (model ? "" : model.error());
        // Two pictures: the first as colours (the base colour and what is given off name it), the second as plain
        // numbers (roughness and metal, the normal and shade name it) — a texture each.
        expect(fatal(eq(model->meshes.size(), std::size_t{1}) && eq(model->materials.size(), std::size_t{1}) && eq(model->textures.size(), std::size_t{2})));
        expect(eq(model->triangles(), std::size_t{2}) && geom::check(model->meshes[0].mesh).has_value());
        expect(eq(model->meshes[0].mesh.positions.size(), std::size_t{4}) && eq(model->meshes[0].mesh.uvs.size(), std::size_t{4}) && eq(model->meshes[0].clusters.size(), std::size_t{1}));
        const xrender::mesh::Material& painted = model->materials[0];
        expect(painted.base == math::Vec3{0.5f, 0.25f, 1} && eq(painted.metallic, 0.25f) && eq(painted.roughness, 0.5f) && painted.emissive == math::Vec3{0.1f, 0.2f, 0.3f});
        // glTF's surface always mirrors its 4%: fully rough and no metal is still not this model's matt.
        const auto rough = asset::gltf(bytes_of(with_uri(R"(,
  "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 3, "material": 0}]}],
  "materials": [{"pbrMetallicRoughness": {"metallicFactor": 0, "roughnessFactor": 1}}])")));
        expect(fatal(rough.has_value())) << (rough ? "" : rough.error());
        expect(!rough->materials[0].matt() && lt(rough->materials[0].roughness, 1.0f) && gt(rough->materials[0].roughness, 0.9999f));
        expect(eq(painted.texture, 0u) && eq(model->textures[0].width, 2) && eq(model->textures[0].height, 2) && model->material_names[0] == "painted");
        expect(eq(painted.surface, 1u) && eq(painted.normal, 1u) && eq(painted.shade, 1u) && eq(painted.glow, 0u) && eq(painted.normal_scale, 0.5f) && eq(painted.shade_strength, 0.75f));
        // The first texel is red: as a colour its light is 1, 0, 0; as plain numbers the same bytes ÷ 255.
        expect(model->textures[0].light[0] == math::Vec3{1, 0, 0} && model->textures[1].light[1] == math::Vec3{0, 1, 0});

        // Ids are the file's places: node 0 and node 1, each their mesh's first primitive.
        expect(fatal(eq(model->instances.size(), std::size_t{2})));
        expect(eq(model->instances[0].id, scene::Id{1 * asset::ids_a_node + 1}) && eq(model->instances[1].id, scene::Id{2 * asset::ids_a_node + 1}));
        expect(model->placed[0].name == "first" && model->placed[1].name == "second" && eq(model->placed[1].node, 1u));
        // The child is where its parent puts it: (1, 2, 3) + (0, 0, 1), twice the size.
        const math::Mat4& child = model->instances[1].world;
        expect(eq(child.at(3, 0), 1.0f) && eq(child.at(3, 1), 2.0f) && eq(child.at(3, 2), 4.0f) && eq(child.at(0, 0), 2.0f) && eq(child.at(1, 1), 2.0f));
        expect(model->instances[0].bounds.low == math::Vec3{0, 0, 0} && model->instances[0].bounds.high == math::Vec3{1, 1, 0});
        const geom::Box3 all = model->bounds();
        expect(all.low == math::Vec3{1, 2, 3} && all.high == math::Vec3{3, 4, 4});

        // The sun shines down its node's −z; the node is turned a quarter about x, so: straight down.
        expect(fatal(eq(model->lights.size(), std::size_t{1})));
        const scene::Light& sun = model->lights[0];
        expect(sun.kind == scene::LightKind::sun && eq(sun.id, scene::Id{3}) && eq(sun.intensity, 3.0f) && sun.colour == math::Vec3{1, 0.5f, 0.25f});
        expect(std::abs(sun.toward.x) < 1e-6f && std::abs(sun.toward.y + 1) < 1e-6f && std::abs(sun.toward.z) < 1e-6f);
        expect(model->notes.empty()) << (model->notes.empty() ? "" : model->notes[0].where + ": " + model->notes[0].what);

        // Into a scene, where its caller keeps the meshes and materials; a second time is refused whole.
        scene::Scene world;
        expect(asset::place(*model, world, {.mesh = 10, .material = 20}).has_value());
        expect(eq(world.instances().size(), std::size_t{2}) && eq(world.lights().size(), std::size_t{1}) && eq(world.instances()[0].mesh, 10u) && eq(world.instances()[0].material, 20u));
        const auto again = asset::place(*model, world);
        expect(!again.has_value() && again.error().contains("65537")) << (again ? "" : again.error());
        expect(eq(world.instances().size(), std::size_t{2}) && eq(world.lights().size(), std::size_t{1}));
        const auto beside = asset::gltf(bytes_of(text), {.base = 1'000'000'000});
        expect(fatal(beside.has_value()));
        expect(asset::place(*beside, world).has_value() && eq(world.instances().size(), std::size_t{4}));
    };

    "the same document twice is the same bytes; a .glb is the .gltf; a buffer beside the document is read by the name it has"_test = [] {
        const std::string text = whole();
        const auto one = asset::gltf(bytes_of(text)), other = asset::gltf(bytes_of(text));
        expect(fatal(one.has_value() && other.has_value()));
        expect(geom::stored(one->meshes[0]) == geom::stored(other->meshes[0]));

        const std::string_view rest = R"(, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3}]}])";
        const auto in_text = asset::gltf(bytes_of(with_uri(rest)));
        const std::vector<std::uint8_t> binary = glb(document("", rest), square_bytes());
        const auto in_glb = asset::gltf(binary);
        expect(fatal(in_text.has_value() && in_glb.has_value())) << (in_glb ? "" : in_glb.error());
        expect(geom::stored(in_text->meshes[0]) == geom::stored(in_glb->meshes[0]) && eq(in_glb->instances.size(), std::size_t{1}));
        // A primitive with no material is given glTF's own: white, fully rough, fully metal.
        expect(fatal(eq(in_glb->materials.size(), std::size_t{1})));
        expect(in_glb->materials[0].base == math::Vec3{1, 1, 1} && eq(in_glb->instances[0].material, 0u));

        const std::string beside = document(R"(, "uri": "a%20square.bin")", rest);
        std::string asked;
        const auto read = asset::gltf(bytes_of(beside), {.read = [&](std::string_view name) -> std::expected<std::vector<std::uint8_t>, std::string> {
                                                             asked = name;
                                                             return square_bytes();
                                                         }});
        expect(fatal(read.has_value())) << (read ? "" : read.error());
        expect(asked == "a square.bin" && geom::stored(read->meshes[0]) == geom::stored(in_text->meshes[0]));
        const auto none = asset::gltf(bytes_of(beside));
        expect(!none.has_value() && none.error().contains("a square.bin") && none.error().contains("Options::read")) << (none ? "" : none.error());
        const auto shorter = asset::gltf(bytes_of(beside), {.read = [](std::string_view) -> std::expected<std::vector<std::uint8_t>, std::string> { return std::vector<std::uint8_t>(100); }});
        expect(!shorter.has_value() && shorter.error().contains("100 bytes")) << (shorter ? "" : shorter.error());
    };

    "from a file: what it names is read from its directory, and a name that climbs out of it is not"_test = [] {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / "openmui-tests" / "xrender-asset";
        std::filesystem::create_directories(directory);
        const std::string_view rest = R"(, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 3}]}])";
        const std::vector<std::uint8_t> buffer = square_bytes();
        std::ofstream(directory / "square.bin", std::ios::binary).write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        std::ofstream(directory / "square.gltf") << document(R"(, "uri": "square.bin")", rest);
        std::ofstream(directory / "outside.gltf") << document(R"(, "uri": "../square.bin")", rest);
        const auto model = asset::gltf_file(directory / "square.gltf");
        expect(fatal(model.has_value())) << (model ? "" : model.error());
        expect(eq(model->triangles(), std::size_t{2}));
        const auto outside = asset::gltf_file(directory / "outside.gltf");
        expect(!outside.has_value() && outside.error().contains("outside the document's directory")) << (outside ? "" : outside.error());
        const auto absent = asset::gltf_file(directory / "no-such.gltf");
        expect(!absent.has_value() && absent.error().contains("cannot be opened"));
    };

    "what the document says that is not carried is a note: where it is, what it is, what was done instead"_test = [] {
        const std::vector<std::uint8_t> not_a_picture{0xFF, 0xD8, 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F'};
        const std::string text = with_uri(std::format(R"(,
  "scenes": [{{"nodes": [0, 1]}}],
  "nodes": [{{"mesh": 0, "camera": 0}}, {{"mesh": 1, "scale": [-1, 1, 1]}}],
  "cameras": [{{"type": "perspective", "perspective": {{"yfov": 1, "znear": 0.1}}}}],
  "meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0, "TEXCOORD_0": 2, "TEXCOORD_1": 2, "COLOR_0": 1}}, "indices": 3, "material": 0}},
                              {{"attributes": {{"POSITION": 0}}, "mode": 1}}]}},
             {{"primitives": [{{"attributes": {{"POSITION": 0, "NORMAL": 1}}, "indices": 3, "material": 1}}]}}],
  "materials": [{{"pbrMetallicRoughness": {{"baseColorTexture": {{"index": 0}}, "metallicRoughnessTexture": {{"index": 0}}}},
                 "normalTexture": {{"index": 0}}, "occlusionTexture": {{"index": 0}}, "emissiveTexture": {{"index": 0}},
                 "alphaMode": "BLEND", "doubleSided": true, "extensions": {{"KHR_materials_clearcoat": {{"clearcoatFactor": 1}}}}}},
                {{"alphaMode": "MASK", "emissiveFactor": [1, 1, 1], "extensions": {{"KHR_materials_emissive_strength": {{"emissiveStrength": 4}}}}}}],
  "textures": [{{"source": 0}}],
  "images": [{{"uri": "{}", "mimeType": "image/jpeg"}}],
  "animations": [{{"channels": [], "samplers": []}}],
  "extensionsUsed": ["KHR_materials_clearcoat", "KHR_materials_emissive_strength", "VENDOR_something"])",
                                                      uri_of(not_a_picture, "image/jpeg")));
        const auto model = asset::gltf(bytes_of(text));
        expect(fatal(model.has_value())) << (model ? "" : model.error());
        for (const std::string_view where :
             {"/images/0", "/materials/0/extensions/KHR_materials_clearcoat",
              "/meshes/0/primitives/0/attributes/TEXCOORD_1", "/meshes/0/primitives/0/attributes/COLOR_0", "/meshes/0/primitives/0/attributes/NORMAL", "/meshes/0/primitives/1/mode",
              "/nodes/0/camera", "/nodes/1", "/animations", "/extensionsUsed/2"})
            expect(noted(*model, where)) << where;
        expect(eq(model->notes.size(), std::size_t{10}));
        // Seen from both sides, cut out and seen through are carried, so they leave no note: the second material's
        // cut is the format's own (0.5) where the document names none.
        expect(!noted(*model, "/materials/0/doubleSided") && !noted(*model, "/materials/0/alphaMode") && !noted(*model, "/materials/1/alphaMode"));
        expect(model->materials[0].two_sided && !model->materials[1].two_sided && eq(model->materials[1].cutout, 0.5f) && eq(model->materials[0].cutout, 0.0f));
        expect(model->materials[0].see_through && !model->materials[1].see_through);
        // What is carried is carried: the first primitive (its normals worked out), not the lines; the picture that
        // is not read leaves the material its factor; emission by its strength.
        expect(eq(model->meshes.size(), std::size_t{2}) && eq(model->instances.size(), std::size_t{2}) && eq(model->textures.size(), std::size_t{0}));
        expect(eq(model->meshes[0].mesh.normals.size(), std::size_t{4}) && model->meshes[0].mesh.normals[0] == math::Vec3{0, 0, 1});
        const xrender::mesh::Material none;
        expect(eq(model->materials[0].texture, none.texture) && eq(model->materials[0].surface, none.surface) && eq(model->materials[0].normal, none.normal) && eq(model->materials[0].glow, none.glow));
        expect(model->materials[1].emissive == math::Vec3{4, 4, 4});
        expect(eq(model->instances[0].id, scene::Id{asset::ids_a_node + 1}) && eq(model->instances[1].id, scene::Id{2 * asset::ids_a_node + 1}));
    };

    "cut out by its alpha, seen through by it, seen from both sides: the picture's fourth byte is how much the surface hides where the material is cut out or seen through by it, and says nothing where it is whole"_test = [] {
        // A 2 × 2 picture: red and whole, green and clear, blue and half there, white and whole.
        mlibs::image::Image image{.width = 2, .height = 2, .rgba = {255, 0, 0, 255, 0, 255, 0, 0, 0, 0, 255, 128, 255, 255, 255, 255}};
        const std::string text = with_uri(std::format(R"(,
  "scenes": [{{"nodes": [0, 1]}}],
  "nodes": [{{"mesh": 0}}, {{"mesh": 1}}, {{"mesh": 2}}],
  "meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}}, "indices": 3, "material": 0}}]}},
             {{"primitives": [{{"attributes": {{"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}}, "indices": 3, "material": 1}}]}},
             {{"primitives": [{{"attributes": {{"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}}, "indices": 3, "material": 2}}]}}],
  "materials": [{{"pbrMetallicRoughness": {{"baseColorFactor": [1, 1, 1, 0.8], "baseColorTexture": {{"index": 0}}}}, "alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true}},
                {{"pbrMetallicRoughness": {{"baseColorFactor": [1, 1, 1, 0.8], "baseColorTexture": {{"index": 0}}}}}},
                {{"pbrMetallicRoughness": {{"baseColorFactor": [1, 1, 1, 0.6], "baseColorTexture": {{"index": 0}}}}, "alphaMode": "BLEND"}}],
  "textures": [{{"source": 0}}],
  "images": [{{"uri": "{}"}}])",
                                                      uri_of(*mlibs::image::encode_png(mlibs::image::view(image)), "image/png")));
        const auto model = asset::gltf(bytes_of(text));
        expect(fatal(model.has_value())) << (model ? "" : model.error());
        expect(eq(model->notes.size(), std::size_t{0})) << (model->notes.empty() ? "" : model->notes.front().where);
        expect(fatal(eq(model->materials.size(), std::size_t{3}) && eq(model->textures.size(), std::size_t{2})));
        const xrender::mesh::Material& leaf = model->materials[0];
        const xrender::mesh::Material& plain = model->materials[1];
        const xrender::mesh::Material& glass = model->materials[2];
        expect(eq(leaf.cutout, 0.3f) && eq(leaf.opacity, 0.8f) && leaf.two_sided && eq(plain.cutout, 0.0f) && eq(plain.opacity, 1.0f) && !plain.two_sided);
        // Seen through by its alpha (BLEND): the factor's alpha is how much it hides, the picture's cover with it —
        // the same texture as the one cut out by it reads; nothing is cut.
        expect(glass.see_through && eq(glass.opacity, 0.6f) && eq(glass.cutout, 0.0f) && eq(glass.texture, leaf.texture) && !leaf.see_through && !plain.see_through);
        // One picture, a texture each way: with its cover for the material it cuts out, without for the whole one —
        // there the clear texel is green, as the format says (alpha is not looked at).
        expect(fatal(leaf.texture != plain.texture && leaf.texture < 2 && plain.texture < 2));
        const xrender::mesh::Texture& covering = model->textures[leaf.texture];
        const xrender::mesh::Texture& solid = model->textures[plain.texture];
        expect(eq(covering.cover.size(), covering.light.size()) && solid.cover.empty());
        expect(near(covering.cover_at({0.75f, 0.25f}), 0.0f, 1e-6f) && near(covering.cover_at({0.25f, 0.75f}), 128.0f / 255.0f, 1e-6f) && near(covering.cover_at({0.25f, 0.25f}), 1.0f, 1e-6f));
        expect(near(solid.at({0.75f, 0.25f}, 1).y, 1.0f, 1e-6f) && near(solid.at({0.75f, 0.25f}, 1).x, 0.0f, 1e-6f));
        expect(near(covering.at({0.25f, 0.75f}, 1).z, 1.0f, 1e-5f)) << "a texel half there is its own colour, whole";
    };

    "a KTX2 picture (KHR_texture_basisu) is read: the texture an extension names, with no other to fall back on"_test = [] {
        // Basis Universal's own test photograph (mlibs.image.ktx2's tests/data), beside the document.
        const std::filesystem::path beside = std::filesystem::path(std::source_location::current().file_name()).parent_path() / "../../../../base/image/ktx2/tests/data";
        const auto read = [&](std::string_view name) -> std::expected<std::vector<std::uint8_t>, std::string> {
            std::ifstream in(beside / name, std::ios::binary);
            if (!in) return std::unexpected(std::format("no {}", name));
            return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        };
        const std::string text = with_uri(R"(,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
  "textures": [{"extensions": {"KHR_texture_basisu": {"source": 0}}}],
  "images": [{"uri": "kodim23.ktx2", "mimeType": "image/ktx2"}],
  "extensionsUsed": ["KHR_texture_basisu"],
  "extensionsRequired": ["KHR_texture_basisu"])");
        const auto model = asset::gltf(bytes_of(text), {.read = read});
        expect(fatal(model.has_value())) << (model ? "" : model.error());
        expect(eq(model->notes.size(), std::size_t{0})) << (model->notes.empty() ? "" : model->notes.front().where + ": " + model->notes.front().what);
        expect(fatal(eq(model->textures.size(), std::size_t{1}) && eq(model->materials.size(), std::size_t{1}) && eq(model->materials[0].texture, 0u)));
        const xrender::mesh::Texture& picture = model->textures[0];
        expect(eq(picture.width, 768) && eq(picture.height, 512));
        // A photograph: its corners are not one colour, and nothing of it is over a display's white.
        const math::Vec3 one = picture.at({0.1f, 0.1f}, 1), other = picture.at({0.6f, 0.5f}, 1);
        expect(gt(math::length(one - other), 0.05f)) << one.x << ' ' << other.x;
        // A file that is cut short is a note, and the material keeps its own numbers.
        const auto cut = asset::gltf(bytes_of(text), {.read = [&](std::string_view name) { return read(name).transform([](std::vector<std::uint8_t> bytes) { bytes.resize(bytes.size() / 2); return bytes; }); }});
        expect(fatal(cut.has_value())) << (cut ? "" : cut.error());
        expect(noted(*cut, "/images/0") && eq(cut->textures.size(), std::size_t{0}) && eq(cut->materials[0].texture, xrender::mesh::Material{}.texture));
    };

    "a model looked at as a viewer frames it: the eye where the orbit says, the picture whole where the model is and nothing where it is not, its rim by how many samples drew"_test = [] {
        const auto model = asset::gltf(bytes_of(whole()));
        expect(fatal(model.has_value())) << (model ? "" : model.error());
        // The eye: at phi a quarter turn and theta none, straight before the target along +z; at theta a quarter turn, along +x; at phi none, above.
        const auto eye_of = [&](const asset::Orbit& orbit) {
            const auto back = math::inverse(asset::view_of(orbit, model->bounds(), 1).view);
            return back ? math::point(*back, {0, 0, 0}) : math::Vec3{};
        };
        const auto close = [](math::Vec3 one, math::Vec3 other) { return math::length(one - other) < 1e-4f; };
        expect(close(eye_of({.radius = 5, .target = {1, 2, 3}}), {1, 2, 8}) && close(eye_of({.theta = 1.5707964f, .radius = 5, .target = {1, 2, 3}}), {6, 2, 3}));
        expect(close(eye_of({.phi = 0.5235988f, .radius = 2}), {0, 1.7320508f, 1}));
        // The model before the eye, lit by an even picture of the surroundings.
        std::vector<float> even(16 * 8 * 3, 1.0f);
        const auto surroundings = xrender::mesh::Surroundings::made(even, 16, 8);
        expect(fatal(surroundings.has_value()));
        const auto box = model->bounds();
        const asset::Orbit orbit{.theta = 0.6f, .phi = 0.9f, .radius = math::length(box.half()) * 3, .target = box.centre()};
        const auto picture = asset::looked(*model, asset::view_of(orbit, box, 1), {.width = 64, .height = 64, .surroundings = &*surroundings});
        expect(fatal(picture.has_value())) << (picture ? "" : picture.error());
        std::size_t whole_pixels = 0, none = 0, rim = 0;
        for (std::size_t pixel = 0; pixel < 64 * 64; ++pixel) {
            const std::uint8_t cover = picture->rgba[pixel * 4 + 3];
            whole_pixels += cover == 255, none += cover == 0, rim += cover != 0 && cover != 255;
            if (cover == 0) expect(picture->rgba[pixel * 4] == 0 && picture->rgba[pixel * 4 + 1] == 0);
        }
        expect(gt(whole_pixels, std::size_t{100}) && gt(none, std::size_t{100}) && gt(rim, std::size_t{4}) && lt(rim, whole_pixels)) << whole_pixels << ' ' << none << ' ' << rim;
        expect(eq(picture->stats.draws, 2u) && eq(picture->rgba[3], std::uint8_t{0}));
        // One sample a pixel: no rim. And no size: refused.
        const auto hard = asset::looked(*model, asset::view_of(orbit, box, 1), {.width = 64, .height = 64, .samples = 1, .surroundings = &*surroundings});
        expect(fatal(hard.has_value()));
        for (std::size_t pixel = 0; pixel < 64 * 64; ++pixel) expect(hard->rgba[pixel * 4 + 3] == 0 || hard->rgba[pixel * 4 + 3] == 255);
        expect(!asset::looked(*model, asset::view_of(orbit, box, 1), {.width = 0}).has_value());
    };
    "strips and fans are triangles with their fronts kept; lamps have a reach"_test = [] {
        const std::string text = with_uri(R"(,
  "scenes": [{"nodes": [0, 1, 2]}],
  "nodes": [{"mesh": 0}, {"translation": [0, 5, 0], "extensions": {"KHR_lights_punctual": {"light": 0}}},
            {"extensions": {"KHR_lights_punctual": {"light": 1}}}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "mode": 6}, {"attributes": {"POSITION": 0}, "mode": 5}]}],
  "extensionsUsed": ["KHR_lights_punctual"],
  "extensions": {"KHR_lights_punctual": {"lights": [{"type": "point", "intensity": 4, "range": 7}, {"type": "spot", "intensity": 9, "spot": {"outerConeAngle": 0.5}}]}})");
        const auto model = asset::gltf(bytes_of(text), {.optimise = false});
        expect(fatal(model.has_value())) << (model ? "" : model.error());
        expect(fatal(eq(model->meshes.size(), std::size_t{2})));
        // The fan 0 1 2 3: (0 1 2), (0 2 3). The strip 0 1 2 3: (0 1 2), (2 1 3). Every one faces +z or −z as its
        // corners say; a strip's second triangle is turned so that it faces as the first would have it.
        expect(model->meshes[0].mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
        expect(model->meshes[1].mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 2, 1, 3});
        expect(fatal(eq(model->lights.size(), std::size_t{2})));
        expect(model->lights[0].kind == scene::LightKind::point && eq(model->lights[0].range, 7.0f) && model->lights[0].at == math::Vec3{0, 5, 0});
        expect(model->lights[1].kind == scene::LightKind::spot && eq(model->lights[1].cone, 0.5f) && eq(model->lights[1].range, 30.0f));
        expect(noted(*model, "/extensions/KHR_lights_punctual/lights/1/range"));
    };

    "what is not a document is refused with its reason"_test = [] {
        const auto refused = [](std::string_view text, std::string_view reason, const asset::Options& options = {}) {
            const auto model = asset::gltf(bytes_of(text), options);
            expect(!model.has_value() && model.error().contains(reason)) << (model ? std::string("read: ") + std::string(reason) : model.error());
        };
        refused(R"({"asset": {"version": "2.0"}, "meshes": [)", "JSON does not parse");
        refused("", "ends before it is whole");
        refused(R"({"asset": {"version": "2.0"}, "nodes": [{"mesh": 3}]})", "names something that is not there");
        // An accessor past the end of its buffer.
        refused(document(std::format(R"(, "uri": "{}")", uri_of(std::vector<std::uint8_t>(140))),
                         R"(, "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 3}]}])")
                    .replace(49, 3, "100"),
                "buffers");
        // An index that names no position.
        std::vector<std::uint8_t> wild = square_bytes();
        wild[128] = 9;
        const std::string rest = R"(, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}], "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 3}]}])";
        refused(document(std::format(R"(, "uri": "{}")", uri_of(wild)), rest), "do not agree");
        refused(with_uri(rest + R"(, "extensionsUsed": ["KHR_draco_mesh_compression"], "extensionsRequired": ["KHR_draco_mesh_compression"])"), "KHR_draco_mesh_compression");
        refused(with_uri(rest), "scene 4 was asked for", {.scene = 4});
    };

    "a mesh ordered for drawing: the same triangles, in clusters of triangles that lie together"_test = [] {
        // A sheet of 48 × 48 quads with its triangles in a scattered order, and a corner no triangle uses.
        geom::Mesh sheet = geom::sheet(8, 8, 48, 48);
        const std::size_t count = sheet.triangles();
        math::SplitMix64 random{7};
        for (std::size_t at = count; at-- > 1;) {
            const std::size_t other = random.next() % (at + 1);
            for (std::size_t corner = 0; corner < 3; ++corner) std::swap(sheet.indices[at * 3 + corner], sheet.indices[other * 3 + corner]);
        }
        sheet.positions.push_back({100, 100, 100}), sheet.normals.push_back({0, 1, 0}), sheet.tangents.push_back({1, 0, 0, 1}), sheet.uvs.push_back({0, 0});
        const std::vector<std::array<float, 9>> before = triangles_of(sheet);
        const auto as_it_comes = geom::clustered(sheet);
        const auto ordered = asset::optimised(sheet);
        expect(fatal(ordered.has_value() && as_it_comes.has_value())) << (ordered ? "" : ordered.error());
        expect(geom::check(ordered->mesh).has_value() && triangles_of(ordered->mesh) == before) << "the same triangles";
        expect(eq(ordered->mesh.positions.size(), sheet.positions.size() - 1) && eq(ordered->mesh.uvs.size(), ordered->mesh.positions.size())) << "the unused corner is gone";
        std::size_t held = 0;
        float       widest = 0, widest_as_it_comes = 0;
        for (const geom::Cluster& cluster : ordered->clusters) {
            expect(le(cluster.triangle_count, geom::most_cluster_triangles) && gt(cluster.triangle_count, 0u) && eq(std::size_t{cluster.first_index}, held * 3));
            held += cluster.triangle_count;
            widest = std::max(widest, cluster.bounds.radius);
        }
        for (const geom::Cluster& cluster : as_it_comes->clusters) widest_as_it_comes = std::max(widest_as_it_comes, cluster.bounds.radius);
        expect(eq(held, count));
        // Scattered, a cluster spans the sheet (a radius near 5.7); put together, a patch of it — the last ones too
        // (what is left over at the end is cut where it grows wide, not gathered into one cluster across the sheet).
        expect(lt(widest, 1.5f) && gt(widest_as_it_comes, 4.0f)) << widest << " against " << widest_as_it_comes;
        expect(le(ordered->clusters.size(), as_it_comes->clusters.size() + as_it_comes->clusters.size() / 2));
        // What cannot be a mesh is refused as mlibs.geom refuses it.
        geom::Mesh broken = geom::box({1, 1, 1});
        broken.indices.push_back(0);
        expect(!asset::optimised(broken).has_value());
        expect(asset::optimised(geom::Mesh{}).has_value());
    };
}};
