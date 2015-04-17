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

// Demonstrates generating vertex data (GL buffer) from OpenCL.

#include <QGuiApplication>
#include <QQuickView>
#include <QQmlEngine>
#include <QSGSimpleTextureNode>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QQuickCLItem>
#include <QQuickCLRunnable>
#include <time.h>

const int PARTICLE_COUNT = 1024;

class CLItem;

// CLNode visualizes the results of CLRunnable. This is done by rendering via a
// FBO since we will have the data (the output from the CL kernel) on the GPU
// already so QSGGeometryNode and friends are not suitable for us.
//
// If mixing with other Quick items is not needed, this can be simplified to
// render straight to the window in a slot connected directly to the
// beforeRendering() or afterRendering() signals of the QQuickWindow.
//
class CLNode : public QObject, public QSGSimpleTextureNode
{
    Q_OBJECT

public:
    CLNode(CLItem *item) : m_item(item), m_fbo(0), m_buf(0), m_clBuf(0), m_program(0) { }

    ~CLNode() {
        delete m_program;
        delete m_fbo;
        if (m_clBuf)
            clReleaseMemObject(m_clBuf);
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        if (m_buf)
            f->glDeleteBuffers(1, &m_buf);
    }

public slots:
    void render();

public:
    CLItem *m_item;
    QOpenGLFramebufferObject *m_fbo;
    GLuint m_buf;
    cl_mem m_clBuf;
    QAtomicInt m_renderPending;
    QOpenGLShaderProgram *m_program;
    int m_tUniformLoc;
};

// The CLRunnable lives on the render thread, like the CLNode.
class CLRunnable : public QObject, public QQuickCLRunnable
{
    Q_OBJECT

public:
    CLRunnable(CLItem *item);
    ~CLRunnable();
    QSGNode *update(QSGNode *node) Q_DECL_OVERRIDE;
    CLNode *node() const { return m_node; }
    void resetComputeInProgress() { m_computeInProgress.testAndSetOrdered(1, 0); }

public slots:
    void handleScreenChange();

private:
    static void CL_CALLBACK computeDoneCallback(cl_event event, cl_int event_command_exec_status, void *user_data);
    void computeDone();
    QSize itemSize() const;
    void createBuffer();
    void createFbo();

    CLItem *m_item;
    CLNode *m_node;
    bool m_recreateFbo;
    cl_command_queue m_queue;
    cl_program m_program;
    cl_kernel m_kernel;
    cl_event m_computeDoneEvent;
    QSize m_itemSize;
    bool m_needsExplicitSync;
    QAtomicInt m_computeInProgress;
    qreal m_lastT;
    cl_mem m_clBufParticleInfo;
};

class CLItem : public QQuickCLItem
{
    Q_OBJECT
    Q_PROPERTY(qreal t READ t WRITE setT NOTIFY tChanged)

public:
    CLItem() : m_t(0) { }

    QQuickCLRunnable *createCL() Q_DECL_OVERRIDE;
    void eventCompleted(cl_event event) Q_DECL_OVERRIDE;

    qreal t() const { return m_t; }
    void setT(qreal v) {
        if (m_t != v) {
            m_t = v;
            emit tChanged();
            update();
        }
    }

signals:
    void tChanged();

private:
    qreal m_t;
    CLRunnable *m_runnable;
};

static const char *vertexShaderSource =
    "attribute vec2 vertex;\n"
    "void main() {\n"
    "   gl_PointSize = 4.0;\n"
    "   gl_Position = vec4(vertex.xy, 0.0, 1.0);\n"
    "}\n";

static const char *fragmentShaderSource =
    "uniform highp float t;\n"
    "void main() {\n"
    "   gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0 - t);\n"
    "}\n";

void CLNode::render()
{
    if (!m_renderPending.testAndSetOrdered(1, 0))
        return;

    m_fbo->bind();
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    QOpenGLFunctions *f = ctx->functions();

    if (!m_program) {
        m_program = new QOpenGLShaderProgram;
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
        m_program->link();
        m_tUniformLoc = m_program->uniformLocation("t");

#ifndef QT_OPENGL_ES_2
        if (!ctx->isOpenGLES()) {
            f->glEnable(GL_POINT_SPRITE);
            f->glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
        }
#endif
    }

    f->glEnable(GL_BLEND);
    f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    f->glViewport(0, 0, m_fbo->width(), m_fbo->height());
    f->glClearColor(0, 0, 0.25f, 1);
    f->glClear(GL_COLOR_BUFFER_BIT);

    m_program->bind();
    m_program->setUniformValue(m_tUniformLoc, GLfloat(m_item->t()));

    f->glBindBuffer(GL_ARRAY_BUFFER, m_buf);
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    f->glDrawArrays(GL_POINTS, 0, PARTICLE_COUNT);
}

