#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <windows.h>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include "arraypicture.h"

// ------------------------------
//  CPython object
// ------------------------------
typedef struct {
    PyObject_HEAD
    HWND hwnd;
    UINT cx;
    UINT cy;
    int  marker;
    PyObject* on_point;  // borrowed/owned? -> owned, INCREF management
    ArrayPicture* pClass;
} PyArrayPictureObject;

static HINSTANCE get_hinstance()
{
    HMODULE hMod = NULL;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCTSTR)&get_hinstance, &hMod);

    if (!hMod) 
        hMod = GetModuleHandle(NULL);

    return (HINSTANCE)hMod;
}

static int
ensure_class_registered()
{
    static volatile bool done = false;

    if (!done)
    {
        if (!ArrayPicture::registerClass(get_hinstance()))
        {
            PyErr_SetString(PyExc_RuntimeError, "ArrayPicture_RegisterClass failed");
            return -1;
        }

        done = true;
    }

    return 0;
}

static void
__stdcall
notify_thunk(HWND from, const POINT* cell, void* user)
{
    PyArrayPictureObject* self = (PyArrayPictureObject*)user;
    if (!self || from != self->hwnd)
        return;

    if (!self->on_point)
        return;

    // call Python-callback
    PyGILState_STATE gil = PyGILState_Ensure();

    PyObject* res = PyObject_CallFunction(self->on_point, "ii", cell->x, cell->y);

    if (!res) {
        // do not throw exception — just print into stderr
        PyErr_Print();
    }
    else
    {

        Py_DECREF(res);    
    }

    PyGILState_Release(gil);
}

static int
PyArrayPicture_init(PyArrayPictureObject* self, PyObject* args, PyObject* kw)
{
    kw = kw;
    args = args;

    self->hwnd = NULL;
    self->cx = 64;
    self->cy = 64;
    self->marker = 1;
    self->on_point = NULL;
    return 0;
}

