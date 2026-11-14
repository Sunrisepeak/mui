// The one unit that names cgltf (D-83): a glTF 2.0 document read and made into what the scene and the mesh pass
// are given.
module xrender.asset;

import std;
import mcpplibs.mwrap.cgltf;
import mlibs.image;
import mlibs.image.ktx2;
import mlibs.base64;

namespace xrender::asset {

namespace {

using mlibs::geom::Vec2;
using mlibs::geom::Vec3;
using mlibs::geom::Vec4;

std::string_view reason(cgltf_result result) noexcept {
    switch (result) {
        case cgltf_result_data_too_short: return "it ends before it is whole";
        case cgltf_result_unknown_format: return "neither glTF's JSON nor a .glb";
        case cgltf_result_invalid_json: return "its JSON does not parse";
        case cgltf_result_invalid_gltf: return "its JSON is not a glTF document (a number names something that is not there, or what must be there is missing)";
        case cgltf_result_file_not_found: return "a file it names is not there";
        case cgltf_result_io_error: return "a file it names could not be read";
        case cgltf_result_out_of_memory: return "no memory for it";
        case cgltf_result_legacy_gltf: return "glTF 1.0, which is not read (2.0 is)";
        default: return "refused by the parser";
    }
}

// What reading the document's own files goes through (cgltf calls back for a buffer that is a file).
struct Reading {
    const Options* options = nullptr;
    std::string    failed;  // why the last file was not read
};

cgltf_result read_named(const cgltf_memory_options*, const cgltf_file_options* file, const char* path, cgltf_size* size, void** data) {
    auto& reading = *static_cast<Reading*>(file->user_data);
    if (!reading.options->read) {
        reading.failed = std::format("it names a file beside itself ({}) and no way to read one was given (Options::read, or gltf_file)", path);
        return cgltf_result_file_not_found;
    }
    const auto bytes = reading.options->read(path);
    if (!bytes) {
        reading.failed = std::format("{}: {}", path, bytes.error());
        return cgltf_result_io_error;
    }
    if (*size && bytes->size() < *size) {
        reading.failed = std::format("{} holds {} bytes; the document says {}", path, bytes->size(), *size);
        return cgltf_result_data_too_short;
    }
    void* copy = std::malloc(bytes->empty() ? 1 : bytes->size());
    if (!copy) return cgltf_result_out_of_memory;
    std::memcpy(copy, bytes->data(), bytes->size());
    *size = bytes->size(), *data = copy;
    return cgltf_result_success;
}
void release_named(const cgltf_memory_options*, const cgltf_file_options*, void* data) { std::free(data); }

struct Document {
    cgltf_data* data = nullptr;
    Document() = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    ~Document() { cgltf_free(data); }
};

// Whether an accessor's numbers can be read: it has bytes behind it. (One with no view is all zeros of any length
// the document cares to say — a length that is not trusted.)
bool readable(const cgltf_accessor* accessor) noexcept { return accessor && accessor->buffer_view && accessor->count > 0; }

template <typename Element>
bool floats(const cgltf_accessor* accessor, cgltf_type type, std::vector<Element>& out) {
    constexpr std::size_t parts = sizeof(Element) / sizeof(float);
    if (!readable(accessor) || accessor->type != type) return false;
    out.resize(accessor->count);
    return cgltf_accessor_unpack_floats(accessor, reinterpret_cast<cgltf_float*>(out.data()), out.size() * parts) == out.size() * parts;
}

// A strip's or a fan's corners as triangles, fronts kept.
std::vector<std::uint32_t> as_triangles(std::vector<std::uint32_t> corners, cgltf_primitive_type type) {
    if (type == cgltf_primitive_type_triangles) return corners;
    std::vector<std::uint32_t> out;
    for (std::size_t at = 2; at < corners.size(); ++at) {
        if (type == cgltf_primitive_type_triangle_fan) out.insert(out.end(), {corners[0], corners[at - 1], corners[at]});
        else if (at % 2 == 0) out.insert(out.end(), {corners[at - 2], corners[at - 1], corners[at]});
        else out.insert(out.end(), {corners[at - 1], corners[at - 2], corners[at]});
    }
    return out;
}

std::string_view mode_name(cgltf_primitive_type type) noexcept {
    switch (type) {
        case cgltf_primitive_type_points: return "points";
        case cgltf_primitive_type_lines: return "lines";
        case cgltf_primitive_type_line_loop: return "a line loop";
        case cgltf_primitive_type_line_strip: return "a line strip";
        default: return "a mode that is not triangles";
    }
}

// The extensions whose data this reads, or that ask nothing more of a reader than glTF itself.
constexpr std::array<std::string_view, 5> read_extensions{"KHR_lights_punctual", "KHR_materials_emissive_strength", "EXT_texture_webp", "KHR_texture_basisu", "KHR_mesh_quantization"};
// The ones without which a mesh's numbers cannot be read at all: a document that requires one is refused.
constexpr std::array<std::string_view, 2> packed_geometry{"KHR_draco_mesh_compression", "EXT_meshopt_compression"};

bool finite(const Mat4& matrix) noexcept {
    return std::ranges::all_of(matrix.values, [](float value) { return std::isfinite(value); });
}
// The bytes of a picture: in a buffer, in the document as base64, or in a file beside it.
std::expected<std::vector<std::uint8_t>, std::string> picture_bytes(const cgltf_image& image, const Options& options) {
    if (image.buffer_view) {
        const std::uint8_t* at = cgltf_buffer_view_data(image.buffer_view);
        if (!at) return std::unexpected(std::string("its buffer has no bytes"));
        return std::vector<std::uint8_t>(at, at + image.buffer_view->size);
    }
    if (!image.uri) return std::unexpected(std::string("it names neither a buffer nor a file"));
    const std::string_view uri(image.uri);
    if (uri.starts_with("data:")) {
        const std::size_t comma = uri.find(',');
        if (comma == std::string_view::npos || !uri.substr(0, comma).ends_with(";base64")) return std::unexpected(std::string("a data URI that is not base64"));
        auto decoded = mlibs::base64::decode(uri.substr(comma + 1));
        if (!decoded) return std::unexpected(std::format("its base64 does not decode: {}", decoded.error()));
        return decoded;
    }
    if (!options.read) return std::unexpected(std::string("it is a file beside the document and no way to read one was given (Options::read, or gltf_file)"));
    std::string name(uri);
    name.resize(cgltf_decode_uri(name.data()));
    return options.read(name);
}

struct Maker {
    const cgltf_data& data;
    const Options&    options;
    Imported          out;
    // By the document's image number, as colours whose fourth byte says how much they hide, as plain numbers, and
    // as colours that hide everywhere (a picture used more than one way is a texture each): not asked for yet
    // (~0), not carried (~0 − 1), or its place in `out.textures`.
    std::vector<std::array<std::uint32_t, 3>> texture_of_image;
    static constexpr std::uint32_t unasked = 0xffffffffu, refused = 0xfffffffeu;

