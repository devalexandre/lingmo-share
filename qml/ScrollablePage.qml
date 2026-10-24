import QtQuick
import QtQuick.Controls
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// A scrolling column that loads its content from a file
Flickable {
    id: flick
    property alias source: loader.source
    clip: true
    contentWidth: width
    contentHeight: loader.height + LingmoUI.Units.largeSpacing * 2
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    LingmoUI.WheelHandler { target: flick }

    Loader {
        id: loader
        x: LingmoUI.Units.largeSpacing * 2
        width: flick.width - LingmoUI.Units.largeSpacing * 4
        height: item ? item.implicitHeight : 0
        onStatusChanged: if (status === Loader.Error) source = "qrc:/qml/ModuleMissing.qml"
    }
}
