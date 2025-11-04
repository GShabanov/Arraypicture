// _arraypicture_abi3.cpp
#define PY_SSIZE_T_CLEAN
#define Py_LIMITED_API 0x03080000

#include <Python.h>
#include <windows.h>
#include <vector>
#include <algorithm>

#include "arraypicture.h"


#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif


static HINSTANCE
get_hinstance()
{
    HMODULE mod = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&get_hinstance, &mod);
    if (!mod) 
        mod = GetModuleHandleW(NULL);

    return (HINSTANCE)mod;
}

static int
ensure_class_registered()
{
    static volatile bool done = false;

    if (!done)
    {
        if (!ArrayPicture::registerClass(get_hinstance()))
        {
            PyErr_SetString(PyExc_RuntimeError, "ArrayPicture::registerClass failed");
            return -1;
        }

        done = true;
    }

    return 0;
}

// ---- Extension type (heap type via PyType_FromSpec)

typedef struct {
    PyObject_HEAD
    HWND hwnd;
    UINT cx;
    UINT cy;
    int  marker;
    PyObject* on_point; // owning ref (may be NULL)
    ArrayPicture* pClass;
} APObject;

// forward
static void WINAPI notify_thunk(HWND from, const POINT* cell, void* user);

// tp_new
static PyObject*
AP_new(PyTypeObject* type, PyObject*, PyObject*)
{
    APObject* self = (APObject*)PyObject_New(APObject, type);
    if (!self) return nullptr;
    self->hwnd = NULL;
    self->cx = 64;
    self->cy = 64;
    self->marker = 1;
    self->on_point = NULL;
    return (PyObject*)self;
}

// tp_init
static int
AP_init(PyObject* self_, PyObject*, PyObject*)
{
    UNREFERENCED_PARAMETER(self_);

    // noting to do
    return 0;
}

// tp_dealloc
static void
AP_dealloc(PyObject* self_)
{
    APObject* self = (APObject*)self_;

    if (self->hwnd && IsWindow(self->hwnd))
    {
        self->pClass->setNotify(NULL, NULL);
        DestroyWindow(self->hwnd);
        self->hwnd = NULL;
    }

    Py_XDECREF(self->on_point);
    PyObject_Del(self_);
}

// methods

static PyObject*
AP_create(PyObject* self_, PyObject* args, PyObject* kw)
{
    static const char* kwlist[] = {
        "parent_hwnd", "x", "y", "w", "h", "cx", "cy", "markerSize", "granularity", NULL
    };

    unsigned long long parent_hwnd = 0ULL;
    int x, y, w, h;
    int cx = 64, cy = 64, marker = 1, granularity = 1;

    if (!PyArg_ParseTupleAndKeywords(
        args, kw, "Kiiii|iiii", (char**)kwlist,
        &parent_hwnd, &x, &y, &w, &h, &cx, &cy, &marker, &granularity))
    {
        return NULL;
    }

    if (ensure_class_registered() != 0)
    {
        return NULL;
    }

    APObject* self = (APObject*)self_;

    if (self->hwnd) {
        Py_RETURN_TRUE; // always created
    }

    ArrayPicture::ARRAYPICTURE_INIT init{};

    init.cx = (UINT)max(1, cx);
    init.cy = (UINT)max(1, cy);
    init.markerSize = max(1, marker);
    init.drawCursor = LoadCursor(NULL, IDC_CROSS);
    init.notify = &notify_thunk;
    init.notifyUser = self;
    init.granularity = granularity;

    HWND hParent = (HWND)(uintptr_t)parent_hwnd;

    self->pClass = new ArrayPicture();

    if (!self->pClass)
    {
        PyErr_SetString(PyExc_RuntimeError, "mem alloc is failed");
        return NULL;
    }

    if (!self->pClass->create(hParent, NULL, &init))
    {
        DWORD e = GetLastError();
        PyErr_Format(PyExc_RuntimeError, "CreateWindowExW(ArrayPicture) failed, err=%lu", e);
        return NULL;
    }

    HWND hwnd = self->pClass->hwnd();

    self->hwnd   = hwnd;
    self->cx     = init.cx;
    self->cy     = init.cy;
    self->marker = init.markerSize;

    Py_RETURN_TRUE;
}

