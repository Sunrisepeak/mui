// UAX #14, the Unicode line breaking algorithm (Unicode 17), rule by rule:
// each boundary is decided by the first rule that applies, in the standard's
// order. LB9 (combining marks take the class of what they attach to) is
// applied by giving every code point an effective class and the index of its
// base; lookbehind rules (LB8, LB14–LB17, LB20a, LB21a, LB25, LB28a, LB30a)
// walk back over effective classes.
module openmui.unicode;

import std;

namespace omui::unicode {

namespace {

constexpr char32_t dotted_circle = 0x25CC;

bool east_asian(char32_t cp) {
    Ea e = east_asian_width(cp);
    return e == Ea::F || e == Ea::W || e == Ea::H;
}

struct Item {
    std::size_t offset;
    char32_t    cp;
    Lb          raw;   // after LB1
    Lb          eff;   // after LB9/LB10
    std::size_t base;  // index whose class this one takes (LB9)
    bool        pi, pf;
};

Lb resolve(Lb c) {
    // LB1 (SA was resolved by the generator).
    switch (c) {
        case Lb::AI:
        case Lb::SG:
        case Lb::XX: return Lb::AL;
        case Lb::CJ: return Lb::NS;
        default: return c;
    }
}

bool hard(Lb c) { return c == Lb::BK || c == Lb::CR || c == Lb::LF || c == Lb::NL; }

}  // namespace

std::vector<LineBreak> line_breaks(std::string_view text) {
    std::vector<LineBreak> out;
    if (text.empty()) return out;
    std::vector<Item> it;
    for (std::size_t i = 0; i < text.size();) {
        auto d = decode(text, i);
        std::uint8_t raw = tables::lookup(tables::line_stage1, tables::line_stage2, d.cp);
        Item x{i, d.cp, resolve(static_cast<Lb>(raw & 0x3F)), Lb::AL, it.size(), (raw & 0x40) != 0, (raw & 0x80) != 0};
        it.push_back(x);
        i += d.length;
    }
    std::size_t n = it.size();
    // LB9, LB10: X (CM | ZWJ)* is X, unless X is BK CR LF NL SP ZW.
    for (std::size_t i = 0; i < n; ++i) {
        Lb c = it[i].raw;
        if ((c == Lb::CM || c == Lb::ZWJ) && i > 0) {
            Lb x = it[i - 1].eff;
            std::size_t b = it[i - 1].base;
            if (!hard(it[b].raw) && it[b].raw != Lb::SP && it[b].raw != Lb::ZW) {
                it[i].eff = x;
                it[i].base = b;
                continue;
            }
        }
        it[i].eff = (c == Lb::CM || c == Lb::ZWJ) ? Lb::AL : c;
        it[i].base = i;
    }
    constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
    // The base index of the effective character before the base of i.
    auto before = [&](std::size_t i) -> std::size_t {
        std::size_t b = it[i].base;
        return b == 0 ? none : it[b - 1].base;
    };
    auto cls = [&](std::size_t i) { return i == none ? Lb::XX : it[i].eff; };
    // The last effective character before boundary i that is not SP.
    auto before_spaces = [&](std::size_t i) -> std::size_t {
        std::size_t k = i == 0 ? none : it[i - 1].base;
        while (k != none && it[k].eff == Lb::SP) k = k == 0 ? none : it[k - 1].base;
        return k;
    };
    // The next character after i that is not attached to i (start of the next base).
    auto next_base = [&](std::size_t i) -> std::size_t {
        for (std::size_t k = i + 1; k < n; ++k)
            if (it[k].base == k) return k;
        return none;
    };
    auto is_qu_pi = [&](std::size_t k) { return k != none && it[k].eff == Lb::QU && it[it[k].base].pi; };
    auto is_qu_pf = [&](std::size_t k) { return k != none && it[k].eff == Lb::QU && it[it[k].base].pf; };
    auto ak_like = [&](std::size_t k) {
        return k != none && (it[k].eff == Lb::AK || it[k].eff == Lb::AS || it[it[k].base].cp == dotted_circle);
    };

    for (std::size_t i = 1; i <= n; ++i) {
        if (i == n) {  // LB3
            out.push_back({text.size(), true});
            break;
        }
        const Item& p = it[i - 1];
        const Item& c = it[i];
        Lb pr = p.raw, cr = c.raw;
        // LB4, LB5
        if (pr == Lb::BK) { out.push_back({c.offset, true}); continue; }
        if (pr == Lb::CR && cr == Lb::LF) continue;
        if (pr == Lb::CR || pr == Lb::LF || pr == Lb::NL) { out.push_back({c.offset, true}); continue; }
        // LB6
        if (hard(cr)) continue;
        // LB7
        if (cr == Lb::SP || cr == Lb::ZW) continue;
        // LB8: ZW SP* ÷
        {
            std::size_t k = before_spaces(i);
            if (k != none && it[k].eff == Lb::ZW) { out.push_back({c.offset, false}); continue; }
        }
        // LB8a
        if (pr == Lb::ZWJ) continue;
        // LB9: a combining mark attached to its base.
        if (c.base != i) continue;
        Lb a = p.eff;  // the effective class before the boundary
        Lb b = c.eff;  // the class after it
        std::size_t ai = p.base;
        // LB11
        if (b == Lb::WJ || a == Lb::WJ) continue;
        // LB12, LB12a
        if (a == Lb::GL) continue;
        if (b == Lb::GL && a != Lb::SP && a != Lb::BA && a != Lb::HY && a != Lb::HH) continue;
        // LB13
        if (b == Lb::CL || b == Lb::CP || b == Lb::EX || b == Lb::SY) continue;
        // LB14: OP SP* ×
        std::size_t ns = before_spaces(i);
        if (ns != none && it[ns].eff == Lb::OP) continue;
        // LB15a: (sot | BK CR LF NL OP QU GL SP ZW) [Pi & QU] SP* ×
        if (is_qu_pi(ns)) {
            std::size_t q = before(ns);
            Lb qc = cls(q);
            if (q == none || hard(qc) || qc == Lb::OP || qc == Lb::QU || qc == Lb::GL || qc == Lb::SP || qc == Lb::ZW) continue;
        }
        // LB15b: × [Pf & QU] (SP GL WJ CL QU CP EX IS SY BK CR LF NL ZW | eot)
        if (is_qu_pf(i)) {
            std::size_t nx = next_base(i);
            Lb nc = cls(nx);
            if (nx == none || nc == Lb::SP || nc == Lb::GL || nc == Lb::WJ || nc == Lb::CL || nc == Lb::QU ||
                nc == Lb::CP || nc == Lb::EX || nc == Lb::IS || nc == Lb::SY || hard(nc) || nc == Lb::ZW)
                continue;
        }
        // LB15c: SP ÷ IS NU
        if (a == Lb::SP && b == Lb::IS && cls(next_base(i)) == Lb::NU) { out.push_back({c.offset, false}); continue; }
        // LB15d
        if (b == Lb::IS) continue;
        // LB16: (CL | CP) SP* × NS
        if (b == Lb::NS && ns != none && (it[ns].eff == Lb::CL || it[ns].eff == Lb::CP)) continue;
        // LB17: B2 SP* × B2
        if (b == Lb::B2 && ns != none && it[ns].eff == Lb::B2) continue;
        // LB18
        if (a == Lb::SP) { out.push_back({c.offset, false}); continue; }
        // LB19: × [QU - Pi]; [QU - Pf] ×
        if (b == Lb::QU && !c.pi) continue;
        if (a == Lb::QU && !it[ai].pf) continue;
        // LB19a
        {
            bool a_ea = east_asian(it[ai].cp), b_ea = east_asian(c.cp);
            if (b == Lb::QU && !a_ea) continue;
            if (b == Lb::QU) {
                std::size_t nx = next_base(i);
                if (nx == none || !east_asian(it[nx].cp)) continue;
            }
            if (a == Lb::QU && !b_ea) continue;
            if (a == Lb::QU) {
                std::size_t q = before(ai);
                if (q == none || !east_asian(it[q].cp)) continue;
            }
        }
        // LB20
        if (b == Lb::CB || a == Lb::CB) { out.push_back({c.offset, false}); continue; }
        // LB20a: (sot | BK CR LF NL SP ZW CB GL) (HY | HH) × (AL | HL)
        if ((a == Lb::HY || a == Lb::HH) && (b == Lb::AL || b == Lb::HL)) {
            std::size_t q = before(ai);
            Lb qc = cls(q);
            if (q == none || hard(qc) || qc == Lb::SP || qc == Lb::ZW || qc == Lb::CB || qc == Lb::GL) continue;
        }
        // LB21
        if (b == Lb::BA || b == Lb::HH || b == Lb::HY || b == Lb::NS || a == Lb::BB) continue;
        // LB21a: HL (HY | HH) × [^HL]
        if ((a == Lb::HY || a == Lb::HH) && b != Lb::HL && cls(before(ai)) == Lb::HL) continue;
        // LB21b
        if (a == Lb::SY && b == Lb::HL) continue;
        // LB22
        if (b == Lb::IN) continue;
        // LB23
        if ((a == Lb::AL || a == Lb::HL) && b == Lb::NU) continue;
        if (a == Lb::NU && (b == Lb::AL || b == Lb::HL)) continue;
        // LB23a
        if (a == Lb::PR && (b == Lb::ID || b == Lb::EB || b == Lb::EM)) continue;
        if ((a == Lb::ID || a == Lb::EB || a == Lb::EM) && b == Lb::PO) continue;
        // LB24
        if ((a == Lb::PR || a == Lb::PO) && (b == Lb::AL || b == Lb::HL)) continue;
        if ((a == Lb::AL || a == Lb::HL) && (b == Lb::PR || b == Lb::PO)) continue;
        // LB25: numbers, (PR | PO)? (OP | HY)? IS? NU (NU | SY | IS)* (CL | CP)? (PR | PO)?
        {
            std::size_t nx = next_base(i);
            Lb n1 = cls(nx);
            Lb n2 = nx == none ? Lb::XX : cls(next_base(nx));
            // Is the boundary preceded by NU (NU | SY | IS)*, optionally (CL | CP)?
            auto after_number = [&](std::size_t k, bool allow_close) {
                if (k == none) return false;
                if (allow_close && (it[k].eff == Lb::CL || it[k].eff == Lb::CP)) k = before(k);
                while (k != none && (it[k].eff == Lb::SY || it[k].eff == Lb::IS)) k = before(k);
                return k != none && it[k].eff == Lb::NU;
            };
            bool num_after = b == Lb::NU || ((b == Lb::OP || b == Lb::HY) && (n1 == Lb::NU || (n1 == Lb::IS && n2 == Lb::NU))) ||
                             (b == Lb::IS && n1 == Lb::NU);
            if ((a == Lb::PR || a == Lb::PO) && num_after) continue;
            if ((a == Lb::OP || a == Lb::HY) && (b == Lb::NU || (b == Lb::IS && n1 == Lb::NU))) continue;
            if (a == Lb::IS && b == Lb::NU) continue;
            if (b == Lb::NU && after_number(ai, false)) continue;
            if ((b == Lb::NU || b == Lb::SY || b == Lb::IS || b == Lb::CL || b == Lb::CP) && after_number(ai, false)) continue;
            if ((b == Lb::PO || b == Lb::PR) && after_number(ai, true)) continue;
        }
        // LB26
        if (a == Lb::JL && (b == Lb::JL || b == Lb::JV || b == Lb::H2 || b == Lb::H3)) continue;
        if ((a == Lb::JV || a == Lb::H2) && (b == Lb::JV || b == Lb::JT)) continue;
        if ((a == Lb::JT || a == Lb::H3) && b == Lb::JT) continue;
        // LB27
        if ((a == Lb::JL || a == Lb::JV || a == Lb::JT || a == Lb::H2 || a == Lb::H3) && b == Lb::PO) continue;
        if (a == Lb::PR && (b == Lb::JL || b == Lb::JV || b == Lb::JT || b == Lb::H2 || b == Lb::H3)) continue;
        // LB28
        if ((a == Lb::AL || a == Lb::HL) && (b == Lb::AL || b == Lb::HL)) continue;
        // LB28a: Brahmic orthographic syllables (Aksara)
        {
            bool b_ak = b == Lb::AK || b == Lb::AS || c.cp == dotted_circle;
            if (a == Lb::AP && b_ak) continue;
            if (ak_like(ai) && (b == Lb::VF || b == Lb::VI)) continue;
            if (a == Lb::VI && ak_like(before(ai)) && (b == Lb::AK || c.cp == dotted_circle)) continue;
            if (ak_like(ai) && b_ak && cls(next_base(i)) == Lb::VF) continue;
        }
        // LB29
        if (a == Lb::IS && (b == Lb::AL || b == Lb::HL)) continue;
        // LB30
        if ((a == Lb::AL || a == Lb::HL || a == Lb::NU) && b == Lb::OP && !east_asian(c.cp)) continue;
        if (a == Lb::CP && !east_asian(it[ai].cp) && (b == Lb::AL || b == Lb::HL || b == Lb::NU)) continue;
        // LB30a: RI pairs
        if (a == Lb::RI && b == Lb::RI) {
            int count = 0;
            for (std::size_t k = ai; k != none && it[k].eff == Lb::RI; k = before(k)) ++count;
            if (count % 2 == 1) continue;
        }
        // LB30b
        if (b == Lb::EM && (a == Lb::EB || (extended_pictographic(it[ai].cp) && unassigned(it[ai].cp)))) continue;
        // LB31
        out.push_back({c.offset, false});
    }
    return out;
}

}  // namespace omui::unicode
