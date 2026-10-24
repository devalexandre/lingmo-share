import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "Send to nearby device": the file manager, Screenshots and the CLI open it
OverlayWindow {
    id: picker
    property var files: []
    readonly property var targets: share.sendTargets

    width: 520
    height: layout.implicitHeight + LingmoUI.Units.largeSpacing * 4
    x: targetScreen ? targetScreen.virtualX + (targetScreen.width - width) / 2 : 0
    y: targetScreen ? targetScreen.virtualY + (targetScreen.height - height) / 3 : 0
    title: qsTr("Send to nearby device")
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Dialog

    // Look around only while the picker is open (even when not visible to others)
    Component.onCompleted: share.setDiscovering(true)
    onVisibleChanged: if (!visible) share.setDiscovering(false)

    Shortcut { sequence: "Escape"; onActivated: picker.close() }

    function baseName(path) {
        const s = String(path)
        return s.substring(s.lastIndexOf("/") + 1)
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: LingmoUI.Units.largeSpacing * 2
        spacing: LingmoUI.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    color: LingmoUI.Theme.textColor
                    text: qsTr("Send to nearby device")
                    font.pointSize: LingmoUI.Units.fontHeading
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: picker.files.length === 1 ? picker.baseName(picker.files[0])
                                                    : qsTr("%n file(s)", "", picker.files.length)
                    elide: Text.ElideMiddle
                    font.pointSize: LingmoUI.Units.fontCaption
                    color: LingmoUI.Theme.secondaryTextColor
                }
            }
            LingmoUI.IconButton {
                iconName: "view-refresh"
                tipText: qsTr("Look again")
                onClicked: share.refresh(true)
            }
            LingmoUI.IconButton {
                iconName: "window-close"
                tipText: qsTr("Close")
                onClicked: picker.close()
            }
        }

        Flow {
            Layout.fillWidth: true
            Layout.minimumHeight: 110
            spacing: LingmoUI.Units.largeSpacing * 1.5
            visible: picker.targets.length > 0
            Repeater {
                model: picker.targets
                delegate: DeviceBubble {
                    required property var modelData
                    device: modelData
                    onClicked: { transferId = share.send(modelData.deviceId, picker.files); refresh() }
                    onDropped: (urls) => { transferId = share.send(modelData.deviceId, urls); refresh() }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumHeight: 110
            visible: picker.targets.length === 0
            spacing: LingmoUI.Units.largeSpacing
            LingmoUI.Spinner { Layout.alignment: Qt.AlignHCenter; running: picker.visible }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Looking for devices… Open LocalSend or KDE Connect on the phone, on the same Wi-Fi.")
                wrapMode: Text.WordWrap
                color: LingmoUI.Theme.secondaryTextColor
            }
            LingmoUI.ActionButton {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("How to connect a phone")
                flat: true
                primary: true
                onClicked: share.showWindow("guide")
            }
        }
    }
}
