// openmui.h's functions over the runtime, view documents and the bus
// interface. Every function checks its arguments, never throws (noexcept:
// a failure that is a bug stops the program rather than unwinding through C),
// and reports through its result and, where it has one, its sink.
module openmui.abi;

import std;
import openmui.core;
import openmui.view;
import openmui.viewdoc;
import openmui.semantic;
import openmui.agent;
import openmui.render;
import openmui.raster;
import openmui.text;
import openmui.trace;
import :c;
import :api;

namespace omui::abi {

namespace {

constexpr Log log{"abi"};

WindowRunner& window_runner() {
    static WindowRunner runner;
    return runner;
}

std::string_view text(OmuiString s) noexcept { return s.data ? std::string_view(s.data, s.size) : std::string_view(); }

void send(PFN_omuiSink sink, void* user, std::string_view data) {
    if (sink) sink(user, data.data(), data.size());
}

std::expected<Value, std::string> value_of(const OmuiValue* v) {
    if (!v) return Value{};
    switch (v->type) {
        case OMUI_VALUE_TYPE_NULL: return Value{};
        case OMUI_VALUE_TYPE_BOOL: return Value(v->b != 0);
        case OMUI_VALUE_TYPE_INT: return Value(v->i);
        case OMUI_VALUE_TYPE_DOUBLE: return Value(v->d);
        case OMUI_VALUE_TYPE_STRING: return Value(text(v->s));
        case OMUI_VALUE_TYPE_JSON: {
            auto parsed = parse_json(text(v->s));
            if (!parsed) return std::unexpected(parsed.error().text());
            return std::move(*parsed);
        }
        default: return std::unexpected(std::format("{} is not a value type", static_cast<int>(v->type)));
    }
}

std::expected<Value, std::string> json_or_null(OmuiString s) {
    if (text(s).empty()) return Value{};
    auto v = parse_json(text(s));
    if (!v) return std::unexpected(v.error().text());
    return std::move(*v);
}

// An envelope's exit code and error code, as a result.
OmuiResult result_of(int exit_code, std::string_view error) {
    if (exit_code == spec::exit_code::success) return OMUI_SUCCESS;
    if (exit_code == spec::exit_code::needs_answer || error == spec::error_code::needs_answer) return OMUI_NEEDS_ANSWER;
    if (exit_code == spec::exit_code::not_ready || error == spec::error_code::timeout) return OMUI_NOT_READY;
    if (error == spec::error_code::not_found) return OMUI_ERROR_NOT_FOUND;
    if (error == spec::error_code::invalid_argument) return OMUI_ERROR_INVALID_ARGUMENT;
    if (error == spec::error_code::stale) return OMUI_ERROR_STALE_HANDLE;
    if (error == spec::error_code::refused) return OMUI_ERROR_REFUSED;
    return OMUI_ERROR_INTERNAL;
}

std::string error_code_of(const Envelope& e) {
    for (const spec::Record& r : e.records())
        if (r.kind == spec::RecordKind::error)
            if (const Value* c = r.body.get("code")) return std::string(c->as_string());
    return {};
}

}  // namespace

// ── the runtime behind a handle ──────────────────────────────────────────────

struct OmuiRuntime_T {
    std::string                                       app_id;
    bool                                              headless = true;
    std::unique_ptr<typeset::FontCollection>          fonts;
    std::unique_ptr<Runtime>                          owned;    // nothing when attached to a host's runtime
    Runtime*                                          rt = nullptr;
    std::shared_ptr<bool>                             alive = std::make_shared<bool>(true);
    std::function<void()>                             wake;  // the host's loop, when it sleeps
    std::unique_ptr<typeset::GlyphCache>              glyphs;
    std::unique_ptr<agent::Protocol>                  protocol;
    render::CpuRenderer                               renderer;
    raster::Surface                                   surface;
    render::DamageTracker                             damage;
    bool                                              have_pixels = false;
    std::map<std::string, viewdoc::View, std::less<>> views;

    std::map<OmuiSignal, Signal<Value>> signals;
    OmuiSignal                          next_signal = 1;
    std::map<std::string, Signal<Value>, std::less<>> shared;  // by the name documents declare

