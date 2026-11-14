module omui.showcase.quay.picture;

import std;
import mlibs.rhi;

namespace quay::picture {

namespace rhi = mlibs::rhi;

rhi::Result<OnDevice> OnDevice::make(rhi::Device& device, const Picture& picture, std::uint32_t width, std::uint32_t height) {
    OnDevice made;
    made.device_ = &device, made.width_ = width, made.height_ = height;
    auto pass = mesh::gpu::Pass::make(device, false, true);
    if (!pass) return std::unexpected(pass.error());
    made.pass_.emplace(std::move(*pass));
    if (auto loaded = made.pass_->load(picture.assets().meshes); !loaded) return std::unexpected(loaded.error());
    auto glow = mesh::gpu::Glow::make(device);
    if (!glow) return std::unexpected(glow.error());
    made.glow_.emplace(std::move(*glow));
    std::optional<rhi::Error> failed;
    const auto target = [&](rhi::Format format, std::uint32_t across, std::uint32_t down, std::string_view label) {
        const bool deep = format == rhi::Format::d32_float;
        auto texture = device.create(rhi::TextureDesc{format, across, down, deep ? rhi::TextureUsage::depth_target : rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
        if (!texture) {
            if (!failed) failed = texture.error();
            return rhi::Texture{};
        }
        return *texture;
    };
    made.colour_ = target(rhi::Format::rgba8_unorm, width, height, "quay.colour"), made.depth_ = target(rhi::Format::d32_float, width, height, "quay.depth");
    made.light_ = target(rhi::Format::rgba16_float, width, height, "quay.light");
    made.one_ = target(rhi::Format::rgba16_float, width, height, "quay.glow.one"), made.other_ = target(rhi::Format::rgba16_float, width, height, "quay.glow.other");
    // The moon's map over the quay about the eye; the lamps', the beacon's, the floodlight's and a flash's.
    made.shadows_ = {.size = 2048, .distance = 60, .lamp_size = 512, .lamps = static_cast<std::uint32_t>(mesh::gpu::most_lamps)};
    made.shadows_.map = target(rhi::Format::r32_float, made.shadows_.size, made.shadows_.size, "quay.shadow"),
    made.shadows_.depth = target(rhi::Format::d32_float, made.shadows_.size, made.shadows_.size, "quay.shadow.depth");
    made.shadows_.lamps_map = target(rhi::Format::r32_float, made.shadows_.lamp_size * 6, made.shadows_.lamp_size * made.shadows_.lamps, "quay.shadow.lamps");
    made.shadows_.lamps_depth = target(rhi::Format::d32_float, made.shadows_.lamp_size * 6, made.shadows_.lamp_size * made.shadows_.lamps, "quay.shadow.lamps.depth");
    if (failed) return std::unexpected(*failed);
    return made;
}

OnDevice::OnDevice(OnDevice&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)), pass_(std::move(other.pass_)), glow_(std::move(other.glow_)), shadows_(other.shadows_), colour_(other.colour_), depth_(other.depth_),
      light_(other.light_), one_(other.one_), other_(other.other_), width_(other.width_), height_(other.height_) {}
OnDevice& OnDevice::operator=(OnDevice&& other) noexcept {
    if (this != &other) {
        release();
        device_ = std::exchange(other.device_, nullptr), pass_ = std::move(other.pass_), glow_ = std::move(other.glow_), shadows_ = other.shadows_;
        colour_ = other.colour_, depth_ = other.depth_, light_ = other.light_, one_ = other.one_, other_ = other.other_, width_ = other.width_, height_ = other.height_;
    }
    return *this;
}
OnDevice::~OnDevice() { release(); }

void OnDevice::release() noexcept {
    if (!device_) return;
    static_cast<void>(device_->wait_idle());
    for (const rhi::Texture made : {colour_, depth_, light_, one_, other_, shadows_.map, shadows_.depth, shadows_.lamps_map, shadows_.lamps_depth})
        if (made) device_->destroy(made);
    pass_.reset(), glow_.reset();
    device_ = nullptr;
}

rhi::Result<void> OnDevice::record(rhi::CommandList& list, const Picture& picture) {
    for (std::size_t warden = 0; warden < picture.bodies(); ++warden) {
        const std::uint32_t which = picture.body_mesh(warden);
        if (auto bent = pass_->bend(list, which, *picture.assets().meshes[which]); !bent) return bent;
    }
    pass_->record(list, picture.world(), picture.seen(), picture.view(), picture.assets().materials, light_, depth_, width_, height_, picture.options(), &shadows_);
    glow_->record(list, light_, one_, other_, colour_, width_, height_, Picture::bloom(static_cast<int>(width_)));
    return {};
}

}  // namespace quay::picture