CLRunnable::CLRunnable(CLItem *item)
    : m_item(item),
      m_node(0),
      m_recreateFbo(false),
      m_program(0),
      m_kernel(0),
      m_computeDoneEvent(0),
      m_lastT(-1),
      m_clBufParticleInfo(0)
{
    qDebug() << "Platform" << m_item->platformName() << "Device extensions" << m_item->deviceExtensions();
    cl_int err;

    m_queue = clCreateCommandQueue(m_item->context(), m_item->device(), 0, &err);
    if (!m_queue) {
        qWarning("Failed to create OpenCL command queue: %d", err);
        return;
    }

    m_program = m_item->buildProgramFromFile(QStringLiteral(":/particles.cl"));
    if (!m_program)
        return;
    m_kernel = clCreateKernel(m_program, "updateParticles", &err);
    if (!m_kernel) {
        qWarning("Failed to create particles OpenCL kernel: %d", err);
        return;
    }

    m_needsExplicitSync = !m_item->deviceExtensions().contains(QByteArrayLiteral("cl_khr_gl_event"));

    // m_clBufParticleInfo is an ordinary OpenCL buffer.
    size_t velBufSize = PARTICLE_COUNT * sizeof(cl_float) * 4;
    m_clBufParticleInfo = clCreateBuffer(m_item->context(), CL_MEM_READ_ONLY | CL_MEM_ALLOC_HOST_PTR, velBufSize, 0, &err);
    if (!m_clBufParticleInfo) {
        qWarning("Failed to create CL buffer: %d", err);
        return;
    }
    cl_float *ptr = (cl_float *) clEnqueueMapBuffer(m_queue, m_clBufParticleInfo, CL_TRUE, CL_MAP_WRITE, 0, velBufSize, 0, 0, 0, &err);
    if (!ptr) {
        qWarning("Failed to map CL buffer: %d", err);
        return;
    }
    cl_float *p = ptr;
    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        // Direction
        *p++ = ((qrand() % 1000) + 1) / 1000.0f;
        *p++ = ((qrand() % 1000) + 1) / 1000.0f;
        // Speed
        *p++ = ((qrand() % 1000) + 1) / 100.0f;
        *p++ = ((qrand() % 1000) + 1) / 100.0f;
    }
    clEnqueueUnmapMemObject(m_queue, m_clBufParticleInfo, ptr, 0, 0, 0);
}

CLRunnable::~CLRunnable()
{
    if (m_clBufParticleInfo)
        clReleaseMemObject(m_clBufParticleInfo);
    if (m_kernel)
        clReleaseKernel(m_kernel);
    if (m_program)
        clReleaseProgram(m_program);
    if (m_queue)
        clReleaseCommandQueue(m_queue);
}

void CLRunnable::createBuffer()
{
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();

    // m_buf is an OpenGL buffer that is read/written from OpenCL and then used
    // as the input to the OpenGL vertex shader.
    GLuint buf;
    f->glGenBuffers(1, &buf);
    f->glBindBuffer(GL_ARRAY_BUFFER, buf);
    f->glBufferData(GL_ARRAY_BUFFER, PARTICLE_COUNT * sizeof(GLfloat) * 2, 0, GL_DYNAMIC_DRAW);
    if (m_node->m_buf)
        f->glDeleteBuffers(1, &m_node->m_buf);
    m_node->m_buf = buf;

    if (m_node->m_clBuf)
        clReleaseMemObject(m_node->m_clBuf);
    cl_int err;
    m_node->m_clBuf = clCreateFromGLBuffer(m_item->context(), CL_MEM_READ_WRITE, buf, &err);
    if (!m_node->m_clBuf)
        qWarning("Failed to create OpenCL object for OpenGL buffer: %d", err);
}

QSize CLRunnable::itemSize() const
{
    QSize size(m_item->width(), m_item->height());
    size *= m_item->window()->effectiveDevicePixelRatio();
    return size;
}

void CLRunnable::createFbo()
{
    QOpenGLFramebufferObject *fbo = new QOpenGLFramebufferObject(itemSize());
    m_node->setTexture(m_item->window()->createTextureFromId(fbo->texture(), fbo->size(),
                                                             QQuickWindow::TextureHasAlphaChannel));
    delete m_node->m_fbo;
    m_node->m_fbo = fbo;
    m_node->m_renderPending.testAndSetOrdered(0, 1);
}

