module mgame.net;

import std;

namespace mgame::net {

namespace {

template <class T>
void put(std::vector<std::byte>& out, T v) {
    using U = std::make_unsigned_t<T>;
    auto u = static_cast<U>(v);
    for (std::size_t i = 0; i < sizeof(T); ++i) out.push_back(static_cast<std::byte>((u >> (8 * i)) & 0xFF));
}

template <class T>
bool get(std::span<const std::byte>& in, T& v) {
    if (in.size() < sizeof(T)) return false;
    using U = std::make_unsigned_t<T>;
    U u = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) u |= static_cast<U>(std::to_integer<U>(in[i]) << (8 * i));
    v = static_cast<T>(u);
    in = in.subspan(sizeof(T));
    return true;
}

constexpr std::uint32_t magic = 0x4E474D31;  // "1MGN": a message of this format

}  // namespace

std::vector<std::byte> encode(const Message& m) {
    std::vector<std::byte> out;
    out.reserve(36 + m.inputs.size());
    put(out, magic);
    put(out, m.first);
    put(out, m.count);
    put(out, static_cast<std::uint32_t>(m.inputs.size()));
    out.insert(out.end(), m.inputs.begin(), m.inputs.end());
    put(out, m.ack);
    put(out, m.frame);
    put(out, m.checksum_frame);
    put(out, m.checksum);
    return out;
}

std::optional<Message> decode(std::span<const std::byte> in) {
    Message m;
    std::uint32_t tag = 0, size = 0;
    if (!get(in, tag) || tag != magic) return std::nullopt;
    if (!get(in, m.first) || !get(in, m.count) || !get(in, size) || in.size() < size) return std::nullopt;
    if (m.count < 0 || (m.count > 0 && size % static_cast<std::uint32_t>(m.count) != 0)) return std::nullopt;
    m.inputs.assign(in.begin(), in.begin() + size);
    in = in.subspan(size);
    if (!get(in, m.ack) || !get(in, m.frame) || !get(in, m.checksum_frame) || !get(in, m.checksum) || !in.empty()) return std::nullopt;
    return m;
}

}  // namespace mgame::net
