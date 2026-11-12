module xengine.cellular.api;
import std;
import mlibs.trace;
import mlibs.log;

namespace xengine::cellular {
namespace {

constexpr std::uint32_t largest = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t saturating_add(std::uint32_t a, std::uint32_t b) noexcept { return a + std::min(b, largest - a); }
constexpr std::uint32_t saturating_multiply(std::uint32_t a, std::uint32_t by) noexcept { return by && a > largest / by ? largest : a * by; }

// The words of a neighbourhood: its size, then each offset as two words (x + 32, y + 32).
void put_offsets(std::vector<std::uint32_t>& words, std::span<const Offset> offsets) {
    words.push_back(static_cast<std::uint32_t>(offsets.size()));
    for (const Offset offset : offsets) {
        words.push_back(static_cast<std::uint32_t>(offset.x + 32));
        words.push_back(static_cast<std::uint32_t>(offset.y + 32));
    }
}

// A neighbourhood as a program takes it: at most 128 offsets, each within ±32, none twice. What is wrong with it,
// and how far it reaches.
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

}  // namespace

std::expected<Program,Failure> Program::prepare(std::uint32_t channels, std::span<const Rule> rules, Limits limits) {
    return prepare(channels, Stages{.rules = rules}, limits);
}

std::expected<Program,Failure> Program::prepare(std::uint32_t channels, Stages stages, Limits limits) {
    const std::span<const Rule> rules = stages.rules;
    const std::span<const Transfer> transfers = stages.transfers;
    mlibs::trace::Span span("simulation.cellular.prepare", "simulation");
    // Limits that cannot hold a program are the caller's error; a program beyond sound limits is refused.
    if (limits.max_words<4 || limits.max_values==0 || limits.max_values>std::numeric_limits<std::uint32_t>::max()/4 ||
        limits.max_words>std::numeric_limits<std::uint32_t>::max()/4) return std::unexpected(Failure{Errc::invalid_limits});
    if (!channels) return std::unexpected(Failure{Errc::channels});
    if (channels > limits.max_channels || channels > limits.max_values || rules.size()>limits.max_rules) return std::unexpected(Failure{Errc::limit});
    // Encoding 2 where a rule aggregates or a write has terms; otherwise the encoding every backend runs.
    bool aggregates = !transfers.empty();
    for (const Rule& rule : rules) {
        for (const Condition& condition : rule.when) aggregates |= condition.kind > ConditionKind::neighbors;
        for (const Write& write : rule.writes) aggregates |= !write.terms.empty();
    }
    std::uint64_t size=4+static_cast<std::uint64_t>(rules.size())+(aggregates ? 2 : 0);
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
        if (transfer.shift > 31 || transfer.offsets.empty()) return fail(Errc::transfer);
        if (const auto wrong = check_offsets(transfer.offsets, halo)) return fail(*wrong);
        // Seen from both ends: with each offset its opposite, and no cell its own neighbour.
        for (const Offset offset : transfer.offsets)
            if ((offset.x == 0 && offset.y == 0) || std::ranges::find(transfer.offsets, Offset{-offset.x, -offset.y}) == transfer.offsets.end())
                return fail(Errc::transfer);
        // No cell gives more than it has: the whole neighbourhood together takes at most all of a difference.
        const std::uint64_t most = static_cast<std::uint64_t>(transfer.by ? transfer.by_limit : 1) * transfer.multiply * transfer.offsets.size();
        if (most > (std::uint64_t{1} << transfer.shift)) return fail(Errc::transfer);
        for (std::size_t earlier = 0; earlier < t; ++earlier)
            if (transfers[earlier].channel == transfer.channel) return fail(Errc::transfer);  // one flow a quantity: two could together take too much
        size += 6 + transfer.offsets.size() * 2;
    }
    if(size>limits.max_words) return std::unexpected(Failure{Errc::limit});
    Program p;p.channels_=channels;p.rules_=static_cast<std::uint32_t>(rules.size());p.halo_=halo;p.limits_=limits;
    p.words_.reserve(static_cast<std::size_t>(size));p.transfers_=static_cast<std::uint32_t>(transfers.size());
    p.words_={aggregates ? 2u : 1u,channels,p.rules_,halo};p.words_.resize(4+rules.size()+(aggregates ? 1 : 0));
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
            p.words_.insert(p.words_.end(), {transfer.channel, transfer.by.value_or(largest), transfer.by ? transfer.by_limit : 1u, transfer.multiply, transfer.shift});
            put_offsets(p.words_, transfer.offsets);
        }
    }
    mlibs::log::Log{"simulation"}.debug("cellular plan prepared: encoding {}, {} channels, {} rules, halo {}, {} words",p.words_[0],channels,p.rules_,halo,p.words_.size());
    return p;
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
    if(in.size()!=*n || out.size()!=*n || boundary.edge>Edge::wrap)return std::unexpected(Failure{Errc::shape});
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
            if(boundary.edge==Edge::constant)return boundary.value;
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
    const bool with_terms=words_[0]==2;
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
        if(with_terms) {
            // What flows: for each neighbour, the same amount both ends compute from the same two values.
            std::size_t pos=words_[4+rules_];
            const std::uint32_t transfers=words_[pos++];
            for(std::uint32_t t=0;t<transfers;++t) {
                const auto channel=words_[pos],by=words_[pos+1],by_limit=words_[pos+2],multiply=words_[pos+3],shift=words_[pos+4],count=words_[pos+5];
                const auto index=static_cast<std::size_t>(channel)*cells+cell;
                const auto mine=in[index];
                const auto conducts=by==largest ? 1u : std::min(in[static_cast<std::size_t>(by)*cells+cell],by_limit);
                std::uint32_t gained=0,lost=0;
                for(std::uint32_t o=0;o<count;++o) {
                    const auto nx=static_cast<std::int32_t>(x)+static_cast<std::int32_t>(words_[pos+6+2*o])-32,ny=static_cast<std::int32_t>(y)+static_cast<std::int32_t>(words_[pos+7+2*o])-32;
                    const auto theirs=read(channel,nx,ny);
                    if(theirs==mine)continue;
                    const auto conductance=by==largest ? 1u : std::min(conducts,read(by,nx,ny));
                    const auto difference=theirs>mine ? theirs-mine : mine-theirs;
                    const auto flow=saturating_multiply(saturating_multiply(difference,conductance),multiply)>>shift;
                    if(theirs>mine)gained=saturating_add(gained,flow);else lost=saturating_add(lost,flow);
                }
                out[index]=saturating_add(out[index]-std::min(out[index],lost),gained);
                pos+=6+2*count;
            }
        }
        bool changed=false;
        for(std::uint32_t ch=0;ch<channels_ && !changed;++ch)changed=out[static_cast<std::size_t>(ch)*cells+cell]!=in[static_cast<std::size_t>(ch)*cells+cell];
        stats.changed+=changed;
    }
    return stats;
}
std::expected<Statistics,Failure> Program::step_tile(Shape shape, Tile tile, std::span<const std::uint32_t> in,
        std::span<std::uint32_t> out, Boundary boundary) const noexcept {
    mlibs::trace::Span span("simulation.cellular.tile", "simulation");
    auto v=validate(shape,in,out,boundary);if(!v)return std::unexpected(v.error());
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
    for(std::uint32_t i=0;i<total;++i) {
        const auto k=execution.reverse_tiles ? total-1-i : i;
        const auto x=(k%columns)*execution.tile.width,y=(k/columns)*execution.tile.height;
        const auto s=run_tile(shape,{x,y,std::min(execution.tile.width,shape.width-x),std::min(execution.tile.height,shape.height-y)},in,out,execution.boundary);
        stats.cells+=s.cells;stats.matched+=s.matched;stats.changed+=s.changed;
        if(!execution.changed_tiles.empty())execution.changed_tiles[k]=s.changed?1:0;
    }
    mlibs::trace::count("simulation.cellular.changed",static_cast<double>(stats.changed),"simulation");
    return stats;
}
}
