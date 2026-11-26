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
    if (auto loaded = made.pass_->load_textures(picture.assets().textures); !loaded) return std::unexpected(loaded.error());
    auto glow = mesh::gpu::Glow::make(device);
    if (!glow) return std::unexpected(glow.error());
    made.glow_.emplace(std::move(*glow));
    auto rain = mrender::particle::gpu::Pass::make(device);
    if (!rain) return std::unexpected(rain.error());
    made.rain_.emplace(std::move(*rain));
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
    // The moon's map over the quay about the eye; the lamps', the beacon's, the floodlight's and a flash's.
    made.shadows_ = {.size = 2048, .distance = 60, .lamp_size = 512, .lamps = static_cast<std::uint32_t>(mesh::gpu::most_lamps)};
    made.shadows_.map = target(rhi::Format::r32_float, made.shadows_.size, made.shadows_.size, "quay.shadow"),
    made.shadows_.depth = target(rhi::Format::d32_float, made.shadows_.size, made.shadows_.size, "quay.shadow.depth");
    made.shadows_.lamps_map = target(rhi::Format::r32_float, made.shadows_.lamp_size * 6, made.shadows_.lamp_size * made.shadows_.lamps, "quay.shadow.lamps");
    made.shadows_.lamps_depth = target(rhi::Format::d32_float, made.shadows_.lamp_size * 6, made.shadows_.lamp_size * made.shadows_.lamps, "quay.shadow.lamps.depth");
    if (failed) return std::unexpected(*failed);
    if (auto sized = made.resize(width, height); !sized) return std::unexpected(sized.error());
    return made;
}

rhi::Result<void> OnDevice::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) return rhi::fail(rhi::Errc::invalid, "a frame of no pixels");
    if (colour_ && width == width_ && height == height_) return {};
    static_cast<void>(device_->wait_idle());
    for (rhi::Texture* made : {&colour_, &depth_, &light_, &one_, &other_, &surface_})
        if (*made) device_->destroy(*made), *made = {};
    width_ = width, height_ = height;
    const auto target = [&](rhi::Format format, std::string_view label) -> rhi::Result<rhi::Texture> {
        const bool deep = format == rhi::Format::d32_float;
        return device_->create(rhi::TextureDesc{format, width, height, deep ? rhi::TextureUsage::depth_target : rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
    };
    for (auto [made, format, label] : {std::tuple{&colour_, rhi::Format::rgba8_unorm, "quay.colour"}, std::tuple{&depth_, rhi::Format::d32_float, "quay.depth"},
                                       std::tuple{&light_, rhi::Format::rgba16_float, "quay.light"}, std::tuple{&surface_, rhi::Format::rgba16_float, "quay.surface"},
                                       std::tuple{&one_, rhi::Format::rgba16_float, "quay.glow.one"}, std::tuple{&other_, rhi::Format::rgba16_float, "quay.glow.other"}}) {
        auto texture = target(format, label);
        if (!texture) return std::unexpected(texture.error());
        *made = *texture;
    }
    return {};
}

OnDevice::OnDevice(OnDevice&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)), pass_(std::move(other.pass_)), glow_(std::move(other.glow_)), rain_(std::move(other.rain_)), shadows_(other.shadows_), colour_(other.colour_), depth_(other.depth_),
      light_(other.light_), one_(other.one_), other_(other.other_), surface_(other.surface_), width_(other.width_), height_(other.height_) {}
OnDevice& OnDevice::operator=(OnDevice&& other) noexcept {
    if (this != &other) {
        release();
        device_ = std::exchange(other.device_, nullptr), pass_ = std::move(other.pass_), glow_ = std::move(other.glow_), rain_ = std::move(other.rain_), shadows_ = other.shadows_;
        colour_ = other.colour_, depth_ = other.depth_, light_ = other.light_, one_ = other.one_, other_ = other.other_, surface_ = other.surface_, width_ = other.width_, height_ = other.height_;
    }
    return *this;
}
OnDevice::~OnDevice() { release(); }

void OnDevice::release() noexcept {
    if (!device_) return;
    static_cast<void>(device_->wait_idle());
    for (const rhi::Texture made : {colour_, depth_, light_, one_, other_, surface_, shadows_.map, shadows_.depth, shadows_.lamps_map, shadows_.lamps_depth})
        if (made) device_->destroy(made);
    pass_.reset(), glow_.reset(), rain_.reset();
    device_ = nullptr;
}

rhi::Result<void> OnDevice::record(rhi::CommandList& list, const Picture& picture) {
    for (std::size_t warden = 0; warden < picture.bodies(); ++warden) {
        const std::uint32_t which = picture.body_mesh(warden);
        if (auto bent = pass_->bend(list, which, *picture.assets().meshes[which]); !bent) return bent;
    }
    pass_->record(list, picture.world(), picture.seen(), picture.view(), picture.assets().materials, light_, depth_, width_, height_, picture.options(), &shadows_);
    // The rain over the light, faded at the surfaces behind it.
    pass_->record_surface(list, picture.world(), picture.seen(), picture.view(), surface_, depth_, width_, height_, picture.options());
    if (auto rained = rain_->record(list, picture.rain(), picture.view(), light_, surface_, width_, height_); !rained) return rained;
    glow_->record(list, light_, one_, other_, colour_, width_, height_, Picture::bloom(static_cast<int>(width_)));
    return {};
}

std::expected<Played, std::string> Played::make(rhi::Device& device, std::uint32_t width, std::uint32_t height) {
    Played made;
    made.device_ = &device;
    auto picture = Picture::make(level::harbour());
    if (!picture) return std::unexpected(picture.error());
    made.picture_.emplace(std::move(*picture));
    auto drawn = OnDevice::make(device, *made.picture_, width, height);
    if (!drawn) return std::unexpected(std::format("Quay's picture on the device: {}", drawn.error().message));
    made.drawn_.emplace(std::move(*drawn));
    // A second in: the first warden in view and the first shots.
    made.state_ = start(level::harbour());
    while (made.state_.frame < 60) step(made.state_, made.guide_(made.state_));
    return made;
}

bool Played::again_if_over() {
    if (state_.outcome == Outcome::playing) return false;
    state_ = start(level::harbour()), guide_ = {}, ++games_;
    return true;
}

std::expected<double, std::string> Played::frame() {
    const auto begun = std::chrono::steady_clock::now();
    step(state_, guide_(state_));
    picture_->prepare(state_);
    list_.clear();
    if (auto recorded = drawn_->record(list_, *picture_); !recorded) return std::unexpected(std::format("the frame could not be recorded: {}", recorded.error().message));
    const auto submitted = device_->submit(list_);
    const double encoded = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count();
    const auto waited = device_->wait_idle();
    if (!submitted || !waited) return std::unexpected(std::format("the frame failed: {}", !submitted ? submitted.error().message : waited.error().message));
    return encoded;
}

}  // namespace quay::picture