static PyObject* AP_destroy(PyObject* self_, PyObject*)
{
    APObject* self = (APObject*)self_;
    if (self->hwnd && IsWindow(self->hwnd))
    {
        self->pClass->setNotify(NULL, NULL);
        delete self->pClass;

        DestroyWindow(self->hwnd);
    }
    self->hwnd = NULL;
    Py_RETURN_NONE;
}

static PyObject*
AP_set_granularity(PyObject* self_, PyObject* args)
{
    int g = 0;
    APObject* self = (APObject*)self_;

    if (!PyArg_ParseTuple(args, "i", &g))
        return NULL;

    if (self->hwnd)
        self->pClass->setGranularity(max(0, g));

    Py_RETURN_NONE;
}

static PyObject*
AP_clear(PyObject* self_, PyObject* /*noargs*/)
{
    APObject* self = (APObject*)self_;

    if (self->hwnd)
        self->pClass->clear();

    Py_RETURN_NONE;
}

static int
ucs4_to_utf16(uint32_t cp, WCHAR out[3])
{
    if (cp == 0) { out[0] = 0; return 0; }
    if (cp <= 0xFFFF)
    {
        out[0] = (WCHAR)cp;
        out[1] = 0;
        return 1;
    }
    if (cp <= 0x10FFFF)
    {
        cp -= 0x10000;
        out[0] = (WCHAR)(0xD800 | (cp >> 10));      // старший сурогат
        out[1] = (WCHAR)(0xDC00 | (cp & 0x3FF));    // молодший сурогат
        out[2] = 0;
        return 2;
    }
    return 0; // поза діапазоном
}

static PyObject*
AP_set_input(PyObject* self_, PyObject* args)
{
    APObject* self = (APObject*)self_;
    PyObject* matrix = NULL;
    if (!PyArg_ParseTuple(args, "O", &matrix)) return NULL;

    if (!PyList_Check(matrix))
    {
        PyErr_SetString(PyExc_TypeError, "set_input expects list[list[int]]");
        return NULL;
    }

    if (!self->hwnd)
        Py_RETURN_NONE;

    std::vector<std::vector<ArrayPicture::ARRAYPICTURE_CELL>> data;

    Py_ssize_t rows = PyList_Size(matrix);

    data.resize((size_t)rows);

    for (Py_ssize_t y = 0; y < rows; ++y)
    {
        PyObject* row = PyList_GetItem(matrix, y);

        if (!PyList_Check(row)) {
            PyErr_SetString(PyExc_TypeError, "set_input expects list[list[int]]");
            return NULL;
        }

        Py_ssize_t cols = PyList_Size(row);
        data[(size_t)y].resize((size_t)cols);

        for (Py_ssize_t x = 0; x < cols; ++x)
        {

            PyObject* it = PyList_GetItem(row, x);
            if (!PyDict_Check(it)) { PyErr_SetString(PyExc_TypeError, "cell must be dict"); return NULL; }

            ArrayPicture::ARRAYPICTURE_CELL c{};
            // color
            PyObject* oCol = PyDict_GetItemString(it, "color");
            unsigned int rgb = 0xFFFFFF;

            if (oCol) {
                long long v = PyLong_AsLongLong(oCol);
                if (PyErr_Occurred()) return NULL;
                rgb = (unsigned)v & 0xFFFFFFu;
            }
            c.color = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);

            // symbol
            PyObject* oSym = PyDict_GetItemString(it, "symbol");

            if (oSym && oSym != Py_None && PyUnicode_Check(oSym))
            {

                // get first symbol
                Py_ssize_t len = 0;
                len = PyUnicode_GetLength(oSym);

                Py_UCS4 ch = PyUnicode_ReadChar(oSym, 0);


                if (len > 0)
                {
                    WCHAR wbuf[3] = { 0 };
                    ucs4_to_utf16(ch, wbuf);

                    c.symbol = wbuf[0];
                }
            }

            // data 
            PyObject* oData = PyDict_GetItemString(it, "data");

            if (oData && oData != Py_None)
            {

                unsigned long long h = PyLong_AsUnsignedLongLong(oData);
                if (PyErr_Occurred()) return NULL;
                c.data = (PVOID)(uintptr_t)h;
            }

            data[(size_t)y][(size_t)x] = c;
        }
    }

    self->pClass->setInputArray(data);
    Py_RETURN_NONE;
}

