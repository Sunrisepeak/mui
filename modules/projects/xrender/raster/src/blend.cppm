// openmui.raster:blend — the compositing loops a run of pixels goes through,
// with the widest path this machine has chosen once at run time (W6, D-143):
// AVX2 on an x86-64 that has it, the scalar loop elsewhere. Every path gives
// the same bytes as the scalar one (the reference, D-43): the arithmetic is
// the same integers, only more of them at a time.
export module xrender.raster.api:blend;

import std;
import :surface;

export namespace xrender::raster {

// x·(255 − a)/255 + s per channel: source-over of one premultiplied colour.
[[nodiscard, gnu::always_inline]] inline Rgba8 over_scalar(Rgba8 d, Rgba8 s) noexcept {
    if (s.a == 255) return s;
    const std::uint32_t inv = 255u - s.a;
    return {static_cast<std::uint8_t>(s.r + mul255(d.r, inv)), static_cast<std::uint8_t>(s.g + mul255(d.g, inv)),
            static_cast<std::uint8_t>(s.b + mul255(d.b, inv)), static_cast<std::uint8_t>(s.a + mul255(d.a, inv))};
}

// A premultiplied colour scaled by coverage k (0…255).
[[nodiscard, gnu::always_inline]] inline Rgba8 scaled_scalar(Rgba8 s, std::uint32_t k) noexcept {
    if (k == 255) return s;
    return {static_cast<std::uint8_t>(mul255(s.r, k)), static_cast<std::uint8_t>(mul255(s.g, k)), static_cast<std::uint8_t>(mul255(s.b, k)),
            static_cast<std::uint8_t>(mul255(s.a, k))};
}

// out[i] = over(out[i], source) for `count` pixels.
void over_run(Rgba8* out, std::int32_t count, Rgba8 source) noexcept;
// out[i] = over(out[i], source scaled by coverage[i]) for `count` pixels.
void over_coverage(Rgba8* out, std::int32_t count, Rgba8 source, const std::uint8_t* coverage) noexcept;
void over_coverage_reference(Rgba8* out, std::int32_t count, Rgba8 source, const std::uint8_t* coverage) noexcept;

// The path over_run takes here: "avx2" or "scalar" (a trace, a report).
[[nodiscard]] std::string_view blend_path() noexcept;
// For tests: the scalar path, whatever the machine has.
void over_run_reference(Rgba8* out, std::int32_t count, Rgba8 source) noexcept;

}  // namespace xrender::raster
