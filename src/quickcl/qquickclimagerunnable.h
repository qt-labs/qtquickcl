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

#ifndef QQUICKCLIMAGERUNNABLE_H
#define QQUICKCLIMAGERUNNABLE_H

#include <QtQuickCL/qtquickclglobal.h>
#include <QtQuickCL/qquickclrunnable.h>

QT_BEGIN_NAMESPACE

class QQuickCLImageRunnablePrivate;
class QQuickCLItem;

class Q_QUICKCL_EXPORT QQuickCLImageRunnable : public QQuickCLRunnable
{
    Q_DECLARE_PRIVATE(QQuickCLImageRunnable)

public:
    enum Flag {
        NoOutputImage = 0x01,
        Profile = 0x02,
        ForceCLFinish = 0x04
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    QQuickCLImageRunnable(QQuickCLItem *item, Flags flags = 0);
    ~QQuickCLImageRunnable();

    cl_command_queue commandQueue() const;

    void setSourcePropertyName(const QByteArray &name);

    double elapsed() const;

protected:
    virtual void runKernel(cl_mem inImage, cl_mem outImage, const QSize &size) = 0;

private:
    QSGNode *update(QSGNode *node) Q_DECL_OVERRIDE;

    QQuickCLImageRunnablePrivate *d_ptr;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(QQuickCLImageRunnable::Flags)

QT_END_NAMESPACE

#endif
