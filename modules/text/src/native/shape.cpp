// Shaping: bidi levels (fribidi), grapheme clusters and line-break
// opportunities (openmui.unicode), script runs, font fallback per cluster,
// and HarfBuzz shaping of each run with the whole text as context.
module openmui.text;

import std;
import openmui.unicode;
import :shaped;
import mcpplibs.mwrap.harfbuzz;
import mcpplibs.mwrap.fribidi;
using namespace mcpplibs::mwrap::harfbuzz::macros;
using namespace mcpplibs::mwrap::fribidi::macros;

namespace omui::typeset {

namespace {

hb_script_t script_of(char32_t cp) { return hb_unicode_script(hb_unicode_funcs_get_default(), cp); }

bool weak_script(hb_script_t s) {
    return s == HB_SCRIPT_COMMON || s == HB_SCRIPT_INHERITED || s == HB_SCRIPT_UNKNOWN;
}

bool is_space(char32_t cp) {
    return cp == ' ' || cp == '\t' || cp == 0x00A0 || cp == 0x3000 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x205F;
}

bool is_newline(char32_t cp) { return cp == '\n' || cp == '\r' || cp == 0x2028 || cp == 0x2029 || cp == 0x85; }

std::size_t style_at(const Paragraph::Shaped& s, std::size_t byte) {
    for (std::size_t i = 0; i < s.ranges.size(); ++i)
        if (byte >= s.ranges[i].begin && byte < s.ranges[i].end) return i + 1;
    return 0;
}

void shape_run(Paragraph::Shaped& s, ShapedRun& run, hb_script_t script, hb_buffer_t* buf, std::string_view source) {
    const TextStyle& st = s.styles[run.style];
    hb_buffer_clear_contents(buf);
    hb_buffer_add_utf8(buf, source.data(), static_cast<int>(source.size()), static_cast<unsigned>(run.begin),
                       static_cast<int>(run.end - run.begin));
    hb_buffer_set_direction(buf, (run.level & 1) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, weak_script(script) ? HB_SCRIPT_LATIN : script);
    if (!st.language.empty()) hb_buffer_set_language(buf, hb_language_from_string(st.language.c_str(), -1));
    hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
    std::vector<hb_feature_t> features;
    for (const Feature& f : st.features)
        features.push_back({HB_TAG(f.tag[0], f.tag[1], f.tag[2], f.tag[3]), f.value, 0, static_cast<unsigned>(-1)});
    auto* font = static_cast<hb_font_t*>(s.fonts->hb_font(run.font));
    hb_shape(font, buf, features.data(), static_cast<unsigned>(features.size()));
    unsigned n = 0;
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
    float k = st.size / static_cast<float>(s.fonts->face(s.fonts->face_of(run.font)).units_per_em);
    run.glyphs.resize(n);
    for (unsigned i = 0; i < n; ++i)
        run.glyphs[i] = {info[i].codepoint, info[i].cluster, static_cast<float>(pos[i].x_advance) * k,
                         static_cast<float>(pos[i].x_offset) * k, -static_cast<float>(pos[i].y_offset) * k};
    run.metrics = s.fonts->metrics(run.font, st.size);
}

}  // namespace

void shape(Paragraph::Shaped& s) {
    const std::string& text = s.text;

    // ── bidi levels per byte ──
    std::vector<std::size_t> cp_offsets;
    std::vector<FriBidiChar> cps;
    for (std::size_t i = 0; i < text.size();) {
        auto d = unicode::decode(text, i);
        cp_offsets.push_back(i);
        cps.push_back(d.cp);
        i += d.length;
    }
    std::vector<std::uint8_t> level_at(text.size(), 0);
    FriBidiParType dir = s.paragraph.direction == TextDirection::rtl   ? FRIBIDI_PAR_RTL
                         : s.paragraph.direction == TextDirection::ltr ? FRIBIDI_PAR_LTR
                                                                       : FRIBIDI_PAR_ON;
    if (!cps.empty()) {
        auto n = static_cast<FriBidiStrIndex>(cps.size());
        std::vector<FriBidiCharType> types(cps.size());
        std::vector<FriBidiBracketType> brackets(cps.size());
        std::vector<FriBidiLevel> levels(cps.size());
        fribidi_get_bidi_types(cps.data(), n, types.data());
        fribidi_get_bracket_types(cps.data(), n, types.data(), brackets.data());
        fribidi_get_par_embedding_levels_ex(types.data(), brackets.data(), n, &dir, levels.data());
        for (std::size_t i = 0; i < cps.size(); ++i) {
            std::size_t end = i + 1 < cps.size() ? cp_offsets[i + 1] : text.size();
            for (std::size_t b = cp_offsets[i]; b < end; ++b) level_at[b] = static_cast<std::uint8_t>(levels[i]);
        }
    }
    s.base_level = (s.paragraph.direction == TextDirection::rtl || (dir == FRIBIDI_PAR_RTL && !cps.empty())) ? 1 : 0;

    // ── graphemes with their face, style, level and script ──
    auto bounds = unicode::grapheme_boundaries(text);
    struct G {
        std::size_t  begin, end, style;
        FaceId       face;
        std::uint8_t level;
        hb_script_t  script;
        bool         space, newline;
    };
    std::vector<G> gs;
    for (std::size_t i = 0; i + 1 < bounds.size(); ++i) {
        std::size_t b = bounds[i], e = bounds[i + 1];
        std::u32string cluster = unicode::to_utf32(std::string_view(text).substr(b, e - b));
        std::size_t style = style_at(s, b);
        char32_t first = cluster.empty() ? U' ' : cluster.front();
        FaceId face = s.fonts->select(s.styles[style].families, cluster);
        gs.push_back({b, e, style, face, level_at[b], script_of(first), is_space(first), is_newline(first)});
    }
    // Weak scripts (punctuation, digits, marks) join their neighbours' script.
    hb_script_t prev = HB_SCRIPT_COMMON;
    for (G& g : gs) {
        if (weak_script(g.script)) g.script = prev;
        else prev = g.script;
    }
    prev = HB_SCRIPT_COMMON;
    for (auto it = gs.rbegin(); it != gs.rend(); ++it) {
        if (!weak_script(it->script)) prev = it->script;
        else it->script = prev;
    }
    // A space or newline takes its neighbours' face so it does not split a run.
    for (std::size_t i = 0; i < gs.size(); ++i)
        if ((gs[i].space || gs[i].newline) && i > 0 && gs[i - 1].style == gs[i].style) gs[i].face = gs[i - 1].face;

    // ── runs ──
    hb_buffer_t* buf = hb_buffer_create();
    s.graphemes.clear();
    s.runs.clear();
    std::vector<hb_script_t> run_scripts;
    for (std::size_t i = 0; i < gs.size();) {
        std::size_t j = i + 1;
        while (j < gs.size() && gs[j].style == gs[i].style && gs[j].face == gs[i].face && gs[j].level == gs[i].level &&
               gs[j].script == gs[i].script)
            ++j;
        ShapedRun run;
        run.begin = gs[i].begin;
        run.end = gs[j - 1].end;
        run.level = gs[i].level;
        run.style = gs[i].style;
        const TextStyle& st = s.styles[run.style];
        run.font = s.fonts->font(gs[i].face, st.weight, st.slant);
        shape_run(s, run, gs[i].script, buf, text);
        s.runs.push_back(std::move(run));
        run_scripts.push_back(gs[i].script);
        i = j;
    }

    // ── grapheme advances: each cluster's glyphs shared by the graphemes it covers ──
    s.graphemes.resize(gs.size());
    {
        // Word segments (UAX #29) of the whole text: each grapheme's index of its segment.
        const std::vector<std::size_t> words = unicode::word_boundaries(text);
        std::uint32_t w = 0;
        std::size_t next = 1;
        for (std::size_t i = 0; i < gs.size(); ++i) {
            while (next < words.size() && words[next] <= gs[i].begin) {
                ++next;
                ++w;
            }
            s.graphemes[i] = {gs[i].begin, gs[i].end, w, 0, 0, gs[i].space, gs[i].newline};
        }
    }
    auto grapheme_index = [&](std::size_t byte) {
        auto it = std::ranges::upper_bound(s.graphemes, byte, {}, &Grapheme::begin);
        return static_cast<std::size_t>(it - s.graphemes.begin()) - 1;
    };
    for (std::size_t r = 0; r < s.runs.size(); ++r) {
        const ShapedRun& run = s.runs[r];
        std::map<std::uint32_t, float> cluster_advance;
        for (const ShapedGlyph& g : run.glyphs) cluster_advance[g.cluster] += g.advance;
        std::vector<std::uint32_t> starts;
        for (auto& [c, a] : cluster_advance) starts.push_back(c);
        for (std::size_t k = 0; k < starts.size(); ++k) {
            std::size_t cb = starts[k];
            std::size_t ce = k + 1 < starts.size() ? starts[k + 1] : run.end;
            std::size_t g0 = grapheme_index(cb), g1 = grapheme_index(ce - 1);
            float share = cluster_advance[starts[k]] / static_cast<float>(g1 - g0 + 1);
            for (std::size_t g = g0; g <= g1; ++g) s.graphemes[g].advance += share;
        }
        std::size_t first = grapheme_index(run.begin), last = grapheme_index(run.end - 1);
        for (std::size_t g = first; g <= last; ++g) {
            s.graphemes[g].run = r;
            if (!s.graphemes[g].newline) s.graphemes[g].advance += s.styles[run.style].letter_spacing;
        }
    }
    for (Grapheme& g : s.graphemes)
        if (g.newline) g.advance = 0;

    // ── break opportunities before each grapheme ──
    s.break_before.assign(gs.size(), 0);
    for (const unicode::LineBreak& b : unicode::line_breaks(text)) {
        if (b.offset >= text.size()) continue;
        auto it = std::ranges::lower_bound(s.graphemes, b.offset, {}, &Grapheme::begin);
        if (it != s.graphemes.end() && it->begin == b.offset)
            s.break_before[static_cast<std::size_t>(it - s.graphemes.begin())] = b.mandatory ? 2 : 1;
    }

    // ── the ellipsis, shaped once in the paragraph's font ──
    {
        static constexpr std::string_view ell = "…";
        const TextStyle& st = s.styles[0];
        FaceId face = s.fonts->select(st.families, U"…");
        s.ellipsis = {};
        s.ellipsis.begin = 0;
        s.ellipsis.end = ell.size();
        s.ellipsis.level = s.base_level;
        s.ellipsis.style = 0;
        s.ellipsis.font = s.fonts->font(face, st.weight, st.slant);
        shape_run(s, s.ellipsis, HB_SCRIPT_COMMON, buf, ell);
        s.ellipsis_width = 0;
        for (const ShapedGlyph& g : s.ellipsis.glyphs) s.ellipsis_width += g.advance;
    }
    hb_buffer_destroy(buf);
}

}  // namespace omui::typeset