static PyObject*
AP_serialize_cells(PyObject* self_, PyObject*)
{

    APObject* self = (APObject*)self_;
    auto v = self->pClass->Serialize();

    PyObject* out = PyList_New((Py_ssize_t)v.size());

    if (!out)
        return NULL;

    for (Py_ssize_t i = 0; i < (Py_ssize_t)v.size(); ++i)
    {
        const auto& c = v[(size_t)i];

        unsigned int rgb = (unsigned(GetRValue(c.color)) << 16) |
            (unsigned(GetGValue(c.color)) << 8) |
            (unsigned(GetBValue(c.color)));

        PyObject* d = PyDict_New();

        if (!d)
        {
            Py_DECREF(out);
            return NULL;
        }

        PyObject* vColor = PyLong_FromUnsignedLong(rgb);
        PyObject* vSym = c.symbol ? PyUnicode_FromWideChar(&c.symbol, 1) : Py_NewRef(Py_None);
        PyObject* vData = c.data ? PyLong_FromUnsignedLongLong((unsigned long long)(uintptr_t)c.data) : Py_NewRef(Py_None);

        if (!vColor || !vSym || !vData ||
            PyDict_SetItemString(d, "color", vColor) != 0 ||
            PyDict_SetItemString(d, "symbol", vSym) != 0 ||
            PyDict_SetItemString(d, "data", vData) != 0) {
            Py_XDECREF(vColor); Py_XDECREF(vSym); Py_XDECREF(vData);
            Py_DECREF(d); Py_DECREF(out); return NULL;
        }

        Py_DECREF(vColor);
        Py_DECREF(vSym);
        Py_DECREF(vData);

        if (PyList_SetItem(out, i, d) != 0) 
        {
            Py_DECREF(d);
            Py_DECREF(out);
            return NULL;
        } // steals ref

    }

    return out;
}
static PyObject* 
AP_serialize_rgb(PyObject* self_, PyObject*)
{
    APObject* self = (APObject*)self_;

    if (!self->hwnd)
        return PyList_New(0);

    std::vector<ArrayPicture::ARRAYPICTURE_CELL> v = self->pClass->Serialize();


    Py_ssize_t n = (Py_ssize_t)v.size();
    PyObject* list = PyList_New(n);

    if (!list) return NULL;

    for (Py_ssize_t i=0; i<n; ++i) {

        COLORREF c = v[(size_t)i].color;

        unsigned int rgb = (unsigned(GetRValue(c)) << 16) |
                           (unsigned(GetGValue(c)) << 8)  |
                           (unsigned(GetBValue(c)));

        PyObject* val = PyLong_FromUnsignedLong(rgb);

        if (!val) {
            Py_DECREF(list);
            return NULL;
        }

        PyList_SetItem(list, i, val);
    }

    return list;
}

static PyObject*
AP_set_on_point(PyObject* self_, PyObject* args)
{
    APObject* self = (APObject*)self_;
    PyObject* cb = NULL;

    if (!PyArg_ParseTuple(args, "O", &cb))
        return NULL;

    if (cb == Py_None)
    {
        if (self->on_point)
        {
            Py_DECREF(self->on_point);
            self->on_point = NULL;
        }

        if (self->hwnd)
            self->pClass->setNotify(&notify_thunk, self);

        Py_RETURN_NONE;
    }

    if (!PyCallable_Check(cb))
    {
        PyErr_SetString(PyExc_TypeError, "callback must be callable or None");
        return NULL;
    }

    Py_INCREF(cb);
    Py_XDECREF(self->on_point);
    self->on_point = cb;

    if (self->hwnd)
        self->pClass->setNotify(&notify_thunk, self);

    Py_RETURN_NONE;
}

// getset

