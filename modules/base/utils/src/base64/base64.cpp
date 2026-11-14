module mlibs.base64.api;

import std;

namespace mlibs::base64 {

namespace {
constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr int value_of(char symbol) noexcept {
    if (symbol >= 'A' && symbol <= 'Z') return symbol - 'A';
    if (symbol >= 'a' && symbol <= 'z') return symbol - 'a' + 26;
    if (symbol >= '0' && symbol <= '9') return symbol - '0' + 52;
    if (symbol == '+') return 62;
    if (symbol == '/') return 63;
    return -1;
}
}  // namespace

std::string encode(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t index = 0;
    for (; index + 2 < bytes.size(); index += 3) {
        const std::uint32_t group = (std::uint32_t{bytes[index]} << 16) | (std::uint32_t{bytes[index + 1]} << 8) | bytes[index + 2];
        for (const int shift : {18, 12, 6, 0}) out += alphabet[(group >> shift) & 63];
    }
    if (const std::size_t rest = bytes.size() - index; rest > 0) {
        std::uint32_t group = std::uint32_t{bytes[index]} << 16;
        if (rest == 2) group |= std::uint32_t{bytes[index + 1]} << 8;
        out += alphabet[(group >> 18) & 63];
        out += alphabet[(group >> 12) & 63];
        out += rest == 2 ? alphabet[(group >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::expected<std::vector<std::uint8_t>, std::string> decode(std::string_view text) {
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    std::uint32_t group = 0;
    int symbols = 0;
    bool padding = false;
    for (std::size_t offset = 0; offset < text.size(); ++offset) {
        const char symbol = text[offset];
        if (symbol == ' ' || symbol == '\n' || symbol == '\r' || symbol == '\t') continue;
        if (symbol == '=') {
            padding = true;
            continue;
        }
        const int value = value_of(symbol);
        if (value < 0 || padding) return std::unexpected(std::format("not base64 at {}: `{}`", offset, symbol));
        group = (group << 6) | static_cast<std::uint32_t>(value);
        if (++symbols == 4) {
            out.push_back(static_cast<std::uint8_t>(group >> 16));
            out.push_back(static_cast<std::uint8_t>(group >> 8));
            out.push_back(static_cast<std::uint8_t>(group));
            group = 0;
            symbols = 0;
        }
    }
    if (symbols == 1) return std::unexpected("base64 ends in the middle of a byte");
    if (symbols == 2) out.push_back(static_cast<std::uint8_t>(group >> 4));
    if (symbols == 3) {
        out.push_back(static_cast<std::uint8_t>(group >> 10));
        out.push_back(static_cast<std::uint8_t>(group >> 2));
    }
    return out;
}

}  // namespace mlibs::base64
