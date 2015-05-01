/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Quick CL module
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

// Demonstrates the texture-to-data use case where the results of the
// computation are exposed to QML and visualized using Qt Quick items.

#include <QGuiApplication>
#include <QQuickView>
#include <QQmlEngine>
#include <QQuickCLItem>
#include <QQuickCLImageRunnable>
#include <QQuickCLContext>
#include <QAbstractListModel>

static bool profile = false;

class HistogramModel : public QAbstractListModel
{
    Q_OBJECT

public:
    Q_INVOKABLE uint get(int idx) const { return h[idx]; }

    void setHistogram(const QVector<uint> &histogram) {
        emit beginResetModel();
        h = histogram;
        emit endResetModel();
    }

    int rowCount(const QModelIndex &) const Q_DECL_OVERRIDE {
        return h.count();
    }

    QVariant data(const QModelIndex &index, int role) const Q_DECL_OVERRIDE {
        if (index.isValid() && index.row() < h.count() && role == Qt::UserRole + 1)
            return h[index.row()];
        return QVariant();
    }

    QHash<int, QByteArray> roleNames() const Q_DECL_OVERRIDE {
        QHash<int, QByteArray> roles;
        roles[Qt::UserRole + 1] = "value";
        return roles;
    }

private:
    QVector<uint> h;
};

class CLRunnable;

class CLItem : public QQuickCLItem
{
    Q_OBJECT
    Q_PROPERTY(QQuickItem *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QAbstractItemModel *result READ result NOTIFY resultChanged)

public:
    CLItem() : m_source(0), m_result(new HistogramModel) { }
    ~CLItem() { delete m_result; }

    QQuickCLRunnable *createCL() Q_DECL_OVERRIDE;
    void eventCompleted(cl_event event) Q_DECL_OVERRIDE;

    QQuickItem *source() const { return m_source; }
    void setSource(QQuickItem *source) {
        if (m_source != source) {
            m_source = source;
            emit sourceChanged();
            update();
        }
    }

    QAbstractItemModel *result() const { return m_result; }
    void updateResult(const QByteArray &histogram) {
        const cl_uint *p = (const cl_uint *) histogram.constData();
        QVector<uint> v(256);
        for (int i = 0; i < v.count(); ++i)
            v[i] = p[i];
        m_result->setHistogram(v);
        emit newResultsAvailable();
    }

signals:
    void sourceChanged();
    void resultChanged(); // not used as there is a single HistogramModel instance
    void newResultsAvailable();

private:
    QQuickItem *m_source;
    HistogramModel *m_result;
    CLRunnable *m_runnable;
};

class CLRunnable : public QQuickCLImageRunnable
{
public:
    CLRunnable(CLItem *item);
    ~CLRunnable();
    void runKernel(cl_mem inImage, cl_mem outImage, const QSize &size) Q_DECL_OVERRIDE;
    QByteArray rawResult() const { return m_result; }
    void resetPending() { m_resultPending.testAndSetOrdered(1, 0); }

private:
    CLItem *m_item;
    cl_program m_program;
    cl_kernel m_kernel;
    cl_kernel m_sumKernel;
    cl_mem m_resultBuf;
    cl_mem m_sharedBuf;
    cl_event m_doneEvent;
    QByteArray m_result;
    QAtomicInt m_resultPending;
};

CLRunnable::CLRunnable(CLItem *item)
    : QQuickCLImageRunnable(item, NoOutputImage | (profile ? Profile : Flag(0))), // note the NoOutputImage flag
      m_item(item),
      m_program(0),
      m_kernel(0),
      m_sumKernel(0),
      m_resultBuf(0),
      m_sharedBuf(0),
      m_doneEvent(0)
{
    QQuickCLContext *clctx = m_item->context();
    QByteArray platform = clctx->platformName();
    qDebug("Using platform %s", platform.constData());
    m_program = clctx->buildProgramFromFile(QStringLiteral(":/histogram.cl"));
    if (!m_program)
        return;
    cl_int err;
    m_kernel = clCreateKernel(m_program, "histogram", &err);
    if (!m_kernel) {
        qWarning("Failed to create histogram OpenCL kernel: %d", err);
        return;
    }
    m_sumKernel = clCreateKernel(m_program, "sum_histogram", &err);
    if (!m_sumKernel) {
        qWarning("Failed to create sum histogram OpenCL kernel: %d", err);
        return;
    }
    m_resultBuf = clCreateBuffer(clctx->context(), CL_MEM_WRITE_ONLY, 256 * sizeof(cl_uint), 0, &err);
    if (!m_resultBuf) {
        qWarning("Failed to create OpenCL buffer: %d", err);
        return;
    }
    m_result.resize(256 * sizeof(cl_uint));
}

