import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// While files are dragged anywhere on the desktop: a thin strip at the screen
// edge that opens into bubbles of the nearby devices when the drag reaches it
// (like AirDrop). A second strip, on the side set in Settings, sends straight
// to the Lingmo computer next to this one.
Window {
    id: edge

    readonly property var scr: Qt.application.screens.length > 0 ? Qt.application.screens[0] : null
    readonly property string peerSide: share.config.edgeSide
    readonly property bool peerActive: peerSide !== "none" && share.config.edgePeer.length > 0
    // The bubbles go right, unless the other computer sits there
    readonly property string bubbleSide: peerActive && peerSide === "right" ? "left" : "right"
    readonly property bool bubblesOn: share.config.dropEdgeEnabled
    readonly property var targets: share.sendTargets
    readonly property int stripWidth: 6
    readonly property int panelWidth: 150
    readonly property int panelHeight: Math.min(scr ? scr.height - 80 : 600,
                                                Math.max(180, bubbles.implicitHeight + LingmoUI.Units.largeSpacing * 4))
    property bool expanded: false
    property bool sending: false
    property bool dragActive: false

    function dragStarted() {
        dragActive = true
        sending = false
        collapseTimer.stop()
        hideTimer.stop()
        expanded = false
        if (bubblesOn)
            edge.visible = true
        if (peerActive)
            peerEdge.visible = true
        if (targets.length === 0)
            share.refresh(false)
    }

    function dragEnded() {
        dragActive = false
        // A drop arrives just after the button is released: give it a moment
        hideTimer.interval = sending ? 2600 : 450
        hideTimer.restart()
    }

    function geometryFor(side, open, w, h) {
        if (!scr)
            return Qt.rect(0, 0, 1, 1)
        const width = open ? w : stripWidth
        const height = open ? h : (side === "top" ? stripWidth : Math.round(scr.height * 0.7))
        if (side === "top") {
            const tw = open ? w : Math.round(scr.width * 0.6)
            return Qt.rect(scr.virtualX + (scr.width - tw) / 2, scr.virtualY, tw, open ? h : stripWidth)
        }
        const x = side === "left" ? scr.virtualX : scr.virtualX + scr.width - width
        return Qt.rect(x, scr.virtualY + (scr.height - height) / 2, width, height)
    }

    readonly property rect geo: geometryFor(bubbleSide, expanded, panelWidth, panelHeight)
    x: geo.x
    y: geo.y
    width: geo.width
    height: geo.height
    color: "transparent"
    // Never takes focus or a taskbar entry; stays above everything
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    visible: false

    Timer {
        id: collapseTimer
        interval: 700
        onTriggered: if (!edge.sending) edge.expanded = false
    }
    Timer {
        id: hideTimer
        onTriggered: {
            edge.visible = false
            peerEdge.visible = false
            edge.expanded = false
            peerEdge.expanded = false
            edge.sending = false
        }
    }

    LingmoUI.WindowHelper { id: windowHelper }
    LingmoUI.WindowBlur {
        view: edge
        geometry: Qt.rect(edge.x, edge.y, edge.width, edge.height)
        windowRadius: LingmoUI.Theme.hugeRadius
        enabled: edge.visible && edge.expanded
    }

    // The resting strip: a soft accent line
    Rectangle {
        anchors.fill: parent
        visible: !edge.expanded
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: LingmoUI.Theme.highlightColor }
            GradientStop { position: 1.0; color: "transparent" }
        }
        opacity: windowHelper.compositing ? 0.75 : 1
    }

    // The open panel
    Rectangle {
        id: panel
        anchors.fill: parent
        visible: edge.expanded
        radius: LingmoUI.Theme.hugeRadius
        color: LingmoUI.Theme.backgroundColor
        opacity: windowHelper.compositing ? (LingmoUI.Theme.darkMode ? 0.78 : 0.86) : 1
        border.width: 1 / LingmoUI.Theme.devicePixelRatio
        border.color: LingmoUI.Theme.darkMode ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(0, 0, 0, 0.15)
    }

    // Opens when the drag reaches the strip, closes a bit after it leaves
    DropArea {
        z: -1
        anchors.fill: parent
        onEntered: (drag) => {
            collapseTimer.stop()
            edge.expanded = true
            drag.accepted = drag.hasUrls
        }
        onExited: collapseTimer.restart()
    }

    ColumnLayout {
        id: bubbles
        visible: edge.expanded
        anchors.centerIn: parent
        spacing: LingmoUI.Units.largeSpacing

        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: edge.panelWidth - 16
            text: edge.targets.length > 0 ? qsTr("Drop to send") : qsTr("No devices nearby")
            font.pointSize: LingmoUI.Units.fontCaption
            font.weight: Font.DemiBold
            color: LingmoUI.Theme.secondaryTextColor
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Repeater {
            model: edge.targets
            delegate: DeviceBubble {
                required property var modelData
                Layout.alignment: Qt.AlignHCenter
                bubbleSize: 54
                device: modelData
                onDragEntered: collapseTimer.stop()
                onDragExited: collapseTimer.restart()
                onDropped: (urls) => {
                    edge.sending = true
                    transferId = share.send(modelData.deviceId, urls)
                    refresh()
                    hideTimer.interval = 2600
                    if (!edge.dragActive)
                        hideTimer.restart()
                }
            }
        }

        Label {
            visible: edge.targets.length === 0
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: edge.panelWidth - 20
            text: qsTr("Open LocalSend or KDE Connect on the phone.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            font.pointSize: LingmoUI.Units.fontCaption
            color: LingmoUI.Theme.secondaryTextColor
        }
    }

    // ------------------------------------------------ the Lingmo computer next door
    Window {
        id: peerEdge
        property bool expanded: false
        property string transferId
        readonly property rect geo: edge.geometryFor(edge.peerSide, expanded, 220, 150)
        transientParent: null
        x: geo.x
        y: geo.y
        width: geo.width
        height: geo.height
        color: "transparent"
        flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
        visible: false

        Rectangle {
            anchors.fill: parent
            visible: !peerEdge.expanded
            gradient: Gradient {
                orientation: edge.peerSide === "top" ? Gradient.Horizontal : Gradient.Vertical
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: LingmoUI.Theme.greenColor }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
        Rectangle {
            anchors.fill: parent
            visible: peerEdge.expanded
            radius: LingmoUI.Theme.hugeRadius
            color: LingmoUI.Theme.backgroundColor
            opacity: windowHelper.compositing ? (LingmoUI.Theme.darkMode ? 0.78 : 0.86) : 1
            border.width: 1
            border.color: LingmoUI.Theme.borderColor
        }
        DeviceBubble {
            id: peerBubble
            visible: peerEdge.expanded
            anchors.centerIn: parent
            bubbleSize: 54
            device: ({ name: share.config.edgePeerName, kind: "localsend", iconName: "computer", battery: -1 })
            transferId: peerEdge.transferId
            onDropped: (urls) => {
                edge.sending = true
                peerEdge.transferId = share.send("ls:" + share.config.edgePeer, urls)
                refresh()
                hideTimer.interval = 2600
                if (!edge.dragActive)
                    hideTimer.restart()
            }
        }
        DropArea {
            anchors.fill: parent
            enabled: !peerEdge.expanded
            onEntered: (drag) => { peerEdge.expanded = true; drag.accepted = drag.hasUrls }
        }
    }
}
