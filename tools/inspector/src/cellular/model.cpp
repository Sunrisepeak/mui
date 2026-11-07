module omui.inspector;
import std;
import omui;
import mlibs.hash;
import xengine.simulation;
import xengine.simulation.cellular;

namespace omui::inspector::cellular {
namespace ca = xengine::simulation::cellular;
namespace {
constexpr std::uint32_t max_values = 16384;
const Value missing;
const Value& field(const Value& v, std::string_view name) {
    if (const auto* found = v.get(name)) return *found;
    return missing;
}
bool integer(const Value& v, std::int64_t low, std::int64_t high) {
    return v.type() == Value::Type::integer && v.as_int() >= low && v.as_int() <= high;
}
bool keys(const Value& v, std::initializer_list<std::string_view> allowed) {
    if (!v.map()) return false;
    for (const auto& [key, value] : *v.map())
        if (std::ranges::find(allowed, key) == allowed.end()) return false;
    return true;
}
bool identifier(std::string_view s) {
    return !s.empty() && s.size() <= 32 && std::ranges::all_of(s, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}
std::optional<Color> hex_color(const Value& v) {
    const auto s = v.as_string();
    if (s.size() != 7 || s.front() != '#') return {};
    std::uint32_t rgb = 0;
    const auto [end, ec] = std::from_chars(s.data() + 1, s.data() + s.size(), rgb, 16);
    if (ec != std::errc{} || end != s.data() + s.size()) return {};
    return Color::hex(rgb);
}
struct Swatch { std::vector<ca::Test> when; Color color; };
struct Brush { std::string id, label; std::array<std::uint32_t,4> values{}; Color color; };
struct Project {
    Value source;
    ca::Shape shape;
    ca::Program program;
    ca::Boundary boundary;
    int rate = 12;
    std::int64_t tick = 0;
    std::vector<std::uint32_t> cells;
    std::vector<Swatch> palette;
    std::vector<Brush> brushes;
};
std::expected<ca::Test,std::string> read_test(const Value& v, std::uint32_t channels) {
    const auto& ch = field(v,"channel");
    const auto* range = field(v,"value").list();
    if (!integer(ch,0,channels-1) || !range || range->size()!=2 ||
        !integer((*range)[0],0,0xffffffffu) || !integer((*range)[1],(*range)[0].as_int(),0xffffffffu))
        return std::unexpected("condition needs an integer channel and ordered uint32 value range");
    return ca::Test{static_cast<std::uint32_t>(ch.as_int()),
        {static_cast<std::uint32_t>((*range)[0].as_int()), static_cast<std::uint32_t>((*range)[1].as_int())}};
}
std::expected<std::shared_ptr<const Project>,std::string> read_project(const Value& v) {
    auto p = std::make_shared<Project>();
    if (!keys(v,{"version","name","size","channels","rate","tick","boundary","cells","rules","palette","brushes"}) ||
        !integer(field(v,"version"),1,1)) return std::unexpected("project version must be 1; unknown project keys are refused");
    const auto name=field(v,"name").as_string();
    const auto* size=field(v,"size").list();
    const auto* channels=field(v,"channels").list();
    if (name.empty() || name.size()>128 || !size || size->size()!=2 ||
        !integer((*size)[0],1,128) || !integer((*size)[1],1,128) ||
        !channels || channels->empty() || channels->size()>4)
        return std::unexpected("name, size (1..128 each) and 1..4 channels are required");
    std::set<std::string> ids;
    for (const auto& c:*channels)
        if (!identifier(c.as_string()) || !ids.insert(std::string(c.as_string())).second)
            return std::unexpected("channel IDs must be unique lower-case identifiers");
    const auto count=static_cast<std::uint32_t>(channels->size());
    p->shape={static_cast<std::uint32_t>((*size)[0].as_int()),static_cast<std::uint32_t>((*size)[1].as_int())};
    const auto values=p->shape.width*p->shape.height*count;
    if (values>max_values || !integer(field(v,"rate"),1,60) || !integer(field(v,"tick"),0,1000000000))
        return std::unexpected("project exceeds 16384 values, rate 1..60, or tick 0..1000000000");
    p->rate=static_cast<int>(field(v,"rate").as_int());p->tick=field(v,"tick").as_int();
    const auto& boundary=field(v,"boundary");
    if (!keys(boundary,{"edge","value"}) || !integer(field(boundary,"value"),0,0xffffffffu))
        return std::unexpected("boundary needs edge and uint32 value");
    const auto edge=field(boundary,"edge").as_string();
    if (edge!="constant" && edge!="clamp" && edge!="wrap") return std::unexpected("unknown boundary edge");
    p->boundary={edge=="constant"?ca::Edge::constant:edge=="clamp"?ca::Edge::clamp:ca::Edge::wrap,
        static_cast<std::uint32_t>(field(boundary,"value").as_int())};
    const auto* cells=field(v,"cells").list();
    if (!cells || cells->size()!=values) return std::unexpected("cells must be the exact channel-major uint32 field");
    p->cells.reserve(values);
    for (const auto& c:*cells) {
        if (!integer(c,0,0xffffffffu)) return std::unexpected("cell values must be uint32 integers");
        p->cells.push_back(static_cast<std::uint32_t>(c.as_int()));
    }
    const auto* rules=field(v,"rules").list();
    if (!rules || rules->size()>32) return std::unexpected("rules must be an array with at most 32 entries");
    std::vector<ca::Rule> compiled;
    for (const auto& rule:*rules) {
        const auto* when=field(rule,"when").list();const auto* writes=field(rule,"write").list();
        if (!keys(rule,{"when","write"}) || !when || when->size()>16 || !writes || writes->size()>count)
            return std::unexpected("rule needs bounded when and write arrays");
        ca::Rule r;
        for (const auto& condition:*when) {
            const auto test=read_test(condition,count);if (!test) return std::unexpected(test.error());
            if (const auto* offsets=condition.get("offsets")) {
                const auto* list=offsets->list();const auto* range=field(condition,"count").list();
                if (!keys(condition,{"channel","value","offsets","count"}) || !list || list->empty() || list->size()>128 ||
                    !range || range->size()!=2 || !integer((*range)[0],0,128) || !integer((*range)[1],(*range)[0].as_int(),128))
                    return std::unexpected("neighbor condition needs offsets and count range");
                std::vector<ca::Offset> at;
                for (const auto& offset:*list) {
                    const auto* xy=offset.list();
                    if (!xy || xy->size()!=2 || !integer((*xy)[0],-32,32) || !integer((*xy)[1],-32,32))
                        return std::unexpected("neighbor offsets must be integer pairs in -32..32");
                    at.push_back({static_cast<int>((*xy)[0].as_int()),static_cast<int>((*xy)[1].as_int())});
                }
                r.when.push_back(ca::Condition::neighbors(*test,at,
                    {static_cast<std::uint32_t>((*range)[0].as_int()),static_cast<std::uint32_t>((*range)[1].as_int())}));
            } else {
                if (!keys(condition,{"channel","value"})) return std::unexpected("unknown self condition key");
                r.when.push_back(ca::Condition::self(*test));
            }
        }
        for (const auto& write:*writes) {
            if (!keys(write,{"channel","op","value"}) || !integer(field(write,"channel"),0,count-1) ||
                !integer(field(write,"value"),0,0xffffffffu)) return std::unexpected("write needs channel, op and uint32 value");
            const auto op=field(write,"op").as_string();
            if (op!="set" && op!="add" && op!="subtract") return std::unexpected("write op must be set, add or subtract");
            r.writes.push_back({static_cast<std::uint32_t>(field(write,"channel").as_int()),
                op=="set"?ca::WriteKind::set:op=="add"?ca::WriteKind::add_saturating:ca::WriteKind::subtract_saturating,
                static_cast<std::uint32_t>(field(write,"value").as_int())});
        }
        compiled.push_back(std::move(r));
    }
    auto program=ca::Program::prepare(count,compiled,{.max_channels=4,.max_rules=32,.max_words=16384,.max_values=max_values});
    if (!program) return std::unexpected(program.error().text());
    p->program=std::move(*program);
    const auto* palette=field(v,"palette").list();
    if (!palette || palette->empty() || palette->size()>32) return std::unexpected("palette needs 1..32 ordered swatches");
    for (const auto& swatch:*palette) {
        const auto color=hex_color(field(swatch,"color"));const auto* when=field(swatch,"when").list();
        if (!keys(swatch,{"when","color"}) || !color || !when || when->size()>4)
            return std::unexpected("swatch needs self tests and #RRGGBB color");
        Swatch s{.color=*color};
        for (const auto& test:*when) {
            if (!keys(test,{"channel","value"})) return std::unexpected("palette supports self tests only");
            auto t=read_test(test,count);if (!t) return std::unexpected(t.error());s.when.push_back(*t);
        }
        p->palette.push_back(std::move(s));
    }
    if (!p->palette.back().when.empty()) return std::unexpected("last palette swatch must be an unconditional fallback");
    const auto* brushes=field(v,"brushes").list();ids.clear();
    if (!brushes || brushes->empty() || brushes->size()>16) return std::unexpected("brushes needs 1..16 entries");
    for (const auto& brush:*brushes) {
        const auto id=field(brush,"id").as_string(),label=field(brush,"label").as_string();
        const auto color=hex_color(field(brush,"color"));const auto* data=field(brush,"values").list();
        if (!keys(brush,{"id","label","color","values"}) || !identifier(id) || !ids.insert(std::string(id)).second ||
            label.empty() || label.size()>64 || !color || !data || data->size()!=count)
            return std::unexpected("brush needs unique id, label, color and one uint32 value per channel");
        Brush b{.id=std::string(id),.label=std::string(label),.color=*color};
        for (std::size_t i=0;i<data->size();++i) {
            if (!integer((*data)[i],0,0xffffffffu)) return std::unexpected("brush values must be uint32 integers");
            b.values[i]=static_cast<std::uint32_t>((*data)[i].as_int());
        }
        p->brushes.push_back(std::move(b));
    }
    p->source=v;return p;
}

struct Model {
    struct State {
        std::shared_ptr<const Project> project;
        std::vector<std::uint32_t> cells, scratch;
        std::int64_t tick = 0;
    };
    struct Input {
        bool paint = false;
        std::uint32_t x=0,y=0,radius=0;
        std::array<std::uint32_t,4> values{};
    };
    static void step(State& s,const Input& in) {
        const auto shape=s.project->shape;const auto area=shape.width*shape.height;
        if (in.paint) {
            for (std::uint32_t y=0;y<shape.height;++y) for (std::uint32_t x=0;x<shape.width;++x) {
                const auto dx=static_cast<int>(x)-static_cast<int>(in.x),dy=static_cast<int>(y)-static_cast<int>(in.y);
                if (dx*dx+dy*dy>static_cast<int>(in.radius*in.radius)) continue;
                for (std::uint32_t c=0;c<s.project->program.channels();++c) s.cells[c*area+y*shape.width+x]=in.values[c];
            }
        } else {
            const auto result=s.project->program.step(shape,s.cells,s.scratch,{.boundary=s.project->boundary});
            if (result) {s.cells.swap(s.scratch);++s.tick;} // dimensions/alias/boundary were prepared
        }
    }
};
using History = xengine::simulation::Timeline<Model>;
Model::State initial(std::shared_ptr<const Project> p) {
    Model::State s{p,p->cells,p->cells,p->tick};return s;
}
}

struct Session::Impl {
    std::unique_ptr<History> history;
    xengine::simulation::Clock clock{12,4};
    std::int64_t revision=0;
    bool playing=false;
    std::uint64_t timer=0;
    Invocation authority;
};
Session::Session() : impl_(std::make_unique<Impl>()) {}
Session::~Session() = default;
void Session::ensure() {
    if (impl_->history) return;
    auto p=read_project(example_project());
    impl_->history=std::make_unique<History>(initial(*p),240,30);
}
void Session::refresh_controls() {
    const auto& p=*impl_->history->state().project;
    cursor_x.set(std::clamp<std::int64_t>(cursor_x.peek(),0,p.shape.width-1));
    cursor_y.set(std::clamp<std::int64_t>(cursor_y.peek(),0,p.shape.height-1));
    if (std::ranges::find(p.brushes,brush.peek(),&Brush::id)==p.brushes.end())
        brush.set(p.brushes.front().id);
    const auto config=perform("config");
    if (config) draft.set(config->json());
}
std::string Session::state_hash() {
    ensure();const auto& s=impl_->history->state();
    mlibs::hash::Fnv1a h;h.text("cellular-v1").u64(s.project->shape.width).u64(s.project->shape.height)
        .u64(s.project->program.channels()).i64(s.tick);
    for (auto v:s.cells) h.u64(v);
    return h.hex();
}
Value Session::snapshot() {
    ensure();const auto& h=*impl_->history;const auto& p=*h.state().project;
    return Value{}.set("name",field(p.source,"name")).set("revision",impl_->revision).set("frame",h.frame())
        .set("earliest",h.earliest()).set("latest",h.latest()).set("tick",h.state().tick).set("rate",p.rate)
        .set("playing",impl_->playing).set("size",field(p.source,"size")).set("channels",field(p.source,"channels"))
        .set("brushes",field(p.source,"brushes")).set("state_hash",state_hash());
}
Value Session::project() {
    ensure();const auto& state=impl_->history->state();Value out=state.project->source;
    Value::List cells;cells.reserve(state.cells.size());for (auto cell:state.cells) cells.emplace_back(cell);
    return out.set("cells",Value(std::move(cells))).set("tick",state.tick);
}
Size Session::shape() {ensure();const auto s=impl_->history->state().project->shape;return {static_cast<float>(s.width),static_cast<float>(s.height)};}
Color Session::color(std::uint32_t x,std::uint32_t y) {
    ensure();const auto& s=impl_->history->state();const auto& p=*s.project;
    if (x>=p.shape.width || y>=p.shape.height) return Color{};
    const auto i=y*p.shape.width+x,area=p.shape.width*p.shape.height;
    for (const auto& swatch:p.palette)
        if (std::ranges::all_of(swatch.when,[&](const auto& t){const auto v=s.cells[t.channel*area+i];return v>=t.value.low && v<=t.value.high;}))
            return swatch.color;
    return Color{};
}
void Session::pause() {
    if (!impl_->playing && !impl_->timer) return;
    impl_->playing=false;
    if (runtime && impl_->timer) runtime->cancel(std::exchange(impl_->timer,0));
    changed.set(++impl_->revision);
    if (runtime) runtime->invalidate_paint(runtime->root());
}
void Session::schedule() {
    if (!runtime || !impl_->playing || impl_->timer) return;
    const auto weak=weak_from_this();
    impl_->timer=runtime->after(seconds(1.0/60),[weak] {
        const auto self=weak.lock();if (!self) return;
        auto& d=*self->impl_;d.timer=0;
        if (!d.playing || !self->runtime) return;
        const int count=d.clock.advance(self->runtime->now().count());
        if (count>0) {
            auto call=d.authority;call.args=Value{}.set("count",count);
            const auto result=self->runtime->invoke("cellular.advance",call);
            if (result.exit_code()!=0) {self->pause();self->problem.set("Playback stopped: permission or operation refused");return;}
        }
        self->schedule();
    });
}
VerbResult Session::perform(std::string_view op,const Value& args) {
    ensure();auto& d=*impl_;
    const auto fail=[&](std::string why)->VerbResult {return std::unexpected(std::move(why));};
    if (op=="describe") {
        return Value{}.set("format","*.cellular.omui.json").set("version",1).set("max_values",max_values)
            .set("history_frames",240).set("operations",[] {
                std::string names;
                for (const auto& entry:catalog) names+=std::format("{}{}",names.empty()?"":" ",entry.operation.name.substr(std::string_view("cellular.").size()));
                return names;
            }())
            .set("sop","snapshot; edit project JSON and load or paint; advance; inspect/capture intermediate frames; seek; project to save")
            .set("history","paint counts as a recorded input, not a simulation tick; seek uses frame; save captures current state")
            .set("undo","load, configure, paint and reset restore owned state; undo/redo starts a new local replay history")
            .set("rendering","integer cells deliberately step; capture actual pixels through the host; no GUI required");
    }
    if (op=="snapshot") return snapshot();
    if (op=="project") return project();
    if (op=="config") {
        Value result;
        for (const auto key:{"name","rate","boundary","rules","palette","brushes"})
            result.set(key,field(d.history->state().project->source,key));
        return result;
    }
    if (op=="inspect") {
        const auto& p=*d.history->state().project;
        if (!keys(args,{"x","y"}) || !integer(field(args,"x"),0,p.shape.width-1) || !integer(field(args,"y"),0,p.shape.height-1))
            return fail("inspect needs integer x/y inside the field");
        const auto index=static_cast<std::uint32_t>(field(args,"y").as_int())*p.shape.width+static_cast<std::uint32_t>(field(args,"x").as_int());
        Value::List values;for (std::uint32_t c=0;c<p.program.channels();++c) values.emplace_back(d.history->state().cells[c*p.shape.width*p.shape.height+index]);
        return Value{}.set("revision",d.revision).set("values",Value(std::move(values)));
    }
    if (const auto* revision=args.get("revision");revision && (!integer(*revision,0,std::numeric_limits<std::int64_t>::max()) || revision->as_int()!=d.revision))
        return fail("stale revision");
    // Edits are the catalog's undoable operations.
    const auto entry=std::ranges::find(catalog,"cellular."+std::string(op),[](const auto& e){return std::string(e.operation.name);});
    const bool edit=entry!=catalog.end() && entry->undoable;
    // Only explicit edits retain before/after state. Time controls use the
    // simulation's bounded checkpoint history, never one UI inverse per tick.
    std::optional<Model::State> before;
    if (edit && runtime) before=d.history->state();
    if (op=="load" || op=="configure") {
        Value candidate;
        if (op=="load") {
            if (!keys(args,{"project","revision"})) return fail("load needs project");
            candidate=field(args,"project");
        } else {
            if (!keys(args,{"config","revision"}) || !keys(field(args,"config"),{"name","rate","boundary","rules","palette","brushes"}))
                return fail("configure takes name, rate, boundary, rules, palette and/or brushes");
            candidate=project();
            for (const auto& [key,value]:*field(args,"config").map()) candidate.set(key,value);
        }
        auto p=read_project(candidate);if (!p) return fail(p.error());
        pause();d.history=std::make_unique<History>(initial(*p),240,30);refresh_controls();
    } else if (op=="paint") {
        const auto& p=*d.history->state().project;
        if (!keys(args,{"x","y","radius","brush","revision"}) || !integer(field(args,"x"),0,p.shape.width-1) ||
            !integer(field(args,"y"),0,p.shape.height-1) || !integer(field(args,"radius"),0,8)) return fail("paint needs x/y, radius 0..8 and brush ID");
        const auto found=std::ranges::find(p.brushes,field(args,"brush").as_string(),&Brush::id);
        if (found==p.brushes.end()) return fail("unknown brush ID");
        pause();
        d.history->step({true,static_cast<std::uint32_t>(field(args,"x").as_int()),static_cast<std::uint32_t>(field(args,"y").as_int()),
            static_cast<std::uint32_t>(field(args,"radius").as_int()),found->values});
    } else if (op=="advance") {
        if (!keys(args,{"count","revision"}) || !integer(field(args,"count"),1,120)) return fail("advance count must be an integer in 1..120");
        const auto count=field(args,"count").as_int();
        if (d.history->state().tick>1000000000-count) return fail("tick limit reached");
        for (std::int64_t i=0;i<count;++i) d.history->step({});
    } else if (op=="seek") {
        if (!keys(args,{"frame","revision"}) || !integer(field(args,"frame"),d.history->earliest(),d.history->latest())) return fail("frame outside retained history");
        pause();(void)d.history->seek(field(args,"frame").as_int());
    } else if (op=="reset") {
        if (!args.is_null() && !keys(args,{"revision"})) return fail("unknown reset argument");
        auto p=d.history->state().project;pause();d.history=std::make_unique<History>(initial(p),240,30);
    } else if (op=="play") {
        if (!keys(args,{"enabled","revision"}) || field(args,"enabled").type()!=Value::Type::boolean) return fail("play needs boolean enabled");
        if (field(args,"enabled").as_bool()) {
            if (!runtime || weak_from_this().expired()) return fail("play needs an attached host; batch clients use advance");
            d.authority=runtime->current_invocation().value_or(Invocation{});
            d.clock=xengine::simulation::Clock(d.history->state().project->rate,4);d.clock.reset(runtime->now().count());
            d.playing=true;schedule();
        } else pause();
    } else return fail("unknown cellular operation");
    changed.set(++d.revision);problem.set({});
    if (runtime) runtime->invalidate_paint(runtime->root());
    if (before) {
        const auto after=d.history->state();const auto weak=weak_from_this();
        auto restore=[weak](const Model::State& state) {if (auto self=weak.lock()) {
            self->pause();self->impl_->history=std::make_unique<History>(state,240,30);
            self->refresh_controls();self->problem.set({});
            self->changed.set(++self->impl_->revision);
            if (self->runtime) self->runtime->invalidate_paint(self->runtime->root());
        }};
        runtime->taken_back_by([restore,before=*before]{restore(before);},[restore,after]{restore(after);});
    }
    return snapshot();
}
}