    struct Subscription {
        std::uint64_t token = 0;  // a node's event
        Watch         watch;      // a signal's changes
    };
    std::map<OmuiSubscription, Subscription> subscriptions;
    OmuiSubscription                         next_subscription = 1;

    // Coalesced events (high-frequency): the last arguments of a frame, delivered after it.
    struct Pending {
        PFN_omuiEvent fn;
        void*         user;
        OmuiNode      node;
        std::string   event;
        Value         args;
    };
    std::map<OmuiSubscription, Pending> pending;

    OmuiSignal handle_of(const Signal<Value>& s) {
        for (const auto& [h, x] : signals)
            if (x.same_as(s)) return h;
        const OmuiSignal h = next_signal++;
        signals.emplace(h, s);
        return h;
    }

    NodeId node(OmuiNode n) const { return NodeId::from_bits(n); }

    void flush_events() {
        auto ready = std::move(pending);
        pending.clear();
        for (auto& [sub, p] : ready) {
            const std::string args = p.args.json();
            p.fn(p.user, p.node, OmuiString{p.event.data(), p.event.size()}, OmuiString{args.data(), args.size()});
        }
    }

    void after_frame() {
        flush_events();
        if (protocol) protocol->poll();
    }

    // The bus interface in-process: one request, its records to the sink.
    OmuiResult request(Value req, PFN_omuiSink sink, void* user, bool run_frames, std::chrono::milliseconds limit = {}) {
        int code = -1;
        std::string error;
        auto reply = [&](std::string_view line) {
            send(sink, user, line);
            send(sink, user, "\n");
            if (auto v = parse_json(line)) {
                const std::string_view kind = v->get("kind") ? v->get("kind")->as_string() : "";
                if (kind == "result") code = static_cast<int>(v->get("exitCode") ? v->get("exitCode")->as_int(1) : 1);
                if (kind == "error" && error.empty() && v->get("code")) error = std::string(v->get("code")->as_string());
            }
        };
        protocol->request(req.json(), reply);
        if (code < 0 && run_frames) {
            // A wait: frames run (a headless runtime's clock advances) until it answers.
            const auto deadline = std::chrono::steady_clock::now() + limit + std::chrono::seconds(1);
            while (code < 0 && std::chrono::steady_clock::now() < deadline) {
                FrameInput in;
                in.time = rt->now() + seconds(1.0 / 60.0);
                (void)rt->frame(in);
                after_frame();
            }
        }
        if (code < 0) return OMUI_ERROR_INTERNAL;
        return result_of(code, error);
    }
};

void set_window_runner(WindowRunner runner) { window_runner() = std::move(runner); }

Runtime* runtime_of(OmuiRuntime rt) noexcept { return rt ? rt->rt : nullptr; }

OmuiRuntime attach(Runtime& runtime, std::string app_id) {
    auto r = std::make_unique<OmuiRuntime_T>();
    r->app_id = app_id.empty() ? std::string("openmui-app") : std::move(app_id);
    r->headless = false;
    r->rt = &runtime;
    r->glyphs = std::make_unique<typeset::GlyphCache>(runtime.fonts());
    agent::ProtocolOptions po;
    po.app = r->app_id;
    po.default_actor = "script:binding";
    r->protocol = std::make_unique<agent::Protocol>(runtime, std::move(po));
    return r.release();
}

void set_wake(OmuiRuntime rt, std::function<void()> wake) {
    if (rt) rt->wake = std::move(wake);
}

void detach(OmuiRuntime rt) {
    if (!rt) return;
    (void)omuiDestroyRuntime(rt);
}

extern "C" {

OmuiResult omuiCreateRuntime(const OmuiRuntimeCreateInfo* info, OmuiRuntime* out) noexcept {
    if (!info || !out || info->type != OMUI_STRUCTURE_TYPE_RUNTIME_CREATE_INFO) return OMUI_ERROR_INVALID_ARGUMENT;
    *out = nullptr;
    const std::uint32_t major = info->spec_version >> 16, minor = info->spec_version & 0xFFFF;
    if (major != OMUI_SPEC_VERSION_MAJOR || minor > OMUI_SPEC_VERSION_MINOR) return OMUI_ERROR_INCOMPATIBLE_VERSION;
    if (info->extension_count > 0) return OMUI_ERROR_EXTENSION_NOT_PRESENT;  // none are provided yet
    const std::string_view profile = text(info->profile);
    if (!profile.empty() && profile != "full") {
        log.warn("the profile `{}` is not one this runtime provides (full)", profile);
        return OMUI_ERROR_EXTENSION_NOT_PRESENT;
    }
    auto r = std::make_unique<OmuiRuntime_T>();
    r->app_id = text(info->app_id).empty() ? std::string("openmui-app") : std::string(text(info->app_id));
    r->headless = info->headless != 0;
    r->fonts = std::make_unique<typeset::FontCollection>();
    if (auto f = r->fonts->add_defaults(); !f) log.warn("the default fonts did not load: {}", f.error());
    RuntimeOptions o;
    o.fonts = r->fonts.get();
    o.clock = anim::Clock::virtual_time();
    r->owned = std::make_unique<Runtime>(std::move(o));
    r->rt = r->owned.get();
    r->glyphs = std::make_unique<typeset::GlyphCache>(*r->fonts);
    agent::ProtocolOptions po;
    po.app = r->app_id;
    po.default_actor = "script:binding";
    r->protocol = std::make_unique<agent::Protocol>(*r->rt, std::move(po));
    *out = r.release();
    return OMUI_SUCCESS;
}

OmuiResult omuiDestroyRuntime(OmuiRuntime rt) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    // Views and subscriptions go before the runtime they refer to.
    rt->subscriptions.clear();
    rt->views.clear();
    rt->protocol.reset();
    *rt->alive = false;
    rt->owned.reset();
    delete rt;
    return OMUI_SUCCESS;
}

OmuiResult omuiEnumerateExtensions(OmuiRuntime rt, std::uint32_t* count, OmuiExtensionProperties* props) noexcept {
    if (!rt || !count) return OMUI_ERROR_INVALID_ARGUMENT;
    (void)props;
    *count = 0;
    return OMUI_SUCCESS;
}

OmuiResult omuiMount(OmuiRuntime rt, OmuiString slot, OmuiString view_json) noexcept {
    if (!rt || text(slot).empty()) return OMUI_ERROR_INVALID_ARGUMENT;
    const std::string name(text(slot));
    if (auto it = rt->views.find(name); it != rt->views.end()) {
        // The same slot again: reconciled in place, state kept.
        auto r = it->second.reload(text(view_json));
        if (!r) {
            for (const auto& d : r.error()) log.warn("{}", d.text(name));
            return OMUI_ERROR_INVALID_ARGUMENT;
        }
        return OMUI_SUCCESS;
    }
    OmuiRuntime_T* self = rt;
    viewdoc::LoadOptions o{.file = name, .translate = {}, .provide = [self](std::string_view n) -> std::optional<Signal<Value>> {
                               auto s = self->shared.find(n);
                               if (s == self->shared.end()) return std::nullopt;
                               return s->second;
                           }};
    auto v = viewdoc::View::load(*rt->rt, text(view_json), std::move(o));
    if (!v) {
        for (const auto& d : v.error()) log.warn("{}", d.text(name));
        return OMUI_ERROR_INVALID_ARGUMENT;
    }
    // A view's own events reach the binding at the slot's root node (omuiOn).
    v->on_any([self, name](std::string_view event, const Value& args) {
        if (const NodeId root = self->rt->find(name)) self->rt->emit(root, event, args);
    });
    auto [it, _] = rt->views.insert_or_assign(name, std::move(*v));
    it->second.mount(*rt->rt, name);
    return OMUI_SUCCESS;
}

OmuiResult omuiUnmount(OmuiRuntime rt, OmuiString slot) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    auto it = rt->views.find(text(slot));
    if (it == rt->views.end()) return OMUI_ERROR_NOT_FOUND;
    rt->rt->unmount(text(slot));
    rt->views.erase(it);
    return OMUI_SUCCESS;
}

