import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// The receiver asks for a PIN before it takes the request
OverlayWindow {
    id: dialog
    property string transferId
    property string deviceName

    width: 360
    height: layout.implicitHeight + LingmoUI.Units.largeSpacing * 4
    x: targetScreen ? targetScreen.virtualX + (targetScreen.width - width) / 2 : 0
    y: targetScreen ? targetScreen.virtualY + 72 : 72
    title: qsTr("PIN")
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Dialog

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: LingmoUI.Units.largeSpacing * 2
        spacing: LingmoUI.Units.largeSpacing
        Label {
            color: LingmoUI.Theme.textColor
            Layout.fillWidth: true
            text: qsTr("%1 asks for a PIN").arg(dialog.deviceName)
            font.pointSize: LingmoUI.Units.fontSubheading
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        TextField {
            id: pinField
            Layout.fillWidth: true
            placeholderText: qsTr("PIN shown on the other device")
            echoMode: TextInput.Password
            focus: true
            onAccepted: sendButton.clicked()
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            LingmoUI.ActionButton {
                text: qsTr("Cancel")
                onClicked: { share.cancelTransfer(dialog.transferId); dialog.close() }
            }
            LingmoUI.ActionButton {
                id: sendButton
                text: qsTr("Send")
                primary: true
                onClicked: { share.sendPin(dialog.transferId, pinField.text); dialog.close() }
            }
        }
    }
}
