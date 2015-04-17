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

// Demonstrates the texture-to-texture use case. This is basically an
// OpenCL-based equivalent to ShaderEffect items.

#include <QGuiApplication>
#include <QQuickView>
#include <QQmlEngine>
#include <QQuickCLItem>
#include <QQuickCLImageRunnable>

static bool profile = false;

class CLItem : public QQuickCLItem
{
    Q_OBJECT
    Q_PROPERTY(QQuickItem *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qreal factor READ factor WRITE setFactor NOTIFY factorChanged)

public:
    CLItem() : m_source(0), m_factor(1) { }

    QQuickCLRunnable *createCL() Q_DECL_OVERRIDE;

    QQuickItem *source() const { return m_source; }
    void setSource(QQuickItem *source) {
        if (m_source != source) {
            m_source = source;
            emit sourceChanged();
            update();
        }
    }

    qreal factor() const { return m_factor; }
    void setFactor(qreal v) {
        if (m_factor != v) {
            m_factor = v;
            emit factorChanged();
            update();
        }
    }

signals:
    void sourceChanged();
    void factorChanged();

private:
    QQuickItem *m_source;
    qreal m_factor;
};

class CLRunnable : public QQuickCLImageRunnable
{
public:
    CLRunnable(CLItem *item);
    ~CLRunnable();
    void runKernel(cl_mem inImage, cl_mem outImage, const QSize &size) Q_DECL_OVERRIDE;

private:
    CLItem *m_item;
    cl_program m_clProgram;
    cl_kernel m_clKernel;
};

QQuickCLRunnable *CLItem::createCL()
{
    return new CLRunnable(this);
}

static const char *openclSrc =
        "__constant sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;\n"
        "__kernel void Emboss(__read_only image2d_t imgIn, __write_only image2d_t imgOut, float factor) {\n"
        "    const int2 pos = { get_global_id(0), get_global_id(1) };\n"
        "    float4 diff = read_imagef(imgIn, sampler, pos + (int2)(1,1)) - read_imagef(imgIn, sampler, pos - (int2)(1,1));\n"
        "    float color = (diff.x + diff.y + diff.z) / factor + 0.5f;\n"
        "    write_imagef(imgOut, pos, (float4)(color, color, color, 1.0f));\n"
        "}\n";

CLRunnable::CLRunnable(CLItem *item)
    : QQuickCLImageRunnable(item, profile ? Profile : Flag(0)),
      m_item(item),
      m_clProgram(0),
      m_clKernel(0)
{
    QByteArray platform = m_item->platformName();
    qDebug("Using platform %s", platform.constData());
    m_clProgram = m_item->buildProgram(openclSrc);
    if (!m_clProgram)
        return;
    cl_int err;
    m_clKernel = clCreateKernel(m_clProgram, "Emboss", &err);
    if (!m_clKernel) {
        qWarning("Failed to create emboss OpenCL kernel: %d", err);
        return;
    }
}

CLRunnable::~CLRunnable()
{
    if (m_clKernel)
        clReleaseKernel(m_clKernel);
    if (m_clProgram)
        clReleaseProgram(m_clProgram);
}

void CLRunnable::runKernel(cl_mem inImage, cl_mem outImage, const QSize &size)
{
    if (!m_clProgram)
        return;

    if (profile)
        qDebug("CL time: %f", elapsed());

    clSetKernelArg(m_clKernel, 0, sizeof(cl_mem), &inImage);
    clSetKernelArg(m_clKernel, 1, sizeof(cl_mem), &outImage);
    const cl_float factor = m_item->factor();
    clSetKernelArg(m_clKernel, 2, sizeof(cl_float), &factor);

    const size_t workSize[] = { size_t(size.width()), size_t(size.height()) };
    cl_int err = clEnqueueNDRangeKernel(commandQueue(), m_clKernel, 2, 0, workSize, 0, 0, 0, 0);
    if (err != CL_SUCCESS)
        qWarning("Failed to enqueue kernel: %d", err);
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

    view.setSource(QUrl("qrc:///qml/imageprocess.qml"));
    view.setResizeMode(QQuickView::SizeRootObjectToView);

    view.resize(1200, 600);
    view.show();

    return app.exec();
}

#include "imageprocess.moc"