OmuiResult omuiFind(OmuiRuntime rt, OmuiString selector, OmuiNode* out) noexcept {
    if (!rt || !out) return OMUI_ERROR_INVALID_ARGUMENT;
    *out = 0;
    const std::string_view s = text(selector);
    std::string path(s);
    if (s.starts_with('{')) {
        auto q = parse_json(s);
        if (!q) return OMUI_ERROR_INVALID_ARGUMENT;
        auto query = semantic::Query::parse(*q);
        if (!query) return OMUI_ERROR_INVALID_ARGUMENT;
        query->limit = 1;
        semantic::Tree tree(*rt->rt);
        auto found = tree.find(*query);
        if (found.empty()) return OMUI_ERROR_NOT_FOUND;
        path = found.front().id;
    }
    NodeId n = rt->rt->find(path);
    if (!n) n = rt->rt->reveal(path);  // a virtualised list's row (S-7)
    if (!n) return OMUI_ERROR_NOT_FOUND;
    *out = n.bits();
    return OMUI_SUCCESS;
}

OmuiResult omuiGet(OmuiRuntime rt, OmuiNode node, OmuiString prop, PFN_omuiSink sink, void* user) noexcept {
    if (!rt || text(prop).empty()) return OMUI_ERROR_INVALID_ARGUMENT;
    const NodeId n = rt->node(node);
    if (!rt->rt->valid(n)) return OMUI_ERROR_STALE_HANDLE;
    send(sink, user, rt->rt->get(n, text(prop)).json());
    return OMUI_SUCCESS;
}

