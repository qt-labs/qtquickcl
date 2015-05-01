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

#include "qquickclimagerunnable.h"
#include "qquickclitem.h"
#include "qquickclcontext.h"
#include <QSGSimpleTextureNode>
#include <QSGTextureProvider>
#include <QOpenGLTexture>
#include <QOpenGLFunctions>

QT_BEGIN_NAMESPACE

/*!
    \class QQuickCLImageRunnable
    \brief A QQuickCLItem backend specialized for operating on a single texture from the scenegraph.

    Specialized QQuickCLRunnable for applications wishing to perform
    OpenCL operations on an OpenCL image object wrapping an OpenGL texture of
    an Image element - or any other texture provider in the Qt Quick scene -
    and show the result in the scene.

    The class provides an OpenCL command queue and a simple texture node for
    the scenegraph. The item providing the texture is read from the associated
    QQuickCLItem's \c source property by default. This can be overridden by
    calling setSourcePropertyName().

    By using this specialized class instead of the more generic base
    QQuickCLRunnable, applications can focus on the kernels and there is no
    need to manually manage OpenCL image objects, textures, and scenegraph nodes.

    For example, assuming a QQuickCLItem subclass named CLItem, running OpenCL
    kernels on an image, producing a new output image rendered by CLItem in an
    accelerated manner without any CPU side readbacks, becomes as simple as the
    following:

    \badcode
        Image {
            id: srcImage
            source: "image.png"
        }
        ...
        CLItem {
            source: srcImage
        }
    \endcode

    The source can be any texture provider. By enabling layering, the OpenCL
    kernels can operate on the rendering of an entire sub-tree instead of just
    a single Image item. In addition, this approach also allows hiding the
    source sub-tree:

    \badcode
        Item {
            id: srcItem
            layer.enabled: true
            visible: false
            ...
        }
        CLItem {
            source: srcItem
        }
    \endcode

    Besides image processing scenarios it is also possible to use
    QQuickCLImageRunnable for computations that produce arbitrary data from an
    image (for example histogram calculation). Passing the flag NoImageOutput to
    the constructor will avoid generating an OpenGL texture and corresponding
    OpenCL image object for the output. Instead, it is up to the runKernel()
    implementation to emit a signal on the associated QQuickCLItem and pass an
    object exposing the results of the computation to QML. The visualization is
    then done by child items since the QQuickCLItem itself does not render
    anything in the Qt Quick scenegraph in this case, although it is still
    present as an item having contents.
 */

/*!
    \fn void QQuickCLImageRunnable::runKernel(cl_mem inImage, cl_mem outImage, const QSize &size)

    Called when the OpenCL kernel(s) performing the image processing need to be
    run. \a inImage and \a outImage are ready to be used as input and output
    \c image2d_t parameters to a kernel. \a size specifies the size of the images.

    \note For QQuickCLImageRunnable instances created with the NoImageOutput
    flag \a outImage is always \c 0.

    \note QQuickCLImageRunnable is aware of \c cl_khr_gl_event and will invoke
    glFinish() and clFinish() as necessary in case the extension is not
    supported. Both will be omitted when the extension is present. However,
    clFinish() is still invoked regardless of the presence of the extension when
    either the \c ForceCLFinish or \c Profile flags are set.
 */

class QQuickCLImageRunnablePrivate
{
public:
    QQuickCLImageRunnablePrivate(QQuickCLItem *item, QQuickCLImageRunnable::Flags flags)
        : item(item),
          flags(flags),
          queue(0),
          inputTexture(0),
          outputTexture(0),
          elapsed(0)
    {
        image[0] = image[1] = 0;
        profEv[0] = profEv[1] = 0;
        sourcePropertyName = QByteArrayLiteral("source");
    }

    ~QQuickCLImageRunnablePrivate() {
        if (image[0])
            clReleaseMemObject(image[0]);
        if (image[1])
            clReleaseMemObject(image[1]);
        if (queue)
            clReleaseCommandQueue(queue);
        delete outputTexture;
    }

    QQuickCLItem *item;
    QQuickCLImageRunnable::Flags flags;
    cl_command_queue queue;
    cl_mem image[2];
    QSize textureSize;
    uint inputTexture;
    QOpenGLTexture *outputTexture;
    QByteArray sourcePropertyName;
    cl_event profEv[2];
    double elapsed;
    bool needsExplicitSync;
};

/*!
    Constructs a new QQuickCLImageRunnable instance associated with \a item.
    Special behavior, for example computations producing arbitrary non-image
    output, can be enabled via \a flags.
 */
QQuickCLImageRunnable::QQuickCLImageRunnable(QQuickCLItem *item, Flags flags)
    : d_ptr(new QQuickCLImageRunnablePrivate(item, flags))
{
    Q_D(QQuickCLImageRunnable);
    cl_int err;
    cl_command_queue_properties queueProps = flags.testFlag(Profile) ? CL_QUEUE_PROFILING_ENABLE : 0;
    QQuickCLContext *clctx = item->context();
    Q_ASSERT(clctx);
    d->queue = clCreateCommandQueue(clctx->context(), clctx->device(), queueProps, &err);
    if (!d->queue) {
        qWarning("Failed to create OpenCL command queue: %d", err);
        return;
    }
    d->needsExplicitSync = !clctx->deviceExtensions().contains(QByteArrayLiteral("cl_khr_gl_event"));
}

QQuickCLImageRunnable::~QQuickCLImageRunnable()
{
    delete d_ptr;
}

/*!
    \return the OpenCL command queue.
 */
cl_command_queue QQuickCLImageRunnable::commandQueue() const
{
    Q_D(const QQuickCLImageRunnable);
    return d->queue;
}

