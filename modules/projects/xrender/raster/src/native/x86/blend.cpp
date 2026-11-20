// The compositing loops on x86-64: AVX2 where the processor has it (chosen
// once, at the first call), the scalar reference otherwise.
module;
#include <cpuid.h>
#include <immintrin.h>
module xrender.raster.api;

import std;

namespace xrender::raster {

namespace {

// Eight pixels at a time, each channel as a 16-bit lane: t = d·inv + 128,
// (t + (t >> 8)) >> 8 — mul255 exactly (t stays below 2^16) — plus the
// source, wrapping as the scalar's uint8_t does.
[[gnu::target("avx2")]] void over_run_avx2(Rgba8* out, std::int32_t count, Rgba8 source) noexcept {
    const auto inv = _mm256_set1_epi16(static_cast<short>(255 - source.a));
    const auto bias = _mm256_set1_epi16(128);
    const auto zero = _mm256_setzero_si256();
    const auto packed = std::bit_cast<std::int32_t>(source);
    const auto src = _mm256_set1_epi32(packed);
    std::int32_t index = 0;
    for (; index + 8 <= count; index += 8) {
        auto* place = reinterpret_cast<__m256i*>(out + index);
        const auto pixels = _mm256_loadu_si256(place);
        auto low = _mm256_add_epi16(_mm256_mullo_epi16(_mm256_unpacklo_epi8(pixels, zero), inv), bias);
        auto high = _mm256_add_epi16(_mm256_mullo_epi16(_mm256_unpackhi_epi8(pixels, zero), inv), bias);
        low = _mm256_srli_epi16(_mm256_add_epi16(low, _mm256_srli_epi16(low, 8)), 8);
        high = _mm256_srli_epi16(_mm256_add_epi16(high, _mm256_srli_epi16(high, 8)), 8);
        _mm256_storeu_si256(place, _mm256_add_epi8(_mm256_packus_epi16(low, high), src));
    }
    for (; index < count; ++index) out[index] = over_scalar(out[index], source);
}

// x·y/255 rounded, sixteen 16-bit lanes at once (mul255's integers).
[[gnu::target("avx2")]] inline __m256i mul255_lanes(__m256i x, __m256i y, __m256i bias) noexcept {
    const auto t = _mm256_add_epi16(_mm256_mullo_epi16(x, y), bias);
    return _mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8);
}

// Four pixels at a time, each channel a 16-bit lane: the source scaled by
// each pixel's coverage (mul255), then over (mul255 of the destination by
// 255 minus the scaled alpha, plus the scaled source) — the scalar's integers.
[[gnu::target("avx2")]] void over_coverage_avx2(Rgba8* out, std::int32_t count, Rgba8 source, const std::uint8_t* coverage) noexcept {
    const auto bias = _mm256_set1_epi16(128);
    const auto full = _mm256_set1_epi16(255);
    const auto source16 = _mm256_cvtepu8_epi16(_mm_set1_epi32(std::bit_cast<std::int32_t>(source)));
    const auto spread = _mm_setr_epi8(0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3);
    std::int32_t index = 0;
    for (; index + 4 <= count; index += 4) {
        std::int32_t four = 0;
        std::memcpy(&four, coverage + index, 4);
        const auto k = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(_mm_cvtsi32_si128(four), spread));
        const auto scaled = mul255_lanes(source16, k, bias);
        // Each pixel's scaled alpha in its four lanes.
        const auto alpha = _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(scaled, 0xFF), 0xFF);
        const auto inverse = _mm256_sub_epi16(full, alpha);
        auto* place = reinterpret_cast<__m128i*>(out + index);
        const auto destination = _mm256_cvtepu8_epi16(_mm_loadu_si128(place));
        const auto result = _mm256_add_epi16(scaled, mul255_lanes(destination, inverse, bias));
        const auto packed = _mm256_permute4x64_epi64(_mm256_packus_epi16(result, result), 0b1000);
        _mm_storeu_si128(place, _mm256_castsi256_si128(packed));
    }
    if (index == count) return;
    // The last one to three: four lanes as above, the missing ones covered 0 (over of nothing keeps a pixel).
    alignas(16) std::array<Rgba8, 4> rest{};
    std::array<std::uint8_t, 4> rest_coverage{};
    const auto left = static_cast<std::size_t>(count - index);
    std::copy_n(out + index, left, rest.begin());
    std::copy_n(coverage + index, left, rest_coverage.begin());
    std::int32_t four = 0;
    std::memcpy(&four, rest_coverage.data(), 4);
    const auto k = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(_mm_cvtsi32_si128(four), spread));
    const auto scaled = mul255_lanes(source16, k, bias);
    const auto inverse = _mm256_sub_epi16(full, _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(scaled, 0xFF), 0xFF));
    auto* place = reinterpret_cast<__m128i*>(rest.data());
    const auto result = _mm256_add_epi16(scaled, mul255_lanes(_mm256_cvtepu8_epi16(_mm_load_si128(place)), inverse, bias));
    _mm_store_si128(place, _mm256_castsi256_si128(_mm256_permute4x64_epi64(_mm256_packus_epi16(result, result), 0b1000)));
    std::copy_n(rest.begin(), left, out + index);
}

// The OS saves the AVX registers (XCR0's SSE and AVX state bits).
[[gnu::target("xsave")]] bool avx_state_saved() noexcept { return (_xgetbv(0) & 0x6) == 0x6; }

// AVX2 here: the processor has it (CPUID leaf 7, EBX bit 5) and the OS keeps its
// registers (leaf 1, ECX bit 27: OSXSAVE; then XCR0) — CPUID itself, so no
// runtime library is needed for the question.
bool wide() noexcept {
    static const bool avx2 = [] {
        unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
        if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx) || (ecx & (1u << 27)) == 0 || !avx_state_saved()) return false;
        if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) return false;
        return (ebx & (1u << 5)) != 0;
    }();
    return avx2;
}

}  // namespace

void over_run_reference(Rgba8* out, std::int32_t count, Rgba8 source) noexcept {
    for (std::int32_t index = 0; index < count; ++index) out[index] = over_scalar(out[index], source);
}

void over_run(Rgba8* out, std::int32_t count, Rgba8 source) noexcept {
    if (source.a == 255) {
        std::fill_n(out, count, source);
        return;
    }
    if (count < 8) {
        over_run_reference(out, count, source);
        return;
    }
    if (wide()) over_run_avx2(out, count, source);
    else over_run_reference(out, count, source);
}

void over_coverage_reference(Rgba8* out, std::int32_t count, Rgba8 source, const std::uint8_t* coverage) noexcept {
    for (std::int32_t index = 0; index < count; ++index) out[index] = over_scalar(out[index], scaled_scalar(source, coverage[index]));
}

void over_coverage(Rgba8* out, std::int32_t count, Rgba8 source, const std::uint8_t* coverage) noexcept {
    if (count > 0 && wide()) over_coverage_avx2(out, count, source, coverage);
    else over_coverage_reference(out, count, source, coverage);
}

std::string_view blend_path() noexcept { return wide() ? "avx2" : "scalar"; }

}  // namespace xrender::raster
