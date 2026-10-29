// The `openmui` module's native half (`_openmui`) and PocketPy. PocketPy is
// C, with one current VM for the whole process and no threads: every Script
// has a VM of its own (sixteen at most) and enters the interpreter under one
// lock, which a call into the runtime gives up while it waits (another
// Script's thread may run then). The Python functions the runtime may call
// later (handlers, watches, verbs) are kept in a dict of `_openmui` by id, so
// PocketPy's collector sees them; what reaches the runtime is the id.
// PocketPy's API is the module mcpplibs.mwrap.pocketpy (pocketpy-m): no
// header here.
module openmui.lang.python.embedded;

import std;
import mcpplibs.mwrap.pocketpy;
import nlohmann.json;
import openmui.abi;
import openmui.lang;
import :prelude;

namespace omui::lang::python::embedded {

using namespace mcpplibs::mwrap::pocketpy::macros;  // py_isint, py_assign, TypeError … (pocketpy.h's macros)

namespace {

// ── one interpreter, many scripts ──

std::mutex& interpreter() {
    static std::mutex m;
    return m;
}
thread_local int held = 0;           // how deep this thread is inside the interpreter
constinit std::array<bool, 16> taken{};  // the VMs in use (under the lock)
constinit bool started = false;

// In the interpreter, with `vm` current (-1: as it is); the VM before is
// current again after (a script that invokes another's verb on its thread).
class Enter {
public:
    explicit Enter(int vm) {
        if (held++ == 0) interpreter().lock();
        if (!started) {
            py_initialize();
            started = true;
        }
        prev_ = py_currentvm();
        if (vm >= 0 && vm != prev_) py_switchvm(vm);
    }
    ~Enter() {
        if (prev_ >= 0 && py_currentvm() != prev_) py_switchvm(prev_);
        if (--held == 0) interpreter().unlock();
    }
    Enter(const Enter&) = delete;
    Enter& operator=(const Enter&) = delete;

private:
    int prev_ = -1;
};

// Out of the interpreter while a native waits on the runtime; back in, with
// its VM current, after.
class Release {
public:
    explicit Release(int vm) : depth_(held), vm_(vm) {
        held = 0;
        if (depth_) interpreter().unlock();
    }
    ~Release() {
        if (depth_) interpreter().lock();
        held = depth_;
        if (depth_ && vm_ >= 0 && py_currentvm() != vm_) py_switchvm(vm_);
    }
    Release(const Release&) = delete;
    Release& operator=(const Release&) = delete;

private:
    int depth_, vm_;
};

std::string_view view(py_Ref v) {
    const c11_sv s = py_tosv(v);
    return {s.data, static_cast<std::size_t>(s.size)};
}

// Text the runtime can carry: JSON's strings are UTF-8 (a str of
// PocketPy's may hold any bytes, "\xff").
bool utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (std::size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

// The pending exception as text (its traceback), cleared down to `p0`.
std::string take_exception(py_StackRef p0) {
    char* m = py_formatexc();
    std::string out = m ? std::string(m) : std::string("error");
    if (m) py_free(m);
    py_clearexc(p0);
    return out;
}

// The last line of a traceback: `ValueError: …`.
std::string last_line(std::string_view text) {
    while (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    const auto nl = text.rfind('\n');
    return std::string(nl == std::string_view::npos ? text : text.substr(nl + 1));
}

}  // namespace

struct Script::Impl {
    Session&                                    session;
    int                                         vm = -1;
    std::string                                 broken;  // why the VM did not start
    std::function<void(std::string_view)>       errors = [](std::string_view m) { std::println(std::cerr, "openmui python: {}", m); };
    std::chrono::milliseconds                   limit{5000};
    int                                         running = 0;  // Python on the stack (the watchdog is around the outermost)
    py_Type                                     error_type = tp_RuntimeError;  // omui.OmuiError, once the module is
    std::vector<abi::OmuiSignal>                ordered;  // omui.signal's results, in order (kept across reloads)
    std::size_t                                 next_signal = 0;
    std::map<std::string, abi::OmuiSignal>      keyed;
    int                                         next_function = 1;
    std::map<std::string, std::vector<int>>     slot_functions;  // a mounted view's handlers
    std::map<std::string, int>                  verb_functions;
    std::map<abi::OmuiSignal, std::vector<int>> watches;   // this run's watches (a reload replaces them)
    std::set<abi::OmuiSignal>                   watching;  // the session calls `dispatch` for these
    std::shared_ptr<bool>                       alive = std::make_shared<bool>(true);  // false once the VM is gone

    explicit Impl(Session& s) : session(s) {}

    // `_openmui._kept` (looked up each time: a module's slot is the VM's to move).
    static py_Ref kept() { return py_getdict(py_getmodule("_openmui"), py_name("_kept")); }
    int keep(py_Ref f) {
        const int id = next_function++;
        (void)py_dict_setitem_by_int(kept(), id, f);
        return id;
    }
    void release(int id) { (void)py_dict_delitem_by_int(kept(), id); }

    // A kept function called with one argument (the interpreter entered):
    // its result if it is data, or why it raised (also reported).
    struct Called {
        bool                ok = false;
        std::optional<Json> value;
        std::string         why;
    };
    Called call(int id, const Json* arg);
    void   dispatch(abi::OmuiSignal s, const Json& value);
};

namespace {

// The script's VM let go, empty for the next Script: once, and never while
// its Python is on the stack (Script::~Script waits for that, or the
// outermost Running lets it go when it returns).
void close(Script::Impl& s) {
    if (s.vm < 0) return;
    Enter in(s.vm);
    py_resetvm();
    taken[static_cast<std::size_t>(s.vm)] = false;
    s.vm = -1;
    s.broken = "the script is gone";
}

// The watchdog, around the outermost Python of a script; it also counts the
// script's Python on the stack, which a teardown waits for.
class Running {
public:
    explicit Running(Script::Impl& s) : s_(s) {
        if (s_.running++ == 0 && s_.limit.count() > 0) py_watchdog_begin(static_cast<py_i64>(s_.limit.count()));
    }
    ~Running() {
        if (--s_.running == 0) {
            py_watchdog_end();
            if (!*s_.alive) close(s_);  // destroyed while it ran (from its own thread): it goes now
        }
    }
    Running(const Running&) = delete;
    Running& operator=(const Running&) = delete;

private:
    Script::Impl& s_;
};

// ── values ──

std::optional<Json> to_json(py_Ref v, int depth = 0) {
    if (depth > 100) return std::nullopt;
    if (py_isnone(v) || py_isnil(v)) return Json(nullptr);
    if (py_isbool(v)) return Json(py_tobool(v));
    if (py_isint(v)) return Json(static_cast<std::int64_t>(py_toint(v)));
    if (py_isfloat(v)) return Json(static_cast<double>(py_tofloat(v)));
    if (py_isstr(v)) {
        if (!utf8(view(v))) return std::nullopt;
        return Json(std::string(view(v)));
    }
    if (py_islist(v) || py_istuple(v)) {
        const bool list = py_islist(v);
        const int n = list ? py_list_len(v) : py_tuple_len(v);
        Json a = Json::array();
        for (int i = 0; i < n; ++i) {
            auto x = to_json(list ? py_list_getitem(v, i) : py_tuple_getitem(v, i), depth + 1);
            if (!x) return std::nullopt;
            a.push_back(std::move(*x));
        }
        return a;
    }
    if (py_isdict(v)) {
        struct Walk {
            Json o = Json::object();
            int  depth = 0;
            bool ok = true;
        } w{Json::object(), depth, true};
        const bool applied = py_dict_apply(
            v,
            [](py_Ref k, py_Ref x, void* user) -> bool {
                auto& w = *static_cast<Walk*>(user);
                if (!w.ok) return true;
                if (!py_isstr(k) || !utf8(view(k))) {
                    w.ok = false;  // a map's keys are strings
                    return true;
                }
                auto value = to_json(x, w.depth + 1);
                if (!value) w.ok = false;
                else w.o[std::string(view(k))] = std::move(*value);
                return true;
            },
            &w);
        if (!applied || !w.ok) return std::nullopt;
        return std::move(w.o);
    }
    return std::nullopt;
}

// Writes `v` to `out`, a slot the collector sees (the stack).
void push_json(py_Ref out, const Json& v) {
    switch (v.type()) {
        case Json::value_t::boolean: py_newbool(out, v.get<bool>()); break;
        case Json::value_t::number_integer: py_newint(out, v.get<std::int64_t>()); break;
        case Json::value_t::number_unsigned: py_newint(out, static_cast<py_i64>(v.get<std::uint64_t>())); break;
        case Json::value_t::number_float: py_newfloat(out, v.get<double>()); break;
        case Json::value_t::string: {
            const auto& s = v.get_ref<const std::string&>();
            py_newstrv(out, c11_sv{s.data(), static_cast<int>(s.size())});
            break;
        }
        case Json::value_t::array: {
            py_newlist(out);
            for (const Json& x : v) {
                py_Ref t = py_pushtmp();
                push_json(t, x);
                py_list_append(out, t);
                py_pop();
            }
            break;
        }
        case Json::value_t::object: {
            py_newdict(out);
            for (auto it = v.begin(); it != v.end(); ++it) {
                py_Ref t = py_pushtmp();
                push_json(t, it.value());
                (void)py_dict_setitem_by_str(out, it.key().c_str(), t);
                py_pop();
            }
            break;
        }
        default: py_newnone(out);
    }
}

Script::Impl& self() { return *static_cast<Script::Impl*>(py_getvmctx()); }

// A native's answer: `v` in py_retval().
bool answer(const Json& v) {
    py_Ref t = py_pushtmp();
    push_json(t, v);
    py_assign(py_retval(), t);
    py_pop();
    return true;
}

bool none() {
    py_newnone(py_retval());
    return true;
}

// omui.OmuiError with the runtime's message.
bool fail(std::string_view message) {
    const std::string m(message);
    return py_exception(self().error_type, "%s", m.c_str());
}

bool usage(int argc, int want, const char* call) {
    if (argc == want) return true;
    return TypeError("_openmui.%s takes %d arguments, got %d", call, want, argc);
}

std::optional<std::string> text(py_Ref v) {
    if (!py_isstr(v)) return std::nullopt;
    return std::string(view(v));
}

constexpr std::string_view not_data = "is not data (None, booleans, numbers, strings, lists, dicts of strings)";

}  // namespace

Script::Impl::Called Script::Impl::call(int id, const Json* arg) {
    Called out;
    Enter in(vm);
    Running r(*this);
    py_StackRef p0 = py_peek(0);
    if (py_dict_getitem_by_int(kept(), id) != 1) {  // released: a handler of a view replaced since
        if (py_checkexc()) (void)take_exception(p0);
        out.ok = true;
        return out;
    }
    py_Ref f = py_pushtmp();
    py_assign(f, py_retval());
    py_Ref a = py_pushtmp();
    if (arg) push_json(a, *arg);
    else py_newnone(a);
    if (!py_call(f, arg ? 1 : 0, a)) {
        const std::string message = take_exception(p0);
        out.why = last_line(message);
        errors(message);
        return out;
    }
    out.ok = true;
    out.value = to_json(py_retval());
    py_shrink(2);
    return out;
}

void Script::Impl::dispatch(abi::OmuiSignal s, const Json& value) {
    auto it = watches.find(s);
    if (it == watches.end()) return;
    const std::vector<int> ids = it->second;  // a watch may add watches
    for (int id : ids) (void)call(id, &value);
}

namespace {

// ── _openmui ──

bool n_literal(int argc, py_StackRef argv) {
    if (!usage(argc, 1, "literal")) return false;
    auto v = to_json(argv);
    if (!v) return fail(std::format("an operand {}, a signal or an expression", not_data));
    const std::string t = literal(*v).text;
    py_newstrv(py_retval(), c11_sv{t.data(), static_cast<int>(t.size())});
    return true;
}

bool n_signal_create(int argc, py_StackRef argv) {
    if (!usage(argc, 2, "signal_create")) return false;
    auto& s = self();
    auto initial = to_json(argv);
    if (!initial) return fail(std::format("omui.signal: the value {}", not_data));
    const std::string key = text(py_offset(argv, 1)).value_or("");
    abi::OmuiSignal h = 0;
    if (!key.empty()) {
        if (auto it = s.keyed.find(key); it != s.keyed.end()) h = it->second;
    } else if (s.next_signal < s.ordered.size()) {
        h = s.ordered[s.next_signal];  // a reload: the same signal, its value kept
    }
    if (!h) {
        Result<abi::OmuiSignal> made = [&] {
            Release out(s.vm);
            return s.session.signal(*initial);
        }();
        if (!made) return fail(made.error().text());
        h = *made;
        if (!key.empty()) s.keyed.emplace(key, h);
        else s.ordered.push_back(h);
    }
    if (key.empty()) ++s.next_signal;
    py_newint(py_retval(), static_cast<py_i64>(h));
    return true;
}

std::optional<abi::OmuiSignal> handle(py_Ref v) {
    if (!py_isint(v)) return std::nullopt;
    return static_cast<abi::OmuiSignal>(py_toint(v));
}

bool n_signal_get(int argc, py_StackRef argv) {
    if (!usage(argc, 1, "signal_get")) return false;
    auto h = handle(argv);
    if (!h) return TypeError("signal_get(handle)");
    return answer(self().session.get(*h));
}

bool n_signal_set(int argc, py_StackRef argv) {
    if (!usage(argc, 2, "signal_set")) return false;
    auto h = handle(argv);
    auto v = to_json(py_offset(argv, 1));
    if (!h) return TypeError("signal_set(handle, value)");
    if (!v) return fail(std::format("signal.set(v): the value {}", not_data));
    self().session.set(*h, std::move(*v));  // its watches run now, in the interpreter
    return none();
}

bool n_signal_watch(int argc, py_StackRef argv) {
    if (!usage(argc, 2, "signal_watch")) return false;
    auto& s = self();
    auto h = handle(argv);
    if (!h || !py_callable(py_offset(argv, 1))) return TypeError("signal.watch(fn): fn gets each new value");
    s.watches[*h].push_back(s.keep(py_offset(argv, 1)));
    if (s.watching.insert(*h).second) {
        Script::Impl* impl = &s;
        std::weak_ptr<bool> alive = s.alive;
        const abi::OmuiSignal sig = *h;
        s.session.watch(sig, [impl, alive, sig](const Json& v) {
            if (auto a = alive.lock(); a && *a) impl->dispatch(sig, v);
        });
    }
    return none();
}

bool n_catalog(int argc, py_StackRef) {
    if (!usage(argc, 0, "catalog")) return false;
    auto& s = self();
    std::string out;
    {
        Release wait(s.vm);
        s.session.host().ui([&] {
            (void)abi::omuiCatalog(s.session.host().runtime(), [](void* u, const char* d, std::size_t n) { static_cast<std::string*>(u)->append(d, n); }, &out);
        });
    }
    const Json c = Json::parse(out, nullptr, false);
    return answer(c.is_discarded() ? Json::object() : c);
}

bool n_natives(int argc, py_StackRef) {
    if (!usage(argc, 0, "natives")) return false;
    auto& s = self();
    Json c;
    {
        Release wait(s.vm);
        c = s.session.natives();
    }
    return answer(c.is_object() ? c : Json::object());
}

bool n_mount(int argc, py_StackRef argv) {
    if (!usage(argc, 3, "mount")) return false;
    auto& s = self();
    auto slot = text(argv);
    auto tree = to_json(py_offset(argv, 1));
    py_Ref list = py_offset(argv, 2);
    if (!slot || !py_islist(list)) return TypeError("mount(slot, tree, handlers)");
    if (!tree) return fail(std::format("omui.mount: a property {}, a signal, an expression or omui.tr", not_data));
    std::vector<int> ids;
    for (int i = 0; i < py_list_len(list); ++i) ids.push_back(s.keep(py_list_getitem(list, i)));
    Script::Impl* impl = &s;
    std::weak_ptr<bool> alive = s.alive;
    auto root = element_from_json(*tree, [&](std::size_t index) -> std::optional<Handler> {
        if (index >= ids.size()) return std::nullopt;
        const int id = ids[index];
        return [impl, alive, id](const Json& event) {
            if (auto a = alive.lock(); a && *a) (void)impl->call(id, &event);
        };
    });
    Result<void> r = root ? Result<void>() : Result<void>(std::unexpected(root.error()));
    if (root) {
        Release wait(s.vm);
        r = s.session.mount(*slot, *root);
    }
    if (!r) {
        for (int id : ids) s.release(id);
        return fail(r.error().text());
    }
    // The view's old handlers go; the new ones stay while it is mounted.
    for (int id : s.slot_functions[*slot]) s.release(id);
    s.slot_functions[*slot] = std::move(ids);
    return none();
}

bool n_mount_document(int argc, py_StackRef argv) {
    if (!usage(argc, 2, "mount_document")) return false;
    auto& s = self();
    auto slot = text(argv);
    std::optional<std::string> document = text(py_offset(argv, 1));
    if (!document)
        if (auto d = to_json(py_offset(argv, 1))) document = d->dump();
    if (!slot || !document) return TypeError("mount_document(slot, document)");
    std::string diagnostics;
    abi::OmuiResult r = abi::OMUI_SUCCESS;
    {
        Release wait(s.vm);
        r = s.session.host().ui([&] {
            auto rt = s.session.host().runtime();
            const abi::OmuiString sl{slot->data(), slot->size()}, tx{document->data(), document->size()};
            const abi::OmuiResult x = abi::omuiMount(rt, sl, tx);
            if (x != abi::OMUI_SUCCESS)
                (void)abi::omuiCheck(rt, tx, [](void* u, const char* d, std::size_t n) { static_cast<std::string*>(u)->append(d, n); }, &diagnostics);
            return x;
        });
    }
    if (r != abi::OMUI_SUCCESS) return fail(diagnostics.empty() ? std::string("the document was refused") : diagnostics);
    return none();
}

bool n_unmount(int argc, py_StackRef argv) {
    if (!usage(argc, 1, "unmount")) return false;
    auto& s = self();
    auto slot = text(argv);
    if (!slot) return TypeError("unmount(slot)");
    Result<void> r;
    {
        Release wait(s.vm);
        r = s.session.unmount(*slot);
    }
    for (int id : s.slot_functions[*slot]) s.release(id);
    s.slot_functions.erase(*slot);
    if (!r) return fail(r.error().text());
    return none();
}

bool n_document(int argc, py_StackRef argv) {
    if (!usage(argc, 1, "document")) return false;
    auto tree = to_json(argv);
    if (!tree) return fail(std::format("omui.document: a property {}, a signal, an expression or omui.tr", not_data));
    // A document only shown: its handlers are placeholders.
    auto root = element_from_json(*tree, [](std::size_t) -> std::optional<Handler> { return [](const Json&) {}; });
    if (!root) return fail(root.error().text());
    return answer(self().session.document(*root));
}

abi::OmuiEffect effect_of(std::string_view e) {
    if (e == "read") return abi::OMUI_EFFECT_READ;
    if (e == "irreversible") return abi::OMUI_EFFECT_IRREVERSIBLE;
    if (e == "external") return abi::OMUI_EFFECT_EXTERNAL;
    return abi::OMUI_EFFECT_REVERSIBLE;
}

bool n_verb(int argc, py_StackRef argv) {
    if (!usage(argc, 3, "verb")) return false;
    auto& s = self();
    auto name = text(argv);
    auto o = to_json(py_offset(argv, 1));
    if (!name || !o || !o->is_object() || !py_callable(py_offset(argv, 2)))
        return TypeError("omui.verb(name, fn, effect=…, answer=…, summary=…, person=False), or @omui.verb(name, …)");
    const auto field = [&](const char* k) { return (*o).contains(k) && (*o)[k].is_string() ? (*o)[k].get<std::string>() : std::string(); };
    Session::VerbSpec spec{*name, field("summary"), effect_of(field("effect")), field("answer"), o->value("person", false)};
    const int id = s.keep(py_offset(argv, 2));
    Script::Impl* impl = &s;
    std::weak_ptr<bool> alive = s.alive;
    Result<void> r;
    {
        Release wait(s.vm);
        r = s.session.verb(std::move(spec), [impl, alive, id](const Json& args) -> Result<Json> {
            if (auto a = alive.lock(); !a || !*a) return std::unexpected(Error{abi::OMUI_ERROR_REFUSED, "the script is gone"});
            auto out = impl->call(id, &args);
            if (!out.ok) return std::unexpected(Error{abi::OMUI_ERROR_REFUSED, std::format("the verb raised an error: {}", out.why)});
            if (!out.value) return std::unexpected(Error{abi::OMUI_ERROR_REFUSED, std::format("the verb answered something that {}", not_data)});
            return std::move(*out.value);
        });
    }
    if (!r) {
        s.release(id);
        return fail(r.error().text());
    }
    // Registered again (a reload): the new function answers; the old goes.
    if (auto it = s.verb_functions.find(*name); it != s.verb_functions.end()) s.release(it->second);
    s.verb_functions[*name] = id;
    return none();
}

bool records(const Result<std::vector<Json>>& r) {
    if (!r) return fail(r.error().text());
    return answer(Json(*r));
}

bool n_invoke(int argc, py_StackRef argv) {
    if (!usage(argc, 2, "invoke")) return false;
    auto& s = self();
    auto name = text(argv);
    auto args = to_json(py_offset(argv, 1));
    if (!name) return TypeError("omui.invoke(verb, args)");
    if (!args) return fail(std::format("omui.invoke: the arguments are data; this {}", not_data));
    Result<std::vector<Json>> r;
    {
        Release wait(s.vm);
        r = s.session.invoke(*name, *args);
    }
    return records(r);
}

bool n_act(int argc, py_StackRef argv) {
    if (!usage(argc, 1, "act")) return false;
    auto& s = self();
    auto req = to_json(argv);
    if (!req || !req->is_object()) return fail("omui.act(node=…, action=…, args=…): the request is data");
    Result<std::vector<Json>> r;
    {
        Release wait(s.vm);
        r = s.session.act(*req);
    }
    return records(r);
}

bool n_snapshot(int argc, py_StackRef argv) {
    if (!usage(argc, 1, "snapshot")) return false;
    auto& s = self();
    auto q = to_json(argv);
    if (!q || !q->is_object()) return fail("omui.snapshot(root=…, depth=…): the query is data");
    Result<std::vector<Json>> r;
    {
        Release wait(s.vm);
        r = s.session.snapshot(*q);
    }
    return records(r);
}

bool n_find(int argc, py_StackRef argv) {
    if (!usage(argc, 1, "find")) return false;
    auto& s = self();
    std::optional<std::string> selector = text(argv);
    if (!selector)
        if (auto q = to_json(argv); q && q->is_object()) selector = q->dump();
    if (!selector) return TypeError("omui.find(path or query)");
    Result<abi::OmuiNode> n;
    {
        Release wait(s.vm);
        n = s.session.find(*selector);
    }
    if (!n) return fail(n.error().text());
    py_newint(py_retval(), static_cast<py_i64>(*n));
    return true;
}

bool n_get(int argc, py_StackRef argv) {
    if (!usage(argc, 2, "get")) return false;
    auto& s = self();
    auto prop = text(py_offset(argv, 1));
    if (!py_isint(argv) || !prop) return TypeError("node.get(prop)");
    const auto node = static_cast<abi::OmuiNode>(py_toint(argv));
    Result<Json> v;
    {
        Release wait(s.vm);
        v = s.session.get(node, *prop);
    }
    if (!v) return fail(v.error().text());
    return answer(*v);
}

bool n_set(int argc, py_StackRef argv) {
    if (!usage(argc, 2, "set")) return false;
    auto& s = self();
    auto props = to_json(py_offset(argv, 1));
    if (!py_isint(argv) || !props || !props->is_object()) return TypeError("node.set(prop=value, …)");
    const auto node = static_cast<abi::OmuiNode>(py_toint(argv));
    std::vector<std::pair<std::string, Json>> list;
    for (auto it = props->begin(); it != props->end(); ++it) list.emplace_back(it.key(), it.value());
    Result<void> r;
    {
        Release wait(s.vm);
        r = s.session.set(node, list);
    }
    if (!r) return fail(r.error().text());
    return none();
}

bool n_rows(int argc, py_StackRef argv) {
    if (!usage(argc, 1, "rows")) return false;
    auto& s = self();
    auto model = text(argv);
    if (!model) return TypeError("model.rows()");
    Result<Json> r;
    {
        Release wait(s.vm);
        r = s.session.rows(*model);
    }
    if (!r) return fail(r.error().text());
    return answer(*r);
}

bool n_splice(int argc, py_StackRef argv) {
    if (!usage(argc, 4, "splice")) return false;
    auto& s = self();
    auto model = text(argv);
    auto rows = to_json(py_offset(argv, 3));
    if (!model || !py_isint(py_offset(argv, 1)) || !py_isint(py_offset(argv, 2))) return TypeError("model.splice(start, remove, rows)");
    if (!rows || !rows->is_array()) return fail("model.splice: rows are a list of {\"key\": …, fields…}");
    const py_i64 from = py_toint(py_offset(argv, 1)), remove = py_toint(py_offset(argv, 2));
    if (from < 0) return fail("model.splice: rows are counted from 0");
    Result<void> r;
    {
        Release wait(s.vm);
        r = s.session.splice(*model, static_cast<std::size_t>(from), static_cast<std::size_t>(std::max<py_i64>(0, remove)), *rows);
    }
    if (!r) return fail(r.error().text());
    return none();
}

bool n_theme(int argc, py_StackRef argv) {
    if (!usage(argc, 1, "theme")) return false;
    auto& s = self();
    auto layers = to_json(argv);
    std::vector<std::string> l;
    if (layers && layers->is_array())
        for (const Json& x : *layers)
            if (x.is_string()) l.push_back(x.get<std::string>());
    if (l.empty()) return fail("omui.theme(\"dark\", \"compact\"): a theme, then overlays");
    Result<void> r;
    {
        Release wait(s.vm);
        r = s.session.theme(l);
    }
    if (!r) return fail(r.error().text());
    return none();
}

bool n_themes(int argc, py_StackRef) {
    if (!usage(argc, 0, "themes")) return false;
    auto& s = self();
    Json t;
    {
        Release wait(s.vm);
        t = s.session.themes();
    }
    return answer(t);
}

// A native module's function (component-binding §8), its arguments a list
// (the contract's order) or a dict (by name). An `async` one is waited for:
// the script's thread is not the runtime's.
bool n_native(int argc, py_StackRef argv) {
    if (!usage(argc, 3, "native")) return false;
    auto& s = self();
    auto module = text(argv), function = text(py_offset(argv, 1));
    auto args = to_json(py_offset(argv, 2));
    if (!module || !function) return TypeError("native(module, function, args)");
    if (!args) return fail(std::format("omui.native: an argument {}", not_data));
    Result<Json> r;
    {
        Release wait(s.vm);
        r = s.session.native(*module, *function, *args);
    }
    if (!r) return fail(r.error().text());
    return answer(*r);
}

bool n_log(int argc, py_StackRef argv) {
    if (!usage(argc, 1, "log")) return false;
    if (auto t = text(argv)) std::println(std::cerr, "python: {}", *t);
    return none();
}

// builtins.exit: SystemExit, which ends the script and not the process.
bool n_exit(int argc, py_StackRef argv) {
    if (argc > 1) return TypeError("exit() takes at most 1 argument");
    if (!py_tpcall(tp_SystemExit, argc, argv)) return false;
    py_Ref e = py_pushtmp();
    py_assign(e, py_retval());
    return py_raise(e);
}

void start(Script::Impl& s) {
    Enter in(-1);
    const auto free = std::ranges::find(taken, false);
    if (free == taken.end()) {
        s.broken = "no PocketPy VM is free: sixteen Python scripts run in this process already";
        return;
    }
    s.vm = static_cast<int>(free - taken.begin());
    *free = true;
    py_switchvm(s.vm);
    py_setvmctx(&s);
    py_bindfunc(py_getmodule("builtins"), "exit", n_exit);

    py_GlobalRef n = py_newmodule("_openmui");
    py_newdict(py_emplacedict(n, py_name("_kept")));
    const std::initializer_list<std::pair<const char*, py_CFunction>> natives{
        {"literal", n_literal},     {"signal_create", n_signal_create}, {"signal_get", n_signal_get}, {"signal_set", n_signal_set},
        {"signal_watch", n_signal_watch}, {"catalog", n_catalog},     {"natives", n_natives},       {"mount", n_mount},
        {"mount_document", n_mount_document}, {"unmount", n_unmount}, {"document", n_document},     {"verb", n_verb},
        {"invoke", n_invoke},       {"act", n_act},                   {"snapshot", n_snapshot},     {"find", n_find},
        {"get", n_get},             {"set", n_set},                   {"rows", n_rows},             {"splice", n_splice},
        {"theme", n_theme},         {"themes", n_themes},             {"native", n_native},         {"log", n_log}};
    for (const auto& [name, fn] : natives) py_bindfunc(py_getmodule("_openmui"), name, fn);

    // openmui, and openmui.native (filled by the prelude from the catalogue).
    py_GlobalRef module = py_newmodule("openmui");
    py_GlobalRef native = py_newmodule("openmui.native");
    py_setdict(module, py_name("native"), native);
    const std::string source(prelude);
    py_StackRef p0 = py_peek(0);
    if (!py_exec(source.c_str(), "<openmui>", EXEC_MODE, module)) {
        s.broken = "the openmui module did not start: " + take_exception(p0);
        return;
    }
    if (py_ItemRef t = py_getdict(module, py_name("OmuiError")); t && py_istype(t, tp_type)) s.error_type = py_totype(t);
}

}  // namespace

Script::Script(Session& session) : impl_(std::make_shared<Impl>(session)) {
    Impl* s = impl_.get();
    session.host().script([s] { start(*s); });
}

// Safe while it loads: its Python on the script thread's stack (a run waiting
// on this thread for the runtime) finishes before the VM goes; the runs still
// queued, and what the runtime calls later, find it gone.
Script::~Script() {
    Impl* s = impl_.get();
    s->session.host().retire([s] {
        *s->alive = false;
        if (s->running > 0) return false;
        close(*s);
        return true;
    });
}

void Script::on_error(std::function<void(std::string_view)> fn) { impl_->errors = std::move(fn); }

void Script::budget(std::chrono::milliseconds limit) {
    Impl* s = impl_.get();
    s->session.host().script([s, limit] { s->limit = limit; });
}

namespace {

Error gone() { return Error{abi::OMUI_ERROR_REFUSED, "the script is gone"}; }

Result<void> run_code(Script::Impl& s, const std::string& code, const std::string& name) {
    if (!*s.alive) return std::unexpected(gone());
    if (s.vm < 0) return std::unexpected(Error{abi::OMUI_ERROR_INTERNAL, s.broken});
    Enter in(s.vm);
    Running r(s);
    py_StackRef p0 = py_peek(0);
    if (py_exec(code.c_str(), name.c_str(), EXEC_MODE, nullptr)) return {};
    if (py_matchexc(tp_SystemExit)) {
        // SystemExit ends the script: an error when its code is not 0.
        py_Ref e = py_pushtmp();
        py_assign(e, py_retval());
        py_clearexc(nullptr);
        std::string status;
        if (py_str(e)) status = std::string(view(py_retval()));
        else if (py_checkexc()) py_clearexc(nullptr);
        py_clearexc(p0);
        if (status.empty() || status == "0" || status == "None") return {};
        return std::unexpected(Error{abi::OMUI_ERROR_FAILED, std::format("{}: SystemExit: {}", name, status)});
    }
    return std::unexpected(Error{abi::OMUI_ERROR_INVALID_ARGUMENT, take_exception(p0)});
}

}  // namespace

std::future<Result<void>> Script::run(std::string code, std::string name) {
    auto done = std::make_shared<std::promise<Result<void>>>();
    auto future = done->get_future();
    // The script's state is shared with the run queued: one queued when the
    // script is destroyed finds it gone, not freed.
    std::shared_ptr<Impl> s = impl_;
    s->session.host().post([s, done, code = std::move(code), name = std::move(name)] { done->set_value(run_code(*s, code, name)); });
    return future;
}

std::future<Result<void>> Script::reload(std::string code, std::string name) {
    auto done = std::make_shared<std::promise<Result<void>>>();
    auto future = done->get_future();
    std::shared_ptr<Impl> s = impl_;
    s->session.host().post([s, done, code = std::move(code), name = std::move(name)] {
        if (!*s->alive) {
            done->set_value(std::unexpected(gone()));
            return;
        }
        s->next_signal = 0;  // the script's signals are the last run's, in order
        if (s->vm >= 0) {
            // Its watches are made again by the run: the last run's go.
            Enter in(s->vm);
            for (auto& [signal, ids] : s->watches)
                for (int id : ids) s->release(id);
            s->watches.clear();
        }
        done->set_value(run_code(*s, code, name));
    });
    return future;
}

}  // namespace omui::lang::python::embedded
