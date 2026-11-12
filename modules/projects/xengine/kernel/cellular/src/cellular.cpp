module xengine.cellular.api;
import std;
import mlibs.math;
import mlibs.trace;
import mlibs.log;

namespace xengine::cellular {
namespace detail {

void put_offsets(std::vector<std::uint32_t>& words, std::span<const Offset> offsets) {
    words.push_back(static_cast<std::uint32_t>(offsets.size()));
    for (const Offset offset : offsets) {
        words.push_back(static_cast<std::uint32_t>(offset.x + 32));
        words.push_back(static_cast<std::uint32_t>(offset.y + 32));
    }
}

std::optional<Errc> check_offsets(std::span<const Offset> offsets, std::uint32_t& halo) {
    if (offsets.size() > 128) return Errc::limit;
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        const Offset offset = offsets[index];
        if (offset.x < -32 || offset.x > 32 || offset.y < -32 || offset.y > 32) return Errc::offset;
        for (std::size_t earlier = 0; earlier < index; ++earlier)
            if (offsets[earlier] == offset) return Errc::offset;
        halo = std::max(halo, static_cast<std::uint32_t>(std::max(std::abs(offset.x), std::abs(offset.y))));
    }
    return std::nullopt;
}

}  // namespace detail
using namespace detail;
namespace {

// A tile's byte of Execution::activity: the low four bits are the block cuts it was seen at rest under (all
// four: asleep; a program that moves nothing shows all four at once); the others are a step's own marks, gone
// when it returns.
inline constexpr std::uint8_t rested = 0x0f, mark_sleeping = 0x10, mark_restless = 0x20, mark_altered = 0x40;
struct TileRun { std::uint32_t first = 0, count = 0; };
// The tiles along one axis that hold the cells `first` to `last` and `reach` cells either side — round a
// wrapped edge, where the count goes on past the last tile to the first.
[[nodiscard]] TileRun tiles_within(std::uint32_t first, std::uint32_t last, std::uint32_t reach, std::uint32_t size, std::uint32_t tile, bool wraps) noexcept {
    const std::uint32_t all = 1 + (size - 1) / tile;
    if (!wraps) {
        const std::uint32_t low = first - std::min(first, reach);
        const std::uint32_t high = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{last} + reach, size - 1));
        return {low / tile, high / tile - low / tile + 1};
    }
    if (std::uint64_t{last} - first + 1 + 2 * std::uint64_t{reach} >= size) return {0, all};
    const std::uint32_t low = (first + size - reach) % size, high = (last + reach) % size;
    if (low <= high) return {low / tile, high / tile - low / tile + 1};
    return {low / tile, std::min(all, all - low / tile + high / tile + 1)};
}
// Awake again: every tile within `reach` cells of the cells given.
void rouse(std::span<std::uint8_t> activity, Shape shape, Shape tile, std::uint32_t first_x, std::uint32_t last_x,
           std::uint32_t first_y, std::uint32_t last_y, std::uint32_t reach, bool wraps) noexcept {
    const std::uint32_t columns = 1 + (shape.width - 1) / tile.width, rows = 1 + (shape.height - 1) / tile.height;
    const TileRun across = tiles_within(first_x, last_x, reach, shape.width, tile.width, wraps);
    const TileRun down = tiles_within(first_y, last_y, reach, shape.height, tile.height, wraps);
    for (std::uint32_t row = 0; row < down.count; ++row)
        for (std::uint32_t column = 0; column < across.count; ++column)
            activity[static_cast<std::size_t>((down.first + row) % rows) * columns + (across.first + column) % columns] &= static_cast<std::uint8_t>(~rested);
}
}  // namespace

std::expected<Program,Failure> Program::prepare(std::uint32_t channels, std::span<const Rule> rules, Limits limits) {
    return prepare(channels, Stages{.rules = rules}, limits);
}

