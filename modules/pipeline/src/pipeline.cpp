module openmui.pipeline;

import std;
import openmui.trace;

namespace omui::pipeline {

// ── resources ─────────────────────────────────────────────────────────────

void Resources::set(std::string_view name, std::string_view type, void* value) {
    for (Slot& s : slots_)
        if (s.name == name) {
            s.type = type;
            s.value = value;
            return;
        }
    slots_.push_back({std::string(name), type, value});
}

void* Resources::find(std::string_view name, std::string_view type) const noexcept {
    for (const Slot& s : slots_)
        if (s.name == name) return s.type == type ? s.value : nullptr;
    return nullptr;
}

bool Resources::has(std::string_view name) const noexcept {
    return std::ranges::any_of(slots_, [&](const Slot& s) { return s.name == name && s.value; });
}

// ── configuration ─────────────────────────────────────────────────────────

void Config::set(std::string key, ConfigValue value) {
    for (auto& [k, v] : values_)
        if (k == key) {
            v = std::move(value);
            return;
        }
    values_.emplace_back(std::move(key), std::move(value));
}

const ConfigValue* Config::get(std::string_view key) const noexcept {
    for (const auto& [k, v] : values_)
        if (k == key) return &v;
    return nullptr;
}

bool Config::flag(std::string_view key, bool fallback) const noexcept {
    const ConfigValue* v = get(key);
    const bool* b = v ? std::get_if<bool>(v) : nullptr;
    return b ? *b : fallback;
}

std::int64_t Config::integer(std::string_view key, std::int64_t fallback) const noexcept {
    const ConfigValue* v = get(key);
    if (!v) return fallback;
    if (const auto* i = std::get_if<std::int64_t>(v)) return *i;
    if (const auto* d = std::get_if<double>(v)) return static_cast<std::int64_t>(*d);
    return fallback;
}

double Config::number(std::string_view key, double fallback) const noexcept {
    const ConfigValue* v = get(key);
    if (!v) return fallback;
    if (const auto* d = std::get_if<double>(v)) return *d;
    if (const auto* i = std::get_if<std::int64_t>(v)) return static_cast<double>(*i);
    return fallback;
}

std::string_view Config::text(std::string_view key, std::string_view fallback) const noexcept {
    const ConfigValue* v = get(key);
    const std::string* s = v ? std::get_if<std::string>(v) : nullptr;
    return s ? std::string_view(*s) : fallback;
}

// ── registry ──────────────────────────────────────────────────────────────

void Registry::add(PluginInfo info, Factory make) {
    for (Entry& e : entries_)
        if (e.info.name == info.name) {
            e = {std::move(info), std::move(make)};
            return;
        }
    entries_.push_back({std::move(info), std::move(make)});
}

const PluginInfo* Registry::info(std::string_view name) const noexcept {
    for (const Entry& e : entries_)
        if (e.info.name == name) return &e.info;
    return nullptr;
}

std::expected<std::unique_ptr<Stage>, std::string> Registry::make(std::string_view name, const Config& config) const {
    for (const Entry& e : entries_)
        if (e.info.name == name) {
            auto stage = e.make(config);
            if (stage && !*stage) return std::unexpected(std::format("plugin `{}` made no stage", name));
            return stage;
        }
    return std::unexpected(std::format("no plugin `{}`", name));
}

std::vector<PluginInfo> Registry::plugins() const {
    std::vector<PluginInfo> out;
    for (const Entry& e : entries_) out.push_back(e.info);
    return out;
}

// ── text ──────────────────────────────────────────────────────────────────

std::string to_text(std::span<const Problem> problems) {
    std::string out;
    for (const Problem& p : problems) {
        out += std::format("{}: {}\n", p.where, p.message);
        if (!p.hint.empty()) out += std::format("    hint: {}\n", p.hint);
    }
    return out;
}

namespace {

std::string toml_string(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + '"';
}

std::string toml_value(const ConfigValue& v) {
    return std::visit(
        [](const auto& x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::same_as<T, bool>) return x ? "true" : "false";
            else if constexpr (std::same_as<T, std::int64_t>) return std::format("{}", x);
            else if constexpr (std::same_as<T, double>) {
                std::string s = std::format("{}", x);
                if (s.find_first_of(".eEn") == std::string::npos) s += ".0";  // stays a float
                return s;
            } else return toml_string(x);
        },
        v);
}

}  // namespace

