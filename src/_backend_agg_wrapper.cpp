#include "mplutils.h"
#include "numpy_cpp.h"
#include "py_converters.h"
#include "_backend_agg.h"

typedef struct
{
    PyObject_HEAD
    RendererAgg *x;
    Py_ssize_t shape[3];
    Py_ssize_t strides[3];
    Py_ssize_t suboffsets[3];
} PyRendererAgg;

#if !TARGET_OS_IPHONE
static PyTypeObject PyRendererAggType;
#else
static __thread PyTypeObject PyRendererAggType;
#endif

typedef struct
{
    PyObject_HEAD
    BufferRegion *x;
    Py_ssize_t shape[3];
    Py_ssize_t strides[3];
    Py_ssize_t suboffsets[3];
} PyBufferRegion;

#if !TARGET_OS_IPHONE
static PyTypeObject PyBufferRegionType;
#else
static __thread PyTypeObject PyBufferRegionType;
#endif


/**********************************************************************
 * BufferRegion
 * */

static PyObject *PyBufferRegion_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyBufferRegion *self;
    self = (PyBufferRegion *)type->tp_alloc(type, 0);
    self->x = NULL;
    return (PyObject *)self;
}

static void PyBufferRegion_dealloc(PyBufferRegion *self)
{
    delete self->x;
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* TODO: This doesn't seem to be used internally.  Remove? */

static PyObject *PyBufferRegion_set_x(PyBufferRegion *self, PyObject *args)
{
    int x;
    if (!PyArg_ParseTuple(args, "i:set_x", &x)) {
        return NULL;
    }
    self->x->get_rect().x1 = x;

    Py_RETURN_NONE;
}

static PyObject *PyBufferRegion_set_y(PyBufferRegion *self, PyObject *args)
{
    int y;
    if (!PyArg_ParseTuple(args, "i:set_y", &y)) {
        return NULL;
    }
    self->x->get_rect().y1 = y;

    Py_RETURN_NONE;
}

static PyObject *PyBufferRegion_get_extents(PyBufferRegion *self, PyObject *args)
{
    agg::rect_i rect = self->x->get_rect();

    return Py_BuildValue("IIII", rect.x1, rect.y1, rect.x2, rect.y2);
}

int PyBufferRegion_get_buffer(PyBufferRegion *self, Py_buffer *buf, int flags)
{
    Py_INCREF(self);
    buf->obj = (PyObject *)self;
    buf->buf = self->x->get_data();
    buf->len = (Py_ssize_t)self->x->get_width() * (Py_ssize_t)self->x->get_height() * 4;
    buf->readonly = 0;
    buf->format = (char *)"B";
    buf->ndim = 3;
    self->shape[0] = self->x->get_height();
    self->shape[1] = self->x->get_width();
    self->shape[2] = 4;
    buf->shape = self->shape;
    self->strides[0] = self->x->get_width() * 4;
    self->strides[1] = 4;
    self->strides[2] = 1;
    buf->strides = self->strides;
    buf->suboffsets = NULL;
    buf->itemsize = 1;
    buf->internal = NULL;

    return 1;
}

static PyTypeObject *PyBufferRegion_init_type()
{
#if !TARGET_OS_IPHONE
    static PyMethodDef methods[] = {
#else
    static __thread PyMethodDef methods[] = {
#endif
        { "set_x", (PyCFunction)PyBufferRegion_set_x, METH_VARARGS, NULL },
        { "set_y", (PyCFunction)PyBufferRegion_set_y, METH_VARARGS, NULL },
        { "get_extents", (PyCFunction)PyBufferRegion_get_extents, METH_NOARGS, NULL },
        { NULL }
    };

#if !TARGET_OS_IPHONE
    static PyBufferProcs buffer_procs;
#else
    static __thread PyBufferProcs buffer_procs;
#endif
    buffer_procs.bf_getbuffer = (getbufferproc)PyBufferRegion_get_buffer;

    PyBufferRegionType.tp_name = "matplotlib.backends._backend_agg.BufferRegion";
    PyBufferRegionType.tp_basicsize = sizeof(PyBufferRegion);
    PyBufferRegionType.tp_dealloc = (destructor)PyBufferRegion_dealloc;
    PyBufferRegionType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    PyBufferRegionType.tp_methods = methods;
    PyBufferRegionType.tp_new = PyBufferRegion_new;
    PyBufferRegionType.tp_as_buffer = &buffer_procs;

    return &PyBufferRegionType;
}

/**********************************************************************
 * RendererAgg
 * */

static PyObject *PyRendererAgg_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyRendererAgg *self;
    self = (PyRendererAgg *)type->tp_alloc(type, 0);
    self->x = NULL;
    return (PyObject *)self;
}

static int PyRendererAgg_init(PyRendererAgg *self, PyObject *args, PyObject *kwds)
{
    unsigned int width;
    unsigned int height;
    double dpi;
    int debug = 0;

    if (!PyArg_ParseTuple(args, "IId|i:RendererAgg", &width, &height, &dpi, &debug)) {
        return -1;
    }

    if (dpi <= 0.0) {
        PyErr_SetString(PyExc_ValueError, "dpi must be positive");
        return -1;
    }

    if (width >= 1 << 16 || height >= 1 << 16) {
        PyErr_Format(
            PyExc_ValueError,
            "Image size of %dx%d pixels is too large. "
            "It must be less than 2^16 in each direction.",
            width, height);
        return -1;
    }

    CALL_CPP_INIT("RendererAgg", self->x = new RendererAgg(width, height, dpi))

    return 0;
}

static void PyRendererAgg_dealloc(PyRendererAgg *self)
{
    delete self->x;
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *PyRendererAgg_draw_path(PyRendererAgg *self, PyObject *args)
{
    GCAgg gc;
    mpl::PathIterator path;
    agg::trans_affine trans;
    PyObject *faceobj = NULL;
    agg::rgba face;

    if (!PyArg_ParseTuple(args,
                          "O&O&O&|O:draw_path",
                          &convert_gcagg,
                          &gc,
                          &convert_path,
                          &path,
                          &convert_trans_affine,
                          &trans,
                          &faceobj)) {
        return NULL;
    }

    if (!convert_face(faceobj, gc, &face)) {
        return NULL;
    }

    CALL_CPP("draw_path", (self->x->draw_path(gc, path, trans, face)));

    Py_RETURN_NONE;
}

#if TARGET_OS_IPHONE
// iOS: need non static member functions. This replaces image.converter_contiguous:
static int (*image2_converter_contiguous)(PyObject *obj, void *arrp) = numpy::numpy_converter_contiguous<agg::int8u, 2>;
#endif

static PyObject *PyRendererAgg_draw_text_image(PyRendererAgg *self, PyObject *args)
{
    numpy::array_view<agg::int8u, 2> image;
    double x;
    double y;
    double angle;
    GCAgg gc;

    if (!PyArg_ParseTuple(args,
                          "O&dddO&:draw_text_image",
#if !TARGET_OS_IPHONE
                          &image.converter_contiguous,
#else
						  image2_converter_contiguous,
#endif
                          &image,
                          &x,
                          &y,
                          &angle,
                          &convert_gcagg,
                          &gc)) {
        return NULL;
    }

    CALL_CPP("draw_text_image", (self->x->draw_text_image(gc, image, x, y, angle)));

    Py_RETURN_NONE;
}

PyObject *PyRendererAgg_draw_markers(PyRendererAgg *self, PyObject *args)
{
    GCAgg gc;
    mpl::PathIterator marker_path;
    agg::trans_affine marker_path_trans;
    mpl::PathIterator path;
    agg::trans_affine trans;
    PyObject *faceobj = NULL;
    agg::rgba face;

    if (!PyArg_ParseTuple(args,
                          "O&O&O&O&O&|O:draw_markers",
                          &convert_gcagg,
                          &gc,
                          &convert_path,
                          &marker_path,
                          &convert_trans_affine,
                          &marker_path_trans,
                          &convert_path,
                          &path,
                          &convert_trans_affine,
                          &trans,
                          &faceobj)) {
        return NULL;
    }

    if (!convert_face(faceobj, gc, &face)) {
        return NULL;
    }

    CALL_CPP("draw_markers",
             (self->x->draw_markers(gc, marker_path, marker_path_trans, path, trans, face)));

    Py_RETURN_NONE;
}

#if TARGET_OS_IPHONE
// iOS: need non static member functions. This replaces image.converter_contiguous:
static int (*image3_converter_contiguous)(PyObject *obj, void *arrp) = numpy::numpy_converter_contiguous<agg::int8u, 3>;
#endif

static PyObject *PyRendererAgg_draw_image(PyRendererAgg *self, PyObject *args)
{
    GCAgg gc;
    double x;
    double y;
    numpy::array_view<agg::int8u, 3> image;

    if (!PyArg_ParseTuple(args,
                          "O&ddO&:draw_image",
                          &convert_gcagg,
                          &gc,
                          &x,
                          &y,
#if !TARGET_OS_IPHONE
                          &image.converter_contiguous,
#else
						  image3_converter_contiguous,
#endif
                          &image)) {
        return NULL;
    }

    x = mpl_round(x);
    y = mpl_round(y);

    gc.alpha = 1.0;
    CALL_CPP("draw_image", (self->x->draw_image(gc, x, y, image)));

    Py_RETURN_NONE;
}

#if TARGET_OS_IPHONE
// iOS: need non static member functions. This replaces linewidths.converter and antialiaseds.converter:
static int (*linewidths_converter)(PyObject *, void *) = numpy::numpy_converter<const double, 1>;
static int (*antialiaseds_converter)(PyObject *, void *) = numpy::numpy_converter<const uint8_t, 1>;
#endif

static PyObject *
PyRendererAgg_draw_path_collection(PyRendererAgg *self, PyObject *args)
{
    GCAgg gc;
    agg::trans_affine master_transform;
    mpl::PathGenerator paths;
    numpy::array_view<const double, 3> transforms;
    numpy::array_view<const double, 2> offsets;
    agg::trans_affine offset_trans;
    numpy::array_view<const double, 2> facecolors;
    numpy::array_view<const double, 2> edgecolors;
    numpy::array_view<const double, 1> linewidths;
    DashesVector dashes;
    numpy::array_view<const uint8_t, 1> antialiaseds;
    PyObject *ignored;
    PyObject *offset_position; // offset position is no longer used

    if (!PyArg_ParseTuple(args,
                          "O&O&O&O&O&O&O&O&O&O&O&OO:draw_path_collection",
                          &convert_gcagg,
                          &gc,
                          &convert_trans_affine,
                          &master_transform,
                          &convert_pathgen,
                          &paths,
                          &convert_transforms,
                          &transforms,
                          &convert_points,
                          &offsets,
                          &convert_trans_affine,
                          &offset_trans,
                          &convert_colors,
                          &facecolors,
                          &convert_colors,
                          &edgecolors,
#if !TARGET_OS_IPHONE
                          &linewidths.converter,
#else
						  linewidths_converter,
#endif
                          &linewidths,
                          &convert_dashes_vector,
                          &dashes,
#if !TARGET_OS_IPHONE
                          &antialiaseds.converter,
#else
						  antialiaseds_converter,
#endif
                          &antialiaseds,
                          &ignored,
                          &offset_position)) {
        return NULL;
    }

    CALL_CPP("draw_path_collection",
             (self->x->draw_path_collection(gc,
                                            master_transform,
                                            paths,
                                            transforms,
                                            offsets,
                                            offset_trans,
                                            facecolors,
                                            edgecolors,
                                            linewidths,
                                            dashes,
                                            antialiaseds)));

    Py_RETURN_NONE;
}


#if TARGET_OS_IPHONE
// iOS: need non static member functions. This replaces coordinates.converter:
int (*coordinates_converter)(PyObject *, void *) = numpy::numpy_converter<const double, 3>;
#endif

static PyObject *PyRendererAgg_draw_quad_mesh(PyRendererAgg *self, PyObject *args)
{
    GCAgg gc;
    agg::trans_affine master_transform;
    unsigned int mesh_width;
    unsigned int mesh_height;
    numpy::array_view<const double, 3> coordinates;
    numpy::array_view<const double, 2> offsets;
    agg::trans_affine offset_trans;
    numpy::array_view<const double, 2> facecolors;
    bool antialiased;
    numpy::array_view<const double, 2> edgecolors;

    if (!PyArg_ParseTuple(args,
                          "O&O&IIO&O&O&O&O&O&:draw_quad_mesh",
                          &convert_gcagg,
                          &gc,
                          &convert_trans_affine,
                          &master_transform,
                          &mesh_width,
                          &mesh_height,
#if !TARGET_OS_IPHONE
                          &coordinates.converter,
#else
						  coordinates_converter,
#endif
                          &coordinates,
                          &convert_points,
                          &offsets,
                          &convert_trans_affine,
                          &offset_trans,
                          &convert_colors,
                          &facecolors,
                          &convert_bool,
                          &antialiased,
                          &convert_colors,
                          &edgecolors)) {
        return NULL;
    }

    CALL_CPP("draw_quad_mesh",
             (self->x->draw_quad_mesh(gc,
                                      master_transform,
                                      mesh_width,
                                      mesh_height,
                                      coordinates,
                                      offsets,
                                      offset_trans,
                                      facecolors,
                                      antialiased,
                                      edgecolors)));

    Py_RETURN_NONE;
}



#if TARGET_OS_IPHONE
// iOS: need non static member functions. This replaces points.converter and colors.converter:
static int (*points3_converter)(PyObject *, void *) = numpy::numpy_converter<const double, 3>;
static int (*colors3_converter)(PyObject *, void *) = numpy::numpy_converter<const double, 3>;
#endif

static PyObject *
PyRendererAgg_draw_gouraud_triangles(PyRendererAgg *self, PyObject *args)
{
    GCAgg gc;
    numpy::array_view<const double, 3> points;
    numpy::array_view<const double, 3> colors;
    agg::trans_affine trans;

    if (!PyArg_ParseTuple(args,
                          "O&O&O&O&|O:draw_gouraud_triangles",
                          &convert_gcagg,
                          &gc,
#if !TARGET_OS_IPHONE
                          &points.converter,
                          &points,
                          &colors.converter,
#else
                          points3_converter,
                          &points,
                          colors3_converter,
#endif
                          &colors,
                          &convert_trans_affine,
                          &trans)) {
        return NULL;
    }
    if (points.shape(0) && !check_trailing_shape(points, "points", 3, 2)) {
        return NULL;
    }
    if (colors.shape(0) && !check_trailing_shape(colors, "colors", 3, 4)) {
        return NULL;
    }
    if (points.shape(0) != colors.shape(0)) {
        PyErr_Format(PyExc_ValueError,
                     "points and colors arrays must be the same length, got "
                     "%" NPY_INTP_FMT " points and %" NPY_INTP_FMT "colors",
                     points.shape(0), colors.shape(0));
        return NULL;
    }

    CALL_CPP("draw_gouraud_triangles", self->x->draw_gouraud_triangles(gc, points, colors, trans));

    Py_RETURN_NONE;
}

int PyRendererAgg_get_buffer(PyRendererAgg *self, Py_buffer *buf, int flags)
{
    Py_INCREF(self);
    buf->obj = (PyObject *)self;
    buf->buf = self->x->pixBuffer;
    buf->len = (Py_ssize_t)self->x->get_width() * (Py_ssize_t)self->x->get_height() * 4;
    buf->readonly = 0;
    buf->format = (char *)"B";
    buf->ndim = 3;
    self->shape[0] = self->x->get_height();
    self->shape[1] = self->x->get_width();
    self->shape[2] = 4;
    buf->shape = self->shape;
    self->strides[0] = self->x->get_width() * 4;
    self->strides[1] = 4;
    self->strides[2] = 1;
    buf->strides = self->strides;
    buf->suboffsets = NULL;
    buf->itemsize = 1;
    buf->internal = NULL;

    return 1;
}

static PyObject *PyRendererAgg_clear(PyRendererAgg *self, PyObject *args)
{
    CALL_CPP("clear", self->x->clear());

    Py_RETURN_NONE;
}

static PyObject *PyRendererAgg_copy_from_bbox(PyRendererAgg *self, PyObject *args)
{
    agg::rect_d bbox;
    BufferRegion *reg;
    PyObject *regobj;

    if (!PyArg_ParseTuple(args, "O&:copy_from_bbox", &convert_rect, &bbox)) {
        return 0;
    }

    CALL_CPP("copy_from_bbox", (reg = self->x->copy_from_bbox(bbox)));

    regobj = PyBufferRegion_new(&PyBufferRegionType, NULL, NULL);
    ((PyBufferRegion *)regobj)->x = reg;

    return regobj;
}

static PyObject *PyRendererAgg_restore_region(PyRendererAgg *self, PyObject *args)
{
    PyBufferRegion *regobj;
    int xx1 = 0, yy1 = 0, xx2 = 0, yy2 = 0, x = 0, y = 0;

    if (!PyArg_ParseTuple(args,
                          "O!|iiiiii:restore_region",
                          &PyBufferRegionType,
                          &regobj,
                          &xx1,
                          &yy1,
                          &xx2,
                          &yy2,
                          &x,
                          &y)) {
        return 0;
    }

    if (PySequence_Size(args) == 1) {
        CALL_CPP("restore_region", self->x->restore_region(*(regobj->x)));
    } else {
        CALL_CPP("restore_region", self->x->restore_region(*(regobj->x), xx1, yy1, xx2, yy2, x, y));
    }

    Py_RETURN_NONE;
}

static PyTypeObject *PyRendererAgg_init_type()
{
#if !TARGET_OS_IPHONE
    static PyMethodDef methods[] = {
#else
    static __thread PyMethodDef methods[] = {
#endif
        {"draw_path", (PyCFunction)PyRendererAgg_draw_path, METH_VARARGS, NULL},
        {"draw_markers", (PyCFunction)PyRendererAgg_draw_markers, METH_VARARGS, NULL},
        {"draw_text_image", (PyCFunction)PyRendererAgg_draw_text_image, METH_VARARGS, NULL},
        {"draw_image", (PyCFunction)PyRendererAgg_draw_image, METH_VARARGS, NULL},
        {"draw_path_collection", (PyCFunction)PyRendererAgg_draw_path_collection, METH_VARARGS, NULL},
        {"draw_quad_mesh", (PyCFunction)PyRendererAgg_draw_quad_mesh, METH_VARARGS, NULL},
        {"draw_gouraud_triangles", (PyCFunction)PyRendererAgg_draw_gouraud_triangles, METH_VARARGS, NULL},

        {"clear", (PyCFunction)PyRendererAgg_clear, METH_NOARGS, NULL},

        {"copy_from_bbox", (PyCFunction)PyRendererAgg_copy_from_bbox, METH_VARARGS, NULL},
        {"restore_region", (PyCFunction)PyRendererAgg_restore_region, METH_VARARGS, NULL},
        {NULL}
    };

#if !TARGET_OS_IPHONE
    static PyBufferProcs buffer_procs;
#else
    static __thread PyBufferProcs buffer_procs;
#endif
    buffer_procs.bf_getbuffer = (getbufferproc)PyRendererAgg_get_buffer;

    PyRendererAggType.tp_name = "matplotlib.backends._backend_agg.RendererAgg";
    PyRendererAggType.tp_basicsize = sizeof(PyRendererAgg);
    PyRendererAggType.tp_dealloc = (destructor)PyRendererAgg_dealloc;
    PyRendererAggType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    PyRendererAggType.tp_methods = methods;
    PyRendererAggType.tp_init = (initproc)PyRendererAgg_init;
    PyRendererAggType.tp_new = PyRendererAgg_new;
    PyRendererAggType.tp_as_buffer = &buffer_procs;

    return &PyRendererAggType;
}

static struct PyModuleDef moduledef = { PyModuleDef_HEAD_INIT, "_backend_agg" };

PyMODINIT_FUNC PyInit__backend_agg(void)
{
    import_array();
    PyObject *m;
    if (!(m = PyModule_Create(&moduledef))
        || prepare_and_add_type(PyRendererAgg_init_type(), m)
        // BufferRegion is not constructible from Python, thus not added to the module.
        || PyType_Ready(PyBufferRegion_init_type())
       ) {
        Py_XDECREF(m);
        return NULL;
    }
    return m;
}