    void note(std::string where, std::string what, std::string instead) { out.notes.push_back({std::move(where), std::move(what), std::move(instead)}); }

    // The texture a material names for one of its pictures: its place in `out.textures`, or none (with a note of
    // why). `where` is the document's place of the naming; `instead` what the material is left with. `covering`:
    // the picture's fourth byte is how much the surface hides there (a material that is cut out by it); without,
    // the byte says nothing, as the format has it for a surface that is whole.
    std::uint32_t picture(const cgltf_texture_view& view, const std::string& where, mesh::Coding coding, std::string_view instead, bool covering = false) {
        const cgltf_texture& texture = *view.texture;
        const std::size_t texture_index = cgltf_texture_index(&data, &texture);
        // The texture's own picture where it has one (PNG or JPEG: the document's fallback, read exactly); else the
        // one an extension names — WebP, or KTX2 (KHR_texture_basisu).
        const cgltf_image* image = texture.image ? texture.image : texture.has_webp ? texture.webp_image : texture.has_basisu ? texture.basisu_image : nullptr;
        if (!image) {
            note(std::format("/textures/{}", texture_index), "a texture with no picture", std::string(instead));
            return mesh::Material{}.texture;
        }
        const std::size_t image_index = cgltf_image_index(&data, image);
        std::array<std::uint32_t, 3>& made_as = texture_of_image[image_index];
        const std::size_t as = coding == mesh::Coding::plain ? 1 : covering ? 0 : 2;
        if (made_as[as] == unasked && !std::ranges::contains(made_as, refused)) {
            made_as[as] = refused;
            const auto bytes = picture_bytes(*image, options);
            // A KTX2 file's largest picture (the texture's smaller sizes are made here, as every picture's are).
            auto decoded = !bytes ? std::unexpected(bytes.error()) : mlibs::image::is_ktx2(*bytes) ? mlibs::image::decode_ktx2(*bytes) : mlibs::image::decode(*bytes);
            std::optional<mesh::Texture> made;
            if (decoded && as == 2)
                for (std::size_t byte = 3; byte < decoded->rgba.size(); byte += 4) decoded->rgba[byte] = 255;
            if (decoded) made = mesh::Texture::made(decoded->rgba, decoded->width, decoded->height, coding);
            if (made) {
                made_as[as] = static_cast<std::uint32_t>(out.textures.size());
                out.textures.push_back(std::move(*made));
            } else {
                note(std::format("/images/{}", image_index),
                     std::format("a picture that is not read{}: {}", image->mime_type ? std::format(" ({})", image->mime_type) : std::string(), decoded ? "it has no pixels" : decoded.error()),
                     "every material that names it is left with its own numbers alone (PNG, WebP, JPEG and KTX2 are read)");
            }
        }
        if (std::ranges::contains(made_as, refused) || made_as[as] == unasked) return mesh::Material{}.texture;
        if (view.texcoord != 0) note(where + "/texCoord", std::format("texture coordinates {}", view.texcoord), "the mesh's first (TEXCOORD_0)");
        if (view.has_transform) note(where + "/extensions/KHR_texture_transform", "the picture moved, turned or scaled over the surface", "laid as the texture coordinates say");
        if (texture.sampler && (texture.sampler->wrap_s != cgltf_wrap_mode_repeat || texture.sampler->wrap_t != cgltf_wrap_mode_repeat))
            note(std::format("/samplers/{}", cgltf_sampler_index(&data, texture.sampler)), "a picture that stops or mirrors at its edge", "it repeats");
        return made_as[as];
    }

