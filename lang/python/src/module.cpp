// The `_openmui` module's functions. Arguments are strings, ints and
// callables; structured values travel as JSON text (the Python half parses
// them). Each function converts its arguments while it holds the GIL, then
// does the Session's work on the script thread with the GIL released.
module openmui.lang.python;

import std;
import nlohmann.json;
import openmui.abi;
import openmui.lang;
import :capi;

namespace omui::lang::python {

namespace {

using namespace capi;

struct State {
    const Api*               py = nullptr;
    Session*                 session = nullptr;
    Hooks                    hooks;
    std::map<int, PyObject*> functions;  // kept callables (each holds a reference)
    int                      next = 1;
    std::map<std::string, std::vector<int>> slot_functions;
    PyMethodDef*             methods = nullptr;
    PyModuleDef              def{};
};

State& state() {
    static State s;
    return s;
}

const Api& py() { return *state().py; }

// ── arguments and results ──

std::optional<std::string> str_arg(PyObject* args, Py_ssize_t i) {
    PyObject* o = py().PyTuple_GetItem(args, i);
    if (!o) return std::nullopt;
    Py_ssize_t n = 0;
    const char* s = py().PyUnicode_AsUTF8AndSize(o, &n);
    if (!s) return std::nullopt;
    return std::string(s, static_cast<std::size_t>(n));
}

std::optional<long long> int_arg(PyObject* args, Py_ssize_t i) {
    PyObject* o = py().PyTuple_GetItem(args, i);
    if (!o) return std::nullopt;
    const long long v = py().PyLong_AsLongLong(o);
    if (v == -1 && py().PyErr_Occurred()) return std::nullopt;
    return v;
}

PyObject* none() {
    py().Py_IncRef(py().none);
    return py().none;
}

PyObject* str(std::string_view s) { return py().PyUnicode_FromStringAndSize(s.data(), static_cast<Py_ssize_t>(s.size())); }

PyObject* raise(std::string_view message, PyObject* type = nullptr) {
    const std::string m(message);
    py().PyErr_SetString(type ? type : py().runtime_error, m.c_str());
    return nullptr;
}

PyObject* arguments(std::string_view usage) { return raise(std::format("_openmui: {}", usage), py().type_error); }

// The Session's work: on the script thread, with the GIL released (a
// handler the runtime calls meanwhile takes it there).
template <class F>
auto on_script(F fn) -> std::invoke_result_t<F&> {
    Host& host = state().session->host();
    if (host.on_script_thread()) return fn();
    PyThreadState* saved = py().PyEval_SaveThread();
    if constexpr (std::is_void_v<std::invoke_result_t<F&>>) {
        host.script(fn);
        py().PyEval_RestoreThread(saved);
    } else {
        auto r = host.script(fn);
        py().PyEval_RestoreThread(saved);
        return r;
    }
}

int keep(PyObject* fn) {
    py().Py_IncRef(fn);
    const int id = state().next++;
    state().functions.emplace(id, fn);
    return id;
}

void release(int id) {
    auto& f = state().functions;
    if (auto it = f.find(id); it != f.end()) {
        py().Py_DecRef(it->second);
        f.erase(it);
    }
}

// Calls a kept function with one string, taking the GIL; answers its result
// as a string, or nothing when it raised (the traceback is printed).
std::optional<std::string> call(int id, std::string_view arg) {
    const PyGILState_STATE gil = py().PyGILState_Ensure();
    std::optional<std::string> out;
    if (auto it = state().functions.find(id); it != state().functions.end()) {
        PyObject* args = py().PyTuple_New(1);
        py().PyTuple_SetItem(args, 0, str(arg));
        PyObject* r = py().PyObject_CallObject(it->second, args);
        py().Py_DecRef(args);
        if (!r) {
            py().PyErr_Print();
        } else {
            out.emplace();
            Py_ssize_t n = 0;
            if (r != py().none)
                if (const char* s = py().PyUnicode_AsUTF8AndSize(r, &n)) out->assign(s, static_cast<std::size_t>(n));
            if (py().PyErr_Occurred()) py().PyErr_Print();  // not a string: its text is empty
            py().Py_DecRef(r);
        }
    }
    py().PyGILState_Release(gil);
    return out;
}

Json parse(const std::string& text) {
    Json v = Json::parse(text, nullptr, false);
    return v.is_discarded() ? Json(nullptr) : v;
}

// ── the functions ──

// The order in which a script made its signals, kept across reloads.
template <class Keys>
Keys& signal_keys() {
    static Keys keys;
    return keys;
}

struct SignalKeys {
    std::vector<abi::OmuiSignal>           ordered;
    std::size_t                            next = 0;
    std::map<std::string, abi::OmuiSignal> keyed;
};

PyObject* f_signal_create(PyObject*, PyObject* args) {
    auto json = str_arg(args, 0);
    auto key = str_arg(args, 1);
    if (!json || !key) return arguments("signal_create(json, key)");
    SignalKeys& keys = signal_keys<SignalKeys>();  // a reload runs the script again: its signals are the last run's, in order
    auto made = on_script([&]() -> Result<abi::OmuiSignal> {
        if (!key->empty()) {
            if (auto it = keys.keyed.find(*key); it != keys.keyed.end()) return it->second;
        } else if (keys.next < keys.ordered.size()) {
            return keys.ordered[keys.next++];
        }
        auto s = state().session->signal(parse(*json));
        if (!s) return s;
        if (!key->empty()) keys.keyed.emplace(*key, *s);
        else {
            keys.ordered.push_back(*s);
            ++keys.next;
        }
        return s;
    });
    if (!made) return raise(made.error().text());
    return py().PyLong_FromLongLong(static_cast<long long>(*made));
}

PyObject* f_signal_get(PyObject*, PyObject* args) {
    auto h = int_arg(args, 0);
    if (!h) return arguments("signal_get(handle)");
    const std::string text = on_script([&] { return state().session->get(static_cast<abi::OmuiSignal>(*h)).dump(); });
    return str(text);
}

PyObject* f_signal_set(PyObject*, PyObject* args) {
    auto h = int_arg(args, 0);
    auto json = str_arg(args, 1);
    if (!h || !json) return arguments("signal_set(handle, json)");
    on_script([&] { state().session->set(static_cast<abi::OmuiSignal>(*h), parse(*json)); });
    return none();
}

PyObject* f_signal_watch(PyObject*, PyObject* args) {
    auto h = int_arg(args, 0);
    PyObject* fn = py().PyTuple_GetItem(args, 1);
    if (!h || !fn || !py().PyCallable_Check(fn)) return arguments("signal_watch(handle, fn)");
    const int id = keep(fn);
    on_script([&] { state().session->watch(static_cast<abi::OmuiSignal>(*h), [id](const Json& v) { (void)call(id, v.dump()); }); });
    return none();
}

// Native modules (component-binding §8): the catalogue, and a call. A call
// runs here, with the GIL released: a native touches no runtime state, and
// Python's thread is not the runtime's.
PyObject* f_natives(PyObject*, PyObject*) {
    PyThreadState* saved = py().PyEval_SaveThread();
    const std::string out = state().session->natives().dump();
    py().PyEval_RestoreThread(saved);
    return str(out);
}

PyObject* f_native(PyObject*, PyObject* args) {
    auto module = str_arg(args, 0);
    auto function = str_arg(args, 1);
    auto a = str_arg(args, 2);
    if (!module || !function || !a) return arguments("native(module, function, args_json)");
    PyThreadState* saved = py().PyEval_SaveThread();
    auto r = state().session->native(*module, *function, parse(*a));
    py().PyEval_RestoreThread(saved);
    if (!r) return raise(r.error().text());
    return str(r->dump());
}

PyObject* f_catalog(PyObject*, PyObject*) {
    std::string out;
    on_script([&] {
        state().session->host().ui([&] {
            (void)abi::omuiCatalog(state().session->host().runtime(),
                                   [](void* u, const char* d, std::size_t n) { static_cast<std::string*>(u)->append(d, n); }, &out);
        });
    });
    return str(out);
}

PyObject* f_mount(PyObject*, PyObject* args) {
    auto slot = str_arg(args, 0);
    auto tree = str_arg(args, 1);
    PyObject* list = py().PyTuple_GetItem(args, 2);
    if (!slot || !tree || !list) return arguments("mount(slot, tree, handlers)");
    std::vector<int> ids;
    const Py_ssize_t n = py().PyList_Size(list);
    if (n < 0) return arguments("mount(slot, tree, handlers): handlers is a list");
    for (Py_ssize_t i = 0; i < n; ++i) ids.push_back(keep(py().PyList_GetItem(list, i)));
    auto r = on_script([&]() -> Result<void> {
        auto root = element_from_json(parse(*tree), [&](std::size_t index) -> std::optional<Handler> {
            if (index >= ids.size()) return std::nullopt;
            const int id = ids[index];
            return [id](const Json& event) { (void)call(id, event.dump()); };
        });
        if (!root) return std::unexpected(root.error());
        return state().session->mount(*slot, *root);
    });
    if (!r) {
        for (int id : ids) release(id);
        return raise(r.error().text());
    }
    for (int id : state().slot_functions[*slot]) release(id);  // the view's old handlers
    state().slot_functions[*slot] = std::move(ids);
    return none();
}

PyObject* f_mount_document(PyObject*, PyObject* args) {
    auto slot = str_arg(args, 0);
    auto text = str_arg(args, 1);
    if (!slot || !text) return arguments("mount_document(slot, text)");
    std::string diagnostics;
    const auto r = on_script([&] {
        return state().session->host().ui([&] {
            auto rt = state().session->host().runtime();
            const abi::OmuiString sl{slot->data(), slot->size()}, tx{text->data(), text->size()};
            const abi::OmuiResult x = abi::omuiMount(rt, sl, tx);
            if (x != abi::OMUI_SUCCESS)
                (void)abi::omuiCheck(rt, tx, [](void* u, const char* d, std::size_t n) { static_cast<std::string*>(u)->append(d, n); },
                                     &diagnostics);
            return x;
        });
    });
    if (r != abi::OMUI_SUCCESS) return raise(diagnostics.empty() ? std::string("the document was refused") : diagnostics);
    return none();
}

PyObject* f_unmount(PyObject*, PyObject* args) {
    auto slot = str_arg(args, 0);
    if (!slot) return arguments("unmount(slot)");
    auto r = on_script([&] { return state().session->unmount(*slot); });
    for (int id : state().slot_functions[*slot]) release(id);
    state().slot_functions.erase(*slot);
    if (!r) return raise(r.error().text());
    return none();
}

PyObject* f_document(PyObject*, PyObject* args) {
    auto tree = str_arg(args, 0);
    if (!tree) return arguments("document(tree)");
    auto out = on_script([&]() -> Result<std::string> {
        auto root = element_from_json(parse(*tree), [](std::size_t) -> std::optional<Handler> { return [](const Json&) {}; });
        if (!root) return std::unexpected(root.error());
        return state().session->document(*root).dump();
    });
    if (!out) return raise(out.error().text());
    return str(*out);
}

abi::OmuiEffect effect_of(std::string_view e) {
    if (e == "read") return abi::OMUI_EFFECT_READ;
    if (e == "irreversible") return abi::OMUI_EFFECT_IRREVERSIBLE;
    if (e == "external") return abi::OMUI_EFFECT_EXTERNAL;
    return abi::OMUI_EFFECT_REVERSIBLE;
}

PyObject* f_verb(PyObject*, PyObject* args) {
    auto name = str_arg(args, 0);
    auto spec = str_arg(args, 1);
    PyObject* fn = py().PyTuple_GetItem(args, 2);
    if (!name || !spec || !fn || !py().PyCallable_Check(fn)) return arguments("verb(name, spec, fn)");
    const Json o = parse(*spec);
    Session::VerbSpec v{*name, o.value("summary", ""), effect_of(o.value("effect", "reversible")), o.value("answer", "")};
    const int id = keep(fn);
    auto r = on_script([&] {
        return state().session->verb(std::move(v), [id](const Json& a) -> Result<Json> {
            auto out = call(id, a.dump());
            if (!out) return std::unexpected(Error{abi::OMUI_ERROR_REFUSED, "the verb raised an exception"});
            return parse(*out);
        });
    });
    if (!r) {
        release(id);
        return raise(r.error().text());
    }
    return none();
}

PyObject* records(const Result<std::vector<Json>>& r) {
    if (!r) return raise(r.error().text());
    return str(Json(*r).dump());
}

PyObject* f_invoke(PyObject*, PyObject* args) {
    auto name = str_arg(args, 0);
    auto a = str_arg(args, 1);
    if (!name || !a) return arguments("invoke(verb, args_json)");
    return records(on_script([&] { return state().session->invoke(*name, parse(*a)); }));
}

PyObject* f_act(PyObject*, PyObject* args) {
    auto req = str_arg(args, 0);
    if (!req) return arguments("act(request_json)");
    return records(on_script([&] { return state().session->act(parse(*req)); }));
}

PyObject* f_snapshot(PyObject*, PyObject* args) {
    auto q = str_arg(args, 0);
    if (!q) return arguments("snapshot(query_json)");
    return records(on_script([&] { return state().session->snapshot(parse(*q)); }));
}

PyObject* f_find(PyObject*, PyObject* args) {
    auto selector = str_arg(args, 0);
    if (!selector) return arguments("find(selector)");
    auto n = on_script([&] { return state().session->find(*selector); });
    if (!n) return raise(n.error().text());
    return py().PyLong_FromLongLong(static_cast<long long>(*n));
}

PyObject* f_get(PyObject*, PyObject* args) {
    auto node = int_arg(args, 0);
    auto prop = str_arg(args, 1);
    if (!node || !prop) return arguments("get(node, prop)");
    auto v = on_script([&] { return state().session->get(static_cast<abi::OmuiNode>(*node), *prop); });
    if (!v) return raise(v.error().text());
    return str(v->dump());
}

PyObject* f_set(PyObject*, PyObject* args) {
    auto node = int_arg(args, 0);
    auto props = str_arg(args, 1);
    if (!node || !props) return arguments("set(node, props_json)");
    auto r = on_script([&] {
        std::vector<std::pair<std::string, Json>> list;
        const Json p = parse(*props);
        if (p.is_object())
            for (auto it = p.begin(); it != p.end(); ++it) list.emplace_back(it.key(), it.value());
        return state().session->set(static_cast<abi::OmuiNode>(*node), list);
    });
    if (!r) return raise(r.error().text());
    return none();
}

PyObject* f_rows(PyObject*, PyObject* args) {
    auto model = str_arg(args, 0);
    if (!model) return arguments("rows(model)");
    auto r = on_script([&] { return state().session->rows(*model); });
    if (!r) return raise(r.error().text());
    return str(r->dump());
}

PyObject* f_splice(PyObject*, PyObject* args) {
    auto model = str_arg(args, 0);
    auto from = int_arg(args, 1);
    auto remove = int_arg(args, 2);
    auto rows = str_arg(args, 3);
    if (!model || !from || !remove || !rows || *from < 0 || *remove < 0) return arguments("splice(model, from, remove, rows_json)");
    auto r = on_script(
        [&] { return state().session->splice(*model, static_cast<std::size_t>(*from), static_cast<std::size_t>(*remove), parse(*rows)); });
    if (!r) return raise(r.error().text());
    return none();
}

PyObject* f_run(PyObject*, PyObject* args) {
    auto options = str_arg(args, 0);
    if (!options) return arguments("run(options_json)");
    if (!state().hooks.run) return raise("this build of openmui cannot show a window");
    // The application runs on this thread; Python continues on the script thread.
    PyThreadState* saved = py().PyEval_SaveThread();
    const int code = state().hooks.run(parse(*options));
    py().PyEval_RestoreThread(saved);
    return py().PyLong_FromLongLong(code);
}

PyObject* f_step(PyObject*, PyObject* args) {
    auto frames = int_arg(args, 0);
    if (!frames) return arguments("step(frames)");
    if (!state().hooks.step) return raise("this build of openmui runs no frames itself");
    PyThreadState* saved = py().PyEval_SaveThread();
    for (long long i = 0; i < *frames; ++i) state().hooks.step();
    py().PyEval_RestoreThread(saved);
    return none();
}

PyObject* f_settle(PyObject*, PyObject*) {
    // Frames until the script thread has nothing left and the runtime is still.
    if (!state().hooks.settle) return raise("this build of openmui runs no frames itself");
    PyThreadState* saved = py().PyEval_SaveThread();
    state().hooks.settle();
    py().PyEval_RestoreThread(saved);
    return none();
}

PyObject* f_png(PyObject*, PyObject* args) {
    auto file = str_arg(args, 0);
    if (!file) return arguments("png(file)");
    if (!state().hooks.png) return raise("this build of openmui keeps no pixels");
    const std::string error = state().hooks.png(*file);
    if (!error.empty()) return raise(error);
    return none();
}

PyObject* f_pixels(PyObject*, PyObject*) {
    if (!state().hooks.pixels) return raise("this build of openmui keeps no pixels");
    return str(state().hooks.pixels());
}

PyObject* f_reload(PyObject*, PyObject*) {
    // The script runs again: its n-th signal() is the last run's n-th.
    signal_keys<SignalKeys>().next = 0;
    return none();
}

PyObject* f_log(PyObject*, PyObject* args) {
    auto text = str_arg(args, 0);
    if (!text) return arguments("log(text)");
    std::println(std::cerr, "python: {}", *text);
    return none();
}

}  // namespace

PyObject* create_module(Session& session, Hooks hooks) {
    auto api = capi::api();
    if (!api) return nullptr;  // no interpreter to raise in: the loader reports the import failed
    State& s = state();
    s.py = *api;
    s.session = &session;
    s.hooks = std::move(hooks);
    static PyMethodDef methods[] = {
        {"signal_create", f_signal_create, METH_VARARGS, "signal_create(json, key) -> handle"},
        {"signal_get", f_signal_get, METH_VARARGS, "signal_get(handle) -> json"},
        {"signal_set", f_signal_set, METH_VARARGS, "signal_set(handle, json)"},
        {"signal_watch", f_signal_watch, METH_VARARGS, "signal_watch(handle, fn(json))"},
        {"catalog", f_catalog, METH_NOARGS, "catalog() -> json: the registered components"},
        {"natives", f_natives, METH_NOARGS, "natives() -> json: the registered native modules"},
        {"native", f_native, METH_VARARGS, "native(module, function, args_json) -> json: a native module's function"},
        {"mount", f_mount, METH_VARARGS, "mount(slot, tree_json, handlers)"},
        {"mount_document", f_mount_document, METH_VARARGS, "mount_document(slot, text): a *.view.omui.json"},
        {"unmount", f_unmount, METH_VARARGS, "unmount(slot)"},
        {"document", f_document, METH_VARARGS, "document(tree_json) -> json: the view document"},
        {"verb", f_verb, METH_VARARGS, "verb(name, spec_json, fn(args_json) -> json)"},
        {"invoke", f_invoke, METH_VARARGS, "invoke(verb, args_json) -> records_json"},
        {"act", f_act, METH_VARARGS, "act(request_json) -> records_json"},
        {"snapshot", f_snapshot, METH_VARARGS, "snapshot(query_json) -> records_json"},
        {"find", f_find, METH_VARARGS, "find(selector) -> node"},
        {"get", f_get, METH_VARARGS, "get(node, prop) -> json"},
        {"set", f_set, METH_VARARGS, "set(node, props_json)"},
        {"rows", f_rows, METH_VARARGS, "rows(model) -> json"},
        {"splice", f_splice, METH_VARARGS, "splice(model, from, remove, rows_json)"},
        {"run", f_run, METH_VARARGS, "run(options_json) -> exit code"},
        {"step", f_step, METH_VARARGS, "step(frames)"},
        {"settle", f_settle, METH_NOARGS, "settle(): frames until the scripts are done"},
        {"png", f_png, METH_VARARGS, "png(file): the last frame's pixels"},
        {"pixels", f_pixels, METH_NOARGS, "pixels(): the last frame's pixels, as the hash tests pin"},
        {"reload", f_reload, METH_NOARGS, "reload(): the script runs again; its signals keep their values"},
        {"log", f_log, METH_VARARGS, "log(text)"},
        {nullptr, nullptr, 0, nullptr},
    };
    s.methods = methods;
    s.def = PyModuleDef{{{1, nullptr}, nullptr, 0, nullptr}, "_openmui", "openmui's native half (openmui.lang.python).", -1, methods,
                        nullptr, nullptr, nullptr, nullptr};
    return s.py->PyModule_Create2(&s.def, python_abi_version);
}

}  // namespace omui::lang::python
