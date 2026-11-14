module xrender.particle.gpu.cts;

import std;
import mlibs.rhi;
import mlibs.cts;
import mlibs.math;
import mlibs.geom;
import xrender.particle.gpu;
import xrender.mesh.gpu;

namespace xrender::particle::gpu::cts {

namespace particle = xrender::particle;
namespace mesh = xrender::mesh;
namespace scene = xrender::scene;
namespace math = mlibs::math;
namespace geom = mlibs::geom;
namespace rhi = mlibs::rhi;

namespace {

thread_local mlibs::cts::Check* current = nullptr;

// A check and what is said of it: `expect(found) << "why"` (as xrender.mesh.gpu.cts's).
class Said {
public:
    Said(bool held, std::source_location at) : held_(held), at_(at) {}
    Said(const Said&) = delete;
    Said& operator=(const Said&) = delete;
    ~Said() {
        if (current) current->that(held_, why_.empty() ? std::string_view("the check did not hold") : std::string_view(why_), at_);
    }
    template <class Value>
    Said& operator<<(const Value& value) {
        if (!held_) why_ += std::format("{}", value);
        return *this;
    }
    explicit operator bool() const noexcept { return held_; }

private:
    bool                 held_;
    std::source_location at_;
    std::string          why_;
};
Said expect(bool held, std::source_location at = std::source_location::current()) { return {held, at}; }

constexpr std::uint32_t width = 320, height = 180;

// A floor and a wall behind it, grey; the sun; the eye a little above the floor looking at the wall.
struct Room {
    std::vector<geom::ClusteredMesh> made;
    std::vector<const geom::ClusteredMesh*> meshes;
    std::vector<mesh::Material> materials{{{0.4f, 0.4f, 0.4f}, {}}};
    scene::Scene world;
    scene::View view{.view = math::look_at({0, 1, 4}, {0, 0.6f, -2}, {0, 1, 0}), .projection = math::perspective(1.0f, static_cast<float>(width) / height, 0.1f, 100.0f)};
    Room() {
        made.push_back(*geom::clustered(geom::sheet(20, 20, 2, 2)));
        meshes.push_back(&made[0]);
        expect(world.add({.id = 1, .mesh = 0, .material = 0, .world = math::Mat4{}, .bounds = geom::bounds(made[0].mesh)}).has_value());
        expect(world.add({.id = 2, .mesh = 0, .material = 0, .world = math::translation({0, 0, -3}) * math::rotation(math::Quat::about({1, 0, 0}, 1.5707964f)), .bounds = geom::bounds(made[0].mesh)}).has_value());
        expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .colour = {1, 1, 1}, .intensity = 0.8f, .toward = {-0.3f, -1, -0.4f}}).has_value());
    }
};

// Sparks over the floor (added, some brighter than white), and puffs (laid over) — two of them touching the wall,
// one through the floor. Made the same way every time.
struct Particles {
    std::vector<particle::Particle> sparks, puffs;
    Particles() {
        std::uint64_t seed = 7;
        const auto unit = [&] {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<float>(seed >> 40) / static_cast<float>(1ull << 24);
        };
        for (std::uint32_t index = 0; index < 120; ++index)
            sparks.push_back({.at = {unit() * 4 - 2, 0.1f + unit() * 1.5f, unit() * 3 - 2.5f}, .size = 0.04f + unit() * 0.08f, .colour = {2.5f, 1.2f + unit(), 0.3f, 1}, .id = index});
        for (std::uint32_t index = 0; index < 24; ++index)
            puffs.push_back({.at = {unit() * 3 - 1.5f, 0.2f + unit() * 1.2f, -2.95f + unit() * 2}, .size = 0.3f + unit() * 0.5f, .colour = {0.6f, 0.62f, 0.66f, 0.35f + unit() * 0.4f}, .turn = unit() * 6, .id = index});
        puffs.push_back({.at = {0.3f, 0.02f, -0.5f}, .size = 0.8f, .colour = {0.5f, 0.5f, 0.5f, 0.6f}, .id = 99});  // half through the floor
    }
};

float of_half(std::uint16_t half) {
    const std::uint32_t sign = (half & 0x8000u) << 16, exponent = (half >> 10) & 0x1fu, fraction = half & 0x3ffu;
    if (exponent == 0) return (sign ? -1.0f : 1.0f) * std::ldexp(static_cast<float>(fraction), -24);
    return std::bit_cast<float>(sign | ((exponent + 112) << 23) | (fraction << 13));
}