std::string to_toml(const Description& d) {
    std::string out = std::format("name = {}\n", toml_string(d.name));
    for (const StageDesc& s : d.stages) {
        out += std::format("\n[[stage]]\nname = {}\nplugin = {}\n", toml_string(s.name), toml_string(s.plugin));
        if (!s.after.empty()) {
            out += "after = [";
            for (std::size_t i = 0; i < s.after.size(); ++i) out += (i ? ", " : "") + toml_string(s.after[i]);
            out += "]\n";
        }
        if (!s.config.entries().empty()) {
            out += "config = { ";
            bool first = true;
            for (const auto& [k, v] : s.config.entries()) {
                out += std::format("{}{} = {}", first ? "" : ", ", k, toml_value(v));
                first = false;
            }
            out += " }\n";
        }
    }
    return out;
}

// ── pipelines ─────────────────────────────────────────────────────────────

struct Pipeline::Impl {
    Description                         description;
    std::vector<Port>                   inputs;
    std::vector<std::unique_ptr<Stage>> stages;    // in run order
    std::vector<std::string>            names;     // in run order (stable: span names point into them)
    std::vector<std::string>            plugins;
    std::vector<std::string>            categories;
    std::vector<std::vector<Port>>      writes;    // in run order: checked after each run
    std::vector<StageTime>              timings;
    std::string                         graph;     // describe()
    std::uint64_t                       runs = 0;
};