OmuiResult omuiSet(OmuiRuntime rt, OmuiNode node, const OmuiProp* props, std::uint32_t count) noexcept {
    if (!rt || (count && !props)) return OMUI_ERROR_INVALID_ARGUMENT;
    const NodeId n = rt->node(node);
    if (!rt->rt->valid(n)) return OMUI_ERROR_STALE_HANDLE;
    std::vector<std::pair<std::string, Value>> batch_of;
    for (std::uint32_t i = 0; i < count; ++i) {
        auto v = value_of(&props[i].value);
        if (!v || text(props[i].name).empty()) return OMUI_ERROR_INVALID_ARGUMENT;
        batch_of.emplace_back(std::string(text(props[i].name)), std::move(*v));
    }
    // One batch, at the next frame boundary.
    Runtime* r = rt->rt;
    r->queue().post([r, n, values = std::move(batch_of)] {
        if (!r->valid(n)) return;
        batch([&] {
            for (const auto& [k, v] : values) r->set(n, k, v);
        });
    });
    return OMUI_SUCCESS;
}

OmuiResult omuiOn(OmuiRuntime rt, OmuiNode node, OmuiString event, PFN_omuiEvent fn, void* user, OmuiSubscription* out) noexcept {
    if (!rt || !fn || !out || text(event).empty()) return OMUI_ERROR_INVALID_ARGUMENT;
    const NodeId n = rt->node(node);
    if (!rt->rt->valid(n)) return OMUI_ERROR_STALE_HANDLE;
    const std::string name(text(event));
    const spec::ContractInfo* contract = rt->rt->type(n).contract;
    const spec::EventInfo* info = contract ? contract->event(name) : nullptr;
    const bool coalesce = (info && info->coalesce_per_frame) || name == "pointer_move" || name == "wheel";
    const OmuiSubscription sub = rt->next_subscription++;
    OmuiRuntime_T* self = rt;
    const std::uint64_t token = rt->rt->on(n, name, [self, sub, fn, user, node, name, coalesce](const Value& args) {
        if (coalesce) {
            // The last of a frame's, delivered at the next frame boundary.
            if (self->pending.empty()) {
                std::weak_ptr<bool> alive = self->alive;
                self->rt->queue().post([self, alive] {
                    if (auto a = alive.lock(); a && *a) self->flush_events();
                });
            }
            self->pending.insert_or_assign(sub, OmuiRuntime_T::Pending{fn, user, node, name, args});
            return;
        }
        const std::string json = args.json();
        fn(user, node, OmuiString{name.data(), name.size()}, OmuiString{json.data(), json.size()});
    });
    rt->subscriptions[sub].token = token;
    *out = sub;
    return OMUI_SUCCESS;
}

OmuiResult omuiOff(OmuiRuntime rt, OmuiSubscription sub) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    auto it = rt->subscriptions.find(sub);
    if (it == rt->subscriptions.end()) return OMUI_ERROR_NOT_FOUND;
    if (it->second.token) rt->rt->off(it->second.token);
    rt->pending.erase(sub);
    rt->subscriptions.erase(it);
    return OMUI_SUCCESS;
}