void CLRunnable::handleScreenChange()
{
    // We could have been moved from a high to low dpi screen or vice versa.
    m_recreateFbo = true;
    m_item->scheduleUpdate();
}

class StateGuard
{
public:
    StateGuard(CLRunnable *r) : r(r) { }
    ~StateGuard() { if (r) r->resetComputeInProgress(); }
    void release() { r = 0; }
private:
    CLRunnable *r;
};

QSGNode *CLRunnable::update(QSGNode *node)
{
    if (!m_queue || !m_program || !m_kernel || !m_clBufParticleInfo)
        return 0;

    if (!node) {
        m_node = new CLNode(m_item);
        m_node->setFiltering(QSGTexture::Linear);
        QObject::connect(m_item->window(), SIGNAL(beforeRendering()), m_node, SLOT(render()));
        QObject::connect(m_item->window(), SIGNAL(screenChanged(QScreen*)), this, SLOT(handleScreenChange()));
        node = m_node;
        createBuffer();
    }
    if (m_itemSize != itemSize()) {
        m_itemSize = itemSize();
        m_recreateFbo = true;
    }
    if (m_recreateFbo) {
        m_recreateFbo = false;
        createFbo();
    }
    m_node->setRect(0, 0, m_item->width(), m_item->height());
    if (m_node->m_renderPending)
        m_node->markDirty(QSGNode::DirtyMaterial);

    // Animation is based on the time property. No need to enqueue anything if
    // the value has not yet changed.
    if (m_lastT == m_item->t())
        return node;

    if (!m_computeInProgress.testAndSetOrdered(0, 1))
        return node;

    StateGuard sg(this);

    cl_float dt = m_item->t() - m_lastT;
    m_lastT = m_item->t();

    if (m_needsExplicitSync)
        QOpenGLContext::currentContext()->functions()->glFinish();

    cl_int err = clEnqueueAcquireGLObjects(m_queue, 1, &m_node->m_clBuf, 0, 0, 0);
    if (err != CL_SUCCESS) {
        qWarning("Failed to queue acquiring the GL buffer: %d", err);
        return node;
    }

    clSetKernelArg(m_kernel, 0, sizeof(cl_mem), &m_node->m_clBuf);
    cl_float t = m_item->t();
    clSetKernelArg(m_kernel, 1, sizeof(cl_float), &t);
    clSetKernelArg(m_kernel, 2, sizeof(cl_float), &dt);
    clSetKernelArg(m_kernel, 3, sizeof(cl_mem), &m_clBufParticleInfo);
    size_t globalWorkSize = PARTICLE_COUNT;
    err = clEnqueueNDRangeKernel(m_queue, m_kernel, 1, 0, &globalWorkSize, 0, 0, 0, 0);
    if (err != CL_SUCCESS) {
        qWarning("Failed to enqueue kernel: %d", err);
        return node;
    }

    err = clEnqueueReleaseGLObjects(m_queue, 1, &m_node->m_clBuf, 0, 0, &m_computeDoneEvent);
    if (err != CL_SUCCESS) {
        qWarning("Failed to queue releasing the GL buffer: %d", err);
        return node;
    }

    sg.release();
    m_item->watchEvent(m_computeDoneEvent);

    if (m_needsExplicitSync)
        clFinish(m_queue);

    return node;
}

void CLItem::eventCompleted(cl_event event)
{
    clReleaseEvent(event);
    m_runnable->resetComputeInProgress();
    // Tell the node that the FBO's contents is stale and needs updating.
    m_runnable->node()->m_renderPending.testAndSetOrdered(0, 1);
    scheduleUpdate();
}

QQuickCLRunnable *CLItem::createCL()
{
    m_runnable = new CLRunnable(this);
    return m_runnable;
}

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#endif
    QGuiApplication app(argc, argv);

    qsrand(time(0));

    QQuickView view;
    QObject::connect(view.engine(), SIGNAL(quit()), &app, SLOT(quit()));

    qmlRegisterType<CLItem>("quickcl.qt.io", 1, 0, "CLItem");

    view.setSource(QUrl("qrc:///qml/particles.qml"));
    view.setResizeMode(QQuickView::SizeRootObjectToView);

    view.resize(1200, 600);
    view.show();

    return app.exec();
}

#include "particles.moc"
