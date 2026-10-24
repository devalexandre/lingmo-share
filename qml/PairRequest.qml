import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// An Android phone asks to pair (KDE Connect): compare the key, then accept
OverlayWindow {
    id: dialog
    property string deviceId
    property string deviceName
    property string verificationKey

    width: 400
    height: layout.implicitHeight + LingmoUI.Units.largeSpacing * 4
    x: targetScreen ? targetScreen.virtualX + (targetScreen.width - width) / 2 : 0
    y: targetScreen ? targetScreen.virtualY + 72 : 72
    title: qsTr("Pair phone")

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: LingmoUI.Units.largeSpacing * 2
        spacing: LingmoUI.Units.largeSpacing

        RowLayout {
            spacing: LingmoUI.Units.largeSpacing
            DeviceTile { size: 44; kind: "android"; iconName: "smartphone" }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    color: LingmoUI.Theme.textColor
                    Layout.fillWidth: true
                    text: qsTr("%1 wants to pair").arg(dialog.deviceName)
                    font.pointSize: LingmoUI.Units.fontSubheading
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Check that the phone shows the same key.")
                    font.pointSize: LingmoUI.Units.fontCaption
                    color: LingmoUI.Theme.secondaryTextColor
                    wrapMode: Text.WordWrap
                }
            }
        }

        LingmoUI.Card {
            Layout.fillWidth: true
            Label {
                color: LingmoUI.Theme.textColor
                anchors.centerIn: parent
                text: dialog.verificationKey
                font.pointSize: LingmoUI.Units.fontDisplay
                font.weight: Font.DemiBold
                font.letterSpacing: 4
            }
            implicitHeight: 72
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            LingmoUI.ActionButton {
                text: qsTr("Reject")
                onClicked: { share.rejectPairing(dialog.deviceId); dialog.close() }
            }
            LingmoUI.ActionButton {
                text: qsTr("Pair")
                primary: true
                onClicked: { share.acceptPairing(dialog.deviceId); dialog.close() }
            }
        }
    }
}
