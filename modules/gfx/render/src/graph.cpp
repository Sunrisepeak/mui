module openmui.render;

import std;
import openmui.rhi;
import openmui.trace;

namespace omui::render {

namespace {

bool contains(std::span<const rhi::Texture> list, rhi::Texture t) { return std::ranges::find(list, t) != list.end(); }

}  // namespace

rhi::Result<rhi::Texture> Graph::transient(const TextureNeed& need, std::string_view label) {
    auto t = pool_->acquire(need, label);
    if (t) transients_.push_back({*t, std::string(label)});
    return t;
}

void Graph::pass(PassDesc desc, std::function<void(rhi::CommandList&)> record) {
    passes_.push_back({std::move(desc), std::move(record)});
}

const Graph::Made* Graph::made(rhi::Texture t) const noexcept {
    for (const Made& m : transients_)
        if (m.texture == t) return &m;
    return nullptr;
}

bool Graph::transient(rhi::Texture t) const noexcept { return made(t) != nullptr; }

std::expected<GraphPlan, std::vector<GraphProblem>> Graph::plan(std::span<const rhi::Texture> outputs) const {
    std::vector<GraphProblem> problems;
    auto label = [&](std::size_t i) {
        const PassDesc& d = passes_[i].desc;
        return d.label.empty() ? std::string(d.name) : std::format("{} ({})", d.name, d.label);
    };
    // A transient texture is written before it is read.
    std::vector<rhi::Texture> written;
    for (std::size_t i = 0; i < passes_.size(); ++i) {
        for (rhi::Texture r : passes_[i].desc.reads)
            if (const Made* m = made(r); m && !contains(written, r))
                problems.push_back({i, std::format("{} reads `{}` before any pass writes it", label(i), m->label)});
        for (rhi::Texture w : passes_[i].desc.writes) {
            if (!w) problems.push_back({i, std::format("{} writes a texture that does not exist", label(i))});
            written.push_back(w);
        }
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    // From the last pass back: one runs when what it writes is an output,
    // lies outside the graph, or is read by a pass that runs after it.
    std::vector<rhi::Texture> needed(outputs.begin(), outputs.end());
    std::vector<bool>         runs(passes_.size(), false);
    for (std::size_t k = passes_.size(); k-- > 0;) {
        const PassDesc& d = passes_[k].desc;
        const bool live = d.writes.empty() || std::ranges::any_of(d.writes, [&](rhi::Texture w) { return !transient(w) || contains(needed, w); });
        if (!live) continue;
        runs[k] = true;
        for (rhi::Texture r : d.reads)
            if (!contains(needed, r)) needed.push_back(r);
    }
    GraphPlan plan;
    for (std::size_t i = 0; i < passes_.size(); ++i) (runs[i] ? plan.run : plan.culled).push_back(i);
    return plan;
}

std::expected<GraphReport, std::vector<GraphProblem>> Graph::record(rhi::CommandList& list, std::span<const rhi::Texture> outputs) {
    auto p = plan(outputs);
    if (!p) return std::unexpected(std::move(p.error()));
    for (std::size_t i : p->run) {
        trace::Span span(passes_[i].desc.name, "render");
        if (passes_[i].record) passes_[i].record(list);
    }
    return GraphReport{p->run.size(), p->culled.size(), transients_.size()};
}

void Graph::clear() {
    passes_.clear();
    transients_.clear();
}

}  // namespace omui::render
