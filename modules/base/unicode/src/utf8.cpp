module openmui.unicode;

import std;

namespace omui::unicode {

Decoded decode(std::string_view text, std::size_t offset) noexcept {
    auto b = [&](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
    std::uint8_t c0 = b(offset);
    if (c0 < 0x80) return {c0, 1};
    std::size_t left = text.size() - offset;
    // Lengths and the valid range of the second byte (Unicode Table 3-7).
    auto cont = [&](std::size_t i) { return i < text.size() && (b(i) & 0xC0) == 0x80; };
    if (c0 >= 0xC2 && c0 <= 0xDF) {
        if (left >= 2 && cont(offset + 1)) return {static_cast<char32_t>(((c0 & 0x1F) << 6) | (b(offset + 1) & 0x3F)), 2};
        return {0xFFFD, 1};
    }
    if (c0 >= 0xE0 && c0 <= 0xEF) {
        std::uint8_t lo = c0 == 0xE0 ? 0xA0 : 0x80, hi = c0 == 0xED ? 0x9F : 0xBF;
        if (left < 2 || b(offset + 1) < lo || b(offset + 1) > hi) return {0xFFFD, 1};
        if (left < 3 || !cont(offset + 2)) return {0xFFFD, 2};
        return {static_cast<char32_t>(((c0 & 0x0F) << 12) | ((b(offset + 1) & 0x3F) << 6) | (b(offset + 2) & 0x3F)), 3};
    }
    if (c0 >= 0xF0 && c0 <= 0xF4) {
        std::uint8_t lo = c0 == 0xF0 ? 0x90 : 0x80, hi = c0 == 0xF4 ? 0x8F : 0xBF;
        if (left < 2 || b(offset + 1) < lo || b(offset + 1) > hi) return {0xFFFD, 1};
        if (left < 3 || !cont(offset + 2)) return {0xFFFD, 2};
        if (left < 4 || !cont(offset + 3)) return {0xFFFD, 3};
        return {static_cast<char32_t>(((c0 & 0x07) << 18) | ((b(offset + 1) & 0x3F) << 12) | ((b(offset + 2) & 0x3F) << 6) |
                                      (b(offset + 3) & 0x3F)),
                4};
    }
    return {0xFFFD, 1};
}

std::size_t previous(std::string_view text, std::size_t offset) noexcept {
    // Step back over continuation bytes, at most three, then check the
    // sequence really ends at `offset`.
    std::size_t start = offset;
    for (int i = 0; i < 4 && start > 0; ++i) {
        --start;
        if ((static_cast<std::uint8_t>(text[start]) & 0xC0) != 0x80) break;
    }
    if (start + decode(text, start).length == offset) return start;
    return offset - 1;
}

void append_utf8(std::string& out, char32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::u32string to_utf32(std::string_view text) {
    std::u32string out;
    for (std::size_t i = 0; i < text.size();) {
        auto d = decode(text, i);
        out += d.cp;
        i += d.length;
    }
    return out;
}

Lb line_class(char32_t cp) noexcept {
    return static_cast<Lb>(tables::lookup(tables::line_stage1, tables::line_stage2, cp) & 0x3F);
}
Gcb grapheme_class(char32_t cp) noexcept {
    return static_cast<Gcb>(tables::lookup(tables::grapheme_stage1, tables::grapheme_stage2, cp) & 0x0F);
}
Wb word_class(char32_t cp) noexcept {
    return static_cast<Wb>(tables::lookup(tables::word_stage1, tables::word_stage2, cp) & 0x1F);
}
Ea east_asian_width(char32_t cp) noexcept {
    return static_cast<Ea>(tables::lookup(tables::word_stage1, tables::word_stage2, cp) >> 5);
}
bool extended_pictographic(char32_t cp) noexcept {
    return (tables::lookup(tables::grapheme_stage1, tables::grapheme_stage2, cp) & 0x10) != 0;
}
bool unassigned(char32_t cp) noexcept {
    return (tables::lookup(tables::grapheme_stage1, tables::grapheme_stage2, cp) & 0x80) != 0;
}

}  // namespace omui::unicode