OmuiResult omuiSignalCreate(OmuiRuntime rt, const OmuiValue* initial, OmuiSignal* out) noexcept {
    if (!rt || !out) return OMUI_ERROR_INVALID_ARGUMENT;
    auto v = value_of(initial);
    if (!v) return OMUI_ERROR_INVALID_ARGUMENT;
    *out = rt->handle_of(Signal<Value>(std::move(*v)));
    return OMUI_SUCCESS;
}

OmuiResult omuiSignalSet(OmuiRuntime rt, OmuiSignal signal, const OmuiValue* value) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    auto it = rt->signals.find(signal);
    if (it == rt->signals.end()) return OMUI_ERROR_NOT_FOUND;
    auto v = value_of(value);
    if (!v) return OMUI_ERROR_INVALID_ARGUMENT;
    it->second.set(std::move(*v));
    return OMUI_SUCCESS;
}

OmuiResult omuiSignalGet(OmuiRuntime rt, OmuiSignal signal, PFN_omuiSink sink, void* user) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    auto it = rt->signals.find(signal);
    if (it == rt->signals.end()) return OMUI_ERROR_NOT_FOUND;
    send(sink, user, it->second.peek().json());
    return OMUI_SUCCESS;
}

OmuiResult omuiBind(OmuiRuntime rt, OmuiNode node, OmuiString prop, OmuiSignal signal) noexcept {
    if (!rt || text(prop).empty()) return OMUI_ERROR_INVALID_ARGUMENT;
    const NodeId n = rt->node(node);
    if (!rt->rt->valid(n)) return OMUI_ERROR_STALE_HANDLE;
    auto it = rt->signals.find(signal);
    if (it == rt->signals.end()) return OMUI_ERROR_NOT_FOUND;
    Signal<Value> s = it->second;
    rt->rt->bind(n, std::string(text(prop)), Source{std::nullopt, [s] { return s.get(); }, [s](const Value& v) { s.set(v); }});
    return OMUI_SUCCESS;
}

OmuiResult omuiRegisterVerb(OmuiRuntime rt, const OmuiVerbDesc* desc, PFN_omuiVerb fn, void* user) noexcept {
    if (!rt || !desc || !fn || desc->type != OMUI_STRUCTURE_TYPE_VERB_DESC) return OMUI_ERROR_INVALID_ARGUMENT;
    VerbDesc d;
    d.name = std::string(text(desc->name));
    d.summary = std::string(text(desc->summary));
    if (desc->effect < 0 || desc->effect > static_cast<std::int32_t>(Effect::external)) return OMUI_ERROR_INVALID_ARGUMENT;
    d.effect = static_cast<Effect>(desc->effect);
    d.answer = std::string(text(desc->answer));
    auto added = rt->rt->verbs().add(std::move(d), [fn, user](const Value& args) -> VerbResult {
        const std::string in = args.json();
        std::string out;
        const OmuiResult r = fn(
            user, OmuiString{in.data(), in.size()}, [](void* u, const char* data, std::size_t size) { static_cast<std::string*>(u)->append(data, size); }, &out);
        if (r != OMUI_SUCCESS) return std::unexpected(out.empty() ? std::format("the verb failed ({})", static_cast<int>(r)) : out);
        if (out.empty()) return Value{};
        if (auto v = parse_json(out)) return std::move(*v);
        return Value(out);
    });
    if (!added) {
        log.warn("{}", added.error());
        return OMUI_ERROR_INVALID_ARGUMENT;
    }
    return OMUI_SUCCESS;
}

OmuiResult omuiInvoke(OmuiRuntime rt, OmuiString verb, OmuiString args_json, PFN_omuiSink sink, void* user) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    auto args = json_or_null(args_json);
    if (!args) return OMUI_ERROR_INVALID_ARGUMENT;
    Invocation call;
    call.args = std::move(*args);
    call.actor = {"script", "binding", {}};
    const Envelope e = rt->rt->verbs().invoke(text(verb), call);
    send(sink, user, e.ndjson());
    return result_of(e.exit_code().value_or(1), error_code_of(e));
}

