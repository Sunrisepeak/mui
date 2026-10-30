module openmui.site;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.base;

namespace omui::site {

namespace {

constexpr float meter_width = 288;
constexpr float meter_height = 30;
constexpr float pad = 10;
constexpr float bar_step = 2;      // a bar and the gap after it
constexpr float bars_height = 14;  // twice the budget at the top: the line is half-way
constexpr double run_gap = 0.25;   // frames further apart than this are separate runs

// How the host's renderer is written in the meter.
std::string device_name(std::string_view renderer) {
    if (renderer == "webgpu") return "WebGPU";
    if (renderer == "webgl") return "WebGL 2";
    if (renderer == "cpu" || renderer.empty()) return "CPU";
    if (renderer == "vulkan") return "Vulkan";
    if (renderer == "metal") return "Metal";
    if (renderer == "d3d12") return "D3D12";
    return std::string(renderer);
}

}  // namespace

FrameReading read_frames(const ShownFrames& f, std::string_view renderer) {
    FrameReading r;
    r.renderer = std::string(renderer.empty() ? "cpu" : renderer);
    if (f.empty()) return r;
    // Runs: frames each shown within run_gap of the one before. A run of one
    // frame is a change, not motion (a hover; the meter's own frame saying
    // the page is at rest): the page is idle after it.
    const auto run_start = [&](std::size_t end) {  // the first frame of the run ending at `end`
        std::size_t first = end;
        while (first > 0 && f[first].at.count() - f[first - 1].at.count() < run_gap) --first;
        return first;
    };
    std::size_t end = f.size() - 1, first = run_start(end);
    r.idle = first == end || f.now.count() - f[end].at.count() >= FrameMeter::rest;
    // What is read: the last run that moved (of two frames or more).
    while (first == end && first > 0) {
        end = first - 1;
        first = run_start(end);
    }
    if (first == end) return r;
    const double last = f[end].at.count();
    r.frames = end + 1 - first;
    // Its last second: the rate, and the median cost.
    std::size_t from = first;
    while (from < end && last - f[from].at.count() > 1.0) ++from;
    if (const double span = last - f[from].at.count(); end > from && span > 0) r.fps = static_cast<double>(end - from) / span;
    std::array<double, ShownFrames::capacity> costs{};
    std::size_t n = 0;
    for (std::size_t i = from; i <= end; ++i)
        if (f[i].cost_ms >= 0) costs[n++] = f[i].cost_ms;
    if (n > 0) {
        std::nth_element(costs.begin(), costs.begin() + static_cast<std::ptrdiff_t>(n / 2), costs.begin() + static_cast<std::ptrdiff_t>(n));
        r.cost_ms = costs[n / 2];
    }
    return r;
}

void FrameMeter::prop_changed(FrameMeterProp) {
    if (!attached()) return;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size FrameMeter::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, layout::Available, layout::Available) {
    return {width.value_or(meter_width), height.value_or(meter_height)};
}

void FrameMeter::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    auto& out = ctx.list;
    const ShownFrames& f = services().shown_frames();
    const FrameReading r = read_frames(f, services().renderer());
    // While frames are shown, one frame a moment after the last says the
    // page is at rest; the meter asks for no other.
    if (!r.idle) {
        if (timer_) services().cancel(timer_);
        timer_ = services().after(hanim::seconds(rest), [this, life = std::weak_ptr<int>(life_)] {
            if (life.expired()) return;
            timer_ = 0;
            services().invalidate_paint(node());
        });
    }
    const float w = ctx.size.width, h = ctx.size.height;
    out.fill_round_rect({0, 0, w, h}, CornerRadii::all(h / 2), render::Paint::solid(th.color("background").with_alpha(0.6f)));
    out.stroke_round_rect({0.5f, 0.5f, w - 1, h - 1}, CornerRadii::all(h / 2 - 0.5f), 1, render::Paint::solid(th.color("border").with_alpha(0.7f)));

