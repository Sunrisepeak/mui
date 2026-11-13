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
// No local array is indexed by a value known only at run time (fxc, Direct3D's DXBC compiler, cannot keep one in
// a loop it does not unroll): near sums are made when read, a derived value when read (its column, then the
// adjustments that change it, in order — they read the cell and its row alone), and a reaction's changes are
// summed a channel at a time over the reactions that held (a 64-bit mask). The same arithmetic as the CPU's.
struct Cell { uint x; uint y; uint cell; uint key; uint row; };

uint near_sum(Cell c, uint slot) {
    uint kind = rules[params.slots + 1 + 2 * slot], index = rules[params.slots + 2 + 2 * slot];
    uint sum = 0, count = rules[params.near];
    for (uint offset = 0; offset < count; ++offset) {
        int at_x = int(c.x) + int(rules[params.near + 1 + 2 * offset]) - 32, at_y = int(c.y) + int(rules[params.near + 2 + 2 * offset]) - 32;
        uint other = sample_field(params.key, at_x, at_y);
        if (other >= params.rows) continue;
        sum = saturating_add(sum, kind != 0 ? rules[params.table + other * params.columns + index] : sample_field(index, at_x, at_y));
    }
    return sum;
}
uint skip_amount(uint position) { return position + 1 + 5 * rules[position]; }
// An amount of numbers, the cell's channels and its row's columns (an adjustment's): what is added less what is taken.
uint plain_amount(Cell c, inout uint position) {
    uint plus = 0, minus = 0, count = rules[position++];
    for (uint term = 0; term < count; ++term) {
        uint read = rules[position], index = rules[position + 1], value = 0;
        if (read == 0) value = index;
        else if (read == 1) value = before[index * params.width * params.height + c.cell];
        else value = rules[c.row + index];
        uint scaled = saturating_multiply(value, rules[position + 2]) >> rules[position + 3];
        if (rules[position + 4] != 0) minus = saturating_add(minus, scaled); else plus = saturating_add(plus, scaled);
        position += 5;
    }
    return plus - min(plus, minus);
}
bool compared(uint compare, uint left, uint right) {
    return compare == 0 ? left >= right : compare == 1 ? left <= right : compare == 2 ? left > right
         : compare == 3 ? left < right : compare == 4 ? left == right : left != right;
}
bool plain_holds(Cell c, inout uint position) {
    uint count = rules[position++];
    for (uint index = 0; index < count; ++index) {
        uint compare = rules[position++];
        uint left = plain_amount(c, position);
        uint right = plain_amount(c, position);
        if (!compared(compare, left, right)) return false;
    }
    return true;
}
// A derived value: its column of the row, then each adjustment of it whose conditions hold, in order.
uint derived_value(Cell c, uint which) {
    uint value = rules[c.row + rules[params.derived + which]];
    uint position = params.adjustments, count = rules[position++];
    for (uint index = 0; index < count; ++index) {
        uint end = rules[position], target = rules[position + 1], raise = rules[position + 2], at = position + 3;
        if (target == which && plain_holds(c, at)) {
            uint by = plain_amount(c, at);
            value = raise != 0 ? saturating_add(value, by) : value - min(value, by);
        }
        position = end;
    }
    return value;
}
// A reaction's amount: also derived values and near sums.
uint amount(Cell c, inout uint position) {
    uint plus = 0, minus = 0, count = rules[position++];
    for (uint term = 0; term < count; ++term) {
        uint read = rules[position], index = rules[position + 1], value = 0;
        if (read == 0) value = index;
        else if (read == 1) value = before[index * params.width * params.height + c.cell];
        else if (read == 2) value = rules[c.row + index];
        else if (read == 3) value = derived_value(c, index);
        else value = near_sum(c, index);
        uint scaled = saturating_multiply(value, rules[position + 2]) >> rules[position + 3];
        if (rules[position + 4] != 0) minus = saturating_add(minus, scaled); else plus = saturating_add(plus, scaled);
        position += 5;
    }
    return plus - min(plus, minus);
}
bool holds(Cell c, inout uint position) {
    uint count = rules[position++];
    for (uint index = 0; index < count; ++index) {
        uint compare = rules[position++];
        uint left = amount(c, position);
        uint right = amount(c, position);
        if (!compared(compare, left, right)) return false;
    }
    return true;
}
uint skip_conditions(uint position) {
    uint count = rules[position++];
    for (uint index = 0; index < count; ++index) position = skip_amount(skip_amount(position + 1));
    return position;
}
bool held(uint low, uint high, uint index) { return index < 32 ? ((low >> index) & 1) != 0 : ((high >> (index - 32)) & 1) != 0; }
// Onto what the rules made of the cell: the row it becomes, or the largest number.
uint react(uint x, uint y, uint cell) {
    uint cells = params.width * params.height;
    Cell c;
    c.x = x; c.y = y; c.cell = cell;
    c.key = before[params.key * cells + cell];
    if (c.key >= params.rows) return largest;
    c.row = params.table + c.key * params.columns;
    // Which reactions hold (at most 64, the host's check), and the first becoming.
    uint low = 0, high = 0, becomes = largest;
    uint position = params.reactions, count = rules[position++];
    for (uint index = 0; index < count; ++index) {
        uint end = rules[position], become = rules[position + 2], at = position + 3;
        if (holds(c, at)) {
            if (index < 32) low |= 1u << index; else high |= 1u << (index - 32);
            if (become != largest && becomes == largest) {
                uint next = rules[c.row + become];
                if (next < params.rows && next != c.key) becomes = next;
            }
        }
        position = end;
    }
    // A channel at a time: base (the first set; a flag's 1 while a reaction that flags it holds) + added − taken.
    uint flags = rules[params.flags];
    for (uint channel = 0; channel < params.channels; ++channel) {
        bool touched = false, is_set = false;
        uint plus = 0, minus = 0, base = 0;
        for (uint flag = 0; flag < flags; ++flag)
            if (rules[params.flags + 1 + flag] == channel) { touched = true; is_set = true; }
        position = params.reactions + 1;
        for (uint index = 0; index < count; ++index) {
            uint end = rules[position], flag = rules[position + 1];
            if (held(low, high, index)) {
                if (flag == channel) base = 1;
                uint at = skip_conditions(position + 3), changes = rules[at++];
                for (uint change = 0; change < changes; ++change) {
                    uint target = rules[at], kind = rules[at + 1];
                    at += 2;
                    if (target != channel) { at = skip_amount(at); continue; }
                    uint by = amount(c, at);
                    touched = true;
                    if (kind == 0) plus = saturating_add(plus, by);
                    else if (kind == 1) minus = saturating_add(minus, by);
                    else if (!is_set) { base = by; is_set = true; }
                }
            }
            position = end;
        }
        if (!touched) continue;
        uint index = channel * cells + cell, value = after[index];
        if (is_set) value = base;
        value = saturating_add(value, plus);
        value -= min(value, minus);
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

uint pick(uint v0, uint v1, uint v2, uint v3, uint which) { return which == 0 ? v0 : which == 1 ? v1 : which == 2 ? v2 : v3; }
uint corner_of(uint at, uint place) { return (at >> (2 * place)) & 3; }
uint swapped(uint at, uint one, uint other) {
    uint a = corner_of(at, one), b = corner_of(at, other);
    at &= ~((3u << (2 * one)) | (3u << (2 * other)));
    return at | (b << (2 * one)) | (a << (2 * other));
}
// How heavy the cell at `place` is and how it may move: its channels, or its row's columns.
void weigh(uint place, uint weight, uint mobility, uint cells, out uint heavy, out uint moves) {
    if ((weight & of_table) != 0) {
        uint row = row_at(before[params.key * cells + place]);
        heavy = row != largest ? rules[row + (weight & ~of_table)] : 0;
        moves = row != largest ? rules[row + (mobility & ~of_table)] : 0;
    } else {
        heavy = before[weight * cells + place];
        moves = before[mobility * cells + place];
    }
}

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
    // Four of each as scalars, picked by a corner (no local array indexed at run time: fxc); the corners are
    // above left, above right, below left, below right, and which cell is at each is 2 bits of `at`.
    uint p0 = top * params.width + left, p1 = top * params.width + right, p2 = bottom * params.width + left, p3 = bottom * params.width + right;
    uint weight = rules[params.exchange], mobility = rules[params.exchange + 1], carried = rules[params.exchange + 3];
    uint h0, h1, h2, h3, m0, m1, m2, m3;
    weigh(p0, weight, mobility, cells, h0, m0);
    weigh(p1, weight, mobility, cells, h1, m1);
    weigh(p2, weight, mobility, cells, h2, m2);
    weigh(p3, weight, mobility, cells, h3, m3);
    uint at = 0 | (1u << 2) | (2u << 4) | (3u << 6), moved = 0;  // moved: a bit a cell
    // 1 a cell that falls sinks below a lighter one that falls.
    for (uint column = 0; column < 2; ++column) {
        uint above = corner_of(at, column), below = corner_of(at, column + 2);
        if ((pick(m0, m1, m2, m3, above) & falls) != 0 && (pick(m0, m1, m2, m3, below) & falls) != 0 && pick(h0, h1, h2, h3, above) > pick(h0, h1, h2, h3, below)) {
            moved |= (1u << above) | (1u << below);
            at = swapped(at, column, column + 2);
        }
    }
    // 2 a cell that slides, resting, goes diagonally down past a lighter neighbour.
    for (uint from = 0; from < 2; ++from) {
        uint beside = 1 - from, diagonal = 3 - from;
        uint cell = corner_of(at, from), other = corner_of(at, beside), under = corner_of(at, diagonal);
        if ((moved & ((1u << cell) | (1u << other) | (1u << under))) != 0) continue;
        uint heavy = pick(h0, h1, h2, h3, cell);
        if ((pick(m0, m1, m2, m3, cell) & slides) != 0 && (pick(m0, m1, m2, m3, under) & falls) != 0 && heavy > pick(h0, h1, h2, h3, under) &&
            (pick(m0, m1, m2, m3, other) & falls) != 0 && pick(h0, h1, h2, h3, other) < heavy) {
            moved |= (1u << cell) | (1u << under);
            at = swapped(at, from, diagonal);
            break;
        }
    }
    // 3 a cell that spreads changes places with a lighter one beside it, or not, with equal chance.
    for (uint row = 0; row < 2; ++row) {
        uint one = corner_of(at, row * 2), other = corner_of(at, row * 2 + 1);
        uint heavy_one = pick(h0, h1, h2, h3, one), heavy_other = pick(h0, h1, h2, h3, other);
        if ((moved & ((1u << one) | (1u << other))) != 0 || heavy_one == heavy_other) continue;
        uint heavier = heavy_one > heavy_other ? one : other, lighter = heavier == one ? other : one;
        if ((pick(m0, m1, m2, m3, heavier) & spreads) == 0 || (pick(m0, m1, m2, m3, lighter) & falls) == 0) continue;
        uint chance = mix32(params.tick_key ^ (left * 0x9e3779b1u) ^ mix32(top * 2 + row));
        if ((chance & 1) != 0) { moved |= (1u << one) | (1u << other); at = swapped(at, row * 2, row * 2 + 1); }
    }
    if (at == (0 | (1u << 2) | (2u << 4) | (3u << 6))) return;
    for (uint index = 0; index < carried; ++index) {
        uint base = rules[params.exchange + 4 + index] * cells;
        uint v0 = after[base + p0], v1 = after[base + p1], v2 = after[base + p2], v3 = after[base + p3];
        after[base + p0] = pick(v0, v1, v2, v3, corner_of(at, 0));
        after[base + p1] = pick(v0, v1, v2, v3, corner_of(at, 1));
        after[base + p2] = pick(v0, v1, v2, v3, corner_of(at, 2));
        after[base + p3] = pick(v0, v1, v2, v3, corner_of(at, 3));
    }
}