static void
PyArrayPicture_dealloc(PyArrayPictureObject* self)
{
    // destroy call in case of neccesety
    if (self->hwnd && IsWindow(self->hwnd))
    {

        self->pClass->setNotify(NULL, NULL);
        DestroyWindow(self->hwnd);
        self->hwnd = NULL;
    }

    Py_XDECREF(self->on_point);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject*
PyArrayPicture_create(PyArrayPictureObject* self, PyObject* args, PyObject* kw)
{
    static const char* kwlist[] = {
        "parent_hwnd", "x", "y", "w", "h", "cx", "cy", "markerSize", NULL
    };

    unsigned long long parent_hwnd = 0ULL;
    int x, y, w, h;
    int cx = 64, cy = 64, marker = 1;

    if (!PyArg_ParseTupleAndKeywords(
        args, kw, "Kiiii|iii", (char**)kwlist,
        &parent_hwnd, &x, &y, &w, &h, &cx, &cy, &marker))
    {
        return NULL;
    }

    if (ensure_class_registered() != 0)
    {
        return NULL;
    }

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
    init.granularity = 6;

    HWND hParent = (HWND)(uintptr_t)parent_hwnd;

    self->pClass = new ArrayPicture();

    if (!self->pClass) {
        PyErr_SetString(PyExc_RuntimeError, "mem alloc is failed");
        return NULL;
    }

    self->pClass->create(hParent, NULL, &init);

    self->hwnd = self->pClass->hwnd();

    if (!self->hwnd) {
        PyErr_SetString(PyExc_RuntimeError, "CreateWindowExW(ArrayPicture) failed");
        return NULL;
    }

    self->cx = init.cx;
    self->cy = init.cy;
    self->marker = init.markerSize;

    Py_RETURN_TRUE;
}

static PyObject*
PyArrayPicture_destroy(PyArrayPictureObject* self, PyObject* /*noargs*/)
{
    if (self->hwnd && IsWindow(self->hwnd))
    {

        self->pClass->setNotify(NULL, NULL);
        delete self->pClass;
        self->pClass = NULL;
        //DestroyWindow(self->hwnd);
    }

    self->hwnd = NULL;
    Py_RETURN_NONE;
}

static PyObject*
PyArrayPicture_hwnd(PyArrayPictureObject* self, void* /*closure*/)
{
    return PyLong_FromUnsignedLongLong((unsigned long long)(uintptr_t)self->hwnd);
}

static PyObject*
PyArrayPicture_width(PyArrayPictureObject* self, void* /*closure*/)
{
    int w = self->hwnd ? self->pClass->getWidth() : 0;

    return PyLong_FromLong(w);
}

static PyObject*
PyArrayPicture_height(PyArrayPictureObject* self, void* /*closure*/)
{
    int h = self->hwnd ? self->pClass->getHeight() : 0;

    return PyLong_FromLong(h);
}

static PyObject*
PyArrayPicture_set_granularity(PyArrayPictureObject* self, PyObject* args)
{
    int g = 0;

    if (!PyArg_ParseTuple(args, "i", &g))
        return NULL;

    if (self->hwnd)
        self->pClass->setGranularity(max(0, g));

    Py_RETURN_NONE;
}

static PyObject*
PyArrayPicture_clear(PyArrayPictureObject* self, PyObject* /*noargs*/)
{
    if (self->hwnd)
        self->pClass->clear();

    Py_RETURN_NONE;
}

static PyObject*
PyArrayPicture_set_input(PyArrayPictureObject* self, PyObject* args)
{
    PyObject* matrix = NULL; // expect for list[list[int]]

    if (!PyArg_ParseTuple(args, "O", &matrix)) return NULL;

    if (!PyList_Check(matrix))
    {

        PyErr_SetString(PyExc_TypeError, "set_input expects list[list[int]]");
        return NULL;
    }

    if (!self->hwnd)
        Py_RETURN_NONE;

    std::vector<std::vector<COLORREF>> data;
    Py_ssize_t rows = PyList_GET_SIZE(matrix);

    data.resize((size_t)rows);

    for (Py_ssize_t y = 0; y < rows; ++y)
    {
        PyObject* row = PyList_GET_ITEM(matrix, y);

        if (!PyList_Check(row)) {
            PyErr_SetString(PyExc_TypeError, "set_input expects list[list[int]]");
            return NULL;
        }

        Py_ssize_t cols = PyList_GET_SIZE(row);

        data[(size_t)y].resize((size_t)cols);

        for (Py_ssize_t x = 0; x < cols; ++x)
        {

            PyObject* it = PyList_GET_ITEM(row, x);

            long long v = PyLong_AsLongLong(it);

            if (PyErr_Occurred())
                return NULL;

            unsigned int rgb = (unsigned int)(v & 0xFFFFFFu);
            BYTE R = (BYTE)((rgb >> 16) & 0xFF);
            BYTE G = (BYTE)((rgb >> 8) & 0xFF);
            BYTE B = (BYTE)((rgb) & 0xFF);
            data[(size_t)y][(size_t)x] = RGB(R, G, B); // WinAPI: 0x00BBGGRR
        }
    }

    self->pClass->setInputArray(data);

    Py_RETURN_NONE;
}

static PyObject*
PyArrayPicture_serialize_rgb(PyArrayPictureObject* self, PyObject* /*noargs*/)
{
    PyObject* out = PyList_New(0);

    if (!out)
        return NULL;

    if (!self->hwnd)
        return out;

    std::vector<COLORREF> v = self->pClass->Serialize();

    const Py_ssize_t n = (Py_ssize_t)v.size();
    PyObject* list = PyList_New(n);

    if (!list) 
    {
        Py_DECREF(out);
        return NULL;
    }

    for (Py_ssize_t i = 0; i < n; ++i)
    {

        COLORREF c = v[(size_t)i];

        unsigned int rgb = (unsigned(GetRValue(c)) << 16) |
            (unsigned(GetGValue(c)) << 8) |
            (unsigned(GetBValue(c)));

        PyObject* val = PyLong_FromUnsignedLong(rgb);

        if (!val)
        {
            Py_DECREF(list);
            Py_DECREF(out);
            return NULL;
        }

        PyList_SET_ITEM(list, i, val); // steals ref
    }

    Py_DECREF(out);

    return list;
}

static PyObject*
PyArrayPicture_set_on_point(PyArrayPictureObject* self, PyObject* args)
{
    PyObject* cb = NULL; // callable or None

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
            self->pClass->setNotify(&notify_thunk, self); // left thunk, but without callback

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

// ------------------------------
//  Type & module
// ------------------------------
static PyGetSetDef PyArrayPicture_getset[] = 
{
    { (char*)"hwnd",   (getter)PyArrayPicture_hwnd,   NULL, (char*)"HWND as int", NULL },
    { (char*)"width",  (getter)PyArrayPicture_width,  NULL, (char*)"grid width",  NULL },
    { (char*)"height", (getter)PyArrayPicture_height, NULL, (char*)"grid height", NULL },
    { NULL, NULL, NULL, NULL, NULL }
};

static PyMethodDef PyArrayPicture_methods[] = {
    {"create",         (PyCFunction)PyArrayPicture_create,          METH_VARARGS | METH_KEYWORDS, "create(parent_hwnd, x,y,w,h, cx=64, cy=64, markerSize=1)"},
    {"destroy",        (PyCFunction)PyArrayPicture_destroy,         METH_NOARGS,  "Destroy control"},
    {"set_granularity",(PyCFunction)PyArrayPicture_set_granularity, METH_VARARGS,"Set pixel size"},
    {"clear",          (PyCFunction)PyArrayPicture_clear,           METH_NOARGS,  "Fill with white"},
    {"set_input",      (PyCFunction)PyArrayPicture_set_input,       METH_VARARGS, "Set matrix of #RRGGBB ints (list[list[int]])"},
    {"serialize_rgb",  (PyCFunction)PyArrayPicture_serialize_rgb,   METH_NOARGS,  "Get row-major #RRGGBB ints"},
    {"set_on_point",   (PyCFunction)PyArrayPicture_set_on_point,    METH_VARARGS, "Set callback (x:int,y:int)"},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject PyArrayPictureType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "arraypicture.ArrayPicture",              /* tp_name */
    sizeof(PyArrayPictureObject),             /* tp_basicsize */
    0,                                        /* tp_itemsize */
    (destructor)PyArrayPicture_dealloc,       /* tp_dealloc */
    0,                                        /* tp_vectorcall_offset */
    0, 0, 0, 0,
    0,                                        /* tp_repr */
    0, 0, 0, 0, 0, 0,                          /* tp_as_number/sequence/mapping/call/str/getattro */
    0, 0,                                     /* tp_setattro/buffer */
    Py_TPFLAGS_DEFAULT,                       /* tp_flags */
    "ArrayPicture WinAPI control",            /* tp_doc */
    0, 0, 0, 0, 0, 0,                          /* traverse/clear/richcompare/weaklist/iter/iternext */
    PyArrayPicture_methods,                   /* tp_methods */
    0, 0,                                     /* tp_members/getset (we use getset below) */
    0, 0, 0, 0, 0,                            /* tp_base / tp_dict / tp_descr_get / tp_descr_set / tp_dictoffset */
    (initproc)PyArrayPicture_init,            /* tp_init */
    0,                                        /* tp_alloc */
    PyType_GenericNew,                        /* tp_new */
};

static struct PyModuleDef arraypicture_module = {
    PyModuleDef_HEAD_INIT,
    "arraypicture",
    "WinAPI ArrayPicture control (CPython C-API)",
    -1,
    NULL, NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_arraypicture(void)
{
    if (ensure_class_registered() != 0)
        return NULL;

    if (PyType_Ready(&PyArrayPictureType) < 0)
        return NULL;

    PyObject* m = PyModule_Create(&arraypicture_module);
    if (!m) return NULL;

    // add properties (getset)

    PyTypeObject* T = &PyArrayPictureType;

    T->tp_getset = PyArrayPicture_getset;

    Py_INCREF(&PyArrayPictureType);

    if (PyModule_AddObject(m, "ArrayPicture", (PyObject*)&PyArrayPictureType) < 0)
    {
        Py_DECREF(&PyArrayPictureType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}
