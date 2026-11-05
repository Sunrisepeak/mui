// Capture is the region an effect reads; output bounds are where it may
// change its source. Check the latter against actual CPU/GPU pixels.
import std;
import mcpplibs.os;
import openmui.geom;
import openmui.raster;
import openmui.render.vector;
import openmui.rhi;
import openmui.rhi.sdl;
import openmui.testing;

using namespace omui::testing;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;
namespace rhi = omui::rhi;

namespace {
constexpr int W = 192, H = 144;
constexpr geom::Rect layer{64, 48, 64, 48};

struct Case {
    std::string_view name;
    std::vector<render::EffectShape> shapes;
    float smoothness = 0, thickness = 12, blur = 0;
};

std::vector<Case> cases() {
    const render::EffectShape single{layer, geom::CornerRadii::all(8)};
    const std::vector<render::EffectShape> pair{
        {{64, 48, 28, 48}, geom::CornerRadii::all(8)}, {{100, 48, 28, 48}, geom::CornerRadii::all(8)}};
    return {{"single", {single}}, {"single smooth", {single}, 12}, {"pair smooth", pair, 12},
            {"eight overlapping", std::vector<render::EffectShape>(8, single), 12},
            {"thick frosted", pair, 12, 70, 18}, {"thin frosted", {single}, 0, 4, 18}};
}

std::vector<float> params(render::EffectRef ref, const Case& c) {
    const std::array<std::pair<std::string_view, float>, 5> values{{
        {"smoothness", c.smoothness}, {"thickness", c.thickness}, {"blur", c.blur}, {"tint_a", 0.6f}, {"adaptive", 0}}};
    return render::effects().params(ref, values);
}

geom::Rect scaled(geom::Rect r, float scale) { return {r.x * scale, r.y * scale, r.width * scale, r.height * scale}; }

geom::Rect geometry(const Case& c) {
    float left = layer.x, top = layer.y, right = layer.right(), bottom = layer.bottom();
    for (const auto& shape : c.shapes) {
        left = std::min(left, shape.rect.x);
        top = std::min(top, shape.rect.y);
        right = std::max(right, shape.rect.right());
        bottom = std::max(bottom, shape.rect.bottom());
    }
    return {left, top, right - left, bottom - top};
}

bool inside(geom::Rect r, geom::Point p, float reach) {
    return p.x >= r.x - reach && p.x < r.right() + reach && p.y >= r.y - reach && p.y < r.bottom() + reach;
}

void compare(std::span<const raster::Rgba8> source, std::span<const raster::Rgba8> result,
             const Case& c, float scale, float reach) {
    const int width = static_cast<int>(static_cast<float>(W) * scale);
    const int height = static_cast<int>(static_cast<float>(H) * scale);
    const auto bounds = scaled(geometry(c), scale);
    int outside = 0, escaped = 0, changed = 0, beyond_geometry = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const auto i = static_cast<std::size_t>(y * width + x);
            const bool different = source[i] != result[i];
            const geom::Point p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            // One logical pixel is a conservative allowance for the CPU and
            // shader's half-device-pixel glass coverage ramp.
            if (!inside(bounds, p, (reach + 1) * scale)) {
                ++outside;
                escaped += different;
            }
            changed += different;
            if (!inside(bounds, p, scale)) beyond_geometry += different;
        }
    check(outside > 1000, std::format("{} has substantial outside-source evidence", c.name));
    check(escaped == 0, std::format("{} scale {}: {} pixels outside output reach + AA differ from input", c.name, scale, escaped));
    check(changed > 50, std::format("{} actually runs the effect", c.name));
    if (c.name == "eight overlapping")
        check(beyond_geometry > 0, "smooth unions really generate pixels beyond geometry; zero reach would be false");
}

// The layer's own content extends outside the declared geometry, but within
// capture reach. It must survive: output reach describes newly written
// pixels, not a clip for existing source commands.
void scene(render::DisplayList& dl, render::EffectRef ref, const Case& c, bool effect) {
    dl.fill_rect({0, 0, W, H}, geom::Color::hex(0x243456));
    for (int x = 0; x < W; x += 8)
        dl.fill_rect({static_cast<float>(x), 0, 4, H}, geom::Color::hex(0xc07845));
    const auto p = params(ref, c);
    if (effect) dl.push_layer(1, render::BlendMode::normal, ref, p, layer, c.shapes);
    dl.fill_rect({60, 56, 3, 6}, geom::Color::hex(0x18bca1));
    dl.fill_rect({88, 70, 6, 5}, geom::Color::hex(0x152141));
    if (effect) dl.pop_layer();
}

std::unique_ptr<rhi::Device> open_device() {
    rhi::sdl::Options o;
    o.formats = render::gpu_shader_formats();
    if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
    if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
    auto device = rhi::sdl::create(o);
    if (!device) {
        if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1")
            check(false, std::format("GPU required: {}", device.error().message));
        else std::println("GPU output-domain check unavailable: {} (OMUI_REQUIRE_GPU=1 makes this fail)", device.error().message);
        return nullptr;
    }
    return std::move(*device);
}
}  // namespace

