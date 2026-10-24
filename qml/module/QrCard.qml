import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI
import Lingmo.Share

// A store link as a QR code (drawn here, no web service) with its name
ColumnLayout {
    id: control
    property string label
    property string url
    property int codeSize: 112

    spacing: LingmoUI.Units.smallSpacing

    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: control.codeSize + LingmoUI.Units.smallSpacing * 2
        implicitHeight: implicitWidth
        radius: LingmoUI.Theme.smallRadius
        // QR codes need a light background in both themes to scan well
        color: LingmoUI.Theme.lightBackgroundColor
        border.width: 1
        border.color: LingmoUI.Theme.borderColor

        QrImage {
            anchors.centerIn: parent
            width: control.codeSize
            height: control.codeSize
            text: control.url
            color: LingmoUI.Theme.lightTextColor
            background: LingmoUI.Theme.lightBackgroundColor
        }
    }

    Label {
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: control.codeSize + 24
        text: control.label
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.pointSize: LingmoUI.Units.fontCaption
        color: LingmoUI.Theme.secondaryTextColor
    }
}
