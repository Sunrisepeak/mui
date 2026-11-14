// The compositing loops where no wider path is written (aarch64, wasm32):
// the scalar reference.
module xrender.raster.api;

import std;

namespace xrender::raster {

void over_run_reference(Rgba8* out, std::int32_t count, Rgba8 source) noexcept {
    for (std::int32_t index = 0; index < count; ++index) out[index] = over_scalar(out[index], source);
}

void over_run(Rgba8* out, std::int32_t count, Rgba8 source) noexcept { over_run_reference(out, count, source); }

void over_coverage_reference(Rgba8* out, std::int32_t count, Rgba8 source, const std::uint8_t* coverage) noexcept {
    for (std::int32_t index = 0; index < count; ++index) out[index] = over_scalar(out[index], scaled_scalar(source, coverage[index]));
}

void over_coverage(Rgba8* out, std::int32_t count, Rgba8 source, const std::uint8_t* coverage) noexcept {
    over_coverage_reference(out, count, source, coverage);
}

std::string_view blend_path() noexcept { return "scalar"; }

}  // namespace xrender::raster