OmuiResult omuiFrame(OmuiRuntime rt, const OmuiFrameInput* in, OmuiFrameOutput* out) noexcept {
    if (!rt || !in || in->type != OMUI_STRUCTURE_TYPE_FRAME_INPUT) return OMUI_ERROR_INVALID_ARGUMENT;
    if (out && out->type != OMUI_STRUCTURE_TYPE_FRAME_OUTPUT) return OMUI_ERROR_INVALID_ARGUMENT;
    trace::Span span("abi.frame", "abi");
    Runtime& r = *rt->rt;
    const float scale = in->scale > 0 ? static_cast<float>(in->scale) : r.scale();
    if (in->width && in->height) {
        const Size logical{static_cast<float>(in->width) / scale, static_cast<float>(in->height) / scale};
        if (logical != r.size() || scale != r.scale()) r.resize(logical, scale);
    }
    FrameInput fi;
    fi.time = seconds(in->time);
    if (!text(in->events_json).empty()) {
        auto events = parse_json(text(in->events_json));
        if (!events || !events->list()) return OMUI_ERROR_INVALID_ARGUMENT;
        for (const Value& e : *events->list()) {
            auto ev = event_from_value(e);
            if (!ev) return OMUI_ERROR_INVALID_ARGUMENT;
            fi.events.push_back(std::move(*ev));
        }
    }
    const FrameOutput fo = r.frame(fi);
    rt->after_frame();
    if (!out) return OMUI_SUCCESS;
    out->revision = fo.revision;
    out->animating = fo.animating ? 1u : 0u;
    if (out->pixels) {
        // The frame's pixels, premultiplied BGRA8 rows of `stride` bytes.
        const int w = static_cast<int>(in->width), h = static_cast<int>(in->height);
        if (w <= 0 || h <= 0 || out->stride < static_cast<std::uint32_t>(w) * 4) return OMUI_ERROR_INVALID_ARGUMENT;
        if (rt->surface.width() != w || rt->surface.height() != h) {
            rt->surface = raster::Surface(w, h);
            rt->have_pixels = false;
        }
        const render::Resources resources{&r.images(), rt->glyphs.get()};
        if (!rt->have_pixels) rt->damage.invalidate();
        const auto rows = rt->damage.update(r.display_list(), w, h, scale, resources);
        if (!rows.empty()) {
            render::CpuOptions o;
            o.scale = scale;
            o.rows = rows;
            (void)rt->renderer.render(r.display_list(), rt->surface, resources, o);
        }
        rt->have_pixels = true;
        auto* dst = static_cast<std::uint8_t*>(out->pixels);
        for (int y = 0; y < h; ++y) {
            const raster::Rgba8* src = rt->surface.row(y);
            std::uint8_t* row = dst + static_cast<std::size_t>(y) * out->stride;
            for (int x = 0; x < w; ++x) {
                row[x * 4 + 0] = src[x].b;
                row[x * 4 + 1] = src[x].g;
                row[x * 4 + 2] = src[x].r;
                row[x * 4 + 3] = src[x].a;
            }
        }
    }
    return OMUI_SUCCESS;
}

OmuiResult omuiSnapshot(OmuiRuntime rt, OmuiString query_json, PFN_omuiSink sink, void* user) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    auto q = json_or_null(query_json);
    if (!q || !(q->is_null() || q->map())) return OMUI_ERROR_INVALID_ARGUMENT;
    Value req = q->is_null() ? Value(Value::Map{}) : *q;
    req.set("op", "snapshot");
    return rt->request(std::move(req), sink, user, false);
}

OmuiResult omuiAct(OmuiRuntime rt, OmuiString request_json, PFN_omuiSink sink, void* user) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    auto req = json_or_null(request_json);
    if (!req || !req->map()) return OMUI_ERROR_INVALID_ARGUMENT;
    req->set("op", "act");
    return rt->request(std::move(*req), sink, user, false);
}

OmuiResult omuiWait(OmuiRuntime rt, OmuiString condition_json, std::uint32_t timeout_ms, PFN_omuiSink sink, void* user) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    auto req = json_or_null(condition_json);
    if (!req || !req->map()) return OMUI_ERROR_INVALID_ARGUMENT;
    req->set("op", "wait").set("timeout", static_cast<double>(timeout_ms) / 1000.0);
    return rt->request(std::move(*req), sink, user, true, std::chrono::milliseconds(timeout_ms));
}

