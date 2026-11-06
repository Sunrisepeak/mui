// A real external consumer: no UI, specification, renderer or test library.
import std;
import hanim.core;
import mlibs.log;

namespace {
constexpr mlibs::log::Log diagnostic{"hanim.consumer"};
struct Logs { std::size_t count = 0; bool history_visible = true; };
void sink(void* pointer, const mlibs::log::LogRecord& record) {
    auto& state = *static_cast<Logs*>(pointer);
    const auto latest = mlibs::log::recent_logs(1);
    state.history_visible &= latest.size() == 1 && latest.back().message == record.message && latest.back().category == record.category;
    if (!latest.empty()) std::println("{}", omui::to_json(latest.back()));
    ++state.count;
}
bool same(hanim::Vec2 a, hanim::Vec2 b) {
    return std::bit_cast<std::uint64_t>(a.x) == std::bit_cast<std::uint64_t>(b.x) &&
           std::bit_cast<std::uint64_t>(a.y) == std::bit_cast<std::uint64_t>(b.y);
}
bool same(const hanim::ShapeCubic& a, const hanim::ShapeCubic& b) {
    return same(a.start,b.start) && same(a.control1,b.control1) && same(a.control2,b.control2) && same(a.end,b.end);
}
bool connected(std::span<const hanim::ShapeCubic> contour) {
    if (contour.empty()) return false;
    for (std::size_t i = 0; i < contour.size(); ++i)
        if (contour[i].end != contour[(i+1)%contour.size()].start) return false;
    return true;
}
bool triangle_boundary(hanim::Vec2 p) {
    constexpr std::array<hanim::Vec2,3> triangle{{{0,-1},{-1,1},{1,1}}};
    for (std::size_t i=0; i<triangle.size(); ++i) {
        const auto a=triangle[i], b=triangle[(i+1)%triangle.size()];
        const double cross=(p.x-a.x)*(b.y-a.y)-(p.y-a.y)*(b.x-a.x);
        if (std::abs(cross)<1e-12 && p.x>=std::min(a.x,b.x)-1e-12 && p.x<=std::max(a.x,b.x)+1e-12 &&
            p.y>=std::min(a.y,b.y)-1e-12 && p.y<=std::max(a.y,b.y)+1e-12) return true;
    }
    return false;
}
int fail(std::string_view message) { diagnostic.error("consumer failed: {}", message); return 1; }
}

int main() {
    Logs logs;
    mlibs::log::set_log_history(0); mlibs::log::set_log_history(32); mlibs::log::set_log_level(mlibs::log::Level::info); mlibs::log::set_log_sink(sink,&logs);
    const std::array triangle{hanim::ShapeSegment::line({-1,1}),hanim::ShapeSegment::line({1,1})};
    constexpr double k=0.5522847498307936;
    const std::array circle{
        hanim::ShapeSegment::cubic({k,-1},{1,-k},{1,0}), hanim::ShapeSegment::cubic({1,k},{k,1},{0,1}),
        hanim::ShapeSegment::cubic({-k,1},{-1,k},{-1,0}), hanim::ShapeSegment::cubic({-1,-k},{-k,-1},{0,-1})};
    const auto plan=hanim::PreparedShapeMorph::prepare({{0,-1},triangle},{{0,-1},circle},
                                                     {.max_output_segments=64});
    if (!plan) return fail("triangle/circle public preparation");
    diagnostic.info("prepared triangle-to-circle with {} cubics",plan->segment_count());
    const auto count=plan->segment_count();
    std::vector<hanim::ShapeCubic> current(count),first(count),last(count),quarter(count);
    if (!plan->sample(0,first) || !plan->sample(1,last)) return fail("public endpoint sampling");
    if (!connected(first) || !connected(last)) return fail("endpoint contour continuity");
    bool endpoints_differ=false;
    for (std::size_t i=0; i<count; ++i) {
        for (double t : {0.0,0.25,0.5,0.75,1.0}) {
            if (!triangle_boundary(first[i].point(t))) return fail("source lost an original triangle edge");
            const auto p=last[i].point(t);
            // The target is the original four-cubic circle approximation.
            if (std::abs(hanim::dm::sqrt(p.x*p.x+p.y*p.y)-1)>0.0004) return fail("target circle endpoint geometry");
        }
        endpoints_differ |= !same(first[i],last[i]);
    }
    if (!endpoints_differ) return fail("the endpoints must be different geometry");
    hanim::Timeline timeline;
    timeline.at(hanim::seconds(0),"contour.progress",hanim::Tween<double>{.from=0,.to=1,.duration=hanim::seconds(1),.ease=hanim::ease::linear})
            .at(hanim::seconds(0),"object.position",hanim::Tween<hanim::Vec2>{.from={0,0},.to={12,-8},.duration=hanim::seconds(1),.ease=hanim::ease::linear});
    const std::array times{1.0,0.25,0.75,0.0,0.5,0.25};
    bool seen_quarter=false;
    for (double t : times) {
        std::size_t channels=0; double progress=-1; hanim::Vec2 position{};
        timeline.sample(hanim::seconds(t),[&](std::string_view name,const hanim::Value& value) {
            if (name=="contour.progress" && value.is<double>()) { progress=*value.as<double>(); ++channels; }
            if (name=="object.position" && value.is<hanim::Vec2>()) { position=*value.as<hanim::Vec2>(); ++channels; }
        });
        if (channels!=2 || progress!=t || position!=hanim::Vec2{12*t,-8*t}) return fail("arbitrary-order timeline values");
        if (!plan->sample(progress,current) || !connected(current)) return fail("one connected current contour");
        for (std::size_t i=0;i<count;++i) {
            const auto streamed=plan->sample_segment(progress,i);
            if (!streamed || !same(*streamed,current[i])) return fail("streaming and bulk geometry disagree");
            if (t==0 && !same(first[i],current[i])) return fail("reverse seek changed the original endpoint");
            if (t==1 && !same(last[i],current[i])) return fail("forward seek changed the target endpoint");
            if (t==0.25 && seen_quarter && !same(quarter[i],current[i])) return fail("repeated seek changed its bits");
        }
        if (t==0.25 && !seen_quarter) { quarter=current; seen_quarter=true; }
        diagnostic.info("sample t={} position=({}, {}) contour=1",t,position.x,position.y);
    }
    diagnostic.info("completed six unordered seeks");
    const auto history=mlibs::log::recent_logs(32);
    const bool valid=logs.history_visible && logs.count==8 && history.size()==8 && seen_quarter;
    mlibs::log::set_log_sink(nullptr,nullptr);
    if (!valid) return 1;
    std::println(R"({{"kind":"hanim.core-consumer","ok":true,"source_lines":3,"target_cubics":4,"contours":1,"prepared_cubics":{},"seek_count":6,"timeline_channels":2,"repeated_seek_equal":true,"endpoints_differ":true,"log_records":{}}})",count,history.size());
}