Pipeline::Pipeline(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Pipeline::Pipeline(Pipeline&&) noexcept = default;
Pipeline& Pipeline::operator=(Pipeline&&) noexcept = default;
Pipeline::~Pipeline() = default;

std::expected<Pipeline, std::vector<Problem>> Pipeline::build(const Description& d, const Registry& registry, std::span<const Port> inputs) {
    std::vector<Problem> problems;
    const std::size_t n = d.stages.size();
    auto where = [&](std::size_t i) { return std::format("pipeline `{}`, stage `{}`", d.name, d.stages[i].name); };

    // Names are unique.
    for (std::size_t i = 0; i < n; ++i) {
        if (d.stages[i].name.empty()) problems.push_back({std::format("pipeline `{}`, stage {}", d.name, i + 1), "a stage needs a name", ""});
        for (std::size_t j = 0; j < i; ++j)
            if (d.stages[j].name == d.stages[i].name)
                problems.push_back({where(i), "two stages have this name", "every stage of a pipeline has its own name"});
    }

    // The plugins make the stages.
    std::vector<std::unique_ptr<Stage>> made(n);
    std::vector<std::vector<Port>> reads(n), writes(n);
    for (std::size_t i = 0; i < n; ++i) {
        auto s = registry.make(d.stages[i].plugin, d.stages[i].config);
        if (!s) {
            std::string known;
            for (const PluginInfo& p : registry.plugins())
                if (p.kind == "stage") known += known.empty() ? p.name : ", " + p.name;
            problems.push_back({where(i), s.error(), known.empty() ? "" : "stage plugins: " + known});
            continue;
        }
        made[i] = std::move(*s);
        reads[i] = made[i]->reads();
        writes[i] = made[i]->writes();
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));

    // Every resource has one type, one producer (an input, or a stage that
    // writes it without reading it) and any number of updaters after it.
    struct Use {
        std::string                type;
        std::optional<std::size_t> producer;  // nullopt: an input (or nobody)
        bool                       input = false;
    };
    std::map<std::string, Use, std::less<>> uses;
    for (const Port& p : inputs) uses[p.name] = {p.type, std::nullopt, true};
    auto reads_it = [&](std::size_t i, std::string_view name) {
        return std::ranges::any_of(reads[i], [&](const Port& p) { return p.name == name; });
    };
    for (std::size_t i = 0; i < n; ++i)
        for (const Port& w : writes[i]) {
            auto [it, fresh] = uses.try_emplace(w.name, Use{w.type, std::nullopt, false});
            if (!fresh && it->second.type != w.type)
                problems.push_back({where(i), std::format("writes `{}` as {}, but it is {}", w.name, w.type, it->second.type), ""});
            if (reads_it(i, w.name)) continue;  // an updater
            if (it->second.input)
                problems.push_back({where(i), std::format("writes `{}`, which the caller provides", w.name),
                                    "read it too, to update it in place"});
            else if (it->second.producer)
                problems.push_back({where(i), std::format("writes `{}`, which stage `{}` already produces", w.name,
                                                          d.stages[*it->second.producer].name),
                                    "read it too, to update it after that stage"});
            else
                it->second.producer = i;
        }
    for (std::size_t i = 0; i < n; ++i)
        for (const Port& r : reads[i]) {
            auto it = uses.find(r.name);
            if (it == uses.end() || (!it->second.input && !it->second.producer)) {
                problems.push_back({where(i), std::format("reads `{}` ({}), which no stage produces and the caller does not provide", r.name, r.type),
                                    "add the stage that produces it, or provide it as an input"});
                continue;
            }
            if (it->second.type != r.type)
                problems.push_back({where(i), std::format("reads `{}` as {}, but it is {}", r.name, r.type, it->second.type), ""});
        }
    if (!problems.empty()) return std::unexpected(std::move(problems));

    // Edges, in declaration order per resource: the producer before everyone;
    // an updater after the stages declared before it that use the resource
    // (they see it as it was); a reader after the updaters declared before it.
    std::vector<std::set<std::size_t>> before(n);  // before[i]: stages that must run before i
    for (const auto& [name, use] : uses) {
        std::vector<std::size_t> users;
        for (std::size_t i = 0; i < n; ++i)
            if (reads_it(i, name) || std::ranges::any_of(writes[i], [&](const Port& p) { return p.name == name; })) users.push_back(i);
        for (std::size_t k = 0; k < users.size(); ++k) {
            const std::size_t i = users[k];
            if (use.producer && *use.producer == i) continue;  // the producer runs first, wherever it is declared
            if (use.producer) before[i].insert(*use.producer);
            const bool updates = reads_it(i, name) && std::ranges::any_of(writes[i], [&](const Port& p) { return p.name == name; });
            for (std::size_t m = 0; m < k; ++m) {
                const std::size_t j = users[m];
                if (use.producer && j == *use.producer) continue;
                const bool j_updates = reads_it(j, name) && std::ranges::any_of(writes[j], [&](const Port& p) { return p.name == name; });
                if (updates || j_updates) before[i].insert(j);
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i)
        for (const std::string& a : d.stages[i].after) {
            auto it = std::ranges::find(d.stages, a, &StageDesc::name);
            if (it == d.stages.end()) {
                problems.push_back({where(i), std::format("runs after `{}`, which is not a stage of this pipeline", a), ""});
                continue;
            }
            before[i].insert(static_cast<std::size_t>(it - d.stages.begin()));
        }
    if (!problems.empty()) return std::unexpected(std::move(problems));

    // Order: what must come first, then the order of declaration (Kahn's
    // algorithm taking the smallest ready index).
    std::vector<std::size_t> pending(n, 0);
    std::vector<std::vector<std::size_t>> after_me(n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j : before[i]) {
            ++pending[i];
            after_me[j].push_back(i);
        }
    std::set<std::size_t> ready;
    for (std::size_t i = 0; i < n; ++i)
        if (pending[i] == 0) ready.insert(i);
    std::vector<std::size_t> order;
    while (!ready.empty()) {
        const std::size_t i = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(i);
        for (std::size_t k : after_me[i])
            if (--pending[k] == 0) ready.insert(k);
    }
    if (order.size() != n) {
        // A cycle: walk back from a stuck stage until a stage repeats.
        std::size_t at = 0;
        while (pending[at] == 0) ++at;
        std::vector<std::size_t> path{at};
        for (;;) {
            std::size_t next = at;
            for (std::size_t j : before[at])
                if (pending[j] != 0) {
                    next = j;
                    break;
                }
            if (auto seen = std::ranges::find(path, next); seen != path.end()) {
                std::string cycle;
                for (auto it = seen; it != path.end(); ++it) cycle += d.stages[*it].name + " ← ";
                cycle += d.stages[next].name;
                problems.push_back({std::format("pipeline `{}`", d.name), "the stages wait for each other: " + cycle,
                                    "remove an `after`, or a read that makes one of them wait"});
                break;
            }
            path.push_back(next);
            at = next;
        }
        return std::unexpected(std::move(problems));
    }

    auto impl = std::make_unique<Impl>();
    impl->description = d;
    impl->inputs.assign(inputs.begin(), inputs.end());
    for (std::size_t i : order) {
        impl->stages.push_back(std::move(made[i]));
        impl->names.push_back(d.stages[i].name);
        impl->plugins.push_back(d.stages[i].plugin);
        const PluginInfo* info = registry.info(d.stages[i].plugin);
        impl->categories.push_back(info ? info->trace_category : "frame");
        impl->writes.push_back(writes[i]);
        impl->timings.push_back({d.stages[i].name, 0});
    }
    // What tools draw.
    std::string g = std::format("pipeline {}\n  order: ", d.name);
    for (std::size_t k = 0; k < order.size(); ++k) g += (k ? " → " : "") + d.stages[order[k]].name;
    g += "\n";
    for (std::size_t i : order) {
        g += std::format("  {} ({})\n", d.stages[i].name, d.stages[i].plugin);
        for (const Port& r : reads[i]) g += std::format("    reads  {}: {}\n", r.name, r.type);
        for (const Port& w : writes[i]) g += std::format("    writes {}: {}\n", w.name, w.type);
    }
    for (const Port& p : inputs) g += std::format("  input {}: {}\n", p.name, p.type);
    impl->graph = std::move(g);
    return Pipeline(std::move(impl));
}

std::expected<void, std::string> Pipeline::run(Resources& resources) {
    Impl& d = *impl_;
    Context ctx{resources, d.runs++};
    for (std::size_t i = 0; i < d.stages.size(); ++i) {
        trace::Span span(d.names[i].c_str(), d.categories[i].c_str());
        const auto start = std::chrono::steady_clock::now();
        auto r = d.stages[i]->run(ctx);
        d.timings[i].ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!r) return std::unexpected(std::format("stage `{}` ({}): {}", d.names[i], d.plugins[i], r.error()));
        for (const Port& w : d.writes[i])
            if (!resources.has(w.name))
                return std::unexpected(std::format("stage `{}` ({}) did not provide `{}`", d.names[i], d.plugins[i], w.name));
    }
    return {};
}

std::span<const StageTime> Pipeline::timings() const noexcept { return impl_->timings; }
std::vector<std::string> Pipeline::order() const { return impl_->names; }

bool Pipeline::produces(std::string_view resource) const noexcept {
    for (const auto& ws : impl_->writes)
        for (const Port& w : ws)
            if (w.name == resource) return true;
    return false;
}
const Description& Pipeline::description() const noexcept { return impl_->description; }
std::string Pipeline::describe() const { return impl_->graph; }

namespace {

std::vector<Problem> no_stage(const Description& d, std::string_view stage) {
    return {{std::format("pipeline `{}`", d.name), std::format("no stage `{}`", stage), ""}};
}

}  // namespace

std::expected<void, std::vector<Problem>> Pipeline::replace(std::string_view stage, std::string_view plugin, const Config& config,
                                                            const Registry& registry) {
    Description next = impl_->description;
    auto it = std::ranges::find(next.stages, stage, &StageDesc::name);
    if (it == next.stages.end()) return std::unexpected(no_stage(next, stage));
    it->plugin = std::string(plugin);
    it->config = config;
    auto built = build(next, registry, impl_->inputs);
    if (!built) return std::unexpected(std::move(built.error()));
    built->impl_->runs = impl_->runs;
    *this = std::move(*built);
    return {};
}

std::expected<void, std::vector<Problem>> Pipeline::insert(StageDesc stage, const Registry& registry) {
    // Declared right after the last stage it names in `after` (its place in
    // the declaration decides which readers see what it updates), else last.
    Description next = impl_->description;
    std::size_t at = 0;
    bool placed = false;
    for (std::size_t i = 0; i < next.stages.size(); ++i)
        if (std::ranges::find(stage.after, next.stages[i].name) != stage.after.end()) {
            at = i + 1;
            placed = true;
        }
    next.stages.insert(placed ? next.stages.begin() + static_cast<std::ptrdiff_t>(at) : next.stages.end(), std::move(stage));
    auto built = build(next, registry, impl_->inputs);
    if (!built) return std::unexpected(std::move(built.error()));
    built->impl_->runs = impl_->runs;
    *this = std::move(*built);
    return {};
}

std::expected<void, std::vector<Problem>> Pipeline::remove(std::string_view stage, const Registry& registry) {
    Description next = impl_->description;
    auto it = std::ranges::find(next.stages, stage, &StageDesc::name);
    if (it == next.stages.end()) return std::unexpected(no_stage(next, stage));
    next.stages.erase(it);
    for (StageDesc& s : next.stages) std::erase(s.after, std::string(stage));
    auto built = build(next, registry, impl_->inputs);
    if (!built) return std::unexpected(std::move(built.error()));
    built->impl_->runs = impl_->runs;
    *this = std::move(*built);
    return {};
}

}  // namespace omui::pipeline
