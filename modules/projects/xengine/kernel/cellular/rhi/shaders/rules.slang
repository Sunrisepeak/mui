// The device's run of a prepared cellular program — encodings 1, 2 and 3 — not authored per scenario: every
// stage reads the words xengine.cellular's Program::prepare wrote, with the CPU reference's arithmetic (32-bit
// unsigned, every step saturating), so a step here is the CPU's step bit for bit (CONTRACT.md).
//
//   evolve    a thread a cell: the rules (counted, summed, least, greatest; writes with terms), the reactions on
//             the table, the transfers, the resets — all of the immutable input
//   exchange  a thread a 2 × 2 block of this tick's cut: cells change places in the output, decided from the
//             input (blocks are disjoint, so the output is changed in place)
[[vk::binding(0, 0)]] StructuredBuffer<uint> before : register(t0, space0);
[[vk::binding(1, 0)]] StructuredBuffer<uint> rules : register(t1, space0);
[[vk::binding(0, 1)]] RWStructuredBuffer<uint> after : register(u0, space1);
// Where the program's parts are in its words (found by the host walking them once), the step's boundary, and its
// tick as the exchange takes it (the key of its choices, the cut).
struct Params {
    uint width; uint height; uint channels; uint edge;
    uint outside; uint baths; uint tick_key; uint cut;
    uint encoding; uint stages; uint exchange; uint tabled;
    uint key; uint rows; uint columns; uint table;
    uint near; uint slots; uint derived; uint derived_count;
    uint adjustments; uint reactions; uint flags; uint resets;
    uint4 bath[8];  // with `baths`: the outside's value a channel (32 at most)
};
[[vk::binding(0, 2)]] ConstantBuffer<Params> params : register(b0, space2);

static const uint largest = 0xffffffffu;
static const uint of_table = 0x80000000u;

uint saturating_add(uint a, uint b) { return a + min(b, largest - a); }
uint saturating_multiply(uint a, uint by) { return (by != 0 && a > largest / by) ? largest : a * by; }
uint finalize32(uint z) {
    z = (z ^ (z >> 16)) * 0x7feb352du;
    z = (z ^ (z >> 15)) * 0x846ca68bu;
    return z ^ (z >> 16);
}
uint mix32(uint x) { return finalize32(x + 0x9e3779b9u); }

uint outside_value(uint channel) {
    if (params.baths != 0) return params.bath[channel >> 2][channel & 3];
    return params.outside;
}
uint sample_field(uint channel, int x, int y) {
    int w = int(params.width), h = int(params.height);
    if (x < 0 || x >= w || y < 0 || y >= h) {
        if (params.edge == 0) return outside_value(channel);
        if (params.edge == 1) { x = clamp(x, 0, w - 1); y = clamp(y, 0, h - 1); }
        else { x = x % w; y = y % h; if (x < 0) x += w; if (y < 0) y += h; }
    }
    return before[channel * params.width * params.height + uint(y) * params.width + uint(x)];
}
// An aggregate of a channel over the neighbourhood at `position` (its size, then its offsets): how many values are
// within [low, high] (1), or their sum (2), the least (3) or the greatest (4) of those that are.
uint aggregate(uint kind, uint channel, uint low, uint high, uint position, uint x, uint y) {
    uint count = rules[position], found = 0, sum = 0, least = largest, greatest = 0;
    for (uint o = 0; o < count; ++o) {
        uint value = sample_field(channel, int(x) + int(rules[position + 1 + 2 * o]) - 32, int(y) + int(rules[position + 2 + 2 * o]) - 32);
        if (value < low || value > high) continue;
        ++found; sum = saturating_add(sum, value); least = min(least, value); greatest = max(greatest, value);
    }
    return kind == 1 ? found : kind == 2 ? sum : kind == 3 ? least : greatest;
}
// A table's row by a key: its first word, or the largest number where the key is no row.
uint row_at(uint key) { return key < params.rows ? params.table + key * params.columns : largest; }

