import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "X wants to send you N files": shown for every device that is not trusted
OverlayWindow {
    id: dialog
    property var request: ({})
    readonly property bool isMessage: (request.message || "").length > 0

    width: 420
    height: layout.implicitHeight + LingmoUI.Units.largeSpacing * 4
    x: targetScreen ? targetScreen.virtualX + (targetScreen.width - width) / 2 : 0
    y: targetScreen ? targetScreen.virtualY + 72 : 72
    title: qsTr("Receive files")

    function sizeText(bytes) {
        if (bytes < 1024) return qsTr("%1 bytes").arg(bytes)
        if (bytes < 1024 * 1024) return qsTr("%1 KB").arg((bytes / 1024).toFixed(0))
        if (bytes < 1024 * 1024 * 1024) return qsTr("%1 MB").arg((bytes / 1024 / 1024).toFixed(1))
        return qsTr("%1 GB").arg((bytes / 1024 / 1024 / 1024).toFixed(2))
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: LingmoUI.Units.largeSpacing * 2
        spacing: LingmoUI.Units.largeSpacing

        RowLayout {
            spacing: LingmoUI.Units.largeSpacing
            DeviceTile {
                size: 44
                iconName: request.deviceType === "mobile" ? "smartphone" : "computer"
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    color: LingmoUI.Theme.textColor
                    Layout.fillWidth: true
                    text: dialog.isMessage ? qsTr("%1 sent a message").arg(request.name)
                                           : qsTr("%1 wants to send you %n file(s)", "", request.fileCount || 0).arg(request.name)
                    font.pointSize: LingmoUI.Units.fontSubheading
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    text: dialog.isMessage ? qsTr("LocalSend · %1").arg(request.address)
                                           : qsTr("%1 · saved to %2").arg(dialog.sizeText(request.total || 0)).arg(request.folder)
                    font.pointSize: LingmoUI.Units.fontCaption
                    color: LingmoUI.Theme.secondaryTextColor
                    elide: Text.ElideMiddle
                }
            }
        }

        // What is coming: names only, nothing is opened
        LingmoUI.Card {
            Layout.fillWidth: true
            visible: !dialog.isMessage
            implicitHeight: Math.min(filesList.contentHeight, 132) + LingmoUI.Units.smallSpacing * 2
            ListView {
                id: filesList
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.smallSpacing
                clip: true
                interactive: contentHeight > height
                model: request.files || []
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    spacing: LingmoUI.Units.smallSpacing
                    LingmoUI.IconItem {
                        width: 16; height: 16
                        source: modelData.type && modelData.type.startsWith("image/") ? "image-x-generic" : "text-x-generic"
                    }
                    Label {
                        color: LingmoUI.Theme.textColor
                        Layout.fillWidth: true
                        text: modelData.name
                        elide: Text.ElideMiddle
                        font.pointSize: LingmoUI.Units.fontBody
                    }
                    Label {
                        text: dialog.sizeText(modelData.size)
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }
                }
            }
        }

        LingmoUI.Card {
            Layout.fillWidth: true
            visible: dialog.isMessage
            Label {
                color: LingmoUI.Theme.textColor
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.largeSpacing
                text: request.message || ""
                wrapMode: Text.Wrap
                maximumLineCount: 6
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
        }

        CheckBox {
            id: trustBox
            visible: request.verified === true
            text: qsTr("Always accept from this device")
        }
        Label {
            Layout.fillWidth: true
            visible: request.verified !== true
            text: qsTr("This device did not prove its identity (older LocalSend), so it can't be trusted for good.")
            font.pointSize: LingmoUI.Units.fontCaption
            color: LingmoUI.Theme.secondaryTextColor
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: LingmoUI.Units.smallSpacing
            Item { Layout.fillWidth: true }
            LingmoUI.ActionButton {
                text: qsTr("Decline")
                onClicked: share.answerIncoming(request.session, false, false)
            }
            LingmoUI.ActionButton {
                text: dialog.isMessage ? qsTr("Show") : qsTr("Accept")
                primary: true
                onClicked: share.answerIncoming(request.session, true, trustBox.checked)
            }
        }
    }
}
