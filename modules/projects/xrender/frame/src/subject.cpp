module xrender.frame;

import std;
import openas;
import mlibs.agent;
import mlibs.base64;
import mlibs.hash;
import mlibs.image;
import mlibs.json;

namespace xrender {

namespace {

// The most pixels a capture answers: beyond it the answer would not be read, only paid for.
inline constexpr std::int64_t most_pixels = 4'000'000;

}  // namespace

std::uint64_t FrameSubject::state_hash() const {
    if (before_) before_();
    mlibs::hash::Fnv1a hash;
    if (!target_) return hash.value();
    hash.u64(static_cast<std::uint64_t>(target_->width())).u64(static_cast<std::uint64_t>(target_->height()));
    for (const raster::Rgba8& pixel : target_->pixels()) hash.object(pixel);
    return hash.value();
}

void FrameSubject::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    const auto invalid = [&](std::string_view why) { answer.fail(openas::error_code::invalid_argument, why); };
    if (before_) before_();
    const int width = target_ ? target_->width() : 0, height = target_ ? target_->height() : 0;

    if (operation == "frame.layers") {
        std::string out = std::format("{{\"size\":[{},{}],\"hash\":\"{:016x}\",\"layers\":[", width, height, state_hash());
        if (frame_) {
            bool first = true;
            for (const LayerReport& layer : frame_->report().layers) {
                out += std::format("{}{{\"name\":{},\"commands\":{},\"rows\":{},\"ids\":{},\"missing\":{{\"images\":{},\"glyphs\":{},\"effects\":{},\"external\":{}}}}}",
                                   first ? "" : ",", mlibs::agent::json_text(layer.name), layer.report.commands, layer.report.rows, layer.object_ids,
                                   layer.report.missing_images, layer.report.missing_glyphs, layer.report.effects_skipped, layer.report.external_skipped);
                first = false;
            }
        }
        return answer.data(out + "]}");
    }
    if (!target_ || width <= 0 || height <= 0) return answer.fail(openas::error_code::stale, "no frame has been drawn yet");

    if (operation == "frame.capture") {
        std::array<std::int64_t, 4> region{0, 0, width, height};
        if (const auto asked = request["region"]) {
            const auto parts = asked.items();
            for (std::size_t index = 0; index < 4; ++index) {
                const auto value = parts.size() == 4 ? parts[index].integer() : std::nullopt;
                if (!value) return invalid("`region` is [x, y, width, height], whole pixels");
                region[index] = *value;
            }
            if (region[0] < 0 || region[1] < 0 || region[2] < 1 || region[3] < 1 || region[0] + region[2] > width || region[1] + region[3] > height)
                return invalid(std::format("`region` [{}, {}, {}, {}] is not within the frame ({} × {})", region[0], region[1], region[2], region[3], width, height));
        }
        if (region[2] * region[3] > most_pixels)
            return invalid(std::format("{} pixels asked, and a capture answers at most {}: ask for a region", region[2] * region[3], most_pixels));
        const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(target_->row(static_cast<int>(region[1])) + region[0]), static_cast<int>(region[2]),
                                      static_cast<int>(region[3]), width * 4, mlibs::image::Alpha::premultiplied};
        const auto png = mlibs::image::encode_png(view);
        if (!png) return answer.fail(openas::error_code::failed, png.error());
        return answer.data(std::format("{{\"region\":[{},{},{},{}],\"format\":\"png\",\"bytes\":{},\"png\":\"{}\"}}", region[0], region[1], region[2], region[3],
                                       png->size(), mlibs::base64::encode(*png)));
    }
    if (operation == "frame.at") {
        const auto parts = request["at"].items();
        const auto x = parts.size() == 2 ? parts[0].integer() : std::nullopt;
        const auto y = parts.size() == 2 ? parts[1].integer() : std::nullopt;
        if (!x || !y) return invalid("`at` is [x, y], whole pixels of the frame");
        if (*x < 0 || *y < 0 || *x >= width || *y >= height) return invalid(std::format("[{}, {}] is not within the frame ({} × {})", *x, *y, width, height));
        const raster::Rgba8 pixel = target_->at(static_cast<int>(*x), static_cast<int>(*y));
        std::string out = std::format("{{\"at\":[{},{}],\"pixel\":[{},{},{},{}],\"chain\":[", *x, *y, static_cast<int>(pixel.r), static_cast<int>(pixel.g),
                                      static_cast<int>(pixel.b), static_cast<int>(pixel.a));
        const PixelSource source = frame_ ? frame_->at(static_cast<int>(*x), static_cast<int>(*y)) : PixelSource{};
        if (source.layer >= 0) {
            out += std::format("{{\"layer\":{},\"index\":{},\"object\":{}", mlibs::agent::json_text(frame_->report().layers[static_cast<std::size_t>(source.layer)].name),
                               source.layer, source.object);
            // What the layer itself knows of it (a 3D layer: the instance, the cluster, the triangle).
            std::string more;
            frame_->describe(static_cast<std::size_t>(source.layer), static_cast<int>(*x), static_cast<int>(*y), more);
            if (!more.empty()) out += "," + more;
            out += "}";
        }
        return answer.data(out + "]}");
    }
}

}  // namespace xrender