CLRunnable::~CLRunnable()
{
    if (m_sharedBuf)
        clReleaseMemObject(m_sharedBuf);
    if (m_resultBuf)
        clReleaseMemObject(m_resultBuf);
    if (m_kernel)
        clReleaseKernel(m_kernel);
    if (m_sumKernel)
        clReleaseKernel(m_sumKernel);
    if (m_program)
        clReleaseProgram(m_program);
}

class PendingGuard
{
public:
    PendingGuard(CLRunnable *r) : r(r) { }
    ~PendingGuard() { if (r) r->resetPending(); }
    void take() { r = 0; }
private:
    CLRunnable *r;
};

#define DIV(a, b) ((a + b - 1) / b)

void CLRunnable::runKernel(cl_mem inImage, cl_mem, const QSize &size)
{
    if (!m_program || !m_kernel || !m_sumKernel || !m_resultBuf)
        return;

    if (!m_resultPending.testAndSetOrdered(0, 1))
        return;
    PendingGuard pg(this);

    if (profile)
        qDebug("CL time: %f ms", elapsed());

    const size_t group_size_x = 16;
    const size_t group_size_y = 8;
    const cl_int num_pixels_per_item = 32;
    const size_t num_items_per_row = DIV(size.width(), num_pixels_per_item);
    const size_t num_groups_x = DIV(num_items_per_row, group_size_x);
    const size_t num_groups_y = DIV(size.height(), group_size_y);
    const cl_int num_groups = cl_int(num_groups_x * num_groups_y);

    const size_t localWorkSize[2] = { group_size_x, group_size_y };
    const size_t globalWorkSize[2] = { localWorkSize[0] * num_groups_x, localWorkSize[1] * num_groups_y };

    cl_int err;
    if (!m_sharedBuf) {
        const size_t sharedBufSize = num_groups * 256 * sizeof(cl_uint);
        m_sharedBuf = clCreateBuffer(m_item->context()->context(), CL_MEM_READ_WRITE, sharedBufSize, 0, &err);
        if (!m_sharedBuf) {
            qWarning("Failed to create shared buffer: %d", err);
            return;
        }
    }

    clSetKernelArg(m_kernel, 0, sizeof(cl_mem), &inImage);
    clSetKernelArg(m_kernel, 1, sizeof(cl_int), &num_pixels_per_item);
    clSetKernelArg(m_kernel, 2, sizeof(cl_mem), &m_sharedBuf);
    err = clEnqueueNDRangeKernel(commandQueue(), m_kernel, 2, 0, globalWorkSize, localWorkSize, 0, 0, 0);
    if (err != CL_SUCCESS) {
        qWarning("Failed to enqueue kernel: %d", err);
        return;
    }

    clSetKernelArg(m_sumKernel, 0, sizeof(cl_mem), &m_sharedBuf);
    clSetKernelArg(m_sumKernel, 1, sizeof(cl_int), &num_groups);
    clSetKernelArg(m_sumKernel, 2, sizeof(cl_mem), &m_resultBuf);
    const size_t sumLocalWorkSize = 256;
    const size_t sumGlobalWorkSize = 256;
    err = clEnqueueNDRangeKernel(commandQueue(), m_sumKernel, 1, 0, &sumGlobalWorkSize, &sumLocalWorkSize, 0, 0, 0);
    if (err != CL_SUCCESS) {
        qWarning("Failed to enqueue sum kernel: %d", err);
        return;
    }

    err = clEnqueueReadBuffer(commandQueue(), m_resultBuf, CL_FALSE, 0, m_result.size(), m_result.data(), 0, 0, &m_doneEvent);
    if (err != CL_SUCCESS) {
        qWarning("Failed to enqueue read buffer: %d", err);
        return;
    }

    pg.take();
    m_item->watchEvent(m_doneEvent);
}

QQuickCLRunnable *CLItem::createCL()
{
    m_runnable = new CLRunnable(this);
    return m_runnable;
}

void CLItem::eventCompleted(cl_event event)
{
    // We are on the gui thread here.
    updateResult(m_runnable->rawResult());

    clReleaseEvent(event);
    m_runnable->resetPending();

    // Request an update, which will eventually also lead to issuing the next
    // round of computation.
    scheduleUpdate();
}

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#endif
    QGuiApplication app(argc, argv);

    if (app.arguments().contains(QStringLiteral("--profile")))
        profile = true;

    QQuickView view;
    QObject::connect(view.engine(), SIGNAL(quit()), &app, SLOT(quit()));

    qmlRegisterType<CLItem>("quickcl.qt.io", 1, 0, "CLItem");

    view.setSource(QUrl("qrc:///qml/histogram.qml"));
    view.setResizeMode(QQuickView::SizeRootObjectToView);

    view.resize(1200, 600);
    view.show();

    return app.exec();
}

#include "histogram.moc"