std::expected<Program,Failure> Program::prepare(std::uint32_t channels, Stages stages, Limits limits) {
    const std::span<const Rule> rules = stages.rules;
    const std::span<const Transfer> transfers = stages.transfers;
    const std::span<const Exchange> exchanges = stages.exchanges;
    mlibs::trace::Span span("simulation.cellular.prepare", "simulation");
    // Limits that cannot hold a program are the caller's error; a program beyond sound limits is refused.
    if (limits.max_words<4 || limits.max_values==0 || limits.max_values>std::numeric_limits<std::uint32_t>::max()/4 ||
        limits.max_words>std::numeric_limits<std::uint32_t>::max()/4) return std::unexpected(Failure{Errc::invalid_limits});
    if (!channels) return std::unexpected(Failure{Errc::channels});
    if (channels > limits.max_channels || channels > limits.max_values || rules.size()>limits.max_rules) return std::unexpected(Failure{Errc::limit});
    // Encoding 2 where a rule aggregates or a write has terms; otherwise the encoding every backend runs.
    bool aggregates = !transfers.empty() || !exchanges.empty() || stages.reactions;
    const Reactions* const reactions = stages.reactions;
    const std::uint32_t table_columns = reactions ? reactions->table.columns : 0;
    if (reactions && channels > 32) return std::unexpected(Failure{Errc::limit});
    for (const Rule& rule : rules) {
        for (const Condition& condition : rule.when) aggregates |= condition.kind > ConditionKind::neighbors;
        for (const Write& write : rule.writes) aggregates |= !write.terms.empty();
    }
    std::uint64_t size=4+static_cast<std::uint64_t>(rules.size())+(aggregates ? 3 : 0);
    std::uint32_t halo=0;
    for(std::size_t r=0;r<rules.size();++r) {
        const auto& rule=rules[r];
        if(rule.when.size()>limits.max_words || rule.writes.size()>limits.max_words) return std::unexpected(Failure{Errc::limit, {}, {r}});
        size+=4;
        for(std::size_t c=0;c<rule.when.size();++c) {
            const auto& test=rule.when[c];
            const auto fail=[&](Errc e){return std::unexpected(Failure{e, {}, {r, c}});};
            if(test.kind>ConditionKind::maximum || (test.kind==ConditionKind::self && !test.offsets.empty())) return fail(Errc::condition);
            if(test.test.channel>=channels) return fail(Errc::channels);
            if(test.test.value.low>test.test.value.high || test.count.low>test.count.high) return fail(Errc::range);
            if(const auto wrong=check_offsets(test.offsets,halo)) return fail(*wrong);
            size+=7+test.offsets.size()*2;
            if(size>limits.max_words) return fail(Errc::limit);
        }
        for(std::size_t w=0;w<rule.writes.size();++w) {
            const Write& write=rule.writes[w];
            const auto fail=[&](Errc e){return std::unexpected(Failure{e, {}, {r, w}});};
            if(write.channel>=channels) return fail(Errc::channels);
            if(write.kind>WriteKind::subtract_saturating) return fail(Errc::condition);
            for(std::size_t k=0;k<w;++k)if(rule.writes[k].channel==write.channel) return fail(Errc::duplicate_write);
            if(write.terms.size()>32) return fail(Errc::limit);
            size+=aggregates ? 4 : 3;
            for(const Term& term:write.terms) {
                if(term.kind>SourceKind::maximum || (term.kind==SourceKind::self && !term.offsets.empty()) || term.shift>31) return fail(Errc::condition);
                if(term.test.channel>=channels) return fail(Errc::channels);
                if(term.test.value.low>term.test.value.high) return fail(Errc::range);
                if(const auto wrong=check_offsets(term.offsets,halo)) return fail(*wrong);
                size+=8+term.offsets.size()*2;
            }
            if(size>limits.max_words) return fail(Errc::limit);
        }
        if(size>limits.max_words) return std::unexpected(Failure{Errc::limit, {}, {r}});
    }
    if (transfers.size() > 16) return std::unexpected(Failure{Errc::limit});
    for (std::size_t t = 0; t < transfers.size(); ++t) {
        const Transfer& transfer = transfers[t];
        const auto fail = [&](Errc e) { return std::unexpected(Failure{e, {}, {.transfer = t}}); };
        if (transfer.channel >= channels || (transfer.by && *transfer.by >= channels)) return fail(Errc::channels);
        // By a column of the table: with a table only, and not by a channel too; and what a cell is does not flow.
        if (transfer.by_column && (!reactions || *transfer.by_column >= table_columns || transfer.by)) return fail(Errc::table);
        if (reactions && transfer.channel == reactions->table.key) return fail(Errc::transfer);
        if (transfer.shift > 31 || transfer.offsets.empty()) return fail(Errc::transfer);
        if (const auto wrong = check_offsets(transfer.offsets, halo)) return fail(*wrong);
        // Seen from both ends: with each offset its opposite, and no cell its own neighbour.
        for (const Offset offset : transfer.offsets)
            if ((offset.x == 0 && offset.y == 0) || std::ranges::find(transfer.offsets, Offset{-offset.x, -offset.y}) == transfer.offsets.end())
                return fail(Errc::transfer);
        // No cell gives more than it has: the whole neighbourhood together takes at most all of a difference.
        const std::uint64_t most = static_cast<std::uint64_t>(transfer.by || transfer.by_column ? transfer.by_limit : 1) * transfer.multiply * transfer.offsets.size();
        if (most > (std::uint64_t{1} << transfer.shift)) return fail(Errc::transfer);
        for (std::size_t earlier = 0; earlier < t; ++earlier)
            if (transfers[earlier].channel == transfer.channel) return fail(Errc::transfer);  // one flow a quantity: two could together take too much
        size += 6 + transfer.offsets.size() * 2;
    }
    if (exchanges.size() > 1) return std::unexpected(Failure{Errc::limit, {}, {.exchange = std::size_t{1}}});
    for (std::size_t e = 0; e < exchanges.size(); ++e) {
        const Exchange& exchange = exchanges[e];
        const auto fail = [&](Errc code) { return std::unexpected(Failure{code, {}, {.exchange = e}}); };
        if (exchange.by_table) {
            if (!reactions || exchange.weight >= table_columns || exchange.mobility >= table_columns) return fail(Errc::table);
        } else if (exchange.weight >= channels || exchange.mobility >= channels) {
            return fail(Errc::channels);
        }
        for (std::size_t index = 0; index < exchange.carried.size(); ++index) {
            if (exchange.carried[index] >= channels) return fail(Errc::channels);
            for (std::size_t earlier = 0; earlier < index; ++earlier)
                if (exchange.carried[earlier] == exchange.carried[index]) return fail(Errc::exchange);
        }
        // What decides a move goes with the cell that moves, or the next step would judge a place by what left it.
        // (Looked up by its key, it is the key that goes with it.)
        const std::uint32_t decides = exchange.by_table ? reactions->table.key : exchange.weight, also = exchange.by_table ? reactions->table.key : exchange.mobility;
        if (!exchange.carried.empty() &&
            (std::ranges::find(exchange.carried, decides) == exchange.carried.end() || std::ranges::find(exchange.carried, also) == exchange.carried.end()))
            return fail(Errc::exchange);
        size += 4 + (exchange.carried.empty() ? channels : exchange.carried.size());
    }
    if(size>limits.max_words) return std::unexpected(Failure{Errc::limit});
    Program p;p.channels_=channels;p.rules_=static_cast<std::uint32_t>(rules.size());p.halo_=halo;p.limits_=limits;
    p.words_.reserve(static_cast<std::size_t>(size));p.transfers_=static_cast<std::uint32_t>(transfers.size());
    p.moves_=!exchanges.empty();
    p.words_={reactions ? 3u : aggregates ? 2u : 1u,channels,p.rules_,halo};p.words_.resize(4+rules.size()+(aggregates ? 1 : 0));
    for(std::size_t r=0;r<rules.size();++r) {
        const auto& rule=rules[r];const auto start=p.words_.size();p.words_[4+r]=static_cast<std::uint32_t>(start);
        p.words_.insert(p.words_.end(),{0,static_cast<std::uint32_t>(rule.when.size()),static_cast<std::uint32_t>(rule.writes.size()),0});
        for(const auto& c:rule.when) {
            p.words_.insert(p.words_.end(),{static_cast<std::uint32_t>(c.kind),c.test.channel,c.test.value.low,c.test.value.high,c.count.low,c.count.high});
            put_offsets(p.words_,c.offsets);
        }
        p.words_[start+3]=static_cast<std::uint32_t>(p.words_.size());
        for(const Write& w:rule.writes) {
            p.words_.insert(p.words_.end(),{w.channel,static_cast<std::uint32_t>(w.kind),w.value});
            if(!aggregates)continue;
            // Encoding 2: a write's terms follow it — source, channel, the values that take part, multiply, shift, sign, neighbourhood.
            p.words_.push_back(static_cast<std::uint32_t>(w.terms.size()));
            for(const Term& term:w.terms) {
                p.words_.insert(p.words_.end(),{static_cast<std::uint32_t>(term.kind),term.test.channel,term.test.value.low,term.test.value.high,
                    term.multiply,term.shift,term.negative ? 1u : 0u});
                put_offsets(p.words_,term.offsets);
            }
        }
        p.words_[start]=static_cast<std::uint32_t>(p.words_.size());
    }
    if (aggregates) {
        // Encoding 2: after the rules, the stages that follow them — the table's last entry says where. The transfers:
        // how many, then each as channel, the conductance's channel (the largest number: none) and its limit,
        // multiply, shift, neighbourhood.
        p.words_[4 + rules.size()] = static_cast<std::uint32_t>(p.words_.size());
        p.words_.push_back(p.transfers_);
        for (const Transfer& transfer : transfers) {
            p.words_.insert(p.words_.end(), {transfer.channel, transfer.by_column ? of_table | *transfer.by_column : transfer.by.value_or(largest),
                                             transfer.by || transfer.by_column ? transfer.by_limit : 1u, transfer.multiply, transfer.shift});
            put_offsets(p.words_, transfer.offsets);
        }
        // Then the exchanges: how many (one at most), then weight, mobility, seed and the channels that go with a cell.
        p.words_.push_back(static_cast<std::uint32_t>(exchanges.size()));
        for (const Exchange& exchange : exchanges) {
            p.words_.insert(p.words_.end(), {exchange.by_table ? of_table | exchange.weight : exchange.weight, exchange.by_table ? of_table | exchange.mobility : exchange.mobility,
                                             exchange.seed, static_cast<std::uint32_t>(exchange.carried.empty() ? channels : exchange.carried.size())});
            if (exchange.carried.empty())
                for (std::uint32_t channel = 0; channel < channels; ++channel) p.words_.push_back(channel);
            else
                p.words_.insert(p.words_.end(), exchange.carried.begin(), exchange.carried.end());
        }
        // Encoding 3: then the table and the reactions.
        if (reactions) {
            if (auto encoded = p.encode(*reactions, halo); !encoded) return std::unexpected(encoded.error());
            p.halo_ = halo;
            p.words_[3] = halo;
            if (p.words_.size() > limits.max_words) return std::unexpected(Failure{Errc::limit});
        }
    }
    mlibs::log::Log{"simulation"}.debug("cellular plan prepared: encoding {}, {} channels, {} rules, halo {}, {} words",p.words_[0],channels,p.rules_,halo,p.words_.size());
    return p;
}