// ── encoding 3: one cell's reactions ─────────────────────────────────────
struct Cell {
    uint x; uint y; uint cell; uint key; uint row;
    uint near[16]; uint known;
    uint derived[16];
};
uint near_of(inout Cell c, uint slot) {
    if (((c.known >> slot) & 1) != 0) return c.near[slot];
    uint kind = rules[params.slots + 1 + 2 * slot], index = rules[params.slots + 2 + 2 * slot];
    uint sum = 0, count = rules[params.near];
    for (uint offset = 0; offset < count; ++offset) {
        int at_x = int(c.x) + int(rules[params.near + 1 + 2 * offset]) - 32, at_y = int(c.y) + int(rules[params.near + 2 + 2 * offset]) - 32;
        uint other = sample_field(params.key, at_x, at_y);
        if (other >= params.rows) continue;
        sum = saturating_add(sum, kind != 0 ? rules[params.table + other * params.columns + index] : sample_field(index, at_x, at_y));
    }
    c.known |= 1u << slot;
    c.near[slot] = sum;
    return sum;
}
// An amount at `position` (left after it): what is added less what is taken.
uint amount(inout Cell c, inout uint position) {
    uint plus = 0, minus = 0, count = rules[position++];
    for (uint term = 0; term < count; ++term) {
        uint read = rules[position], index = rules[position + 1], value = 0;
        if (read == 0) value = index;
        else if (read == 1) value = before[index * params.width * params.height + c.cell];
        else if (read == 2) value = rules[c.row + index];
        else if (read == 3) value = c.derived[index];
        else value = near_of(c, index);
        uint scaled = saturating_multiply(value, rules[position + 2]) >> rules[position + 3];
        if (rules[position + 4] != 0) minus = saturating_add(minus, scaled); else plus = saturating_add(plus, scaled);
        position += 5;
    }
    return plus - min(plus, minus);
}
bool holds(inout Cell c, inout uint position) {
    uint count = rules[position++];
    for (uint index = 0; index < count; ++index) {
        uint compare = rules[position++];
        uint left = amount(c, position);
        uint right = amount(c, position);
        bool passed = compare == 0 ? left >= right : compare == 1 ? left <= right : compare == 2 ? left > right
                    : compare == 3 ? left < right : compare == 4 ? left == right : left != right;
        if (!passed) return false;
    }
    return true;
}
// Onto what the rules made of the cell: the row it becomes, or the largest number.
uint react(uint x, uint y, uint cell) {
    uint cells = params.width * params.height;
    Cell c;
    c.x = x; c.y = y; c.cell = cell; c.known = 0;
    c.key = before[params.key * cells + cell];
    if (c.key >= params.rows) return largest;
    c.row = params.table + c.key * params.columns;
    for (uint index = 0; index < 16; ++index) { c.near[index] = 0; c.derived[index] = 0; }
    for (uint index = 0; index < params.derived_count; ++index) c.derived[index] = rules[c.row + rules[params.derived + index]];
    uint position = params.adjustments, count = rules[position++];
    for (uint index = 0; index < count; ++index) {
        uint end = rules[position], which = rules[position + 1], raise = rules[position + 2], at = position + 3;
        if (holds(c, at)) {
            uint by = amount(c, at);
            c.derived[which] = raise != 0 ? saturating_add(c.derived[which], by) : c.derived[which] - min(c.derived[which], by);
        }
        position = end;
    }
    uint plus[32], minus[32], base[32];
    for (uint channel = 0; channel < 32; ++channel) { plus[channel] = 0; minus[channel] = 0; base[channel] = 0; }
    uint is_set = 0, touched = 0, becomes = largest;
    uint flags = rules[params.flags];
    for (uint index = 0; index < flags; ++index) { uint channel = rules[params.flags + 1 + index]; is_set |= 1u << channel; touched |= 1u << channel; }
    position = params.reactions; count = rules[position++];
    for (uint index = 0; index < count; ++index) {
        uint end = rules[position], flag = rules[position + 1], become = rules[position + 2], at = position + 3;
        if (holds(c, at)) {
            uint changes = rules[at++];
            for (uint change = 0; change < changes; ++change) {
                uint channel = rules[at], kind = rules[at + 1];
                at += 2;
                uint by = amount(c, at);
                touched |= 1u << channel;
                if (kind == 0) plus[channel] = saturating_add(plus[channel], by);
                else if (kind == 1) minus[channel] = saturating_add(minus[channel], by);
                else if (((is_set >> channel) & 1) == 0) { base[channel] = by; is_set |= 1u << channel; }
            }
            if (flag != largest) base[flag] = 1;
            uint next = become != largest ? rules[c.row + become] : largest;
            if (become != largest && becomes == largest && next < params.rows && next != c.key) becomes = next;
        }
        position = end;
    }
    for (uint channel = 0; channel < params.channels; ++channel) {
        if (((touched >> channel) & 1) == 0) continue;
        uint index = channel * cells + cell, value = after[index];
        if (((is_set >> channel) & 1) != 0) value = base[channel];
        value = saturating_add(value, plus[channel]);
        value -= min(value, minus[channel]);
        after[index] = value;
    }
    if (becomes != largest) after[params.key * cells + cell] = becomes;
    return becomes;
}
// How well the cell at (x, y) conducts by a transfer's `by` word: by a channel, by its row's column, or 1 — and,
// with a table, nothing where it is no row.
uint conductance_at(uint by, uint by_limit, int x, int y) {
    if (params.tabled == 0) return by == largest ? 1u : min(sample_field(by, x, y), by_limit);
    uint row = row_at(sample_field(params.key, x, y));
    if (row == largest) return 0;
    if (by == largest) return 1u;
    return min((by & of_table) != 0 ? rules[row + (by & ~of_table)] : sample_field(by, x, y), by_limit);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void evolve(uint3 id : SV_DispatchThreadID) {
    if (id.x >= params.width || id.y >= params.height) return;
    uint cells = params.width * params.height, cell = id.y * params.width + id.x;
    bool with_terms = params.encoding >= 2;
    for (uint channel = 0; channel < params.channels; ++channel) after[channel * cells + cell] = before[channel * cells + cell];
    for (uint r = 0; r < rules[2]; ++r) {
        uint start = rules[4 + r], pos = start + 4;
        bool matches = true;
        for (uint c = 0; c < rules[start + 1]; ++c) {
            uint kind = rules[pos], channel = rules[pos + 1], low = rules[pos + 2], high = rules[pos + 3], count = rules[pos + 6];
            if (kind == 0) { uint value = before[channel * cells + cell]; matches = value >= low && value <= high; }
            else { uint found = aggregate(kind, channel, low, high, pos + 6, id.x, id.y); matches = found >= rules[pos + 4] && found <= rules[pos + 5]; }
            if (!matches) break;
            pos += 7 + 2 * count;
        }
        if (!matches) continue;
        pos = rules[start + 3];
        for (uint w = 0; w < rules[start + 2]; ++w) {
            uint index = rules[pos] * cells + cell, old = before[index], kind = rules[pos + 1];
            uint plus = rules[pos + 2], minus = 0;
            pos += 3;
            if (with_terms) {
                uint terms = rules[pos++];
                for (uint t = 0; t < terms; ++t) {
                    uint source = rules[pos], channel = rules[pos + 1];
                    uint value = source == 0 ? before[channel * cells + cell] : aggregate(source, channel, rules[pos + 2], rules[pos + 3], pos + 7, id.x, id.y);
                    uint scaled = saturating_multiply(value, rules[pos + 4]) >> rules[pos + 5];
                    if (rules[pos + 6] != 0) minus = saturating_add(minus, scaled); else plus = saturating_add(plus, scaled);
                    pos += 8 + 2 * rules[pos + 7];
                }
            }
            uint next = plus - min(plus, minus);
            if (kind == 1) { next = saturating_add(old, plus); next -= min(next, minus); }
            else if (kind == 2) { next = old - min(old, plus); next = saturating_add(next, minus); }
            after[index] = next;
        }
        break;
    }
    if (!with_terms) return;
    uint becomes = params.tabled != 0 ? react(id.x, id.y, cell) : largest;
    // What flows: for each neighbour, the same amount both ends compute from the same two values.
    uint pos = params.stages, transfers = rules[pos++];
    for (uint t = 0; t < transfers; ++t) {
        uint channel = rules[pos], by = rules[pos + 1], by_limit = rules[pos + 2], multiply = rules[pos + 3], shift = rules[pos + 4], count = rules[pos + 5];
        uint index = channel * cells + cell, mine = before[index];
        uint conducts = conductance_at(by, by_limit, int(id.x), int(id.y));
        uint gained = 0, lost = 0;
        for (uint o = 0; o < count; ++o) {
            int nx = int(id.x) + int(rules[pos + 6 + 2 * o]) - 32, ny = int(id.y) + int(rules[pos + 7 + 2 * o]) - 32;
            uint theirs = sample_field(channel, nx, ny);
            if (theirs == mine) continue;
            uint conductance = min(conducts, conductance_at(by, by_limit, nx, ny));
            uint difference = theirs > mine ? theirs - mine : mine - theirs;
            uint flow = saturating_multiply(saturating_multiply(difference, conductance), multiply) >> shift;
            if (theirs > mine) gained = saturating_add(gained, flow); else lost = saturating_add(lost, flow);
        }
        uint made = after[index];
        after[index] = saturating_add(made - min(made, lost), gained);
        pos += 6 + 2 * count;
    }
    if (becomes != largest) {
        // It became another row: the channels that start again do, at the new row's own start where it has one.
        uint row = params.table + becomes * params.columns, at = params.resets, count = rules[at++];
        for (uint index = 0; index < count; ++index) {
            uint own = rules[at + 2] == largest ? 0u : rules[row + rules[at + 2]];
            after[rules[at] * cells + cell] = own != 0 ? own : rules[at + 1];
            at += 3;
        }
    }
}

// ── the exchange: cells change places within blocks of 2 × 2 ─────────────
static const uint falls = 1, slides = 2, spreads = 4;

[shader("compute")]
[numthreads(8, 8, 1)]
void exchange(uint3 id : SV_DispatchThreadID) {
    uint cut_x = params.cut & 1, cut_y = (params.cut >> 1) & 1;
    uint left = cut_x + 2 * id.x, top = cut_y + 2 * id.y;
    if (left >= params.width || top >= params.height) return;
    bool round_x = params.edge == 2 && (params.width & 1) == 0, round_y = params.edge == 2 && (params.height & 1) == 0;
    bool over_x = left + 1 >= params.width, over_y = top + 1 >= params.height;
    if ((over_x && !round_x) || (over_y && !round_y)) return;
    uint right = over_x ? 0 : left + 1, bottom = over_y ? 0 : top + 1;
    uint cells = params.width * params.height;
    uint place[4];
    place[0] = top * params.width + left; place[1] = top * params.width + right;
    place[2] = bottom * params.width + left; place[3] = bottom * params.width + right;
    uint weight = rules[params.exchange], mobility = rules[params.exchange + 1], carried = rules[params.exchange + 3];
    uint heavy[4], moves[4];
    for (uint corner = 0; corner < 4; ++corner) {
        if ((weight & of_table) != 0) {
            uint row = row_at(before[params.key * cells + place[corner]]);
            heavy[corner] = row != largest ? rules[row + (weight & ~of_table)] : 0;
            moves[corner] = row != largest ? rules[row + (mobility & ~of_table)] : 0;
        } else {
            heavy[corner] = before[weight * cells + place[corner]];
            moves[corner] = before[mobility * cells + place[corner]];
        }
    }
    uint at[4]; bool moved[4];
    for (uint corner = 0; corner < 4; ++corner) { at[corner] = corner; moved[corner] = false; }
    // 1 a cell that falls sinks below a lighter one that falls.
    for (uint column = 0; column < 2; ++column) {
        uint above = at[column], below = at[column + 2];
        if ((moves[above] & falls) != 0 && (moves[below] & falls) != 0 && heavy[above] > heavy[below]) {
            moved[above] = true; moved[below] = true; at[column] = below; at[column + 2] = above;
        }
    }
    // 2 a cell that slides, resting, goes diagonally down past a lighter neighbour.
    for (uint from = 0; from < 2; ++from) {
        uint beside = 1 - from, diagonal = 3 - from;
        uint cell = at[from], other = at[beside], under = at[diagonal];
        if (moved[cell] || moved[other] || moved[under]) continue;
        if ((moves[cell] & slides) != 0 && (moves[under] & falls) != 0 && heavy[cell] > heavy[under] && (moves[other] & falls) != 0 && heavy[other] < heavy[cell]) {
            moved[cell] = true; moved[under] = true; at[from] = under; at[diagonal] = cell;
            break;
        }
    }
    // 3 a cell that spreads changes places with a lighter one beside it, or not, with equal chance.
    for (uint row = 0; row < 2; ++row) {
        uint one = at[row * 2], other = at[row * 2 + 1];
        if (moved[one] || moved[other] || heavy[one] == heavy[other]) continue;
        uint heavier = heavy[one] > heavy[other] ? one : other, lighter = heavier == one ? other : one;
        if ((moves[heavier] & spreads) == 0 || (moves[lighter] & falls) == 0) continue;
        uint chance = mix32(params.tick_key ^ (left * 0x9e3779b1u) ^ mix32(top * 2 + row));
        if ((chance & 1) != 0) { moved[one] = true; moved[other] = true; at[row * 2] = other; at[row * 2 + 1] = one; }
    }
    if (at[0] == 0 && at[1] == 1 && at[2] == 2 && at[3] == 3) return;
    for (uint index = 0; index < carried; ++index) {
        uint base = rules[params.exchange + 4 + index] * cells;
        uint made[4];
        for (uint corner = 0; corner < 4; ++corner) made[corner] = after[base + place[corner]];
        for (uint corner = 0; corner < 4; ++corner) after[base + place[corner]] = made[at[corner]];
    }
}
