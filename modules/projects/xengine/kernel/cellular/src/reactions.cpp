// Encoding 3 (D-167): a table looked up by a cell's key, and reactions — every one that holds acts. Checked and
// written into the program's words here, and run a cell at a time.
//
// The words, after the exchanges:
//   key, rows, columns, the table (rows × columns)
//   the near neighbourhood (its size, then offsets as x + 32, y + 32)
//   the near slots: how many, then each as what is summed (0 a channel, 1 a column) and which
//   the derived values: how many, then each one's column
//   the adjustments: how many, then each as end, derived, raise, conditions, amount
//   the reactions:   how many, then each as end, flag, become, conditions, how many changes, each as channel, kind, amount
//   the flags: how many, then the channels
//   the resets: how many, then each as channel, initial, from
// where conditions are how many, then each as compare, amount, amount; an amount is how many terms, then each as
// read, index, multiply, shift, negative (a near read's index is its slot); and `none` is the largest number.
module xengine.cellular.api;

import std;

namespace xengine::cellular {
using namespace detail;

std::expected<void, Failure> Program::encode(const Reactions& reactions, std::uint32_t& halo) {
    const Table& table = reactions.table;
    if (!table.columns || table.values.size() % table.columns != 0) return std::unexpected(Failure{Errc::table});
    if (table.key >= channels_) return std::unexpected(Failure{Errc::channels});
    const std::uint32_t rows = static_cast<std::uint32_t>(table.values.size() / table.columns);
    if (const auto wrong = check_offsets(reactions.near, halo)) return std::unexpected(Failure{*wrong});
    // Summed from both ends alike: with each offset its opposite.
    for (const Offset offset : reactions.near)
        if ((offset.x == 0 && offset.y == 0) || std::ranges::find(reactions.near, Offset{-offset.x, -offset.y}) == reactions.near.end())
            return std::unexpected(Failure{Errc::offset});
    if (reactions.derived.size() > 16) return std::unexpected(Failure{Errc::limit});
    for (const std::uint32_t column : reactions.derived)
        if (column >= table.columns) return std::unexpected(Failure{Errc::table});

    Staged staged;
    staged.at = static_cast<std::uint32_t>(words_.size());
    staged.key = table.key;
    staged.rows = rows;
    staged.columns = table.columns;
    words_.insert(words_.end(), {table.key, rows, table.columns});
    staged.table = static_cast<std::uint32_t>(words_.size());
    words_.insert(words_.end(), table.values.begin(), table.values.end());
    staged.near = static_cast<std::uint32_t>(words_.size());
    put_offsets(words_, reactions.near);

    // What is near, that an amount reads: a slot each.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> slots;
    // An amount's words; `plain`: an adjustment's, which reads the cell alone and its row as written.
    const auto amount = [&](const Amount& terms, std::vector<std::uint32_t>& into, bool plain) -> std::optional<Errc> {
        if (terms.size() > 32) return Errc::limit;
        into.push_back(static_cast<std::uint32_t>(terms.size()));
        for (const Operand& term : terms) {
            if (term.read > Read::near_lookup || term.shift > 31) return Errc::reaction;
            std::uint32_t index = term.index;
            switch (term.read) {
                case Read::constant: break;
                case Read::channel:
                    if (index >= channels_) return Errc::channels;
                    break;
                case Read::lookup:
                    if (index >= table.columns) return Errc::table;
                    break;
                case Read::derived:
                    if (plain || index >= reactions.derived.size()) return Errc::reaction;
                    break;
                case Read::near_channel:
                case Read::near_lookup: {
                    if (plain || reactions.near.empty()) return Errc::reaction;
                    if (term.read == Read::near_channel ? index >= channels_ : index >= table.columns) return term.read == Read::near_channel ? Errc::channels : Errc::table;
                    const std::pair<std::uint32_t, std::uint32_t> what{term.read == Read::near_lookup ? 1u : 0u, index};
                    auto found = std::ranges::find(slots, what);
                    if (found == slots.end()) {
                        if (slots.size() == 16) return Errc::limit;
                        slots.push_back(what);
                        found = slots.end() - 1;
                    }
                    index = static_cast<std::uint32_t>(found - slots.begin());
                    break;
                }
            }
            into.insert(into.end(), {static_cast<std::uint32_t>(term.read), index, term.multiply, term.shift, term.negative ? 1u : 0u});
        }
        return std::nullopt;
    };
    const auto conditions = [&](const std::vector<Comparison>& when, std::vector<std::uint32_t>& into, bool plain) -> std::optional<Errc> {
        if (when.size() > 32) return Errc::limit;
        into.push_back(static_cast<std::uint32_t>(when.size()));
        for (const Comparison& comparison : when) {
            if (comparison.compare > Compare::not_equal) return Errc::reaction;
            into.push_back(static_cast<std::uint32_t>(comparison.compare));
            if (const auto wrong = amount(comparison.left, into, plain)) return wrong;
            if (const auto wrong = amount(comparison.right, into, plain)) return wrong;
        }
        return std::nullopt;
    };

    // The adjustments and the reactions, each written apart first: the slots they find come before them.
    std::vector<std::uint32_t> adjusted, reacted;
    adjusted.push_back(static_cast<std::uint32_t>(reactions.adjustments.size()));
    for (std::size_t index = 0; index < reactions.adjustments.size(); ++index) {
        const Adjustment& adjustment = reactions.adjustments[index];
        const auto fail = [&](Errc code) { return std::unexpected(Failure{code, {}, {.adjustment = index}}); };
        if (adjustment.derived >= reactions.derived.size()) return fail(Errc::reaction);
        const std::size_t start = adjusted.size();
        adjusted.insert(adjusted.end(), {0u, adjustment.derived, adjustment.raise ? 1u : 0u});
        if (const auto wrong = conditions(adjustment.when, adjusted, true)) return fail(*wrong);
        if (const auto wrong = amount(adjustment.amount, adjusted, true)) return fail(*wrong);
        adjusted[start] = static_cast<std::uint32_t>(adjusted.size());  // its end, from the adjustments' start: placed below
    }
    std::uint32_t flagged = 0, changed = 0;
    reacted.push_back(static_cast<std::uint32_t>(reactions.reactions.size()));
    for (std::size_t index = 0; index < reactions.reactions.size(); ++index) {
        const Reaction& reaction = reactions.reactions[index];
        const auto fail = [&](Errc code) { return std::unexpected(Failure{code, {}, {.reaction = index}}); };
        if (reaction.flag && (*reaction.flag >= channels_ || *reaction.flag == table.key)) return fail(Errc::channels);
        if (reaction.become && *reaction.become >= table.columns) return fail(Errc::table);
        const std::size_t start = reacted.size();
        reacted.insert(reacted.end(), {0u, reaction.flag.value_or(largest), reaction.become.value_or(largest)});
        if (const auto wrong = conditions(reaction.when, reacted, false)) return fail(*wrong);
        reacted.push_back(static_cast<std::uint32_t>(reaction.changes.size()));
        std::uint32_t here = 0;
        for (const Change& change : reaction.changes) {
            if (change.channel >= channels_) return fail(Errc::channels);
            // What a cell is changes by `become`; and a reaction changes a channel once.
            if (change.channel == table.key || change.kind > ChangeKind::set || (here >> change.channel & 1)) return fail(Errc::reaction);
            here |= 1u << change.channel;
            reacted.insert(reacted.end(), {change.channel, static_cast<std::uint32_t>(change.kind)});
            if (const auto wrong = amount(change.amount, reacted, false)) return fail(*wrong);
        }
        changed |= here;
        if (reaction.flag) flagged |= 1u << *reaction.flag;
        reacted[start] = static_cast<std::uint32_t>(reacted.size());
    }
    // A flag is 1 while a reaction that flags it holds: nothing adds to it, takes from it or sets it.
    if (flagged & changed) return std::unexpected(Failure{Errc::reaction});
    for (const Reset& reset : reactions.resets) {
        if (reset.channel >= channels_ || reset.channel == table.key) return std::unexpected(Failure{Errc::channels});
        if (reset.from && *reset.from >= table.columns) return std::unexpected(Failure{Errc::table});
    }

    staged.slots = static_cast<std::uint32_t>(words_.size());
    staged.slot_count = static_cast<std::uint32_t>(slots.size());
    words_.push_back(staged.slot_count);
    for (const auto& [kind, index] : slots) words_.insert(words_.end(), {kind, index});
    staged.derived = static_cast<std::uint32_t>(words_.size()) + 1;
    staged.derived_count = static_cast<std::uint32_t>(reactions.derived.size());
    words_.push_back(staged.derived_count);
    words_.insert(words_.end(), reactions.derived.begin(), reactions.derived.end());
    // The ends written above are from each list's own start: now from the words'.
    const auto place = [&](std::vector<std::uint32_t>& list) {
        const std::uint32_t base = static_cast<std::uint32_t>(words_.size());
        std::size_t at = 1;
        for (std::uint32_t count = list[0], index = 0; index < count; ++index) {
            const std::size_t end = list[at];
            list[at] += base;
            at = end;
        }
        words_.insert(words_.end(), list.begin(), list.end());
        return base;
    };
    staged.adjustments = place(adjusted);
    staged.reactions = place(reacted);
    staged.flags = static_cast<std::uint32_t>(words_.size());
    words_.push_back(static_cast<std::uint32_t>(std::popcount(flagged)));
    for (std::uint32_t channel = 0; channel < channels_; ++channel)
        if (flagged >> channel & 1) words_.push_back(channel);
    staged.resets = static_cast<std::uint32_t>(words_.size());
    words_.push_back(static_cast<std::uint32_t>(reactions.resets.size()));
    for (const Reset& reset : reactions.resets) words_.insert(words_.end(), {reset.channel, reset.initial, reset.from.value_or(largest)});
    staged_ = staged;
    // What a row rules out: a comparison whose amounts read only numbers and the row's own columns is the same
    // for every cell of the row — where it fails, the reaction is not judged for that row's cells.
    const std::uint32_t reaction_count = words_[staged.reactions], stride = (reaction_count + 63) / 64;
    possible_.assign(static_cast<std::size_t>(rows) * stride, ~std::uint64_t{0});
    const auto fixed = [&](std::size_t position) {  // an amount of numbers and the row's columns only
        for (std::uint32_t count = words_[position++], term = 0; term < count; ++term, position += 5)
            if (words_[position] != static_cast<std::uint32_t>(Read::constant) && words_[position] != static_cast<std::uint32_t>(Read::lookup)) return false;
        return true;
    };
    const auto amount_of = [&](std::size_t& position, const std::uint32_t* row) {
        std::uint32_t plus = 0, minus = 0;
        for (std::uint32_t count = words_[position++], term = 0; term < count; ++term, position += 5) {
            const std::uint32_t index = words_[position + 1];
            const std::uint32_t value = words_[position] == static_cast<std::uint32_t>(Read::constant) ? index : row[index];
            const std::uint32_t scaled = saturating_multiply(value, words_[position + 2]) >> words_[position + 3];
            if (words_[position + 4]) minus = saturating_add(minus, scaled);
            else plus = saturating_add(plus, scaled);
        }
        return plus - std::min(plus, minus);
    };
    const auto skip = [&](std::size_t& position) { position += 1 + 5 * static_cast<std::size_t>(words_[position]); };
    std::size_t position = staged.reactions + 1;
    for (std::uint32_t index = 0; index < reaction_count; ++index) {
        const std::size_t end = words_[position];
        for (std::uint32_t key = 0; key < rows; ++key) {
            const std::uint32_t* row = words_.data() + staged.table + static_cast<std::size_t>(key) * staged.columns;
            std::size_t at = position + 3;
            bool can = true;
            for (std::uint32_t count = words_[at++], comparison = 0; comparison < count && can; ++comparison) {
                const auto compare = static_cast<Compare>(words_[at++]);
                std::size_t right_at = at;
                skip(right_at);
                if (!fixed(at) || !fixed(right_at)) { at = right_at; skip(at); continue; }
                const std::uint32_t left = amount_of(at, row), right = amount_of(at, row);
                switch (compare) {
                    case Compare::greater_equal: can = left >= right; break;
                    case Compare::less_equal: can = left <= right; break;
                    case Compare::greater: can = left > right; break;
                    case Compare::less: can = left < right; break;
                    case Compare::equal: can = left == right; break;
                    case Compare::not_equal: can = left != right; break;
                }
            }
            if (!can) possible_[static_cast<std::size_t>(key) * stride + index / 64] &= ~(std::uint64_t{1} << (index % 64));
        }
        position = end;
    }
    return {};
}

std::uint32_t Program::react(Shape shape, std::uint32_t x, std::uint32_t y, std::span<const std::uint32_t> in, std::span<std::uint32_t> out, Boundary boundary,
                             Statistics& stats) const noexcept {
    const Staged& staged = staged_;
    const std::size_t cells = static_cast<std::size_t>(shape.width) * shape.height, cell = static_cast<std::size_t>(y) * shape.width + x;
    const std::uint32_t key = in[static_cast<std::size_t>(staged.key) * cells + cell];
    if (key >= staged.rows) return largest;  // no row: it takes no part
    const std::uint32_t* const table = words_.data() + staged.table;
    const std::uint32_t* const row = table + static_cast<std::size_t>(key) * staged.columns;
    // A channel of the cell at an offset: beyond an edge, as the boundary says.
    const auto read = [&](std::uint32_t channel, std::int32_t at_x, std::int32_t at_y) {
        const auto width = static_cast<std::int32_t>(shape.width), height = static_cast<std::int32_t>(shape.height);
        if (at_x < 0 || at_x >= width || at_y < 0 || at_y >= height) {
            if (boundary.edge == Edge::constant) return boundary.values.empty() ? boundary.value : boundary.values[channel];
            if (boundary.edge == Edge::clamp) {
                at_x = std::clamp(at_x, 0, width - 1);
                at_y = std::clamp(at_y, 0, height - 1);
            } else {
                at_x %= width;
                at_y %= height;
                if (at_x < 0) at_x += width;
                if (at_y < 0) at_y += height;
            }
        }
        return in[static_cast<std::size_t>(channel) * cells + static_cast<std::uint32_t>(at_y) * shape.width + static_cast<std::uint32_t>(at_x)];
    };
    // What is near, summed when first read: a neighbour that is no row reads as nothing.
    std::array<std::uint32_t, 16> near;  // read only where `known` says
    std::uint32_t known = 0;
    const auto near_of = [&](std::uint32_t slot) {
        if (known >> slot & 1) return near[slot];
        const std::uint32_t kind = words_[staged.slots + 1 + 2 * slot], index = words_[staged.slots + 2 + 2 * slot];
        std::uint32_t sum = 0;
        for (std::uint32_t count = words_[staged.near], offset = 0; offset < count; ++offset) {
            const std::int32_t at_x = static_cast<std::int32_t>(x) + static_cast<std::int32_t>(words_[staged.near + 1 + 2 * offset]) - 32;
            const std::int32_t at_y = static_cast<std::int32_t>(y) + static_cast<std::int32_t>(words_[staged.near + 2 + 2 * offset]) - 32;
            const std::uint32_t other = read(staged.key, at_x, at_y);
            if (other >= staged.rows) continue;
            sum = saturating_add(sum, kind ? table[static_cast<std::size_t>(other) * staged.columns + index] : read(index, at_x, at_y));
        }
        known |= 1u << slot;
        return near[slot] = sum;
    };
    std::array<std::uint32_t, 16> derived;  // the first derived_count
    for (std::uint32_t index = 0; index < staged.derived_count; ++index) derived[index] = row[words_[staged.derived + index]];
    // An amount at `position` (left after it): what is added less what is taken.
    const auto amount = [&](std::size_t& position) {
        std::uint32_t plus = 0, minus = 0;
        for (std::uint32_t count = words_[position++], term = 0; term < count; ++term, position += 5) {
            const std::uint32_t index = words_[position + 1];
            std::uint32_t value = 0;
            switch (static_cast<Read>(words_[position])) {
                case Read::constant: value = index; break;
                case Read::channel: value = in[static_cast<std::size_t>(index) * cells + cell]; break;
                case Read::lookup: value = row[index]; break;
                case Read::derived: value = derived[index]; break;
                case Read::near_channel:
                case Read::near_lookup: value = near_of(index); break;
            }
            const std::uint32_t scaled = saturating_multiply(value, words_[position + 2]) >> words_[position + 3];
            if (words_[position + 4]) minus = saturating_add(minus, scaled);
            else plus = saturating_add(plus, scaled);
        }
        return plus - std::min(plus, minus);
    };
    // The conditions at `position` (left after them, when they all hold).
    const auto holds = [&](std::size_t& position) {
        for (std::uint32_t count = words_[position++], index = 0; index < count; ++index) {
            const auto compare = static_cast<Compare>(words_[position++]);
            const std::uint32_t left = amount(position), right = amount(position);
            bool passed = false;
            switch (compare) {
                case Compare::greater_equal: passed = left >= right; break;
                case Compare::less_equal: passed = left <= right; break;
                case Compare::greater: passed = left > right; break;
                case Compare::less: passed = left < right; break;
                case Compare::equal: passed = left == right; break;
                case Compare::not_equal: passed = left != right; break;
            }
            if (!passed) return false;
        }
        return true;
    };
    // The derived values: in the adjustments' order, each judged on the row as written.
    std::size_t position = staged.adjustments;
    for (std::uint32_t count = words_[position++], index = 0; index < count; ++index) {
        const std::size_t end = words_[position];
        const std::uint32_t which = words_[position + 1], raise = words_[position + 2];
        std::size_t at = position + 3;
        if (holds(at)) {
            const std::uint32_t by = amount(at);
            derived[which] = raise ? saturating_add(derived[which], by) : derived[which] - std::min(derived[which], by);
        }
        position = end;
    }
    // The reactions that hold: what they add and take, the first set of each channel, the flags, the first becoming.
    // A channel's sums are made when a reaction first touches it (`touched`), and read only where it does.
    std::array<std::uint32_t, 32> plus, minus, base;
    std::uint32_t is_set = 0, touched = 0, becomes = largest;
    const auto touch = [&](std::uint32_t channel) {
        if (touched >> channel & 1) return;
        touched |= 1u << channel;
        plus[channel] = minus[channel] = base[channel] = 0;
    };
    for (std::uint32_t count = words_[staged.flags], index = 0; index < count; ++index) {
        const std::uint32_t channel = words_[staged.flags + 1 + index];
        touch(channel);
        is_set |= 1u << channel;
    }
    position = staged.reactions;
    const std::uint64_t* const possible = possible_.data() + static_cast<std::size_t>(key) * ((words_[position] + 63) / 64);
    for (std::uint32_t count = words_[position++], index = 0; index < count; ++index) {
        const std::size_t end = words_[position];
        const std::uint32_t flag = words_[position + 1], become = words_[position + 2];
        std::size_t at = position + 3;
        if ((possible[index / 64] >> (index % 64) & 1) && holds(at)) {
            ++stats.matched;
            for (std::uint32_t changes = words_[at++], change = 0; change < changes; ++change) {
                const std::uint32_t channel = words_[at], kind = words_[at + 1];
                at += 2;
                const std::uint32_t by = amount(at);
                touch(channel);
                if (kind == static_cast<std::uint32_t>(ChangeKind::add)) plus[channel] = saturating_add(plus[channel], by);
                else if (kind == static_cast<std::uint32_t>(ChangeKind::take)) minus[channel] = saturating_add(minus[channel], by);
                else if (!(is_set >> channel & 1)) base[channel] = by, is_set |= 1u << channel;
            }
            if (flag != largest) base[flag] = 1;
            if (become != largest && becomes == largest && row[become] < staged.rows && row[become] != key) becomes = row[become];
        }
        position = end;
    }
    for (std::uint32_t channel = 0; channel < channels_; ++channel) {
        if (!(touched >> channel & 1)) continue;
        std::uint32_t& value = out[static_cast<std::size_t>(channel) * cells + cell];
        if (is_set >> channel & 1) value = base[channel];
        value = saturating_add(value, plus[channel]);
        value -= std::min(value, minus[channel]);
    }
    if (becomes != largest) out[static_cast<std::size_t>(staged.key) * cells + cell] = becomes;
    return becomes;
}

}  // namespace xengine::cellular