    void materials() {
        for (std::size_t index = 0; index < data.materials_count; ++index) {
            const cgltf_material& from = data.materials[index];
            const std::string where = std::format("/materials/{}", index);
            // glTF's own defaults where the document says nothing: white, fully rough, fully metal.
            mesh::Material made{.base = {1, 1, 1}, .roughness = 1, .metallic = 1};
            if (from.has_pbr_metallic_roughness) {
                const cgltf_pbr_metallic_roughness& pbr = from.pbr_metallic_roughness;
                made.base = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
                // glTF's surface always mirrors its 4%: a roughness of 1 is held a step under it, where this model's
                // surfaces of roughness 1 and no metal are matt and mirror nothing.
                made.roughness = std::min(pbr.roughness_factor, 0.99999994f), made.metallic = pbr.metallic_factor;
                // Cut out by its alpha (MASK): where the factor's alpha times the picture's is below the cutoff, the
                // surface is not there. Seen through by it (BLEND): that much of what is behind it is hidden.
                if (from.alpha_mode == cgltf_alpha_mode_mask) made.opacity = pbr.base_color_factor[3], made.cutout = from.alpha_cutoff;
                if (from.alpha_mode == cgltf_alpha_mode_blend) made.opacity = pbr.base_color_factor[3];
                if (pbr.base_color_texture.texture)
                    made.texture = picture(pbr.base_color_texture, where + "/pbrMetallicRoughness/baseColorTexture", mesh::Coding::colour, "the material's base colour factor alone",
                                           from.alpha_mode != cgltf_alpha_mode_opaque);
                if (pbr.metallic_roughness_texture.texture)
                    made.surface = picture(pbr.metallic_roughness_texture, where + "/pbrMetallicRoughness/metallicRoughnessTexture", mesh::Coding::plain, "the two factors over the whole surface");
            }
            const float strength = from.has_emissive_strength ? from.emissive_strength.emissive_strength : 1.0f;
            made.emissive = {from.emissive_factor[0] * strength, from.emissive_factor[1] * strength, from.emissive_factor[2] * strength};
            if (from.normal_texture.texture) {
                made.normal = picture(from.normal_texture, where + "/normalTexture", mesh::Coding::plain, "the mesh's own normals");
                made.normal_scale = from.normal_texture.scale;
            }
            if (from.occlusion_texture.texture) {
                made.shade = picture(from.occlusion_texture, where + "/occlusionTexture", mesh::Coding::plain, "all of the light from everywhere");
                made.shade_strength = from.occlusion_texture.scale;
            }
            if (from.emissive_texture.texture) made.glow = picture(from.emissive_texture, where + "/emissiveTexture", mesh::Coding::colour, "the emissive factor over the whole surface");
            if (from.alpha_mode == cgltf_alpha_mode_mask && !from.has_pbr_metallic_roughness) made.cutout = from.alpha_cutoff;
            made.see_through = from.alpha_mode == cgltf_alpha_mode_blend;
            made.two_sided = from.double_sided != 0;
            if (from.unlit) note(where + "/extensions/KHR_materials_unlit", "a surface no light changes", "lit as any other");
            const std::array<std::pair<bool, std::string_view>, 11> more{{{from.has_pbr_specular_glossiness != 0, "KHR_materials_pbrSpecularGlossiness"},
                                                                          {from.has_clearcoat != 0, "KHR_materials_clearcoat"},
                                                                          {from.has_transmission != 0, "KHR_materials_transmission"},
                                                                          {from.has_volume != 0, "KHR_materials_volume"},
                                                                          {from.has_ior != 0, "KHR_materials_ior"},
                                                                          {from.has_specular != 0, "KHR_materials_specular"},
                                                                          {from.has_sheen != 0, "KHR_materials_sheen"},
                                                                          {from.has_iridescence != 0, "KHR_materials_iridescence"},
                                                                          {from.has_diffuse_transmission != 0, "KHR_materials_diffuse_transmission"},
                                                                          {from.has_anisotropy != 0, "KHR_materials_anisotropy"},
                                                                          {from.has_dispersion != 0, "KHR_materials_dispersion"}}};
            for (const auto& [has, name] : more)
                if (has) note(std::format("{}/extensions/{}", where, name), "a part of the surface model this does not draw", "left out: base colour, roughness, metal and emission are what is kept");
            out.materials.push_back(made);
            out.material_names.emplace_back(from.name ? from.name : "");
        }
    }

