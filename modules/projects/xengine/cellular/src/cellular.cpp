module xengine.cellular.api;
import std;
import mlibs.trace;
import mlibs.log;

namespace xengine::cellular {
std::expected<Program,Failure> Program::prepare(std::uint32_t channels, std::span<const Rule> rules, Limits limits) {
    mlibs::trace::Span span("simulation.cellular.prepare", "simulation");
    // Limits that cannot hold a program are the caller's error; a program beyond sound limits is refused.
    if (limits.max_words<4 || limits.max_values==0 || limits.max_values>std::numeric_limits<std::uint32_t>::max()/4 ||
        limits.max_words>std::numeric_limits<std::uint32_t>::max()/4) return std::unexpected(Failure{Errc::invalid_limits});
    if (!channels) return std::unexpected(Failure{Errc::channels});
    if (channels > limits.max_channels || channels > limits.max_values || rules.size()>limits.max_rules) return std::unexpected(Failure{Errc::limit});
    std::uint64_t size=4+static_cast<std::uint64_t>(rules.size());
    std::uint32_t halo=0;
    for(std::size_t r=0;r<rules.size();++r) {
        const auto& rule=rules[r];
        if(rule.when.size()>limits.max_words) return std::unexpected(Failure{Errc::limit, {}, {r}});
        size+=4+rule.writes.size()*3;
        for(std::size_t c=0;c<rule.when.size();++c) {
            const auto& test=rule.when[c];
            const auto fail=[&](Errc e){return std::unexpected(Failure{e, {}, {r, c}});};
            if(test.kind>ConditionKind::neighbors || (test.kind==ConditionKind::self && !test.offsets.empty())) return fail(Errc::condition);
            if(test.test.channel>=channels) return fail(Errc::channels);
            if(test.test.value.low>test.test.value.high || test.count.low>test.count.high) return fail(Errc::range);
            if(test.offsets.size()>128) return fail(Errc::limit);
            for(std::size_t i=0;i<test.offsets.size();++i) {
                const auto o=test.offsets[i];
                if(o.x < -32 || o.x>32 || o.y < -32 || o.y>32) return fail(Errc::offset);
                for(std::size_t j=0;j<i;++j)if(test.offsets[j]==o)return fail(Errc::offset);
                halo=std::max(halo,static_cast<std::uint32_t>(std::max(std::abs(o.x),std::abs(o.y))));
            }
            size+=7+test.offsets.size()*2;
            if(size>limits.max_words) return fail(Errc::limit);
        }
        for(std::size_t w=0;w<rule.writes.size();++w) {
            const auto write=rule.writes[w];
            if(write.channel>=channels) return std::unexpected(Failure{Errc::channels, {}, {r, w}});
            if(write.kind>WriteKind::subtract_saturating) return std::unexpected(Failure{Errc::condition, {}, {r, w}});
            for(std::size_t k=0;k<w;++k)if(rule.writes[k].channel==write.channel) return std::unexpected(Failure{Errc::duplicate_write, {}, {r, w}});
        }
        if(size>limits.max_words) return std::unexpected(Failure{Errc::limit, {}, {r}});
    }
    if(size>limits.max_words) return std::unexpected(Failure{Errc::limit});
    Program p;p.channels_=channels;p.rules_=static_cast<std::uint32_t>(rules.size());p.halo_=halo;p.limits_=limits;
    p.words_.reserve(static_cast<std::size_t>(size));p.words_={1,channels,p.rules_,halo};p.words_.resize(4+rules.size());
    for(std::size_t r=0;r<rules.size();++r) {
        const auto& rule=rules[r];const auto start=p.words_.size();p.words_[4+r]=static_cast<std::uint32_t>(start);
        p.words_.insert(p.words_.end(),{0,static_cast<std::uint32_t>(rule.when.size()),static_cast<std::uint32_t>(rule.writes.size()),0});
        for(const auto& c:rule.when) {
            p.words_.insert(p.words_.end(),{static_cast<std::uint32_t>(c.kind),c.test.channel,c.test.value.low,c.test.value.high,
                c.count.low,c.count.high,static_cast<std::uint32_t>(c.offsets.size())});
            for(const auto o:c.offsets) {p.words_.push_back(static_cast<std::uint32_t>(o.x+32));p.words_.push_back(static_cast<std::uint32_t>(o.y+32));}
        }
        p.words_[start+3]=static_cast<std::uint32_t>(p.words_.size());
        for(const auto w:rule.writes)p.words_.insert(p.words_.end(),{w.channel,static_cast<std::uint32_t>(w.kind),w.value});
        p.words_[start]=static_cast<std::uint32_t>(p.words_.size());
    }
    mlibs::log::Log{"simulation"}.debug("cellular plan prepared: {} channels, {} rules, halo {}, {} words",channels,p.rules_,halo,p.words_.size());
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
                    std::uint32_t found=0;
                    for(std::uint32_t o=0;o<count;++o) {
                        const auto value=read(channel,static_cast<std::int32_t>(x)+static_cast<std::int32_t>(words_[pos+7+2*o])-32,
                            static_cast<std::int32_t>(y)+static_cast<std::int32_t>(words_[pos+8+2*o])-32);
                        found+=value>=low && value<=high;
                    }
                    matches=found>=words_[pos+4] && found<=words_[pos+5];
                }
                if(!matches)break;
                pos+=7+2*count;
            }
            if(!matches)continue;
            ++stats.matched;bool changed=false;pos=words_[start+3];
            for(std::uint32_t w=0;w<words_[start+2];++w,pos+=3) {
                const auto index=static_cast<std::size_t>(words_[pos])*cells+cell;
                const auto old=in[index],value=words_[pos+2],kind=words_[pos+1];std::uint32_t next=value;
                if(kind==1)next=old+std::min(value,std::numeric_limits<std::uint32_t>::max()-old);
                else if(kind==2)next=old-std::min(old,value);
                out[index]=next;changed|=next!=old;
            }
            stats.changed+=changed;break;
        }
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
