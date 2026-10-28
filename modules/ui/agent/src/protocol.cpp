module openmui.agent;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;
import openmui.trace;
import openmui.pipeline;

namespace omui::agent {

namespace {

constexpr std::array<std::string_view, 15> operations{"describe", "snapshot", "changes", "find", "act", "invoke", "wait", "capture",
                                                       "inspect", "stats", "profile", "logs", "debug", "pipeline", "clock"};

// A stage's configuration from a request's JSON object.
pipeline::Config config_of(const Value* v) {
    pipeline::Config c;
    if (!v || !v->map()) return c;
    for (const auto& [k, x] : *v->map()) {
        switch (x.type()) {
            case Value::Type::boolean: c.set(k, x.as_bool()); break;
            case Value::Type::integer: c.set(k, x.as_int()); break;
            case Value::Type::real: c.set(k, x.as_double()); break;
            case Value::Type::text: c.set(k, std::string(x.as_string())); break;
            default: break;
        }
    }
    return c;
}

Value value_of(const pipeline::ConfigValue& v) {
    return std::visit([](const auto& x) { return Value(x); }, v);
}

// What `pipeline` answers: the stages in order with their plugins, the last
// frame's time per stage, the plugins there are, and the description as TOML.
Value pipeline_value(Runtime& rt) {
    pipeline::Pipeline& p = rt.frame_pipeline();
    const pipeline::Description& d = p.description();
    Value v;
    v.set("name", d.name);
    Value::List stages;
    for (const pipeline::StageTime& t : p.timings()) {
        Value s;
        s.set("name", t.name).set("ms", t.ms);
        if (auto it = std::ranges::find(d.stages, t.name, &pipeline::StageDesc::name); it != d.stages.end()) {
            s.set("plugin", it->plugin);
            Value cfg = Value(Value::Map{});
            for (const auto& [k, x] : it->config.entries()) cfg.set(k, value_of(x));
            s.set("config", std::move(cfg));
        }
        stages.push_back(std::move(s));
    }
    v.set("stages", Value(std::move(stages)));
    Value::List plugins;
    for (const pipeline::PluginInfo& info : rt.stage_plugins().plugins()) {
        Value x;
        x.set("name", info.name).set("kind", info.kind).set("description", info.description);
        plugins.push_back(std::move(x));
    }
    v.set("plugins", Value(std::move(plugins)));
    v.set("graph", p.describe()).set("toml", pipeline::to_toml(d));
    return v;
}

Envelope invalid(std::string message, std::string_view hint = {}) {
    Envelope e;
    Value extra;
    if (!hint.empty()) extra.set("hint", hint);
    e.error(spec::error_code::invalid_argument, message, std::move(extra)).result(spec::exit_code::failure);
    return e;
}

std::string base64(std::span<const std::uint8_t> bytes) {
    static constexpr std::string_view table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const std::uint32_t v = (std::uint32_t{bytes[i]} << 16) | (std::uint32_t{bytes[i + 1]} << 8) | bytes[i + 2];
        for (int s : {18, 12, 6, 0}) out += table[(v >> s) & 63];
    }
    if (const std::size_t rest = bytes.size() - i; rest > 0) {
        std::uint32_t v = std::uint32_t{bytes[i]} << 16;
        if (rest == 2) v |= std::uint32_t{bytes[i + 1]} << 8;
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += rest == 2 ? table[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string_view text_of(const Value& req, std::string_view key) {
    const Value* v = req.get(key);
    return v ? v->as_string() : std::string_view{};
}

}  // namespace

std::vector<std::string> lines(const Envelope& envelope, const Value& re) {
    std::vector<std::string> out;
    for (const spec::Record& r : envelope.records()) {
        if (re.is_null()) {
            out.push_back(r.json());
            continue;
        }
        Value v;
        v.set("kind", spec::name(r.kind));
        if (const Value::Map* m = r.body.map())
            for (const auto& [k, x] : *m) v.set(k, x);
        v.set("re", re);
        out.push_back(v.json());
    }
    return out;
}

Protocol::Protocol(Runtime& runtime, ProtocolOptions options)
    : runtime_(runtime), tree_(runtime), options_(std::move(options)) {}

void Protocol::send(const Reply& reply, const Envelope& envelope, const Value& re) const {
    for (const std::string& l : lines(envelope, re)) reply(l);
}

void Protocol::request(std::string_view line, const Reply& reply) {
    auto req = parse_json(line);
    if (!req) {
        send(reply, invalid(std::format("the request is not JSON: {}", req.error().text()),
                            R"(one JSON object per line, e.g. {"op":"snapshot"})"),
             {});
        return;
    }
    if (!req->map()) {
        send(reply, invalid("a request is a JSON object", R"({"op":"snapshot"})"), {});
        return;
    }
    const Value re = req->get("re") ? *req->get("re") : Value{};
    bool deferred = false;
    Envelope e = handle(*req, re, reply, deferred);
    if (!deferred) send(reply, e, re);
}

Envelope Protocol::handle(const Value& req, const Value& re, const Reply& reply, bool& deferred) {
    const std::string_view op = text_of(req, "op");
    Envelope e;
    if (op == "describe") {
        Value::List ops;
        for (std::string_view o : operations) ops.emplace_back(o);
        Value d;
        d.set("interface", interface_name).set("spec", spec::version).set("app", options_.app).set("operations", Value(std::move(ops)));
        d.set("revision", static_cast<std::int64_t>(runtime_.semantic_revision()));
        d.set("capture", static_cast<bool>(options_.capture));
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
    if (op == "snapshot") {
        semantic::SnapshotOptions o;
        o.root = std::string(text_of(req, "root"));
        if (const Value* v = req.get("depth")) o.depth = static_cast<int>(v->as_int(-1));
        if (const Value* v = req.get("interactive")) o.interactive = v->as_bool();
        if (const Value* v = req.get("offset")) o.offset = static_cast<std::size_t>(std::max<std::int64_t>(0, v->as_int()));
        if (const Value* v = req.get("limit")) o.limit = static_cast<std::size_t>(std::max<std::int64_t>(0, v->as_int()));
        if (!o.root.empty() && !tree_.node(o.root)) {
            e.error(spec::error_code::not_found, std::format("no node '{}'", o.root)).result(spec::exit_code::failure);
            return e;
        }
        return e.data(tree_.snapshot(o).to_value()).result(spec::exit_code::success);
    }
    if (op == "changes") {
        return e.data(spec::to_value(tree_.changes())).result(spec::exit_code::success);
    }
    if (op == "find") {
        const Value* q = req.get("q");
        if (!q) return invalid("`find` needs a query `q`", R"({"op":"find","q":{"role":"button","name":"Save*"}})");
        auto query = semantic::Query::parse(*q);
        if (!query) return invalid(query.error());
        Value::List nodes;
        for (const spec::SemanticNode& n : tree_.find(*query)) nodes.push_back(spec::to_value(n));
        return e.data(Value{}.set("nodes", Value(std::move(nodes)))).result(spec::exit_code::success);
    }
    if (op == "act") {
        const std::string_view node = text_of(req, "node");
        const std::string_view action = text_of(req, "action");
        if (node.empty() || action.empty())
            return invalid("`act` needs `node` and `action`", R"({"op":"act","node":"main/save","action":"activate"})");
        Invocation call;
        if (const Value* a = req.get("args")) call.args = *a;
        call.answer = std::string(text_of(req, "answer"));
        const std::string_view actor = text_of(req, "actor");
        auto parsed = Actor::parse(actor.empty() ? std::string_view(options_.default_actor) : actor);
        if (!parsed) return invalid(std::format("`{}` is not an actor", actor), "kind:name[;via=kind:name], e.g. agent:planner;via=person:alice");
        call.actor = std::move(*parsed);
        call.remote = options_.remote;  // over the bus: whatever actor it names (S-10)
        return tree_.act(node, action, call);
    }
    if (op == "invoke") {
        // An application verb (S-1 as for a person's: an irreversible or
        // external one needs its answer).
        const std::string_view verb = text_of(req, "verb");
        if (verb.empty()) return invalid("`invoke` needs a `verb`", R"({"op":"invoke","verb":"notes.new","args":{"title":"Today"}})");
        Invocation call;
        if (const Value* a = req.get("args")) call.args = *a;
        call.answer = std::string(text_of(req, "answer"));
        const std::string_view actor = text_of(req, "actor");
        auto parsed = Actor::parse(actor.empty() ? std::string_view(options_.default_actor) : actor);
        if (!parsed) return invalid(std::format("`{}` is not an actor", actor), "kind:name[;via=kind:name], e.g. agent:planner;via=person:alice");
        call.actor = std::move(*parsed);
        call.remote = options_.remote;  // over the bus: whatever actor it names (S-10)
        return runtime_.verbs().invoke(verb, call);
    }
    if (op == "wait") {
        // The condition's fields sit in the request itself: q, absent, settled.
        Value fields = Value(Value::Map{});
        for (std::string_view k : {"q", "absent", "settled"})
            if (const Value* v = req.get(k)) fields.set(k, *v);
        auto condition = semantic::Condition::parse(fields);
        if (!condition) return invalid(condition.error(), R"({"op":"wait","q":{"id":"main/toast"},"settled":true,"timeout":5})");
        const double timeout = req.get("timeout") ? req.get("timeout")->as_double(options_.default_timeout) : options_.default_timeout;
        if (auto now = tree_.check(*condition); now.satisfied) {
            return e.data(Value{}.set("satisfied", true).set("revision", static_cast<std::int64_t>(tree_.revision())))
                       .result(spec::exit_code::success);
        }
        deferred = true;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(std::max(0.0, timeout)));
        waits_.push_back({std::move(*condition), re, deadline, reply});
        runtime_.request_frame();
        return e;
    }
    if (op == "capture") {
        if (!options_.capture) {
            e.error(spec::error_code::refused, "this host does not render (headless without a renderer)").result(spec::exit_code::failure);
            return e;
        }
        Rect region{0, 0, runtime_.size().width, runtime_.size().height};
        if (const std::string_view id = text_of(req, "node"); !id.empty()) {
            NodeId n = runtime_.find(id);
            if (!runtime_.valid(n)) {
                e.error(spec::error_code::not_found, std::format("no node '{}'", id)).result(spec::exit_code::failure);
                return e;
            }
            region = runtime_.bounds(n);
        }
        auto png = options_.capture(region);
        if (!png) {
            e.error(spec::error_code::internal, png.error()).result(spec::exit_code::failure);
            return e;
        }
        Value d;
        d.set("format", "png").set("bounds", Value(Value::List{Value(region.x), Value(region.y), Value(region.width), Value(region.height)}));
        d.set("base64", base64(*png));
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
    // ── development: inspection, statistics, profiling, logs, debug overlays ──
    if (op == "inspect") {
        const std::string_view id = text_of(req, "node");
        const NodeId n = id.empty() ? runtime_.root() : runtime_.reveal(id);
        if (!runtime_.valid(n)) {
            e.error(spec::error_code::not_found, std::format("no node '{}'", id)).result(spec::exit_code::failure);
            return e;
        }
        return e.data(runtime_.inspect(n)).result(spec::exit_code::success);
    }
    if (op == "stats") {
        const auto frames = runtime_.frame_history();
        Value d;
        d.set("frames", static_cast<std::int64_t>(frames.size()));
        if (!frames.empty()) {
            std::vector<double> totals;
            FrameTimings mean;
            for (const FrameStats& f : frames) {
                totals.push_back(f.timings.total);
                for (auto [m, x] : {std::pair{&mean.timers, f.timings.timers}, {&mean.input, f.timings.input}, {&mean.regions, f.timings.regions},
                                    {&mean.animations, f.timings.animations}, {&mean.layout, f.timings.layout},
                                    {&mean.bounds, f.timings.bounds}, {&mean.paint, f.timings.paint}, {&mean.total, f.timings.total}})
                    *m += x / static_cast<double>(frames.size());
            }
            std::ranges::sort(totals);
            auto pct = [&](double p) {
                const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(totals.size())));
                return totals[std::clamp<std::size_t>(rank, 1, totals.size()) - 1];
            };
            d.set("mean_ms", mean.total).set("p50_ms", pct(50)).set("p95_ms", pct(95)).set("p99_ms", pct(99)).set("max_ms", totals.back());
            Value phases;
            phases.set("timers", mean.timers).set("input", mean.input).set("regions", mean.regions).set("animations", mean.animations);
            phases.set("layout", mean.layout).set("bounds", mean.bounds).set("paint", mean.paint);
            d.set("phases_mean_ms", std::move(phases));
            const FrameStats& last = frames.back();
            Value l;
            l.set("nodes", static_cast<std::int64_t>(last.nodes)).set("laid_out", static_cast<std::int64_t>(last.laid_out));
            l.set("painted", static_cast<std::int64_t>(last.painted)).set("bindings", static_cast<std::int64_t>(last.bindings));
            l.set("animating", static_cast<std::int64_t>(last.animating));
            d.set("last", std::move(l));
            // "history": N — the last N frames, oldest first: total and the phases (a frame-time graph).
            if (const Value* h = req.get("history"); h && h->is_number()) {
                const auto want = static_cast<std::size_t>(std::clamp<std::int64_t>(h->as_int(0), 0, 240));
                Value::List recent;
                for (std::size_t i = frames.size() - std::min(want, frames.size()); i < frames.size(); ++i) {
                    const FrameTimings& ft = frames[i].timings;
                    Value f;
                    f.set("total", ft.total).set("layout", ft.layout).set("paint", ft.paint).set("animations", ft.animations);
                    f.set("other", ft.timers + ft.input + ft.regions + ft.bounds + ft.other);
                    recent.push_back(std::move(f));
                }
                d.set("history", Value(std::move(recent)));
            }
        }
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
    if (op == "profile") {
        const std::string_view what = text_of(req, "action");
        if (what == "start") {
            trace::start();
            return e.data(Value{}.set("recording", true)).result(spec::exit_code::success);
        }
        if (what == "stop") {
            const trace::Trace t = trace::stop();
            Value::List stats;
            for (const trace::Stat& st : t.summary()) {
                Value x;
                x.set("name", st.name).set("category", st.category).set("count", static_cast<std::int64_t>(st.count));
                x.set("total_ms", st.total_ms).set("mean_ms", st.mean_ms).set("p50_ms", st.p50_ms).set("p95_ms", st.p95_ms);
                x.set("p99_ms", st.p99_ms).set("max_ms", st.max_ms);
                stats.push_back(std::move(x));
            }
            Value d;
            d.set("summary", Value(std::move(stats))).set("events", static_cast<std::int64_t>(t.events.size()));
            d.set("dropped", static_cast<std::int64_t>(t.dropped));
            if (const Value* c = req.get("chrome"); c && c->as_bool()) d.set("chrome", t.chrome_json());
            return e.data(std::move(d)).result(spec::exit_code::success);
        }
        return invalid("`profile` takes action start or stop", R"({"op":"profile","action":"stop","chrome":true})");
    }
    if (op == "logs") {
        Level at_least = Level::trace;
        if (const std::string_view lv = text_of(req, "level"); !lv.empty()) {
            bool known = false;
            for (Level l : {Level::trace, Level::debug, Level::info, Level::warning, Level::error})
                if (name(l) == lv) {
                    at_least = l;
                    known = true;
                }
            if (!known) return invalid(std::format("`{}` is not a level", lv), "trace, debug, info, warning, error");
        }
        const auto limit = static_cast<std::size_t>(std::max<std::int64_t>(1, req.get("limit") ? req.get("limit")->as_int(100) : 100));
        const std::string_view category = text_of(req, "category");
        Value::List records;
        for (const LoggedRecord& r : recent_logs(10'000, at_least)) {
            if (!category.empty() && r.category != category) continue;
            records.push_back(parse_json(to_json(r)).value_or(Value{}));
        }
        if (records.size() > limit) records.erase(records.begin(), records.end() - static_cast<std::ptrdiff_t>(limit));
        return e.data(Value{}.set("records", Value(std::move(records)))).result(spec::exit_code::success);
    }
    if (op == "clock") {
        // The application's time (F4): pause, step, slow down, seek — the
        // animation inspector and time-travel debugging drive these.
        const std::string_view action = text_of(req, "action");
        auto number = [&](std::string_view key) -> std::optional<double> {
            const Value* v = req.get(key);
            if (!v || !v->is_number()) return std::nullopt;
            return v->as_double();
        };
        if (action.empty() || action == "show") {
        } else if (action == "pause") {
            runtime_.pause_time(true);
        } else if (action == "resume") {
            runtime_.pause_time(false);
        } else if (action == "rate") {
            const auto r = number("rate");
            if (!r || *r < 0) return invalid("`clock` rate needs a \"rate\" ≥ 0", R"({"op":"clock","action":"rate","rate":0.25})");
            runtime_.set_time_rate(*r);
        } else if (action == "step") {
            const auto dt = number("seconds");
            if (!dt) return invalid("`clock` step needs \"seconds\"", R"({"op":"clock","action":"step","seconds":0.016})");
            runtime_.step_time(seconds(*dt));
        } else if (action == "seek") {
            const auto t = number("seconds");
            if (!t) return invalid("`clock` seek needs \"seconds\"", R"({"op":"clock","action":"seek","seconds":1.5})");
            runtime_.seek_time(seconds(*t));
        } else if (action == "release") {
            runtime_.release_time();
        } else {
            return invalid(std::format("`clock` has no action `{}`", action), "actions: show, pause, resume, rate, step, seek, release");
        }
        runtime_.request_frame();
        const Runtime::TimeState ts = runtime_.time_state();
        Value d;
        d.set("host", ts.host.count()).set("now", ts.shown.count()).set("controlled", ts.controlled);
        d.set("paused", ts.paused).set("rate", ts.rate);
        d.set("pending", action == "step" || action == "seek");  // shown at the next frame
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
    if (op == "pipeline") {
        // The frame's pipeline (D-66): read it, or change one stage between frames.
        const std::string_view action = text_of(req, "action");
        std::expected<void, std::vector<pipeline::Problem>> changed;
        if (action.empty() || action == "show") {
        } else if (action == "replace") {
            changed = runtime_.frame_pipeline().replace(text_of(req, "stage"), text_of(req, "plugin"), config_of(req.get("config")),
                                                         runtime_.stage_plugins());
        } else if (action == "remove") {
            changed = runtime_.frame_pipeline().remove(text_of(req, "stage"), runtime_.stage_plugins());
        } else if (action == "insert") {
            pipeline::StageDesc s;
            s.name = std::string(text_of(req, "stage"));
            s.plugin = std::string(text_of(req, "plugin"));
            if (const Value* after = req.get("after"); after && after->list())
                for (const Value& a : *after->list()) s.after.emplace_back(a.as_string());
            s.config = config_of(req.get("config"));
            changed = runtime_.frame_pipeline().insert(std::move(s), runtime_.stage_plugins());
        } else {
            return invalid(std::format("`pipeline` has no action `{}`", action),
                           R"({"op":"pipeline","action":"replace","stage":"paint","plugin":"openmui.view.paint"} — actions: show, replace, insert, remove)");
        }
        if (!changed) {
            e.error(spec::error_code::invalid_argument, pipeline::to_text(changed.error())).result(spec::exit_code::failure);
            return e;
        }
        runtime_.request_frame();
        return e.data(pipeline_value(runtime_)).result(spec::exit_code::success);
    }
    if (op == "debug") {
        DebugOptions o = runtime_.debug();
        for (auto [k, field] : {std::pair{"bounds", &o.bounds}, {"repaints", &o.repaints}, {"inspector", &o.inspector}, {"hud", &o.hud}})
            if (const Value* v = req.get(k)) *field = v->as_bool();
        if (const Value* v = req.get("slow_frame_ms")) o.slow_frame_ms = v->as_double();
        if (const Value* v = req.get("log")) {
            if (auto r = set_log_filter(v->as_string()); !r) return invalid(r.error(), R"({"op":"debug","log":"layout=debug,*=info"})");
        }
        runtime_.set_debug(o);
        Value d;
        d.set("bounds", o.bounds).set("repaints", o.repaints).set("inspector", o.inspector).set("hud", o.hud);
        d.set("slow_frame_ms", o.slow_frame_ms);
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
    std::string known;
    for (std::string_view o : operations) known += known.empty() ? std::string(o) : std::format(", {}", o);
    return invalid(op.empty() ? std::string("a request names its operation in `op`") : std::format("no operation `{}`", op),
                   std::format("operations: {}", known));
}

void Protocol::poll() {
    if (waits_.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    std::vector<Wait> still;
    for (Wait& w : waits_) {
        const semantic::WaitState s = tree_.check(w.condition);
        Envelope e;
        if (s.satisfied) {
            e.data(Value{}.set("satisfied", true).set("revision", static_cast<std::int64_t>(tree_.revision())))
                .result(spec::exit_code::success);
        } else if (now >= w.deadline) {
            e.error(spec::error_code::timeout, std::format("the condition did not hold in time: {}", s.why))
                .result(spec::exit_code::not_ready);
        } else {
            still.push_back(std::move(w));
            continue;
        }
        send(w.reply, e, w.re);
    }
    waits_ = std::move(still);
    if (!waits_.empty()) runtime_.request_frame();
}

}  // namespace omui::agent
