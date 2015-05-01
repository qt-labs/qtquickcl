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

#include "qquickclitem.h"
#include "qquickclcontext.h"
#include <QtCore/QAtomicInt>
#include <QtCore/QHash>
#include <QtCore/QFile>
#include <QtCore/QLoggingCategory>
#include <QtQuick/private/qquickitem_p.h>

QT_BEGIN_NAMESPACE

Q_DECLARE_LOGGING_CATEGORY(logCL)

/*!
    \class QQuickCLItem

    \brief QQuickCLItem is a QQuickItem that automatically gets an OpenCL
    context with the proper platform and device chosen for CL-GL interop.

    Each instance of QQuickCLItem is backed by a QQuickCLContext and
    QQuickCLRunnable instance.

     \note When animating properties that are used in OpenCL kernels, call the
     \l{QQuickItem::update()}{update()} function (from the gui thread) to
     trigger updates.
 */

/*!
    \class QQuickCLRunnable

    \brief QQuickCLRunnable instances live on the scenegraph's render thread
    encapsulating OpenCL and related OpenGL resources.

    The constructor, destructor and update() are always invoked on the render
    thread with the OpenGL context bound and the OpenCL context ready. This
    allows easy usage of OpenCL and especially OpenGL resources without
    worrying about lifetime and threading. They can simply be created in the
    constructor and released in the destructor.
 */

/*!
    \fn QSGNode *QQuickCLRunnable::update(QSGNode *node)

    Called on the render thread every time the QQuickCLItem is updated.
    Semantically equivalent to
    \l{QQuickItem::updatePaintNode()}{updatePaintNode()}.

    It is up to the implementation to decide what to do here: it can launch
    OpenCL operations, wait for them to finish, and return a scenegraph node,
    for example a QSGSimpleTextureNode wrapping the resulting texture.

    Alternatively, it can also launch a longer running CL operation and return
    \c null or a node providing some temporary content. Then, when the CL
    operation is finished, \l{QQuickCLItem::scheduleUpdate()}{scheduleUpdate()}
    is called from an OpenCL event callback to schedule an update for the Quick
    item, which means eventually invoking this function again. This function
    can then provide a new node with the results of the computation.

    \note When necessary, future updates for the QQuickCLItem can also be
    scheduled from this function. However, this requires calling
    QQuickCLItem::scheduleUpdate() instead of QQuickItem::update().
 */

/*!
    \fn QQuickCLRunnable *QQuickCLItem::createCL()

    Factory function invoked on the render thread after initializing OpenCL.
 */

class QQuickCLItemPrivate : public QQuickItemPrivate
{
    Q_DECLARE_PUBLIC(QQuickCLItem)

public:
    QQuickCLItemPrivate() : clctx(0), clnode(0) { }

    static void CL_CALLBACK eventCallback(cl_event event, cl_int status, void *user_data);

    QQuickCLContext *clctx;
    QQuickCLRunnable *clnode;
};

QQuickCLItem::QQuickCLItem(QQuickItem *parent)
    : QQuickItem(*new QQuickCLItemPrivate, parent)
{
    setFlag(ItemHasContents);
}

/*!
  \return the associated QQuickCLContext.

  \note The value is only available after the item is first rendered. It is
  always safe to call this function from QQuickCLRunnable's constructor,
  destructor and \l{QQuickCLRunnable::update()}{update()} function.
 */
QQuickCLContext *QQuickCLItem::context() const
{
    Q_D(const QQuickCLItem);
    return d->clctx;
}

QSGNode *QQuickCLItem::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    Q_D(QQuickCLItem);

    if (width() <= 0 || height() <= 0) {
        delete node;
        return 0;
    }

    // render thread, initialize CL if not yet done
    if (!d->clctx) {
        d->clctx = new QQuickCLContext;
        if (!d->clctx->create()) {
            qWarning("Failed to create OpenCL context");
            delete d->clctx;
            d->clctx = 0;
        }
    }

    if (!d->clctx)
        return 0;

    if (!d->clnode)
        d->clnode = createCL();

    return d->clnode ? d->clnode->update(node) : 0;
}