static PyObject* AP_get_hwnd(PyObject* self_, void*)
{
    APObject* self = (APObject*)self_;
    return PyLong_FromUnsignedLongLong((unsigned long long)(uintptr_t)self->hwnd);
}

static PyObject* AP_get_width(PyObject* self_, void*)
{
    APObject* self = (APObject*)self_;
    int w = self->hwnd ? self->pClass->getWidth() : 0;

    return PyLong_FromLong(w);
}

static PyObject* AP_get_height(PyObject* self_, void*) {
    APObject* self = (APObject*)self_;
    int h = self->hwnd ? self->pClass->getHeight() : 0;

    return PyLong_FromLong(h);
}

// WinAPI callback -> Python
static void WINAPI notify_thunk(HWND from, const POINT* cell, void* user) {
    APObject* self = (APObject*)user;
    if (!self || from != self->hwnd) return;
    if (!self->on_point) return;

    // GIL-safe
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject* res = PyObject_CallFunction(self->on_point, "ii", cell->x, cell->y);
    if (!res) {
        PyErr_Print(); // log to stderr
    } else {
        Py_DECREF(res);
    }
    PyGILState_Release(gil);
}

// tables

static PyMethodDef AP_methods[] = {
    {"create",         (PyCFunction)(void*)AP_create,         METH_VARARGS|METH_KEYWORDS, "create(parent_hwnd, x, y, w, h, cx = 64, cy = 64, markerSize = 1, granularity = 24)"},
    {"destroy",        (PyCFunction)AP_destroy,               METH_NOARGS,  "Destroy control"},
    {"set_granularity",(PyCFunction)AP_set_granularity,       METH_VARARGS, "Set pixel size"},
    {"clear",          (PyCFunction)AP_clear,                 METH_NOARGS,  "Fill with white"},
    {"set_input",      (PyCFunction)AP_set_input,             METH_VARARGS, "Set matrix of #RRGGBB ints (list[list[int]])"},
    {"serialize_rgb",  (PyCFunction)AP_serialize_rgb,         METH_NOARGS,  "Get row-major #RRGGBB ints"},
    {"serialize_cells",(PyCFunction)AP_serialize_cells,       METH_NOARGS,  "Get row-major cell data"},
    {"set_on_point",   (PyCFunction)AP_set_on_point,          METH_VARARGS, "Set callback (x:int, y:int) or None"},
    {NULL, NULL, 0, NULL}
};

static PyGetSetDef AP_getset[] = {
    {(char*)"hwnd",   AP_get_hwnd,   NULL, (char*)"HWND as int", NULL},
    {(char*)"width",  AP_get_width,  NULL, (char*)"grid width",  NULL},
    {(char*)"height", AP_get_height, NULL, (char*)"grid height", NULL},
    {NULL, NULL, NULL, NULL, NULL}
};

// slots
static PyType_Slot AP_slots[] = {
    {Py_tp_new,      (void*)AP_new},
    {Py_tp_init,     (void*)AP_init},
    {Py_tp_dealloc,  (void*)AP_dealloc},
    {Py_tp_methods,  (void*)AP_methods},
    {Py_tp_getset,   (void*)AP_getset},
    {Py_tp_doc,      (void*)"ArrayPicture WinAPI control (abi3)"},
    {0, 0}
};

static PyType_Spec AP_spec = {
    "arraypicture.ArrayPicture",        // qualname
    sizeof(APObject),
    0,                                   // itemsize
    Py_TPFLAGS_DEFAULT,
    AP_slots
};

// module

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "arraypicture",
    "WinAPI ArrayPicture control (CPython abi3)",
    -1,
    NULL, NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit_arraypicture(void)
{
    if (ensure_class_registered() != 0) return NULL;

    PyObject* m = PyModule_Create(&moduledef);
    if (!m) return NULL;

    PyObject* type = PyType_FromSpec(&AP_spec);
    if (!type) { Py_DECREF(m); return NULL; }

    if (PyModule_AddObject(m, "ArrayPicture", type) < 0) {
        Py_DECREF(type);
        Py_DECREF(m);
        return NULL;
    }
    return m;
}
