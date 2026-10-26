module openmui.agent;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;

namespace omui::agent {

namespace {

constexpr std::array<std::string_view, 7> operations{"describe", "snapshot", "changes", "find", "act", "wait", "capture"};

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
        return tree_.act(node, action, call);
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