    // A primitive as a mesh in clusters, or none (with a note of why) — or the document refused, with the reason.
    std::expected<std::optional<mlibs::geom::ClusteredMesh>, std::string> primitive(const cgltf_primitive& from, const std::string& where) {
        if (from.type != cgltf_primitive_type_triangles && from.type != cgltf_primitive_type_triangle_strip && from.type != cgltf_primitive_type_triangle_fan) {
            note(where + "/mode", std::string(mode_name(from.type)), "left out: triangles, strips and fans of them are drawn");
            return std::nullopt;
        }
        if (from.has_draco_mesh_compression) {
            note(where + "/extensions/KHR_draco_mesh_compression", "a mesh packed by Draco", "left out: it is not unpacked");
            return std::nullopt;
        }
        mlibs::geom::Mesh made;
        const cgltf_accessor* positions = cgltf_find_accessor(&from, cgltf_attribute_type_position, 0);
        if (positions && positions->buffer_view && positions->buffer_view->has_meshopt_compression) {
            note(where + "/attributes/POSITION", "a mesh packed by EXT_meshopt_compression", "left out: it is not unpacked yet");
            return std::nullopt;
        }
        if (!floats(positions, cgltf_type_vec3, made.positions)) {
            note(where + "/attributes/POSITION", "no positions that can be read (none, or an accessor with no bytes behind it)", "left out");
            return std::nullopt;
        }
        for (std::size_t index = 0; index < from.attributes_count; ++index) {
            const cgltf_attribute& attribute = from.attributes[index];
            const std::string at = std::format("{}/attributes/{}", where, attribute.name ? attribute.name : "?");
            const bool first = attribute.index == 0;
            bool read = true;
            if (attribute.type == cgltf_attribute_type_position && first) continue;
            if (attribute.type == cgltf_attribute_type_normal && first) read = floats(attribute.data, cgltf_type_vec3, made.normals) && made.normals.size() == made.positions.size();
            else if (attribute.type == cgltf_attribute_type_tangent && first) read = floats(attribute.data, cgltf_type_vec4, made.tangents) && made.tangents.size() == made.positions.size();
            else if (attribute.type == cgltf_attribute_type_texcoord && first) read = floats(attribute.data, cgltf_type_vec2, made.uvs) && made.uvs.size() == made.positions.size();
            else {
                note(at, attribute.type == cgltf_attribute_type_color      ? "a colour a corner"
                         : attribute.type == cgltf_attribute_type_joints || attribute.type == cgltf_attribute_type_weights ? "a skin's bones and weights"
                         : attribute.type == cgltf_attribute_type_texcoord ? "a further set of texture coordinates"
                                                                           : "an attribute this does not carry",
                     "left out");
                continue;
            }
            if (!read) {
                if (attribute.type == cgltf_attribute_type_normal) made.normals.clear();
                if (attribute.type == cgltf_attribute_type_tangent) made.tangents.clear();
                if (attribute.type == cgltf_attribute_type_texcoord) made.uvs.clear();
                note(at, "an attribute that cannot be read (not one a position, or no bytes behind it)", "left out");
            }
        }
        std::vector<std::uint32_t> corners;
        if (from.indices) {
            if (!readable(from.indices)) {
                note(where + "/indices", "indices with no bytes behind them", "the primitive is left out");
                return std::nullopt;
            }
            corners.resize(from.indices->count);
            if (cgltf_accessor_unpack_indices(from.indices, corners.data(), sizeof(std::uint32_t), corners.size()) != corners.size())
                return std::unexpected(std::format("{}/indices cannot be read", where));
        } else {
            corners.resize(made.positions.size());
            std::ranges::iota(corners, 0u);
        }
        made.indices = as_triangles(std::move(corners), from.type);
        if (made.indices.size() < 3) {
            note(where, "a primitive of no triangle", "left out");
            return std::nullopt;
        }
        if (from.targets_count) note(where + "/targets", std::format("{} morph targets", from.targets_count), "the mesh as it is at rest");
        if (auto checked = mlibs::geom::check(made); !checked) return std::unexpected(std::format("{}: {}", where, checked.error()));
        if (made.normals.empty()) {
            mlibs::geom::compute_normals(made);
            note(where + "/attributes/NORMAL", "no normals (glTF then asks for flat faces)", "smooth normals worked out from the triangles");
        }
        auto clustered = options.optimise ? optimised(std::move(made)) : mlibs::geom::clustered(std::move(made));
        if (!clustered) return std::unexpected(std::format("{}: {}", where, clustered.error()));
        return std::optional<mlibs::geom::ClusteredMesh>(std::move(*clustered));
    }

