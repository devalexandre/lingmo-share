import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// A nearby device as a round bubble: name, battery, a ring for the transfer
// in progress, and a drop target for files
Item {
    id: bubble
    property var device: ({})
    property string transferId
    property real progress: 0
    property int transferState: -1 // Transfers::State
    property int bubbleSize: 60
    readonly property bool hot: dropArea.containsDrag || mouse.containsMouse

    signal clicked()
    signal dropped(var urls)
    signal dragEntered()
    signal dragExited()

    implicitWidth: Math.max(bubbleSize + 28, nameLabel.implicitWidth)
    implicitHeight: bubbleSize + nameLabel.implicitHeight + infoLabel.implicitHeight + LingmoUI.Units.smallSpacing * 2

    function refresh() {
        if (!transferId)
            return
        const t = share.transfer(transferId)
        progress = t.progress || 0
        transferState = t.state === undefined ? -1 : t.state
    }

    Connections {
        target: share
        function onTransferChanged(id) { if (id === bubble.transferId) bubble.refresh() }
    }

    Item {
        id: circle
        anchors.horizontalCenter: parent.horizontalCenter
        width: bubble.bubbleSize
        height: bubble.bubbleSize
        scale: bubble.hot ? 1.12 : 1.0
        Behavior on scale { NumberAnimation { duration: LingmoUI.Units.shortDuration; easing.type: LingmoUI.Units.easing } }

        // Glow while a drag hovers
        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 10
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 3
            border.color: LingmoUI.Theme.highlightColor
            opacity: dropArea.containsDrag ? 0.9 : 0
            Behavior on opacity { NumberAnimation { duration: LingmoUI.Units.shortDuration } }
        }

        DeviceTile {
            anchors.fill: parent
            size: bubble.bubbleSize
            kind: bubble.device.kind || "localsend"
            iconName: bubble.device.iconName || "computer"
        }

        // Transfer ring
        Canvas {
            id: ring
            anchors.fill: parent
            anchors.margins: -5
            visible: bubble.transferState === 0 || bubble.transferState === 1
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const r = width / 2 - 2
                ctx.lineWidth = 3
                ctx.strokeStyle = LingmoUI.Theme.alternateBackgroundColor
                ctx.beginPath(); ctx.arc(width / 2, height / 2, r, 0, Math.PI * 2); ctx.stroke()
                ctx.strokeStyle = LingmoUI.Theme.highlightColor
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, r, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * Math.max(0.02, bubble.progress))
                ctx.stroke()
            }
            Connections {
                target: bubble
                function onProgressChanged() { ring.requestPaint() }
                function onTransferStateChanged() { ring.requestPaint() }
            }
            Connections {
                target: LingmoUI.Theme
                function onDarkModeChanged() { ring.requestPaint() }
            }
        }

        // Done / failed mark
        Rectangle {
            visible: bubble.transferState >= 2
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 22; height: 22; radius: 11
            color: bubble.transferState === 2 ? LingmoUI.Theme.successColor : LingmoUI.Theme.errorColor
            border.width: 2
            border.color: LingmoUI.Theme.backgroundColor
            Label {
                anchors.centerIn: parent
                text: bubble.transferState === 2 ? "✓" : "!"
                color: LingmoUI.Theme.highlightedTextColor
                font.pointSize: LingmoUI.Units.fontCaption
                font.weight: Font.Bold
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: bubble.clicked()
        }

        DropArea {
            id: dropArea
            anchors.fill: parent
            anchors.margins: -12
            keys: ["text/uri-list"]
            onEntered: bubble.dragEntered()
            onExited: bubble.dragExited()
            onDropped: (drop) => {
                if (!drop.hasUrls)
                    return
                drop.acceptProposedAction()
                bubble.dropped(drop.urls)
            }
        }
    }

    Label {
        id: nameLabel
        anchors.top: circle.bottom
        anchors.topMargin: LingmoUI.Units.smallSpacing
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(implicitWidth, bubble.bubbleSize + 40)
        text: bubble.device.name || ""
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        font.pointSize: LingmoUI.Units.fontBody
        font.weight: Font.Medium
        color: LingmoUI.Theme.textColor
    }
    Label {
        id: infoLabel
        anchors.top: nameLabel.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: bubble.transferState === 0 ? qsTr("Waiting…")
            : bubble.transferState === 1 ? Math.round(bubble.progress * 100) + "%"
            : bubble.transferState === 2 ? qsTr("Sent")
            : bubble.transferState > 2 ? qsTr("Not sent")
            : (bubble.device.battery >= 0 ? qsTr("%1% battery").arg(bubble.device.battery)
               : bubble.device.kind === "android" ? qsTr("Android") : qsTr("LocalSend"))
        font.pointSize: LingmoUI.Units.fontCaption
        color: LingmoUI.Theme.secondaryTextColor
    }
}