    // Who draws: the GPU's name in the accent, the CPU's quieter; a dot that is lit while frames are drawn.
    const bool gpu = r.renderer != "cpu";
    const Color who = gpu ? th.color("accent") : th.color("text_muted");
    const float cy = h / 2;
    out.fill_round_rect({pad + 2, cy - 3, 6, 6}, CornerRadii::all(3),
                        render::Paint::solid(r.idle ? th.color("text_muted").with_alpha(0.45f) : th.color("success", Color::hex(0x34d399))));
    float x = pad + 14;
    device_.set_style("caption");
    device_.set_text(device_name(r.renderer));
    const Size ds = device_.measure(services(), who);
    device_.paint(ctx, {x, cy - ds.height / 2}, who);
    x += std::max(ds.width, 46.0f) + 8;

    // The last frames, one bar each, against the budget (the line half-way up).
    const double budget = std::max(1.0, props.budget_ms);
    const float bx = x, by = cy - bars_height / 2;
    out.fill_rect({bx, by + bars_height / 2, bar_step * bars - 0.5f, 1}, th.color("text_muted").with_alpha(0.35f));
    const std::size_t count = std::min(f.size(), bars);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t k = f.size() - count + i;
        // What a frame cost, against the budget; where the host does not
        // measure (the headless host's virtual frames) only that it was drawn.
        const double v = f[k].cost_ms;
        const bool measured = v >= 0;
        const float bh = measured ? std::max(1.5f, static_cast<float>(std::min(1.0, v / (2 * budget))) * bars_height) : bars_height * 0.3f;
        const Color c = !measured       ? th.color("text_muted")
                      : v <= budget / 2 ? th.color("success", Color::hex(0x34d399))
                      : v <= budget     ? th.color("warning", Color::hex(0xfbbf24))
                                        : th.color("danger", Color::hex(0xf87171));
        const float px = bx + bar_step * static_cast<float>(bars - count + i);
        out.fill_rect({px, by + bars_height - bh, bar_step - 0.5f, bh}, c.with_alpha(r.idle ? 0.4f : 0.9f));
    }
    x = bx + bar_step * bars + 10;

    // The rate and a frame's cost; at rest, none a second. (How many frames
    // the last motion took is not shown: work that finishes on other threads
    // — a wall's textures — lands on different frames on different machines,
    // the pixels it ends with the same; `reading` gives the count.)
    rate_.set_style("code");
    cost_.set_style("caption");
    if (r.idle) {
        rate_.set_text("idle");
        cost_.set_text("0 fps");
    } else {
        rate_.set_text(std::format("{:>3.0f} fps", std::min(r.fps, 999.0)));
        cost_.set_text(r.cost_ms ? std::format("{:.1f} ms", *r.cost_ms) : std::string());
    }
    const Color text = r.idle ? th.color("text_muted") : th.color("text");
    const Size rs = rate_.measure(services(), text);
    rate_.paint(ctx, {x, cy - rs.height / 2}, text);
    if (!cost_.text().empty()) {
        const Size cs = cost_.measure(services(), th.color("text_muted"));
        // Right-aligned, and never past the meter's edge (a longer label is left out).
        const float at = w - pad - 4 - cs.width;
        if (at >= x + rs.width + 6) cost_.paint(ctx, {at, cy - cs.height / 2}, th.color("text_muted"));
    }
}

void FrameMeter::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    // Its name only: the rate changes with every frame and with the machine,
    // the tree does not (an agent reads the numbers with `reading`).
    node.name = props.label.empty() ? std::string("Frame rate") : props.label;
}

ActResult FrameMeter::reading(ActContext&) {
    const FrameReading r = read_frames(services().shown_frames(), services().renderer());
    Value v;
    v.set("renderer", r.renderer).set("fps", r.fps).set("frames", static_cast<std::int64_t>(r.frames)).set("idle", r.idle);
    v.set("cost_ms", r.cost_ms ? Value(*r.cost_ms) : Value());
    return ActResult::done(std::move(v));
}

}  // namespace omui::site
