module mlibs.hash.api;

import std;

namespace mlibs::hash {

std::string Fnv1a::hex() const { return std::format("{:016x}", h_); }

namespace {

// The message padded as both digests pad it: a one bit, zeros, its length in bits as 64 big-endian
// bits, to a multiple of 64 bytes.
std::vector<std::uint8_t> padded(std::string_view bytes) {
    std::vector<std::uint8_t> out(bytes.begin(), bytes.end());
    const std::uint64_t bits = static_cast<std::uint64_t>(bytes.size()) * 8;
    out.push_back(0x80);
    while (out.size() % 64 != 56)
        out.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>(bits >> shift));
    return out;
}

std::uint32_t big_endian(const std::uint8_t* at) {
    return (std::uint32_t{at[0]} << 24) | (std::uint32_t{at[1]} << 16) |
           (std::uint32_t{at[2]} << 8) | std::uint32_t{at[3]};
}

template <std::size_t N>
std::array<std::uint8_t, N * 4> digest_of(const std::array<std::uint32_t, N>& state) {
    std::array<std::uint8_t, N * 4> out{};
    for (std::size_t word = 0; word < N; ++word)
        for (std::size_t byte = 0; byte < 4; ++byte)
            out[word * 4 + byte] = static_cast<std::uint8_t>(state[word] >> (24 - 8 * byte));
    return out;
}

constexpr std::array<std::uint32_t, 64>
    sha256_rounds{0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
                  0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
                  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
                  0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
                  0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
                  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
                  0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
                  0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
                  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

}  // namespace

std::array<std::uint8_t, 32> sha256(std::string_view bytes) {
    std::array<std::uint32_t, 8> state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const auto message = padded(bytes);
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t block = 0; block < message.size(); block += 64) {
        for (std::size_t word = 0; word < 16; ++word)
            schedule[word] = big_endian(message.data() + block + word * 4);
        for (std::size_t word = 16; word < 64; ++word) {
            const std::uint32_t low = std::rotr(schedule[word - 15], 7) ^
                                      std::rotr(schedule[word - 15], 18) ^
                                      (schedule[word - 15] >> 3);
            const std::uint32_t high = std::rotr(schedule[word - 2], 17) ^
                                       std::rotr(schedule[word - 2], 19) ^
                                       (schedule[word - 2] >> 10);
            schedule[word] = schedule[word - 16] + low + schedule[word - 7] + high;
        }
        auto [a, b, c, d, e, f, g, h] = state;
        for (std::size_t round = 0; round < 64; ++round) {
            const std::uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t first = h + sum1 + choice + sha256_rounds[round] + schedule[round];
            const std::uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + sum0 + majority;
        }
        const std::array<std::uint32_t, 8> added{a, b, c, d, e, f, g, h};
        for (std::size_t word = 0; word < 8; ++word)
            state[word] += added[word];
    }
    return digest_of(state);
}

std::array<std::uint8_t, 20> sha1(std::string_view bytes) {
    std::array<std::uint32_t, 5> state{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    const auto message = padded(bytes);
    std::array<std::uint32_t, 80> schedule{};
    for (std::size_t block = 0; block < message.size(); block += 64) {
        for (std::size_t word = 0; word < 16; ++word)
            schedule[word] = big_endian(message.data() + block + word * 4);
        for (std::size_t word = 16; word < 80; ++word)
            schedule[word] = std::rotl(schedule[word - 3] ^ schedule[word - 8] ^
                                           schedule[word - 14] ^ schedule[word - 16],
                                       1);
        auto [a, b, c, d, e] = state;
        for (std::size_t round = 0; round < 80; ++round) {
            std::uint32_t mixed = 0, constant = 0;
            if (round < 20)
                mixed = (b & c) | (~b & d), constant = 0x5a827999;
            else if (round < 40)
                mixed = b ^ c ^ d, constant = 0x6ed9eba1;
            else if (round < 60)
                mixed = (b & c) | (b & d) | (c & d), constant = 0x8f1bbcdc;
            else
                mixed = b ^ c ^ d, constant = 0xca62c1d6;
            const std::uint32_t next = std::rotl(a, 5) + mixed + e + constant + schedule[round];
            e = d;
            d = c;
            c = std::rotl(b, 30);
            b = a;
            a = next;
        }
        const std::array<std::uint32_t, 5> added{a, b, c, d, e};
        for (std::size_t word = 0; word < 5; ++word)
            state[word] += added[word];
    }
    return digest_of(state);
}

std::string sha256_hex(std::string_view bytes) {
    std::string out;
    for (const std::uint8_t byte : sha256(bytes))
        out += std::format("{:02x}", byte);
    return out;
}

}  // namespace mlibs::hash