Push push_on(Shape shape, std::span<const std::uint32_t> values, Tile region) noexcept {
    Push push;
    if (values.size() != static_cast<std::size_t>(shape.width) * shape.height || !region.width || !region.height || region.x >= shape.width || region.y >= shape.height)
        return push;
    const std::uint32_t right = std::min(region.x + region.width, shape.width), bottom = std::min(region.y + region.height, shape.height);
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return static_cast<std::int64_t>(values[static_cast<std::size_t>(y) * shape.width + x]); };
    for (std::uint32_t y = region.y; y < bottom; ++y) {
        if (region.x > 0) push.x += at(region.x - 1, y);
        if (right < shape.width) push.x -= at(right, y);
    }
    for (std::uint32_t x = region.x; x < right; ++x) {
        if (region.y > 0) push.y += at(x, region.y - 1);
        if (bottom < shape.height) push.y -= at(x, bottom);
    }
    return push;
}

std::expected<std::size_t,Failure> Program::values(Shape shape) const noexcept {
    if(!channels_)return std::unexpected(Failure{Errc::unprepared});
    // Coordinates plus the declared halo always fit signed 32-bit arithmetic.
    const auto cells=static_cast<std::uint64_t>(shape.width)*shape.height;
    if(!shape.width || !shape.height || shape.width>0x7fffffdfu || shape.height>0x7fffffdfu)
        return std::unexpected(Failure{Errc::shape});
    if(cells>limits_.max_values/channels_)return std::unexpected(Failure{Errc::limit});
    return static_cast<std::size_t>(cells*channels_);
}
std::expected<void,Failure> Program::validate(Shape shape, std::span<const std::uint32_t> in,
        std::span<std::uint32_t> out, Boundary boundary) const noexcept {
    auto n=values(shape);if(!n)return std::unexpected(n.error());
    if(in.size()!=*n || out.size()!=*n || boundary.edge>Edge::wrap || (!boundary.values.empty() && boundary.values.size()!=channels_))return std::unexpected(Failure{Errc::shape});
    std::less<const std::uint32_t*> less;
    if(less(in.data(),out.data()+out.size()) && less(out.data(),in.data()+in.size()))return std::unexpected(Failure{Errc::alias});
    return {};
}
Statistics Program::run_tile(Shape shape, Tile tile, std::span<const std::uint32_t> in,
        std::span<std::uint32_t> out, Boundary boundary) const noexcept {
    const auto cells=shape.width*shape.height;
    const auto read=[&](std::uint32_t channel,std::int32_t x,std::int32_t y) {
        const auto w=static_cast<std::int32_t>(shape.width),h=static_cast<std::int32_t>(shape.height);
        if(x<0 || x>=w || y<0 || y>=h) {
            if(boundary.edge==Edge::constant)return boundary.values.empty() ? boundary.value : boundary.values[channel];
            if(boundary.edge==Edge::clamp) {x=std::clamp(x,0,w-1);y=std::clamp(y,0,h-1);}
            else {x%=w;y%=h;if(x<0)x+=w;if(y<0)y+=h;}
        }
        return in[static_cast<std::size_t>(channel)*cells+static_cast<std::uint32_t>(y)*shape.width+static_cast<std::uint32_t>(x)];
    };
    // An aggregate of a channel over the neighbourhood at `position` (its size, then its offsets): how many of the
    // values are within [low, high] (1), or their sum (2), the least (3) or the greatest (4) of those that are.
    const auto aggregate=[&](std::uint32_t kind,std::uint32_t channel,std::uint32_t low,std::uint32_t high,std::size_t position,std::uint32_t x,std::uint32_t y) {
        const std::uint32_t count=words_[position];
        std::uint32_t found=0,sum=0,least=largest,greatest=0;
        for(std::uint32_t o=0;o<count;++o) {
            const auto value=read(channel,static_cast<std::int32_t>(x)+static_cast<std::int32_t>(words_[position+1+2*o])-32,
                static_cast<std::int32_t>(y)+static_cast<std::int32_t>(words_[position+2+2*o])-32);
            if(value<low || value>high)continue;
            ++found;sum=saturating_add(sum,value);least=std::min(least,value);greatest=std::max(greatest,value);
        }
        return kind==1 ? found : kind==2 ? sum : kind==3 ? least : greatest;
    };
    const bool with_terms=words_[0]>=2;
    const bool tabled=staged_.at!=0;
    // With a table: a cell's row, or none; and how well it conducts by a transfer's `by` word.
    const auto row_of=[&](std::uint32_t key) { return key<staged_.rows ? words_.data()+staged_.table+static_cast<std::size_t>(key)*staged_.columns : nullptr; };
    Statistics stats;
    for(auto y=tile.y;y<tile.y+tile.height;++y)for(auto x=tile.x;x<tile.x+tile.width;++x) {
        const auto cell=y*shape.width+x;++stats.cells;
        for(std::uint32_t ch=0;ch<channels_;++ch)out[static_cast<std::size_t>(ch)*cells+cell]=in[static_cast<std::size_t>(ch)*cells+cell];
        for(std::uint32_t r=0;r<rules_;++r) {
            const auto start=words_[4+r];std::size_t pos=start+4;bool matches=true;
            for(std::uint32_t c=0;c<words_[start+1];++c) {
                const auto kind=words_[pos],channel=words_[pos+1],low=words_[pos+2],high=words_[pos+3],count=words_[pos+6];
                if(kind==0) {const auto value=in[static_cast<std::size_t>(channel)*cells+cell];matches=value>=low && value<=high;}
                else {
                    const auto found=aggregate(kind,channel,low,high,pos+6,x,y);
                    matches=found>=words_[pos+4] && found<=words_[pos+5];
                }
                if(!matches)break;
                pos+=7+2*count;
            }
            if(!matches)continue;
            ++stats.matched;pos=words_[start+3];
            for(std::uint32_t w=0;w<words_[start+2];++w) {
                const auto index=static_cast<std::size_t>(words_[pos])*cells+cell;
                const auto old=in[index],kind=words_[pos+1];
                std::uint32_t plus=words_[pos+2],minus=0;
                pos+=3;
                if(with_terms) {
                    const std::uint32_t terms=words_[pos++];
                    for(std::uint32_t t=0;t<terms;++t) {
                        const auto source=words_[pos],channel=words_[pos+1];
                        const auto value=source==0 ? in[static_cast<std::size_t>(channel)*cells+cell] : aggregate(source,channel,words_[pos+2],words_[pos+3],pos+7,x,y);
                        const auto amount=saturating_multiply(value,words_[pos+4])>>words_[pos+5];
                        if(words_[pos+6])minus=saturating_add(minus,amount);else plus=saturating_add(plus,amount);
                        pos+=8+2*words_[pos+7];
                    }
                }
                std::uint32_t next=plus-std::min(plus,minus);
                if(kind==1) {next=saturating_add(old,plus);next-=std::min(next,minus);}
                else if(kind==2) {next=old-std::min(old,plus);next=saturating_add(next,minus);}
                out[index]=next;
            }
            break;
        }
        // Encoding 3: every reaction that holds, onto what the rules made of the cell.
        const std::uint32_t becomes=tabled ? react(shape,x,y,in,out,boundary,stats) : largest;
        if(with_terms) {
            // What flows: for each neighbour, the same amount both ends compute from the same two values.
            std::size_t pos=words_[4+rules_];
            const std::uint32_t transfers=words_[pos++];
            for(std::uint32_t t=0;t<transfers;++t) {
                const auto channel=words_[pos],by=words_[pos+1],by_limit=words_[pos+2],multiply=words_[pos+3],shift=words_[pos+4],count=words_[pos+5];
                const auto index=static_cast<std::size_t>(channel)*cells+cell;
                const auto mine=in[index];
                // A cell conducts by the channel, by its row's column, or 1 — and with a table, nothing where it is no row.
                const auto conductance_at=[&](std::int32_t cx,std::int32_t cy) -> std::uint32_t {
                    if(!tabled)return by==largest ? 1u : std::min(read(by,cx,cy),by_limit);
                    const std::uint32_t* row=row_of(read(staged_.key,cx,cy));
                    if(!row)return 0;
                    return by==largest ? 1u : std::min(by&of_table ? row[by&~of_table] : read(by,cx,cy),by_limit);
                };
                const auto conducts=conductance_at(static_cast<std::int32_t>(x),static_cast<std::int32_t>(y));
                std::uint32_t gained=0,lost=0;
                for(std::uint32_t o=0;o<count;++o) {
                    const auto nx=static_cast<std::int32_t>(x)+static_cast<std::int32_t>(words_[pos+6+2*o])-32,ny=static_cast<std::int32_t>(y)+static_cast<std::int32_t>(words_[pos+7+2*o])-32;
                    const auto theirs=read(channel,nx,ny);
                    if(theirs==mine)continue;
                    const auto conductance=std::min(conducts,conductance_at(nx,ny));
                    const auto difference=theirs>mine ? theirs-mine : mine-theirs;
                    const auto flow=saturating_multiply(saturating_multiply(difference,conductance),multiply)>>shift;
                    if(theirs>mine)gained=saturating_add(gained,flow);else lost=saturating_add(lost,flow);
                }
                out[index]=saturating_add(out[index]-std::min(out[index],lost),gained);
                pos+=6+2*count;
            }
        }
        if(becomes!=largest) {
            // It became another row: the channels that start again do, at the new row's own start where it has one.
            const std::uint32_t* row=row_of(becomes);
            std::size_t pos=staged_.resets;
            for(std::uint32_t count=words_[pos++],index=0;index<count;++index,pos+=3) {
                const std::uint32_t own=words_[pos+2]==largest ? 0u : row[words_[pos+2]];
                out[static_cast<std::size_t>(words_[pos])*cells+cell]=own ? own : words_[pos+1];
            }
        }
        bool changed=false;
        for(std::uint32_t ch=0;ch<channels_ && !changed;++ch)changed=out[static_cast<std::size_t>(ch)*cells+cell]!=in[static_cast<std::size_t>(ch)*cells+cell];
        stats.changed+=changed;
    }
    return stats;
}
// Cells change places within blocks of 2 × 2, decided from the input, moving what the stages before made of them.
void Program::exchange(Shape shape, std::span<const std::uint32_t> in, std::span<std::uint32_t> out, const Execution& execution) const noexcept {
    const Boundary boundary = execution.boundary;
    const std::uint64_t tick = execution.tick;
    // Sparse: a block whose first cell is in a sleeping tile is not looked at (it could not move: it touches the
    // tile); one that could move, whether it does or not, keeps the tiles it touches from resting this step.
    const std::span<std::uint8_t> activity = execution.activity;
    const bool sparse = !activity.empty();
    const Shape tile = execution.tile;
    const std::uint32_t columns = 1 + (shape.width - 1) / tile.width;
    // The exchange's words: after the transfers.
    std::size_t pos = words_[4 + rules_];
    for (std::uint32_t transfers = words_[pos++], t = 0; t < transfers; ++t) pos += 6 + 2 * words_[pos + 5];
    if (!words_[pos++]) return;
    const std::uint32_t weight = words_[pos], mobility = words_[pos + 1], seed = words_[pos + 2], carried = words_[pos + 3];
    const std::span<const std::uint32_t> channels(words_.data() + pos + 4, carried);
    const std::size_t cells = static_cast<std::size_t>(shape.width) * shape.height;
    // Where the cut is this tick: its four positions in turn.
    const std::uint32_t cut_x = static_cast<std::uint32_t>(tick & 1), cut_y = static_cast<std::uint32_t>((tick >> 1) & 1);
    // A block that reaches over an edge goes round a wrapped edge of an even size, and otherwise sits the tick out.
    const bool round_x = boundary.edge == Edge::wrap && shape.width % 2 == 0, round_y = boundary.edge == Edge::wrap && shape.height % 2 == 0;
    const std::uint32_t tick_key = mlibs::math::mix32(seed ^ mlibs::math::mix32(mlibs::math::fold32(tick)));
    for (std::uint32_t top = cut_y; top < shape.height; top += 2) {
        const bool over_y = top + 1 >= shape.height;
        if (over_y && !round_y) continue;
        const std::uint32_t bottom = over_y ? 0 : top + 1;
        const std::size_t top_tiles = static_cast<std::size_t>(top / tile.height) * columns, bottom_tiles = static_cast<std::size_t>(bottom / tile.height) * columns;
        for (std::uint32_t left = cut_x; left < shape.width; left += 2) {
            const bool over_x = left + 1 >= shape.width;
            if (over_x && !round_x) continue;
            const std::uint32_t right = over_x ? 0 : left + 1;
            if (sparse && (activity[top_tiles + left / tile.width] & mark_sleeping)) {
                // On to the first block past this tile.
                const std::uint64_t end = (std::uint64_t{left / tile.width} + 1) * tile.width;
                if (end >= shape.width) break;
                left = static_cast<std::uint32_t>(end) + ((static_cast<std::uint32_t>(end) ^ cut_x) & 1) - 2;
                continue;
            }
            // The block's places: above left, above right, below left, below right.
            const std::array<std::size_t, 4> place{static_cast<std::size_t>(top) * shape.width + left, static_cast<std::size_t>(top) * shape.width + right,
                                                   static_cast<std::size_t>(bottom) * shape.width + left, static_cast<std::size_t>(bottom) * shape.width + right};
            std::array<std::uint32_t, 4> heavy{}, moves{};
            for (std::size_t corner = 0; corner < 4; ++corner) {
                if (weight & of_table) {
                    // Looked up by the cell's key; what is no row neither weighs nor moves.
                    const std::uint32_t key = in[static_cast<std::size_t>(staged_.key) * cells + place[corner]];
                    const std::uint32_t* row = key < staged_.rows ? words_.data() + staged_.table + static_cast<std::size_t>(key) * staged_.columns : nullptr;
                    heavy[corner] = row ? row[weight & ~of_table] : 0;
                    moves[corner] = row ? row[mobility & ~of_table] : 0;
                } else {
                    heavy[corner] = in[static_cast<std::size_t>(weight) * cells + place[corner]];
                    moves[corner] = in[static_cast<std::size_t>(mobility) * cells + place[corner]];
                }
            }
            std::array<std::uint8_t, 4> at{0, 1, 2, 3};  // which of the four cells is at each place
            std::array<bool, 4> moved{};
            bool could_move = false;
            const auto swap = [&](std::size_t one, std::size_t other) {
                could_move = true;
                moved[at[one]] = moved[at[other]] = true;
                std::swap(at[one], at[other]);
            };
            // 1 a cell that falls sinks below a lighter one that falls.
            for (const std::size_t column : {std::size_t{0}, std::size_t{1}}) {
                const std::uint8_t above = at[column], below = at[column + 2];
                if ((moves[above] & falls) && (moves[below] & falls) && heavy[above] > heavy[below]) swap(column, column + 2);
            }
            // 2 a cell that slides, resting, goes diagonally down past a lighter neighbour.
            for (const std::size_t from : {std::size_t{0}, std::size_t{1}}) {
                const std::size_t beside = 1 - from, diagonal = 3 - from;
                const std::uint8_t cell = at[from], other = at[beside], under = at[diagonal];
                if (moved[cell] || moved[other] || moved[under]) continue;
                if ((moves[cell] & slides) && (moves[under] & falls) && heavy[cell] > heavy[under] && (moves[other] & falls) && heavy[other] < heavy[cell]) {
                    swap(from, diagonal);
                    break;
                }
            }
            // 3 a cell that spreads changes places with a lighter one beside it, or not, with equal chance.
            for (const std::size_t row : {std::size_t{0}, std::size_t{1}}) {
                const std::uint8_t one = at[row * 2], other = at[row * 2 + 1];
                if (moved[one] || moved[other] || heavy[one] == heavy[other]) continue;
                const std::uint8_t heavier = heavy[one] > heavy[other] ? one : other, lighter = heavier == one ? other : one;
                if (!(moves[heavier] & spreads) || !(moves[lighter] & falls)) continue;
                could_move = true;
                const std::uint32_t chance = mlibs::math::mix32(tick_key ^ (left * 0x9e3779b1u) ^ mlibs::math::mix32(top * 2 + static_cast<std::uint32_t>(row)));
                if (chance & 1) swap(row * 2, row * 2 + 1);
            }
            if (sparse && could_move) {
                const std::size_t left_tile = left / tile.width, right_tile = right / tile.width;
                for (const std::size_t touched : {top_tiles + left_tile, top_tiles + right_tile, bottom_tiles + left_tile, bottom_tiles + right_tile})
                    activity[touched] |= mark_restless;
            }
            if (at == std::array<std::uint8_t, 4>{0, 1, 2, 3}) continue;
            for (const std::uint32_t channel : channels) {
                const std::size_t base = static_cast<std::size_t>(channel) * cells;
                const std::array<std::uint32_t, 4> made{out[base + place[0]], out[base + place[1]], out[base + place[2]], out[base + place[3]]};
                for (std::size_t corner = 0; corner < 4; ++corner) out[base + place[corner]] = made[at[corner]];
            }
        }
    }
}