// The device's frame of light: the meshes, then (unless none) the batches over it.
std::optional<std::vector<math::Vec3>> on_device(rhi::Device& device, const Room& room, std::span<const particle::Batch> batches) {
    auto meshes = mesh::gpu::Pass::make(device, false, true);
    auto particles = Pass::make(device);
    if (!(expect(meshes.has_value() && particles.has_value()) << (meshes ? (particles ? "" : particles.error().message) : meshes.error().message))) return std::nullopt;
    if (!expect(meshes->load(room.meshes).has_value())) return std::nullopt;
    meshes->use(room.materials);
    const auto target = [&](rhi::Format format, rhi::TextureUsage usage, const char* label) { return device.create(rhi::TextureDesc{format, width, height, usage, label}); };
    const auto sampled_target = rhi::TextureUsage::color_target | rhi::TextureUsage::sampled;
    const auto light = target(rhi::Format::rgba16_float, sampled_target, "particle.light"), surface = target(rhi::Format::rgba16_float, sampled_target, "particle.surface");
    const auto depth = target(rhi::Format::d32_float, rhi::TextureUsage::depth_target, "particle.depth");
    if (!expect(light.has_value() && surface.has_value() && depth.has_value())) return std::nullopt;
    scene::Extracted seen;
    scene::extract(room.world, room.view, seen);
    rhi::CommandList list;
    const mesh::Options options{.shadows = false};
    meshes->record(list, room.world, seen, room.view, room.materials, *light, *depth, width, height, options);
    meshes->record_surface(list, room.world, seen, room.view, *surface, *depth, width, height, options);
    if (!batches.empty()) {
        const auto recorded = particles->record(list, batches, room.view, *light, *surface, width, height);
        if (!(expect(recorded.has_value()) << (recorded ? "" : recorded.error().message))) return std::nullopt;
    }
    const auto submitted = device.submit(list);
    if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return std::nullopt;
    expect(device.wait(*submitted).has_value());
    const auto read = device.read(*light, rhi::Region{0, 0, width, height});
    if (!expect(read.has_value())) return std::nullopt;
    std::vector<math::Vec3> out(static_cast<std::size_t>(width) * height);
    for (std::size_t pixel = 0; pixel < out.size(); ++pixel) {
        std::array<std::uint16_t, 3> halves{};
        std::memcpy(halves.data(), read->data() + pixel * 8, 6);
        out[pixel] = {of_half(halves[0]), of_half(halves[1]), of_half(halves[2])};
    }
    static_cast<void>(device.wait_idle());
    device.destroy(*light), device.destroy(*surface), device.destroy(*depth);
    return out;
}
std::vector<math::Vec3> on_cpu(const Room& room, std::span<const particle::Batch> batches) {
    mesh::Targets frame;
    frame.resize(static_cast<int>(width), static_cast<int>(height));
    scene::Extracted seen;
    scene::extract(room.world, room.view, seen);
    mesh::Pass pass;
    pass.draw(room.world, seen, room.view, {room.meshes, room.materials}, frame, {.shadows = false});
    particle::draw(batches, room.view, frame);
    return frame.light;
}

struct Rig {
    rhi::Device& device;
};
// What the particles change on the device against what they change in the reference: pixels changed, those further
// than 2% (of the change's largest part, at least 0.02), the mean and the worst.
struct Compared {
    std::size_t changed = 0, off = 0;
    double      mean = 0, worst = 0;
};
std::optional<Compared> compared(Rig& rig, const Room& room, std::span<const particle::Batch> batches, std::string_view what) {
    const auto bare = on_device(rig.device, room, {}), drawn = on_device(rig.device, room, batches);
    if (!bare || !drawn) return std::nullopt;
    const std::vector<math::Vec3> reference_bare = on_cpu(room, {}), reference = on_cpu(room, batches);
    Compared out;
    double sum = 0;
    for (std::size_t pixel = 0; pixel < reference.size(); ++pixel) {
        const math::Vec3 truth = reference[pixel] - reference_bare[pixel], device_change = (*drawn)[pixel] - (*bare)[pixel];
        if (math::length(truth) < 1e-3f && math::length(device_change) < 1e-3f) continue;
        ++out.changed;
        const float apart = std::max({std::abs(truth.x - device_change.x), std::abs(truth.y - device_change.y), std::abs(truth.z - device_change.z)});
        const float allowed = 0.02f + 0.02f * std::max({std::abs(truth.x), std::abs(truth.y), std::abs(truth.z)});
        out.off += apart > allowed, sum += apart, out.worst = std::max(out.worst, static_cast<double>(apart));
    }
    out.mean = out.changed ? sum / static_cast<double>(out.changed) : 0.0;
    std::println("{} on {}: {} pixels changed; {} further than 2% from the reference's change ({:.2f}%), mean {:.4f}, worst {:.3f}", what, rig.device.caps().device, out.changed, out.off,
                 out.changed ? 100.0 * static_cast<double>(out.off) / static_cast<double>(out.changed) : 0.0, out.mean, out.worst);
    return out;
}

// A puff (a soft round cloud, its cover falling from the middle out, grey with a lighter top) and a spark's ring,
// 64 × 64 — read at many sizes as the particles near and leave the eye.
struct Pictures {
    mesh::Texture puff, ring;
    Pictures() {
        std::vector<std::uint8_t> puffed(64 * 64 * 4, 0), ringed(64 * 64 * 4, 0);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const float radius = std::hypot(x + 0.5f - 32, y + 0.5f - 32) / 32;
                const std::size_t at = static_cast<std::size_t>(y * 64 + x) * 4;
                const float there = std::clamp(1 - radius, 0.0f, 1.0f);
                const auto byte = [](float value) { return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255 + 0.5f); };
                puffed[at] = puffed[at + 1] = byte(0.55f + 0.35f * (1 - static_cast<float>(y) / 63)), puffed[at + 2] = byte(0.65f), puffed[at + 3] = byte(there * there * 1.4f);
                const float band = std::clamp(1 - std::abs(radius - 0.7f) * 6, 0.0f, 1.0f);
                ringed[at] = ringed[at + 1] = ringed[at + 2] = 255, ringed[at + 3] = byte(band);
            }
        puff = *mesh::Texture::made(puffed, 64, 64), ring = *mesh::Texture::made(ringed, 64, 64);
    }
};