    // Whether a node is under one of the scene's roots.
    bool in_scene(const cgltf_node* node, const cgltf_scene* scene) const noexcept {
        std::size_t steps = 0;
        while (node->parent && steps++ < data.nodes_count) node = node->parent;
        if (!scene) return true;
        for (std::size_t index = 0; index < scene->nodes_count; ++index)
            if (scene->nodes[index] == node) return true;
        return false;
    }

    std::expected<void, std::string> make() {
        for (std::size_t index = 0; index < data.extensions_required_count; ++index)
            if (std::ranges::contains(packed_geometry, std::string_view(data.extensions_required[index])))
                return std::unexpected(std::format("it requires {}, without which its meshes cannot be read; this does not unpack it (/extensionsRequired/{})", data.extensions_required[index], index));
        for (std::size_t index = 0; index < data.extensions_used_count; ++index) {
            const std::string_view name(data.extensions_used[index]);
            if (std::ranges::contains(read_extensions, name) || name.starts_with("KHR_materials_") || name == "KHR_texture_transform") continue;  // noted where used
            bool required = false;
            for (std::size_t other = 0; other < data.extensions_required_count; ++other) required = required || name == data.extensions_required[other];
            note(std::format("/extensionsUsed/{}", index), std::format("the extension {}{}", name, required ? " (the document requires it)" : ""), "what it adds is left out");
        }
        texture_of_image.assign(data.images_count, {unasked, unasked, unasked});
        materials();

        // A mesh a primitive; which of `out.meshes` each of the document's primitives became.
        std::vector<std::vector<std::uint32_t>> made_of(data.meshes_count);
        for (std::size_t mesh = 0; mesh < data.meshes_count; ++mesh) {
            const cgltf_mesh& from = data.meshes[mesh];
            if (from.primitives_count >= ids_a_node) return std::unexpected(std::format("/meshes/{} has {} primitives (fewer than {} are numbered)", mesh, from.primitives_count, ids_a_node));
            for (std::size_t part = 0; part < from.primitives_count; ++part) {
                auto made = primitive(from.primitives[part], std::format("/meshes/{}/primitives/{}", mesh, part));
                if (!made) return std::unexpected(made.error());
                made_of[mesh].push_back(*made ? static_cast<std::uint32_t>(out.meshes.size()) : mesh::none);
                if (*made) out.meshes.push_back(std::move(**made));
            }
        }

        const cgltf_scene* scene = data.scene ? data.scene : data.scenes_count ? &data.scenes[0] : nullptr;
        if (options.scene) {
            if (*options.scene >= data.scenes_count) return std::unexpected(std::format("scene {} was asked for; the document has {}", *options.scene, data.scenes_count));
            scene = &data.scenes[*options.scene];
        }
        std::uint32_t unnamed = mesh::none;  // the material a primitive with none is given
        for (std::size_t index = 0; index < data.nodes_count; ++index) {
            const cgltf_node& node = data.nodes[index];
            if (!in_scene(&node, scene)) continue;
            const std::string where = std::format("/nodes/{}", index);
            Mat4 world;
            cgltf_node_transform_world(&node, world.values.data());
            if (!finite(world)) {
                if (node.mesh || node.light) note(where, "a place that is not a number", "left out");
                continue;
            }
            if (node.skin) note(where + "/skin", "a skin", "the mesh as it is at rest");
            if (node.camera) note(where + "/camera", "a camera", "left out: the view is its user's");
            if (node.has_mesh_gpu_instancing) note(where + "/extensions/EXT_mesh_gpu_instancing", "many of the mesh by a table", "the one the node itself places");
            if (node.mesh) {
                const std::size_t mesh = cgltf_mesh_index(&data, node.mesh);
                for (std::size_t part = 0; part < made_of[mesh].size(); ++part) {
                    if (made_of[mesh][part] == mesh::none) continue;
                    const cgltf_material* material = node.mesh->primitives[part].material;
                    if (!material && unnamed == mesh::none) {
                        unnamed = static_cast<std::uint32_t>(out.materials.size());
                        out.materials.push_back({.base = {1, 1, 1}, .roughness = 1, .metallic = 1});
                        out.material_names.emplace_back();
                    }
                    out.instances.push_back({.id = options.base + (index + 1) * ids_a_node + part + 1,
                                             .mesh = made_of[mesh][part],
                                             .material = material ? static_cast<std::uint32_t>(cgltf_material_index(&data, material)) : unnamed,
                                             .world = world,
                                             .bounds = mlibs::geom::bounds(out.meshes[made_of[mesh][part]].mesh)});
                    out.placed.push_back({.node = static_cast<std::uint32_t>(index), .mesh = static_cast<std::uint32_t>(mesh), .primitive = static_cast<std::uint32_t>(part), .name = node.name ? node.name : ""});
                }
            }
            if (node.light) {
                const cgltf_light& from = *node.light;
                scene::Light light{.id = options.base + index + 1,
                                   .kind = from.type == cgltf_light_type_directional ? scene::LightKind::sun : from.type == cgltf_light_type_spot ? scene::LightKind::spot : scene::LightKind::point,
                                   .colour = {from.color[0], from.color[1], from.color[2]},
                                   .intensity = from.intensity,
                                   .at = {world.at(3, 0), world.at(3, 1), world.at(3, 2)},
                                   // A light shines down its node's −z.
                                   .toward = mlibs::math::normalized(Vec3{-world.at(2, 0), -world.at(2, 1), -world.at(2, 2)}),
                                   .cone = from.spot_outer_cone_angle};
                if (from.type != cgltf_light_type_directional) {
                    if (from.range > 0) light.range = from.range;
                    else {
                        // No reach said: glTF's is without end. The pass's lamps end; where this one has faded to a hundredth is taken.
                        light.range = std::max(1.0f, 10.0f * std::sqrt(std::max(from.intensity, 0.0f)));
                        note(std::format("/extensions/KHR_lights_punctual/lights/{}/range", cgltf_light_index(&data, &from)), "a light with no end to its reach",
                             std::format("a reach of {}", light.range));
                    }
                }
                out.lights.push_back(light);
            }
        }
        if (data.animations_count) note("/animations", std::format("{} animations", data.animations_count), "left out: everything is where it is at rest");
        if (data.skins_count) note("/skins", std::format("{} skins", data.skins_count), "left out: meshes are as they are at rest");
        return {};
    }
};

}  // namespace

std::expected<Imported, std::string> gltf(std::span<const std::uint8_t> bytes, const Options& options) {
    Reading reading{.options = &options};
    cgltf_options parse{};
    parse.file.read = &read_named, parse.file.release = &release_named, parse.file.user_data = &reading;
    Document document;
    if (const cgltf_result parsed = cgltf_parse(&parse, bytes.data(), bytes.size(), &document.data); parsed != cgltf_result_success)
        return std::unexpected(std::format("not a glTF document: {}", reason(parsed)));
    if (const cgltf_result loaded = cgltf_load_buffers(&parse, document.data, ""); loaded != cgltf_result_success)
        return std::unexpected(std::format("its buffers cannot be read: {}", reading.failed.empty() ? std::string(reason(loaded)) : reading.failed));
    if (const cgltf_result valid = cgltf_validate(document.data); valid != cgltf_result_success)
        return std::unexpected(std::format("its accessors, views and buffers do not agree: {}", reason(valid)));
    Maker maker{.data = *document.data, .options = options};
    if (auto made = maker.make(); !made) return std::unexpected(made.error());
    return std::move(maker.out);
}

}  // namespace xrender::asset