std::expected<Statistics,Failure> Program::step_tile(Shape shape, Tile tile, std::span<const std::uint32_t> in,
        std::span<std::uint32_t> out, Boundary boundary) const noexcept {
    mlibs::trace::Span span("simulation.cellular.tile", "simulation");
    auto v=validate(shape,in,out,boundary);if(!v)return std::unexpected(v.error());
    if(moves_)return std::unexpected(Failure{Errc::whole_field});
    if(!tile.width || !tile.height || tile.x>=shape.width || tile.y>=shape.height ||
        tile.width>shape.width-tile.x || tile.height>shape.height-tile.y)return std::unexpected(Failure{Errc::tile});
    return run_tile(shape,tile,in,out,boundary);
}
std::expected<Statistics,Failure> Program::step(Shape shape, std::span<const std::uint32_t> in,
        std::span<std::uint32_t> out, Execution execution) const noexcept {
    mlibs::trace::Span span("simulation.cellular.step", "simulation");
    auto v=validate(shape,in,out,execution.boundary);if(!v)return std::unexpected(v.error());
    if(!execution.tile.width || !execution.tile.height)return std::unexpected(Failure{Errc::tile});
    const auto columns=1+(shape.width-1)/execution.tile.width,rows=1+(shape.height-1)/execution.tile.height;
    const auto total=columns*rows;Statistics stats;
    if(!execution.changed_tiles.empty() && execution.changed_tiles.size()!=total)return std::unexpected(Failure{Errc::tile});
    const std::span<std::uint8_t> activity = execution.activity;
    const bool sparse = !activity.empty();
    if (sparse && activity.size() != total) return std::unexpected(Failure{Errc::tile});
    const std::size_t cells = static_cast<std::size_t>(shape.width) * shape.height;
    const auto tile_at = [&](std::uint32_t index) {
        const auto x = (index % columns) * execution.tile.width, y = (index / columns) * execution.tile.height;
        return Tile{x, y, std::min(execution.tile.width, shape.width - x), std::min(execution.tile.height, shape.height - y)};
    };
    for(std::uint32_t i=0;i<total;++i) {
        const auto k=execution.reverse_tiles ? total-1-i : i;
        const Tile tile = tile_at(k);
        if (sparse) {
            activity[k] &= rested;
            if (asleep(activity[k])) {
                // At rest: its cells are the input's — copied, or already there where two buffers take turns.
                activity[k] |= mark_sleeping;
                ++stats.asleep;
                if (!execution.alternate)
                    for (std::uint32_t channel = 0; channel < channels_; ++channel)
                        for (std::uint32_t y = tile.y; y < tile.y + tile.height; ++y) {
                            const std::size_t row = static_cast<std::size_t>(channel) * cells + static_cast<std::size_t>(y) * shape.width + tile.x;
                            std::copy_n(in.begin() + static_cast<std::ptrdiff_t>(row), tile.width, out.begin() + static_cast<std::ptrdiff_t>(row));
                        }
                if (!execution.changed_tiles.empty()) execution.changed_tiles[k] = 0;
                continue;
            }
        }
        const auto s=run_tile(shape,tile,in,out,execution.boundary);
        stats.cells+=s.cells;stats.matched+=s.matched;stats.changed+=s.changed;
        if(!execution.changed_tiles.empty())execution.changed_tiles[k]=s.changed?1:0;
        if (sparse && s.changed) activity[k] |= mark_altered;
    }
    if(moves_) {
        // Cells moved after the tiles were done: what changed is counted again, cell by cell, where it was run.
        exchange(shape,in,out,execution);
        stats.changed=0;
        for (std::uint32_t k = 0; k < total; ++k) {
            if (sparse && (activity[k] & mark_sleeping)) continue;
            const Tile tile = tile_at(k);
            std::uint64_t changed_here = 0;
            for (std::uint32_t y = tile.y; y < tile.y + tile.height; ++y)
                for (std::uint32_t x = tile.x; x < tile.x + tile.width; ++x) {
                    const std::size_t cell = static_cast<std::size_t>(y) * shape.width + x;
                    bool changed = false;
                    for (std::uint32_t ch = 0; ch < channels_ && !changed; ++ch) changed = out[ch * cells + cell] != in[ch * cells + cell];
                    changed_here += changed;
                }
            stats.changed += changed_here;
            if (!execution.changed_tiles.empty()) execution.changed_tiles[k] = changed_here ? 1 : 0;
            if (sparse) activity[k] = static_cast<std::uint8_t>(changed_here ? activity[k] | mark_altered : activity[k] & ~mark_altered);
        }
    }
    if (sparse) {
        // What this step showed of each tile that was run: nothing changed in it and nothing in it could have
        // moved — at rest as far as this step tells (where cells move, for this tick's cut: all four make a rest).
        const std::uint8_t shown = moves_ ? static_cast<std::uint8_t>(1u << ((execution.tick & 1) | ((execution.tick >> 1 & 1) << 1))) : rested;
        for (std::uint32_t k = 0; k < total; ++k)
            if (!(activity[k] & (mark_sleeping | mark_restless | mark_altered))) activity[k] |= shown;
        // What changed wakes every tile that reads it, itself the first.
        for (std::uint32_t k = 0; k < total; ++k) {
            if (!(activity[k] & mark_altered)) continue;
            const Tile tile = tile_at(k);
            rouse(activity, shape, execution.tile, tile.x, tile.x + tile.width - 1, tile.y, tile.y + tile.height - 1, reach(), execution.boundary.edge == Edge::wrap);
        }
        for (std::uint32_t k = 0; k < total; ++k) activity[k] &= rested;
    }
    mlibs::trace::count("simulation.cellular.changed",static_cast<double>(stats.changed),"simulation");
    if (sparse) mlibs::trace::count("simulation.cellular.asleep", static_cast<double>(stats.asleep), "simulation");
    return stats;
}
void Program::wake(Shape shape, Shape tile, Tile edited, std::span<std::uint8_t> activity, Edge edge) const noexcept {
    if (!tile.width || !tile.height || !shape.width || !shape.height) return;
    const Shape count = tiles(shape, tile);
    if (activity.size() != static_cast<std::size_t>(count.width) * count.height) return;
    if (!edited.width || !edited.height || edited.x >= shape.width || edited.y >= shape.height) return;
    const std::uint32_t last_x = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{edited.x} + edited.width, shape.width) - 1);
    const std::uint32_t last_y = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{edited.y} + edited.height, shape.height) - 1);
    rouse(activity, shape, tile, edited.x, last_x, edited.y, last_y, reach(), edge == Edge::wrap);
}
}
