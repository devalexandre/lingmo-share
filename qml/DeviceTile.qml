import QtQuick
import QtQuick.Controls
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// The colored device square (lists) or circle (bubbles)
Rectangle {
    id: tile
    property string iconName: "computer"
    property string kind: "localsend"
    property int size: LingmoUI.Units.iconTileSize
    implicitWidth: size
    implicitHeight: size
    radius: size > 40 ? size / 2 : LingmoUI.Units.iconTileRadius
    color: kind === "android" ? LingmoUI.Theme.greenColor : LingmoUI.Theme.blueColor
    Behavior on color { ColorAnimation { duration: LingmoUI.Units.shortDuration } }

    function image(name) {
        switch (name) {
        case "smartphone": return "qrc:/images/phone.svg"
        case "tablet": return "qrc:/images/tablet.svg"
        case "computer-laptop": return "qrc:/images/laptop.svg"
        case "internet-web-browser": return "qrc:/images/web.svg"
        default: return "qrc:/images/computer.svg"
        }
    }

    Image {
        anchors.centerIn: parent
        width: Math.round(tile.size * 0.58)
        height: width
        sourceSize: Qt.size(width * 2, height * 2)
        source: tile.image(tile.iconName)
    }
}
