module mlibs.hash.api;

import std;

namespace mlibs::hash {

std::string Fnv1a::hex() const { return std::format("{:016x}", h_); }

namespace {

constexpr std::array<std::uint32_t, 64> rounds{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::uint32_t rotate(std::uint32_t value, int by) noexcept { return std::rotr(value, by); }

}  // namespace

Sha256::Sha256() noexcept : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::block(const std::uint8_t* chunk) noexcept {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t at = 0; at < 16; ++at)
        schedule[at] = std::uint32_t{chunk[at * 4]} << 24 | std::uint32_t{chunk[at * 4 + 1]} << 16 | std::uint32_t{chunk[at * 4 + 2]} << 8 | chunk[at * 4 + 3];
    for (std::size_t at = 16; at < 64; ++at) {
        const std::uint32_t early = rotate(schedule[at - 15], 7) ^ rotate(schedule[at - 15], 18) ^ (schedule[at - 15] >> 3);
        const std::uint32_t late = rotate(schedule[at - 2], 17) ^ rotate(schedule[at - 2], 19) ^ (schedule[at - 2] >> 10);
        schedule[at] = schedule[at - 16] + early + schedule[at - 7] + late;
    }
    auto [a, b, c, d, e, f, g, h] = state_;
    for (std::size_t at = 0; at < 64; ++at) {
        const std::uint32_t choose = (e & f) ^ (~e & g), majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t first = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + choose + rounds[at] + schedule[at];
        const std::uint32_t second = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + majority;
        h = g, g = f, f = e, e = d + first, d = c, c = b, b = a, a = first + second;
    }
    const std::array<std::uint32_t, 8> added{a, b, c, d, e, f, g, h};
    for (std::size_t at = 0; at < 8; ++at) state_[at] += added[at];
}

Sha256& Sha256::bytes(std::span<const std::uint8_t> data) noexcept {
    length_ += data.size();
    std::size_t at = 0;
    if (pending_size_ > 0) {
        const std::size_t taken = std::min(data.size(), pending_.size() - pending_size_);
        std::copy_n(data.begin(), taken, pending_.begin() + static_cast<std::ptrdiff_t>(pending_size_));
        pending_size_ += taken;
        at = taken;
        if (pending_size_ < pending_.size()) return *this;
        block(pending_.data());
        pending_size_ = 0;
    }
    for (; at + 64 <= data.size(); at += 64) block(data.data() + at);
    std::copy(data.begin() + static_cast<std::ptrdiff_t>(at), data.end(), pending_.begin());
    pending_size_ = data.size() - at;
    return *this;
}

Sha256& Sha256::text(std::string_view data) noexcept {
    return bytes(std::span(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

std::array<std::uint8_t, 32> Sha256::digest() noexcept {
    const std::uint64_t bits = length_ * 8;
    std::array<std::uint8_t, 72> tail{0x80};
    const std::size_t padding = (pending_size_ < 56 ? 56 : 120) - pending_size_;
    for (std::size_t at = 0; at < 8; ++at) tail[padding + at] = static_cast<std::uint8_t>(bits >> (56 - 8 * at));
    bytes(std::span(tail.data(), padding + 8));
    std::array<std::uint8_t, 32> out{};
    for (std::size_t at = 0; at < 8; ++at)
        for (std::size_t byte = 0; byte < 4; ++byte) out[at * 4 + byte] = static_cast<std::uint8_t>(state_[at] >> (24 - 8 * byte));
    return out;
}

std::string Sha256::hex() noexcept {
    std::string out;
    for (const auto byte : digest()) out += std::format("{:02x}", byte);
    return out;
}

std::string sha256_hex(std::string_view data) { return Sha256().text(data).hex(); }

}  // namespace mlibs::hash