/*!
    Sets the name of the property that is queried from the item that was passed
    to the constructor. The default value is \c source.
 */
void QQuickCLImageRunnable::setSourcePropertyName(const QByteArray &name)
{
    Q_D(QQuickCLImageRunnable);
    d->sourcePropertyName = name;
}

QSGNode *QQuickCLImageRunnable::update(QSGNode *node)
{
    Q_D(QQuickCLImageRunnable);
    QSGTextureProvider *textureProvider;
    QSGTexture *texture;
    QQuickItem *source = d->item->property(d->sourcePropertyName.constData()).value<QQuickItem *>();
    if (!source
            || !source->isTextureProvider()
            || !(textureProvider = source->textureProvider())
            || !(texture = textureProvider->texture())) {
        delete node;
        return 0;
    }

    QSGDynamicTexture *dtex = qobject_cast<QSGDynamicTexture *>(texture);
    if (dtex)
        dtex->updateTexture();

    if (!texture->textureId()) { // the texture provider may not be ready yet, try again later
        d->item->scheduleUpdate();
        return node;
    }

    if (d->inputTexture != uint(texture->textureId())
            || d->textureSize != texture->textureSize()
            || (!d->flags.testFlag(NoOutputImage) && !d->outputTexture)) {
        if (d->image[0])
            clReleaseMemObject(d->image[0]);
        d->image[0] = 0;
        if (d->image[1])
            clReleaseMemObject(d->image[1]);
        d->image[1] = 0;
        delete d->outputTexture;
        d->outputTexture = 0;
        delete node;
        node = 0;
    }

    QQuickCLContext *clctx = d->item->context();
    Q_ASSERT(clctx);
    cl_int err = 0;
    if (!d->image[0])
        d->image[0] = clCreateFromGLTexture2D(clctx->context(), CL_MEM_READ_ONLY, GL_TEXTURE_2D, 0,
                                              texture->textureId(), &err);
    if (!d->image[0]) {
        if (err == CL_INVALID_GL_OBJECT) // the texture provider may not be ready yet, try again later
            d->item->scheduleUpdate();
        else
            qWarning("Failed to create OpenCL image object from input OpenGL texture: %d", err);
        return node;
    }

    d->inputTexture = texture->textureId();
    d->textureSize = texture->textureSize();

    const int imageCount = d->flags.testFlag(NoOutputImage) ? 1 : 2;
    if (imageCount == 2) {
        if (!d->outputTexture)
            d->outputTexture = new QOpenGLTexture(QImage(d->textureSize, QImage::Format_RGB32));

        if (!d->image[1])
            d->image[1] = clCreateFromGLTexture2D(clctx->context(), CL_MEM_WRITE_ONLY, GL_TEXTURE_2D, 0,
                                                  d->outputTexture->textureId(), &err);
        if (!d->image[1]) {
            qWarning("Failed to create OpenCL image object for output OpenGL texture: %d", err);
            return node;
        }
    }

    if (d->needsExplicitSync)
        QOpenGLContext::currentContext()->functions()->glFinish();

    err = clEnqueueAcquireGLObjects(d->queue, imageCount, d->image, 0, 0, 0);
    if (err != CL_SUCCESS) {
        qWarning("Failed to queue acquiring the GL textures: %d", err);
        return node;
    }

    if (d->flags.testFlag(Profile))
        if (clEnqueueMarker(d->queue, &d->profEv[0]) != CL_SUCCESS)
            qWarning("Failed to enqueue profiling marker (start)");

    runKernel(d->image[0], d->image[1], d->textureSize);

    if (d->flags.testFlag(Profile))
        if (clEnqueueMarker(d->queue, &d->profEv[1]) != CL_SUCCESS)
            qWarning("Failed to enqueue profiling marker (end)");

    clEnqueueReleaseGLObjects(d->queue, imageCount, d->image, 0, 0, 0);

    if (d->flags.testFlag(ForceCLFinish) || d->needsExplicitSync || d->flags.testFlag(Profile))
        clFinish(d->queue);

    if (d->flags.testFlag(Profile)) {
        cl_ulong start = 0, end = 0;
        err = clGetEventProfilingInfo(d->profEv[0], CL_PROFILING_COMMAND_QUEUED, sizeof(cl_ulong), &start, 0);
        if (err != CL_SUCCESS)
            qWarning("Failed to get profiling info for start event: %d", err);
        err = clGetEventProfilingInfo(d->profEv[1], CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &end, 0);
        if (err != CL_SUCCESS)
            qWarning("Failed to get profiling info for end event: %d", err);
        d->elapsed = double(end - start) / 1000000.0;
        clReleaseEvent(d->profEv[0]);
        clReleaseEvent(d->profEv[1]);
    }

    if (imageCount == 1)
        return 0;

    QSGSimpleTextureNode *tnode = static_cast<QSGSimpleTextureNode *>(node);
    if (!tnode) {
        tnode = new QSGSimpleTextureNode;
        tnode->setFiltering(QSGTexture::Linear);
        tnode->setTexture(d->item->window()->createTextureFromId(d->outputTexture->textureId(), d->textureSize));
    }
    tnode->setRect(d->item->boundingRect());
    tnode->markDirty(QSGNode::DirtyMaterial);

    return tnode;
}

/*!
    Returns the number of milliseconds spent on OpenCL operations during the
    last finished invocation of runKernel().

    \note OpenCL command queue profiling must be enabled by passing the \c Profile
    flag to the constructor.
 */
double QQuickCLImageRunnable::elapsed() const
{
    Q_D(const QQuickCLImageRunnable);
    return d->elapsed;
}

QT_END_NAMESPACE
