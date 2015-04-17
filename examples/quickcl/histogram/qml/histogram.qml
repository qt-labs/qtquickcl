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

import QtQuick 2.0
import QtQuick.Controls 1.0
import quickcl.qt.io 1.0

Item {
    Rectangle {
        id: orig
        width: parent.width / 2 - 4
        height: parent.height
        border.color: "magenta"
        border.width: 4

        Item {
            id: src
            anchors.fill: parent
            layer.enabled: true
            Image {
                anchors.fill: parent
                anchors.margins: 4
                source: "image.png"
            }
            Image {
                source: "qt.png"
                width: 100
                height: 100
                smooth: true
                SequentialAnimation on scale {
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 8; duration: 2000; easing.type: Easing.OutBounce }
                    PauseAnimation { duration: 2000 }
                    NumberAnimation { from: 8; to: 1; duration: 2000; easing.type: Easing.OutBounce }
                    PauseAnimation { duration: 2000 }
                }
                NumberAnimation on rotation { from: 0; to: 360; duration: 2000; loops: Animation.Infinite }
                anchors.centerIn: parent
            }
        }
    }

    Rectangle {
        width: parent.width / 2 - 4
        height: parent.height
        anchors.left: orig.right
        anchors.leftMargin: 4
        border.color: "magenta"
        border.width: 4

        CLItem {
            id: clItem
            source: src
            anchors.fill: parent
            anchors.margins: 4
            onNewResultsAvailable: {
                var maxValue = rectRoot.height - topText.y - topText.height;
                for (var i = 0; i < 256; ++i)
                    rep.itemAt(i).height = Math.max(1, Math.min(result.get(i) / scaleFactor.value, maxValue))
            }
            Item {
                anchors.fill: parent
                Row {
                    anchors.fill: parent
                    id: rectRoot
                    Repeater {
                        id: rep
                        // It might be tempting to use clItem.result as the
                        // model. This would however result in a serious
                        // slowdown due to dropping and adding 256 new
                        // rectangles on every update.
                        model: 256
                        Rectangle {
                            width: parent ? parent.width / rep.count : 0
                            height: 0
                            y: parent ? parent.height - height : 0
                            color: "black"
                        }
                    }
                }
            }
        }
        Text {
            id: topText
            font.pointSize: 16
            text: "Histogram calculated on the GPU with OpenCL (scale: " + Math.round(scaleFactor.value) + ")"
            color: "green"
            x: 10
            y: 10
        }
        Slider {
            id: scaleFactor
            x: 10
            anchors.top: topText.bottom
            anchors.bottom: parent.bottom
            anchors.margins: 10
            minimumValue: 1
            maximumValue: 1024
            value: 8
            orientation: Qt.Vertical
        }
    }
}
