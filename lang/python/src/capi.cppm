// openmui.lang.python:capi — the part of CPython's stable ABI (PEP 384; the
// limited API of 3.10) the extension calls, declared here rather than taken
// from Python.h: the layouts the stable ABI fixes, and the functions it
// exports, found in the running interpreter when the module loads. One build
// serves every CPython 3.10+ of a platform, without headers or an import
// library. (Free-threaded builds have another ABI and are not served.)
export module openmui.lang.python:capi;

import std;

export namespace omui::lang::python::capi {

using Py_ssize_t = std::ptrdiff_t;

struct PyTypeObject;
struct PyThreadState;

struct PyObject {
    Py_ssize_t    ob_refcnt;
    PyTypeObject* ob_type;
};

using PyCFunction = PyObject* (*)(PyObject* self, PyObject* args);

struct PyMethodDef {
    const char* ml_name;
    PyCFunction ml_meth;
    int         ml_flags;
    const char* ml_doc;
};

struct PyModuleDef_Base {
    PyObject   ob_base;
    PyObject* (*m_init)();
    Py_ssize_t m_index;
    PyObject*  m_copy;
};

struct PyModuleDef_Slot {
    int   slot;
    void* value;
};

struct PyModuleDef {
    PyModuleDef_Base  m_base;
    const char*       m_name;
    const char*       m_doc;
    Py_ssize_t        m_size;
    PyMethodDef*      m_methods;
    PyModuleDef_Slot* m_slots;
    int (*m_traverse)(PyObject*, void*, void*);
    int (*m_clear)(PyObject*);
    void (*m_free)(void*);
};

inline constexpr int METH_VARARGS = 0x0001;
inline constexpr int METH_NOARGS = 0x0004;
inline constexpr int python_abi_version = 3;  // PYTHON_ABI_VERSION: abi3

enum PyGILState_STATE : int { PyGILState_LOCKED, PyGILState_UNLOCKED };

// The functions and objects of the interpreter the module calls.
struct Api {
    PyObject* (*PyModule_Create2)(PyModuleDef*, int);
    void (*Py_IncRef)(PyObject*);
    void (*Py_DecRef)(PyObject*);
    PyObject* (*PyUnicode_FromStringAndSize)(const char*, Py_ssize_t);
    const char* (*PyUnicode_AsUTF8AndSize)(PyObject*, Py_ssize_t*);
    PyObject* (*PyLong_FromLongLong)(long long);
    long long (*PyLong_AsLongLong)(PyObject*);
    Py_ssize_t (*PyTuple_Size)(PyObject*);
    PyObject* (*PyTuple_GetItem)(PyObject*, Py_ssize_t);  // borrowed
    PyObject* (*PyTuple_New)(Py_ssize_t);
    int (*PyTuple_SetItem)(PyObject*, Py_ssize_t, PyObject*);  // steals
    Py_ssize_t (*PyList_Size)(PyObject*);
    PyObject* (*PyList_GetItem)(PyObject*, Py_ssize_t);  // borrowed
    PyObject* (*PyObject_CallObject)(PyObject*, PyObject*);
    int (*PyCallable_Check)(PyObject*);
    void (*PyErr_SetString)(PyObject*, const char*);
    PyObject* (*PyErr_Occurred)();
    void (*PyErr_Print)();
    PyGILState_STATE (*PyGILState_Ensure)();
    void (*PyGILState_Release)(PyGILState_STATE);
    PyThreadState* (*PyEval_SaveThread)();
    void (*PyEval_RestoreThread)(PyThreadState*);
    PyObject* none;           // &_Py_NoneStruct
    PyObject* runtime_error;  // PyExc_RuntimeError
    PyObject* type_error;     // PyExc_TypeError
};

// The interpreter's functions, found once; an error names the first missing.
[[nodiscard]] std::expected<const Api*, std::string> api();

}  // namespace omui::lang::python::capi