class ReleaseRunnable : public QRunnable
{
public:
    ReleaseRunnable(QQuickCLContext *clctx, QQuickCLRunnable *clnode) : clctx(clctx), clnode(clnode) { }
    void run() Q_DECL_OVERRIDE {
        delete clnode;
        delete clctx;
    }
private:
    QQuickCLContext *clctx;
    QQuickCLRunnable *clnode;
};

void QQuickCLItem::releaseResources()
{
    // gui thread, just schedule. NB this and d may be dead by the time the runnable is run
    Q_D(QQuickCLItem);
    window()->scheduleRenderJob(new ReleaseRunnable(d->clctx, d->clnode), QQuickWindow::BeforeSynchronizingStage);
    d->clnode = 0;
    d->clctx = 0;
}

void QQuickCLItem::invalidateSceneGraph()
{
    // render thread
    Q_D(QQuickCLItem);
    delete d->clnode;
    d->clnode = 0;
    delete d->clctx;
    d->clctx = 0;
}

static const int EV_UPDATE = QEvent::User + 128;
static const int EV_EVENT = QEvent::User + 129;

class EventCompleteEvent : public QEvent
{
public:
    EventCompleteEvent(cl_event event) : QEvent(QEvent::Type(EV_EVENT)), event(event) { }
    cl_event event;
};

bool QQuickCLItem::event(QEvent *e)
{
    if (e->type() == EV_UPDATE) {
        update();
        return true;
    } else if (e->type() == EV_EVENT) {
        EventCompleteEvent *ev = static_cast<EventCompleteEvent *>(e);
        eventCompleted(ev->event);
        return true;
    }
    return QQuickItem::event(e);
}

/*!
    Schedules an update for the item. Unlike \l{QQuickItem::update()}{the base
    class' update()}, this is safe to be called on any thread, hence it is safe
    for use from CL event callbacks.
 */
void QQuickCLItem::scheduleUpdate()
{
    QCoreApplication::postEvent(this, new QEvent(QEvent::Type(EV_UPDATE)));
}

struct EventCallbackParam
{
    EventCallbackParam(QQuickCLItem *item) : item(item) { }
    QPointer<QQuickCLItem> item;
};

/*!
    Registers an event callback for \a event. The virtual function
    eventCompleted() will get invoked on the gui/main thread when the event
    completes.

    This allows easy and safe asynchronous computations because the results can
    directly be exposed to QML from eventCompleted() due to it running on the
    gui/main thread. In addition it is guaranteed that eventCompleted() is
    never called directly from the OpenCL callback function. Special cases like
    destroying the QQuickCLItem before completing the event are also handled
    gracefully.

    \note \a event is not released.
 */
void QQuickCLItem::watchEvent(cl_event event)
{
    EventCallbackParam *param = new EventCallbackParam(this);
    cl_int err = clSetEventCallback(event, CL_COMPLETE, QQuickCLItemPrivate::eventCallback, param);
    if (err != CL_SUCCESS)
        qWarning("Failed to set event callback: %d", err);
}

/*!
    Called on the gui/main thread when the \a event watched via watchEvent()
    completes. The default implementation does nothing.
 */
void QQuickCLItem::eventCompleted(cl_event event)
{
    Q_UNUSED(event);
}

void CL_CALLBACK QQuickCLItemPrivate::eventCallback(cl_event event, cl_int status, void *user_data)
{
    if (status != CL_COMPLETE)
        return;
    EventCallbackParam *param = static_cast<EventCallbackParam *>(user_data);
    if (!param->item.isNull())
        QCoreApplication::postEvent(param->item, new EventCompleteEvent(event));
    delete param;
}

QQuickCLRunnable::~QQuickCLRunnable()
{
}

QT_END_NAMESPACE
