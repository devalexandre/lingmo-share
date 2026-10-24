import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// A file from the Lingmo computer next door lands here: a card slides in
// from that edge, rests a moment, and goes. The file is not opened.
OverlayWindow {
    id: fly
    property string side: "left"
    property string device
    property string title
    property string path
    readonly property bool isImage: /\.(png|jpe?g|webp|gif|bmp)$/i.test(path)

    width: 300
    height: 96
    readonly property real restX: !targetScreen ? 0
        : side === "left" ? targetScreen.virtualX + 24
        : side === "right" ? targetScreen.virtualX + targetScreen.width - width - 24
        : targetScreen.virtualX + (targetScreen.width - width) / 2
    readonly property real restY: !targetScreen ? 0
        : side === "top" ? targetScreen.virtualY + 48 : targetScreen.virtualY + targetScreen.height / 2 - height / 2
    x: restX
    y: restY
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    title: qsTr("Received")

    Item {
        id: card
        anchors.fill: parent
        // Slides from beyond the edge
        transform: Translate { id: shift }
        opacity: 0

        RowLayout {
            anchors.fill: parent
            anchors.margins: LingmoUI.Units.largeSpacing
            spacing: LingmoUI.Units.largeSpacing
            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                radius: LingmoUI.Theme.smallRadius
                color: LingmoUI.Theme.alternateBackgroundColor
                clip: true
                Image {
                    anchors.fill: parent
                    visible: fly.isImage
                    source: fly.isImage ? "file://" + fly.path : ""
                    sourceSize: Qt.size(128, 128)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                LingmoUI.IconItem {
                    anchors.centerIn: parent
                    visible: !fly.isImage
                    width: 32; height: 32
                    source: "text-x-generic"
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    color: LingmoUI.Theme.textColor
                    Layout.fillWidth: true
                    text: qsTr("Received from %1").arg(fly.device)
                    font.pointSize: LingmoUI.Units.fontBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: fly.title
                    font.pointSize: LingmoUI.Units.fontCaption
                    color: LingmoUI.Theme.secondaryTextColor
                    elide: Text.ElideMiddle
                }
            }
        }
    }

    SequentialAnimation {
        running: fly.visible
        ScriptAction {
            script: {
                shift.x = fly.side === "left" ? -fly.width : fly.side === "right" ? fly.width : 0
                shift.y = fly.side === "top" ? -fly.height : 0
            }
        }
        ParallelAnimation {
            NumberAnimation { target: shift; property: "x"; to: 0; duration: LingmoUI.Units.veryLongDuration; easing.type: Easing.OutBack }
            NumberAnimation { target: shift; property: "y"; to: 0; duration: LingmoUI.Units.veryLongDuration; easing.type: Easing.OutBack }
            NumberAnimation { target: card; property: "opacity"; to: 1; duration: LingmoUI.Units.longDuration }
        }
        PauseAnimation { duration: 3200 }
        NumberAnimation { target: card; property: "opacity"; to: 0; duration: LingmoUI.Units.longDuration }
        ScriptAction { script: fly.close() }
    }
}