struct Listed {
    std::string               id;
    std::function<void(Rig&)> body;
};
using Cases = std::vector<Listed>;

Cases suite_of() {
    Cases suite;
    // what the particles add to the frame on the device is what they add in the reference: sparks added, puffs laid over the farthest first, soft at the wall and the floor
    suite.push_back({"particle.room", [](Rig& rig) {
        const Room room;
        const Particles made;
        const std::array<particle::Batch, 2> batches{particle::Batch{.particles = made.sparks, .blend = particle::Blend::add, .soft = 0.05f, .id = 40},
                                                     particle::Batch{.particles = made.puffs, .blend = particle::Blend::over, .soft = 0.4f, .id = 41}};
        const auto found = compared(rig, room, batches, "particles over a room");
        if (!found) return;
        expect(found->changed > 2000) << found->changed << " pixels changed";
        expect(static_cast<double>(found->off) < 0.01 * static_cast<double>(found->changed)) << found->off << " of " << found->changed;
        expect(found->mean < 0.01);
        const auto drawn = on_device(rig.device, room, batches);
        if (!drawn) return;
        // In any order given, the same picture: the device lays them out as the reference does.
        std::vector<particle::Particle> backwards(made.puffs.rbegin(), made.puffs.rend()), sparks_backwards(made.sparks.rbegin(), made.sparks.rend());
        const std::array<particle::Batch, 2> reordered{particle::Batch{.particles = sparks_backwards, .blend = particle::Blend::add, .soft = 0.05f, .id = 40},
                                                       particle::Batch{.particles = backwards, .blend = particle::Blend::over, .soft = 0.4f, .id = 41}};
        const auto again = on_device(rig.device, room, reordered);
        if (!again) return;
        std::size_t differing = 0;
        for (std::size_t pixel = 0; pixel < again->size(); ++pixel) differing += math::length((*again)[pixel] - (*drawn)[pixel]) > 2e-3f;
        expect(differing < 50) << differing << " pixels differ with the particles given backwards";
    }});
    // a picture's particles on the device are the reference's: smoke laid over by its soft cover, rings added, read at the size the reference reads, between two sizes as it does
    suite.push_back({"particle.pictured", [](Rig& rig) {
        const Room room;
        const Particles made;
        const Pictures pictures;
        // Rings near and far (their texels a pixel from under 1 to over 8), the puffs as the room's.
        std::vector<particle::Particle> rings;
        for (std::uint32_t index = 0; index < 16; ++index) {
            const float far = static_cast<float>(index) / 15;
            rings.push_back({.at = {-1.6f + 3.2f * far, 0.4f + 0.6f * far, -0.4f - 2.2f * far}, .size = 0.12f + 0.5f * (1 - far), .colour = {1.6f, 1.2f, 0.5f, 1}, .turn = static_cast<float>(index), .id = index});
        }
        const std::array<particle::Batch, 2> batches{particle::Batch{.particles = rings, .blend = particle::Blend::add, .soft = 0.05f, .sprite = &pictures.ring, .id = 50},
                                                     particle::Batch{.particles = made.puffs, .blend = particle::Blend::over, .soft = 0.4f, .sprite = &pictures.puff, .id = 51}};
        const auto found = compared(rig, room, batches, "pictured particles over a room");
        if (!found) return;
        expect(found->changed > 2000) << found->changed << " pixels changed";
        expect(static_cast<double>(found->off) < 0.01 * static_cast<double>(found->changed)) << found->off << " of " << found->changed;
        expect(found->mean < 0.01);
    }});
    return suite;
}

}  // namespace

rhi::ShaderFormats shader_formats() { return Pass::shader_formats(); }

std::vector<std::string> cases() {
    std::vector<std::string> out;
    for (const Listed& one : suite_of()) out.push_back(one.id);
    return out;
}

void declare(rhi::Device& device, mlibs::cts::Against who, std::source_location at) {
    if (who.subject.empty()) who.subject = device.caps().device;
    Rig rig{device};
    Cases all = suite_of();
    mlibs::cts::against(
        std::move(who),
        [&rig, &all, at] {
            for (Listed& one : all)
                mlibs::cts::declare({.id = std::move(one.id), .contract = "xrender.particle.gpu@1", .level = mlibs::cts::Level::tolerance, .needs = {},
                                     .body = [&rig, body = std::move(one.body)](mlibs::cts::Check& check) {
                                         current = &check;
                                         body(rig);
                                         current = nullptr;
                                     }},
                                    at);
        },
        at);
}

}  // namespace xrender::particle::gpu::cts
