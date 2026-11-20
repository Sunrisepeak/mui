// Shaping, the part that is the same whatever shapes the glyphs: bidi levels (SheenBidi), then the runs and their
// glyphs from the shaper (native/shape.cpp: fonts and HarfBuzz), then each grapheme's word segment (UAX #29) and
// the line-break opportunities before it (UAX #14), from mlibs.unicode.
module mlibs.text.api;

import std;
import mlibs.unicode;
import :shaped;
import mcpplibs.mwrap.sheenbidi;

namespace mlibs::text {

// SheenBidi's constants by name, inside this namespace: where a compiler also sees the C header's enumerators
// (NDK clang, through the wrap's global module fragment), these are the ones found, not an ambiguity.
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypeB;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypeBN;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypeFSI;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypeLRE;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypeLRI;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypeLRO;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypePDF;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypePDI;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypeRLE;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypeRLI;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypeRLO;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypeS;
using mcpplibs::mwrap::sheenbidi::macros::SBBidiTypeWS;
using mcpplibs::mwrap::sheenbidi::macros::SBStringEncodingUTF8;
using mcpplibs::mwrap::sheenbidi::macros::SBLevelDefaultLTR;


namespace {

// UBA L1, the paragraph's part: segment and paragraph separators, and the
// whitespace and isolate formatters before them or at the paragraph's end,
// take the paragraph's level.
void reset_separators(std::string_view text, std::size_t begin, std::size_t end, SBLevel base, std::vector<std::uint8_t>& level_at) {
    struct Cp {
        std::size_t at, length;
        SBBidiType  type;
    };
    std::vector<Cp> cps;
    for (std::size_t i = begin; i < end;) {
        auto d = unicode::decode(text, i);
        cps.push_back({i, d.length, SBCodepointGetBidiType(d.cp)});
        i += d.length;
    }
    auto set = [&](const Cp& c) {
        for (std::size_t b = c.at; b < c.at + c.length; ++b) level_at[b] = base;
    };
    auto trailing = [](SBBidiType t) {
        return t == SBBidiTypeWS || t == SBBidiTypeLRI || t == SBBidiTypeRLI || t == SBBidiTypeFSI || t == SBBidiTypePDI ||
               t == SBBidiTypeBN || t == SBBidiTypeLRE || t == SBBidiTypeRLE || t == SBBidiTypeLRO || t == SBBidiTypeRLO ||
               t == SBBidiTypePDF;
    };
    bool resetting = true;  // the paragraph's end
    for (std::size_t k = cps.size(); k-- > 0;) {
        const SBBidiType t = cps[k].type;
        if (t == SBBidiTypeS || t == SBBidiTypeB) {
            set(cps[k]);
            resetting = true;
        } else if (resetting && trailing(t)) {
            set(cps[k]);
        } else {
            resetting = false;
        }
    }
}


}  // namespace

// SheenBidi's own allocator keeps scratch buffers on a lock-free stack that
// is not safe when paragraphs are shaped on many threads at once (a canvas's
// textures laid out on the pool): two threads came away with one buffer and
// resolved each other's levels. Its default, from the first paragraph on, is
// plain blocks — the stack is never touched.
void plain_bidi_allocator() {
    static std::once_flag once;
    std::call_once(once, [] {
        static const SBAllocatorProtocol plain{
            [](SBUInteger size, void*) { return std::malloc(size); },
            [](void* p, SBUInteger size, void*) { return std::realloc(p, size); },
            [](void* p, void*) { std::free(p); },
            nullptr,
            nullptr,
            nullptr,
        };
        SBAllocatorSetDefault(SBAllocatorCreate(&plain, nullptr));
    });
}

void shape(Paragraph::Shaped& s) {
    plain_bidi_allocator();
    const std::string& text = s.text;

    // ── bidi levels per byte ──
    // SheenBidi reads the UTF-8 itself and resolves each paragraph (UBA P1:
    // a paragraph separator ends one) to levels per code unit — per byte
    // here. Rule L1's paragraph part is applied below; its line part
    // (trailing whitespace) is the layout's, per line.
    std::vector<std::uint8_t> level_at(text.size(), 0);
    const SBLevel wanted = s.paragraph.direction == TextDirection::rtl   ? SBLevel{1}
                           : s.paragraph.direction == TextDirection::ltr ? SBLevel{0}
                                                                         : SBLevelDefaultLTR;
    SBLevel first_base = 0;
    if (!text.empty()) {
        const SBCodepointSequence seq{SBStringEncodingUTF8, const_cast<char*>(text.data()), text.size()};
        SBAlgorithmRef algorithm = SBAlgorithmCreate(&seq);
        for (SBUInteger offset = 0; offset < text.size();) {
            SBParagraphRef par = SBAlgorithmCreateParagraph(algorithm, offset, text.size() - offset, wanted);
            if (!par) break;
            const SBUInteger length = SBParagraphGetLength(par);
            const SBLevel base = SBParagraphGetBaseLevel(par);
            if (offset == 0) first_base = base;
            const SBLevel* levels = SBParagraphGetLevelsPtr(par);
            for (SBUInteger i = 0; i < length; ++i) level_at[offset + i] = levels[i];
            reset_separators(text, offset, offset + length, base, level_at);
            SBParagraphRelease(par);
            if (length == 0) break;
            offset += length;
        }
        SBAlgorithmRelease(algorithm);
    }
    s.base_level = (s.paragraph.direction == TextDirection::rtl || (s.paragraph.direction != TextDirection::ltr && first_base == 1)) ? 1 : 0;

    shape_runs(s, level_at);

    // ── each grapheme's word segment (UAX #29) of the whole text ──
    {
        const std::vector<std::size_t> words = unicode::word_boundaries(text);
        std::uint32_t w = 0;
        std::size_t next = 1;
        for (Grapheme& g : s.graphemes) {
            while (next < words.size() && words[next] <= g.begin) {
                ++next;
                ++w;
            }
            g.word = w;
        }
    }

    // ── break opportunities before each grapheme ──
    s.break_before.assign(s.graphemes.size(), 0);
    for (const unicode::LineBreak& b : unicode::line_breaks(text)) {
        if (b.offset >= text.size()) continue;
        auto it = std::ranges::lower_bound(s.graphemes, b.offset, {}, &Grapheme::begin);
        if (it != s.graphemes.end() && it->begin == b.offset)
            s.break_before[static_cast<std::size_t>(it - s.graphemes.begin())] = b.mandatory ? 2 : 1;
    }
}

}  // namespace mlibs::text
