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
            id: srcSubTree
            anchors.fill: parent
            layer.enabled: true
            visible: !cbHide.checked
            Image {
                id: srcImage
                anchors.fill: parent
                anchors.margins: 4
                source: "image.png"
            }
            Rectangle {
                width: 100
                height: 100
                color: "red"
                NumberAnimation on rotation { from: 0; to: 360; duration: 2000; loops: Animation.Infinite; }
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 20
            }
            Column {
                spacing: 10
                anchors.centerIn: parent
                CheckBox {
                    id: cbLayer
                    text: "Apply to entire sub-tree (FBO)"
                    checked: false
                    onCheckedChanged: clItem.source = checked ? srcSubTree : srcImage
                }
                CheckBox {
                    id: cbHide
                    text: "Hide source"
                    checked: false
                    enabled: cbLayer.checked
                }
            }
        }
        Button {
            text: "Make visible"
            visible: cbHide.checked
            onClicked: cbHide.checked = false
            anchors.centerIn: parent
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
            anchors.fill: parent
            anchors.margins: 4
            source: srcImage
            factor: 1
            NumberAnimation on factor {
                from: 1
                to: 20
                duration: 8000
                loops: Animation.Infinite
            }
        }

        Text {
            x: 10
            y: 10
            text: "Emboss factor: " + Math.round(clItem.factor) + "\nSource is "
                  + (clItem.source === srcImage ? "image" : "layered sub-tree")
            font.pointSize: 16
            color: "green"
        }
    }
}