OmuiResult omuiPost(OmuiRuntime rt, PFN_omuiTask fn, void* user) noexcept {
    if (!rt || !fn) return OMUI_ERROR_INVALID_ARGUMENT;
    rt->rt->queue().post([fn, user] { fn(user); });
    if (rt->wake) rt->wake();
    return OMUI_SUCCESS;
}

OmuiResult omuiCheck(OmuiRuntime rt, OmuiString view_json, PFN_omuiSink sink, void* user) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    const viewdoc::ReadResult r = viewdoc::read(text(view_json), {.verbs = &rt->rt->verbs()});
    for (const viewdoc::Diagnostic& d : r.diagnostics) {
        send(sink, user, d.to_value().json());
        send(sink, user, "\n");
    }
    return r.ok() ? OMUI_SUCCESS : OMUI_ERROR_INVALID_ARGUMENT;
}

OmuiResult omuiDefineComponent(OmuiRuntime rt, OmuiString component_json, PFN_omuiSink sink, void* user) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    auto r = viewdoc::define_component(text(component_json));
    if (!r) {
        for (const viewdoc::Diagnostic& d : r.error()) {
            send(sink, user, d.to_value().json());
            send(sink, user, "\n");
        }
        return OMUI_ERROR_INVALID_ARGUMENT;
    }
    return OMUI_SUCCESS;
}

OmuiResult omuiSignalFind(OmuiRuntime rt, OmuiString slot, OmuiString name, OmuiSignal* out) noexcept {
    if (!rt || !out) return OMUI_ERROR_INVALID_ARGUMENT;
    auto it = rt->views.find(text(slot));
    if (it == rt->views.end()) return OMUI_ERROR_NOT_FOUND;
    auto s = it->second.signal(text(name));
    if (!s) return OMUI_ERROR_NOT_FOUND;
    *out = rt->handle_of(*s);
    return OMUI_SUCCESS;
}

OmuiResult omuiSignalWatch(OmuiRuntime rt, OmuiSignal signal, PFN_omuiChange fn, void* user, OmuiSubscription* out) noexcept {
    if (!rt || !fn || !out) return OMUI_ERROR_INVALID_ARGUMENT;
    auto it = rt->signals.find(signal);
    if (it == rt->signals.end()) return OMUI_ERROR_NOT_FOUND;
    const OmuiSubscription sub = rt->next_subscription++;
    auto first = std::make_shared<bool>(true);
    rt->subscriptions[sub].watch = Watch([s = it->second, fn, user, signal, first] {
        const std::string json = s.get().json();  // a dependency: the watch runs again when it changes
        if (std::exchange(*first, false)) return;  // not for the value it had
        fn(user, signal, OmuiString{json.data(), json.size()});
    });
    *out = sub;
    return OMUI_SUCCESS;
}

OmuiResult omuiSignalShare(OmuiRuntime rt, OmuiSignal signal, OmuiString name) noexcept {
    if (!rt || text(name).empty()) return OMUI_ERROR_INVALID_ARGUMENT;
    auto it = rt->signals.find(signal);
    if (it == rt->signals.end()) return OMUI_ERROR_NOT_FOUND;
    rt->shared.insert_or_assign(std::string(text(name)), it->second);
    return OMUI_SUCCESS;
}

