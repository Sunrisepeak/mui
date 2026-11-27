// The compositing loops on x86-64: AVX2 where the processor has it (chosen
// once, at the first call), the scalar reference otherwise.
module;
#include <cpuid.h>
#include <immintrin.h>
module mrender.raster.api;

import std;

namespace mrender::raster {

namespace {

// The pixels [0, count) of eight of a run (count 1…8), through a mask: nothing past them is read or written.
[[gnu::target("avx2")]] inline __m256i first_of_eight(std::int32_t count) noexcept {
    return _mm256_cmpgt_epi32(_mm256_set1_epi32(count), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
}

// x·y/255 rounded, sixteen 16-bit lanes at once (mul255's integers).
[[gnu::target("avx2")]] inline __m256i mul255_lanes(__m256i x, __m256i y, __m256i bias) noexcept {
    const auto t = _mm256_add_epi16(_mm256_mullo_epi16(x, y), bias);
    return _mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8);
}

// Eight pixels over one source: each channel a 16-bit lane, t = d·inv + 128,
// (t + (t >> 8)) >> 8 — mul255 exactly (t stays below 2^16) — plus the
// source, wrapping as the scalar's uint8_t does.
[[gnu::target("avx2")]] inline __m256i over_eight(__m256i pixels, __m256i src, __m256i inv, __m256i bias) noexcept {
    const auto zero = _mm256_setzero_si256();
    const auto low = mul255_lanes(_mm256_unpacklo_epi8(pixels, zero), inv, bias);
    const auto high = mul255_lanes(_mm256_unpackhi_epi8(pixels, zero), inv, bias);
    return _mm256_add_epi8(_mm256_packus_epi16(low, high), src);
}

// A run, eight pixels at a time; the last one to seven through a mask.
[[gnu::target("avx2")]] void over_run_avx2(Rgba8* out, std::int32_t count, Rgba8 source) noexcept {
    const auto inv = _mm256_set1_epi16(static_cast<short>(255 - source.a));
    const auto bias = _mm256_set1_epi16(128);
    const auto src = _mm256_set1_epi32(std::bit_cast<std::int32_t>(source));
    std::int32_t index = 0;
    for (; index + 8 <= count; index += 8) {
        auto* place = reinterpret_cast<__m256i*>(out + index);
        _mm256_storeu_si256(place, over_eight(_mm256_loadu_si256(place), src, inv, bias));
    }
    if (index == count) return;
    auto* place = reinterpret_cast<int*>(out + index);
    const auto mask = first_of_eight(count - index);
    _mm256_maskstore_epi32(place, mask, over_eight(_mm256_maskload_epi32(place, mask), src, inv, bias));
}

// Eight pixels, each channel a 16-bit lane (unpack's order: pixels 0, 1, 4,
// 5 from the low bytes of each 128-bit half, 2, 3, 6, 7 from the high): the
// source scaled by each pixel's coverage (mul255), then over (mul255 of the
// destination by 255 minus the scaled alpha, plus the scaled source) — the
// scalar's integers. `eight`: the coverages, the first in the low byte.
[[gnu::target("avx2")]] inline __m256i over_eight_covered(__m256i pixels, std::uint64_t eight, __m256i source16, __m256i bias) noexcept {
    const auto zero = _mm256_setzero_si256();
    const auto full = _mm256_set1_epi16(255);
    // Each of eight coverages over its pixel's four bytes: 0–3 in the low half, 4–7 in the high.
    const auto spread = _mm256_setr_epi8(0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7);
    const auto k = _mm256_shuffle_epi8(_mm256_set1_epi64x(static_cast<long long>(eight)), spread);
    const auto scaled_low = mul255_lanes(source16, _mm256_unpacklo_epi8(k, zero), bias);
    const auto scaled_high = mul255_lanes(source16, _mm256_unpackhi_epi8(k, zero), bias);
    // Each pixel's scaled alpha in its four lanes.
    const auto inverse_low = _mm256_sub_epi16(full, _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(scaled_low, 0xFF), 0xFF));
    const auto inverse_high = _mm256_sub_epi16(full, _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(scaled_high, 0xFF), 0xFF));
    const auto low = _mm256_add_epi16(scaled_low, mul255_lanes(_mm256_unpacklo_epi8(pixels, zero), inverse_low, bias));
    const auto high = _mm256_add_epi16(scaled_high, mul255_lanes(_mm256_unpackhi_epi8(pixels, zero), inverse_high, bias));
    return _mm256_packus_epi16(low, high);
}

// A run of varying coverage, eight pixels at a time; the last one to seven
// through a mask (the missing ones covered 0, and neither read nor written).
// `padded`: the coverage readable eight bytes past its last, so the last few
// are read in one load (what lies past them is masked out).
template <bool padded>
[[gnu::target("avx2")]] void over_coverage_avx2(Rgba8* out, std::int32_t count, Rgba8 source, const std::uint8_t* coverage) noexcept {
    const auto bias = _mm256_set1_epi16(128);
    // The source's four channels as 16-bit lanes, a pixel's worth repeated.
    const auto source16 = _mm256_unpacklo_epi8(_mm256_set1_epi32(std::bit_cast<std::int32_t>(source)), _mm256_setzero_si256());
    std::int32_t index = 0;
    for (; index + 8 <= count; index += 8) {
        std::uint64_t eight = 0;
        std::memcpy(&eight, coverage + index, 8);
        auto* place = reinterpret_cast<__m256i*>(out + index);
        _mm256_storeu_si256(place, over_eight_covered(_mm256_loadu_si256(place), eight, source16, bias));
    }
    if (index == count) return;
    const std::int32_t left = count - index;
    std::uint64_t eight = 0;
    if constexpr (padded) {
        std::memcpy(&eight, coverage + index, 8);
    } else {
        for (std::int32_t at = 0; at < left; ++at) eight |= static_cast<std::uint64_t>(coverage[index + at]) << (8 * at);
    }
    auto* place = reinterpret_cast<int*>(out + index);
    const auto mask = first_of_eight(left);
    _mm256_maskstore_epi32(place, mask, over_eight_covered(_mm256_maskload_epi32(place, mask), eight, source16, bias));
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
    if (count > 0 && wide()) over_run_avx2(out, count, source);
    else over_run_reference(out, count, source);
}

void over_coverage_reference(Rgba8* out, std::int32_t count, Rgba8 source, const std::uint8_t* coverage) noexcept {
    for (std::int32_t index = 0; index < count; ++index) out[index] = over_scalar(out[index], scaled_scalar(source, coverage[index]));
}

void over_coverage(Rgba8* out, std::int32_t count, Rgba8 source, const std::uint8_t* coverage) noexcept {
    if (count > 0 && wide()) over_coverage_avx2<false>(out, count, source, coverage);
    else over_coverage_reference(out, count, source, coverage);
}

void over_coverage_padded(Rgba8* out, std::int32_t count, Rgba8 source, const std::uint8_t* coverage) noexcept {
    if (count > 0 && wide()) over_coverage_avx2<true>(out, count, source, coverage);
    else over_coverage_reference(out, count, source, coverage);
}

std::string_view blend_path() noexcept { return wide() ? "avx2" : "scalar"; }

}  // namespace mrender::raster