int main(int argc, char** argv) {
    Suite s{"render.effect_output_bounds"};
    s.test("nested content scopes and scaled objects agree on actual CPU and GPU filter sizes", [] {
        auto device=open_device();if(!device)return;
        auto gpu=render::GpuRenderer::create(*device);check(gpu.has_value());if(!gpu)return;
        const auto blur=*render::effects().find("blur");
        for(float dpi:{1.f,2.f}) {
            const auto w=static_cast<std::uint32_t>(W*dpi),h=static_cast<std::uint32_t>(H*dpi);
            auto texture=device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm,w,h,
                rhi::TextureUsage::color_target|rhi::TextureUsage::sampled,"test.content_scopes"});
            check(texture.has_value());if(!texture)return;
            std::array<std::vector<std::byte>,3> images;
            for(int variant=0;variant<3;++variant) {
                render::DisplayList list;
                if(variant!=2)list.push_transform(geom::Affine::translate(20,20)*geom::Affine::scale(2,2));
                for(float radius:{2.f,3.f}) {
                    const auto values=render::effects().params(blur,std::array{std::pair<std::string_view,float>{"radius",radius}});
                    const auto box=variant==0?geom::Rect{}:variant==1?geom::Rect{0,0,30,30}:geom::Rect{20,20,60,60};
                    list.push_layer(1,render::BlendMode::normal,blur,values,box,{},
                        variant==0?render::LayerBoundsMode::content:render::LayerBoundsMode::declared);
                }
                list.fill_rect(variant==2?geom::Rect{30,30,20,20}:geom::Rect{5,5,10,10},geom::Color::white());
                list.pop_layer();list.pop_layer();if(variant!=2)list.pop_transform();
                const auto report=gpu->render(list,*texture,{}, {.scale=dpi});check(report && report->effects_skipped==0);
                if(!report)return;
                const auto bytes=device->read(*texture,{0,0,w,h});check(bytes.has_value());if(!bytes)return;
                images[static_cast<std::size_t>(variant)].assign(bytes->begin(),bytes->end());
                raster::Surface reference(static_cast<int>(w),static_cast<int>(h));render::CpuRenderer cpu;
                const auto cpu_report=cpu.render(list,reference,{}, {.scale=dpi});check(cpu_report.effects_skipped==0);
                const auto expected=std::as_bytes(reference.pixels());int worst=0;
                for(std::size_t i=0;i<expected.size();++i)worst=std::max(worst,std::abs(std::to_integer<int>(expected[i])-std::to_integer<int>((*bytes)[i])));
                check(worst<=2,std::format("DPI {} variant {}: two nested filter roundings differ by at most two levels, got {}",dpi,variant,worst));
            }
            for(std::size_t variant=1;variant<3;++variant) {
                int worst=0;for(std::size_t i=0;i<images[0].size();++i)
                    worst=std::max(worst,std::abs(std::to_integer<int>(images[0][i])-std::to_integer<int>(images[variant][i])));
                check(worst<=2,"GPU automatic, declared and flattened captures describe the same picture");
            }
            device->destroy(*texture);
        }
    });
    s.test("real GPU glass preserves source outside output bounds with single and smooth grouped shapes", [] {
        auto device = open_device();
        if (!device) return;
        auto gpu = render::GpuRenderer::create(*device);
        check(gpu.has_value(), gpu ? "" : gpu.error().message);
        if (!gpu) return;
        auto texture = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, W, H,
            rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "test.output_bounds"});
        check(texture.has_value());
        if (!texture) return;
        const auto ref = render::effects().find("liquid_glass");
        check(ref.has_value());
        if (!ref) return;
        for (const auto& c : cases()) {
            std::array<std::vector<raster::Rgba8>, 2> images;
            bool complete = true;
            for (int variant = 0; variant < 2; ++variant) {
                render::DisplayList dl;
                scene(dl, *ref, c, variant != 0);
                const auto report = gpu->render(dl, *texture, {}, {.scale = 1, .background = geom::Color::transparent()});
                check(report.has_value(), report ? "" : report.error().message);
                if (!report) { complete = false; break; }
                check(report->effects_skipped == 0, "real shader path runs");
                const auto bytes = device->read(*texture, {0, 0, W, H});
                check(bytes.has_value(), bytes ? "" : bytes.error().message);
                if (!bytes) { complete = false; break; }
                images[static_cast<std::size_t>(variant)].resize(W * H);
                std::memcpy(images[static_cast<std::size_t>(variant)].data(), bytes->data(), W * H * 4);
            }
            if (!complete) continue;
            const auto p = params(*ref, c);
            compare(images[0], images[1], c, 1, *render::effects().output_reach(*ref, p));
            check(images[0][56 * W + 60] == images[1][56 * W + 60], "source command outside geometry survives output-reach declaration");
        }
    });
    return s.run(argc, argv);
}