OmuiResult omuiModelSplice(OmuiRuntime rt, OmuiString model, std::uint32_t from, std::uint32_t remove, OmuiString rows_json) noexcept {
    if (!rt || text(model).empty()) return OMUI_ERROR_INVALID_ARGUMENT;
    auto parsed = json_or_null(rows_json);
    if (!parsed || !(parsed->is_null() || parsed->list())) return OMUI_ERROR_INVALID_ARGUMENT;
    std::vector<Row> rows;
    if (const Value::List* l = parsed->list())
        for (const Value& r : *l) {
            const Value* key = r.get("key");
            if (!r.map() || !key || key->type() != Value::Type::text || !spec::valid_explicit_segment(key->as_string())) {
                log.warn("a row of `{}` has no key that is an id (S-8): {}", text(model), r.json());
                return OMUI_ERROR_INVALID_ARGUMENT;
            }
            Value fields(Value::Map{});
            for (const auto& [k, v] : *r.map())
                if (k != "key") fields.set(k, v);
            rows.push_back({std::string(key->as_string()), std::move(fields)});
        }
    Model& m = rt->rt->model(text(model));
    if (from > m.size()) return OMUI_ERROR_INVALID_ARGUMENT;
    const std::size_t removed = std::min<std::size_t>(remove, m.size() - from);
    // One change, checked whole: the keys that stay and the new ones are unique.
    std::set<std::string_view> keys;
    for (std::size_t i = 0; i < m.size(); ++i)
        if (i < from || i >= from + removed) keys.insert(m.at(i).key);
    for (const Row& r : rows)
        if (!keys.insert(r.key).second) {
            log.warn("the key `{}` is in `{}` twice", r.key, text(model));
            return OMUI_ERROR_INVALID_ARGUMENT;
        }
    batch([&] {
        if (removed) m.remove(from, removed);
        if (!rows.empty()) (void)m.insert(from, std::move(rows));
    });
    return OMUI_SUCCESS;
}

OmuiResult omuiModelRows(OmuiRuntime rt, OmuiString model, std::uint32_t from, std::uint32_t count, PFN_omuiSink sink, void* user) noexcept {
    if (!rt || text(model).empty()) return OMUI_ERROR_INVALID_ARGUMENT;
    send(sink, user, rt->rt->model(text(model)).to_value(from, count).json());
    return OMUI_SUCCESS;
}

OmuiResult omuiCatalog(OmuiRuntime rt, PFN_omuiSink sink, void* user) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    Value components(Value::Map{});
    for (const ComponentType* t : registered_types()) {
        if (t->name == "root") continue;
        Value c(Value::Map{});
        c.set("role", spec::name(t->contract ? t->contract->role : t->role));
        Value props(Value::Map{}), events(Value::Map{}), actions(Value::Map{});
        if (const spec::ContractInfo* k = t->contract) {
            for (const spec::PropInfo& p : k->props) {
                Value x(Value::Map{});
                x.set("type", spec::name(p.type)).set("bindable", p.bindable);
                if (!p.values.empty()) {
                    Value::List values;
                    for (std::string_view v : p.values) values.emplace_back(v);
                    x.set("values", Value(std::move(values)));
                }
                if (!p.doc.empty()) x.set("doc", p.doc);
                props.set(p.name, std::move(x));
            }
            for (const spec::EventInfo& e : k->events) {
                Value args(Value::Map{});
                for (const spec::ArgInfo& a : e.args) args.set(a.name, a.type);
                events.set(e.name, Value(Value::Map{}).set("args", std::move(args)));
            }
            for (const spec::ActionSpec& a : k->actions) actions.set(a.name, Value(Value::Map{}).set("effect", spec::name(a.effect)));
            if (!k->doc.empty()) c.set("doc", k->doc);
        }
        c.set("contract", t->contract != nullptr).set("props", std::move(props)).set("events", std::move(events)).set("actions", std::move(actions));
        components.set(t->name, std::move(c));
    }
    Value common(Value::List{}), common_events(Value::List{});
    for (std::string_view p : runtime_properties()) common.list()->emplace_back(p);
    for (std::string_view e : runtime_events()) common_events.list()->emplace_back(e);
    Value out(Value::Map{});
    out.set("components", std::move(components)).set("properties", std::move(common)).set("events", std::move(common_events));
    send(sink, user, out.json());
    return OMUI_SUCCESS;
}

OmuiResult omuiRun(OmuiRuntime rt) noexcept {
    if (!rt) return OMUI_ERROR_INVALID_ARGUMENT;
    if (rt->headless) return OMUI_ERROR_INVALID_ARGUMENT;  // a headless runtime is driven by omuiFrame
    if (!window_runner()) return OMUI_ERROR_EXTENSION_NOT_PRESENT;
    OmuiRuntime_T* self = rt;
    const int code = window_runner()(*rt->rt, rt->app_id, [self] { self->after_frame(); });
    return code == 0 ? OMUI_SUCCESS : OMUI_ERROR_INTERNAL;
}

}  // extern "C"

}  // namespace omui::abi
