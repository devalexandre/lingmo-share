import QtQuick
import QtQuick.Window
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Frameless glass card on top of everything (requests, picker, bubbles),
// with the Dock's tone, border and blur. Content goes inside.
Window {
    id: control

    default property alias content: _content.data
    property real radius: LingmoUI.Theme.hugeRadius
    property bool blurEnabled: true
    readonly property var targetScreen: Qt.application.screens.length > 0 ? Qt.application.screens[0] : null

    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    transientParent: null

    LingmoUI.WindowHelper {
        id: windowHelper
    }

    LingmoUI.WindowShadow {
        view: control
        radius: control.radius
        strength: 1
    }

    LingmoUI.WindowBlur {
        view: control
        geometry: Qt.rect(control.x, control.y, control.width, control.height)
        windowRadius: control.radius
        enabled: control.visible && control.blurEnabled
    }

    Rectangle {
        id: _background
        anchors.fill: parent
        radius: control.radius
        color: LingmoUI.Theme.backgroundColor
        opacity: windowHelper.compositing ? (LingmoUI.Theme.darkMode ? 0.78 : 0.86) : 1.0
        Behavior on color { ColorAnimation { duration: LingmoUI.Units.longDuration } }
    }

    Rectangle {
        anchors.fill: parent
        radius: control.radius
        color: "transparent"
        border.width: 1 / LingmoUI.Theme.devicePixelRatio
        border.color: LingmoUI.Theme.darkMode ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(0, 0, 0, 0.15)
        antialiasing: true
    }

    Item {
        id: _content
        anchors.fill: parent
    }
}
