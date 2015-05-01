/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the Qt Quick CL module
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qquickclcontext.h"

#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtCore/QLoggingCategory>
#include <qpa/qplatformnativeinterface.h>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(logCL, "qt.quickcl")

/*!
    \class QQuickCLContext

    \brief QQuickCLContext encapsulates an OpenCL context.

    \note In most cases there is no need to directly interact with this class
    as QQuickCLItem takes care of creating and destroying a QQuickCLContext
    instance as necessary.

    \note This class assumes that OpenCL 1.1 and CL-GL interop are available.
 */

class QQuickCLContextPrivate
{
public:
    QQuickCLContextPrivate()
        : platform(0),
          device(0),
          context(0)
    { }

    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
};

/*!
    Constructs a new instance of QQuickCLContext.

    \note No OpenCL initialization takes place before calling create().
 */
QQuickCLContext::QQuickCLContext()
    : d_ptr(new QQuickCLContextPrivate)
{
}

/*!
    Destroys the instance and releases all OpenCL resources by invoking
    destroy().
 */
QQuickCLContext::~QQuickCLContext()
{
    destroy();
    delete d_ptr;
}

/*!
    \return \c true if the OpenCL context was successfully created.
 */
bool QQuickCLContext::isValid() const
{
    Q_D(const QQuickCLContext);
    return d->context != 0;
}

/*!
    \return the OpenCL platform chosen in create().
 */
cl_platform_id QQuickCLContext::platform() const
{
    Q_D(const QQuickCLContext);
    return d->platform;
}

/*!
    \return the OpenCL device chosen in create().
 */
cl_device_id QQuickCLContext::device() const
{
    Q_D(const QQuickCLContext);
    return d->device;
}

/*!
    \return the OpenCL context or \c 0 if not yet created.
 */
cl_context QQuickCLContext::context() const
{
    Q_D(const QQuickCLContext);
    return d->context;
}

/*!
    Creates a new OpenCL context.

    If a context was already created, it is destroyed first.

    An OpenGL context must be current at the time of calling this function.
    This ensures that the OpenCL platform matching the OpenGL implementation's
    vendor is selected and that CL-GL interop is enabled for the context.

    If something fails, warnings are logged with the \c qt.quickcl category.

    \return \c true if successful.
 */
bool QQuickCLContext::create()
{
    Q_D(QQuickCLContext);

    destroy();
    qCDebug(logCL, "Creating new OpenCL context");

    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qWarning("Attempted CL-GL interop without a current OpenGL context");
        return false;
    }
    QOpenGLFunctions *f = ctx->functions();

    cl_uint n;
    cl_int err = clGetPlatformIDs(0, 0, &n);
    if (err != CL_SUCCESS) {
        qWarning("Failed to get platform ID count (error %d)", err);
        if (err == -1001) {
            qWarning("Could not find OpenCL implementation. ICD missing?"
#ifdef Q_OS_LINUX
                     " Check /etc/OpenCL/vendors."
#endif
                    );
        }
        return false;
    }
    if (n == 0) {
        qWarning("No OpenCL platform found");
        return false;
    }
    QVector<cl_platform_id> platformIds;
    platformIds.resize(n);
    if (clGetPlatformIDs(n, platformIds.data(), 0) != CL_SUCCESS) {
        qWarning("Failed to get platform IDs");
        return false;
    }
    d->platform = platformIds[0];
    const char *vendor = (const char *) f->glGetString(GL_VENDOR);
    qCDebug(logCL, "GL_VENDOR: %s", vendor);
    const bool isNV = vendor && strstr(vendor, "NVIDIA");
    const bool isIntel = vendor && strstr(vendor, "Intel");
    const bool isAMD = vendor && strstr(vendor, "ATI");
    qCDebug(logCL, "Found %u OpenCL platforms:", n);
    for (cl_uint i = 0; i < n; ++i) {
        QByteArray name;
        name.resize(1024);
        clGetPlatformInfo(platformIds[i], CL_PLATFORM_NAME, name.size(), name.data(), 0);
        qCDebug(logCL, "Platform %p: %s", platformIds[i], name.constData());
        if (isNV && name.contains(QByteArrayLiteral("NVIDIA")))
            d->platform = platformIds[i];
        else if (isIntel && name.contains(QByteArrayLiteral("Intel")))
            d->platform = platformIds[i];
        else if (isAMD && name.contains(QByteArrayLiteral("AMD")))
            d->platform = platformIds[i];
    }
    qCDebug(logCL, "Using platform %p", d->platform);

