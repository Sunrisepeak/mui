module openmui.lang.python;

import std;
import mcpplibs.os;
import :capi;

namespace omui::lang::python::capi {

namespace {

template <class F>
bool find(F& slot, const char* name, std::string& missing) {
    slot = reinterpret_cast<F>(mcpplibs::os::library::process_symbol(name));
    if (!slot && missing.empty()) missing = name;
    return slot != nullptr;
}

PyObject* object(const char* name, bool pointer, std::string& missing) {
    void* p = mcpplibs::os::library::process_symbol(name);
    if (!p) {
        if (missing.empty()) missing = name;
        return nullptr;
    }
    // PyExc_* are variables holding the object; _Py_NoneStruct is the object.
    return pointer ? *static_cast<PyObject**>(p) : static_cast<PyObject*>(p);
}

}  // namespace

std::expected<const Api*, std::string> api() {
    static const std::expected<Api, std::string> resolved = []() -> std::expected<Api, std::string> {
        Api a{};
        std::string missing;
        find(a.PyModule_Create2, "PyModule_Create2", missing);
        find(a.Py_IncRef, "Py_IncRef", missing);
        find(a.Py_DecRef, "Py_DecRef", missing);
        find(a.PyUnicode_FromStringAndSize, "PyUnicode_FromStringAndSize", missing);
        find(a.PyUnicode_AsUTF8AndSize, "PyUnicode_AsUTF8AndSize", missing);
        find(a.PyLong_FromLongLong, "PyLong_FromLongLong", missing);
        find(a.PyLong_AsLongLong, "PyLong_AsLongLong", missing);
        find(a.PyTuple_Size, "PyTuple_Size", missing);
        find(a.PyTuple_GetItem, "PyTuple_GetItem", missing);
        find(a.PyTuple_New, "PyTuple_New", missing);
        find(a.PyTuple_SetItem, "PyTuple_SetItem", missing);
        find(a.PyList_Size, "PyList_Size", missing);
        find(a.PyList_GetItem, "PyList_GetItem", missing);
        find(a.PyObject_CallObject, "PyObject_CallObject", missing);
        find(a.PyCallable_Check, "PyCallable_Check", missing);
        find(a.PyErr_SetString, "PyErr_SetString", missing);
        find(a.PyErr_Occurred, "PyErr_Occurred", missing);
        find(a.PyErr_Print, "PyErr_Print", missing);
        find(a.PyGILState_Ensure, "PyGILState_Ensure", missing);
        find(a.PyGILState_Release, "PyGILState_Release", missing);
        find(a.PyEval_SaveThread, "PyEval_SaveThread", missing);
        find(a.PyEval_RestoreThread, "PyEval_RestoreThread", missing);
        a.none = object("_Py_NoneStruct", false, missing);
        a.runtime_error = object("PyExc_RuntimeError", true, missing);
        a.type_error = object("PyExc_TypeError", true, missing);
        if (!missing.empty()) return std::unexpected(std::format("the interpreter has no `{}` (openmui needs CPython 3.10 or later)", missing));
        return a;
    }();
    if (!resolved) return std::unexpected(resolved.error());
    return &*resolved;
}

}  // namespace omui::lang::python::capi