#if defined (Q_OS_OSX)
    cl_context_properties contextProps[] = { CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
                                             (cl_context_properties) CGLGetShareGroup(CGLGetCurrentContext()),
                                             0 };
#elif defined(Q_OS_WIN)
    if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES) {
        // We don't do D3D-CL interop.
        qWarning("ANGLE is not supported");
        return false;
    }
    cl_context_properties contextProps[] = { CL_CONTEXT_PLATFORM, (cl_context_properties) d->platform,
                                             CL_GL_CONTEXT_KHR, (cl_context_properties) wglGetCurrentContext(),
                                             CL_WGL_HDC_KHR, (cl_context_properties) wglGetCurrentDC(),
                                             0 };
#elif defined(Q_OS_LINUX)
    cl_context_properties contextProps[] = { CL_CONTEXT_PLATFORM, (cl_context_properties) d->platform,
                                             CL_GL_CONTEXT_KHR, 0,
                                             0, 0,
                                             0 };
    QPlatformNativeInterface *nativeIf = qGuiApp->platformNativeInterface();
    void *dpy = nativeIf->nativeResourceForIntegration(QByteArrayLiteral("egldisplay")); // EGLDisplay
    if (dpy) {
        void *nativeContext = nativeIf->nativeResourceForContext("eglcontext", ctx);
        if (!nativeContext)
            qWarning("Failed to get the underlying EGL context from the current QOpenGLContext");
        contextProps[3] = (cl_context_properties) nativeContext;
        contextProps[4] = CL_EGL_DISPLAY_KHR;
        contextProps[5] = (cl_context_properties) dpy;
    } else {
        dpy = nativeIf->nativeResourceForIntegration(QByteArrayLiteral("display")); // Display *
        void *nativeContext = nativeIf->nativeResourceForContext("glxcontext", ctx);
        if (!nativeContext)
            qWarning("Failed to get the underlying GLX context from the current QOpenGLContext");
        contextProps[3] = (cl_context_properties) nativeContext;
        contextProps[4] = CL_GLX_DISPLAY_KHR;
        contextProps[5] = (cl_context_properties) dpy;
    }
#endif

    d->context = clCreateContextFromType(contextProps, CL_DEVICE_TYPE_GPU, 0, 0, &err);
    if (!d->context) {
        qWarning("Failed to create OpenCL context: %d", err);
        return false;
    }
    qCDebug(logCL, "Using context %p", d->context);

#if defined(Q_OS_OSX)
    err = clGetGLContextInfoAPPLE(d->context, CGLGetCurrentContext(),
                                  CL_CGL_DEVICE_FOR_CURRENT_VIRTUAL_SCREEN_APPLE,
                                  sizeof(cl_device_id), &d->device, 0);
    if (err != CL_SUCCESS) {
        qWarning("Failed to get OpenCL device for current screen: %d", err);
        destroy();
        return false;
    }
#else
    clGetGLContextInfoKHR_fn getGLContextInfo = (clGetGLContextInfoKHR_fn) clGetExtensionFunctionAddress("clGetGLContextInfoKHR");
    if (!getGLContextInfo || getGLContextInfo(contextProps, CL_CURRENT_DEVICE_FOR_GL_CONTEXT_KHR,
                                              sizeof(cl_device_id), &d->device, 0) != CL_SUCCESS) {
        err = clGetDeviceIDs(d->platform, CL_DEVICE_TYPE_GPU, 1, &d->device, 0);
        if (err != CL_SUCCESS) {
            qWarning("Failed to get OpenCL device: %d", err);
            destroy();
            return false;
        }
    }
#endif
    qCDebug(logCL, "Using device %p", d->device);

    return true;
}

/*!
    Releases all OpenCL resources.
 */
void QQuickCLContext::destroy()
{
    Q_D(QQuickCLContext);
    if (d->context) {
        qCDebug(logCL, "Releasing OpenCL context %p", d->context);
        clReleaseContext(d->context);
        d->context = 0;
    }
    d->device = 0;
    d->platform = 0;
}

QT_END_NAMESPACE
