import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

LingmoUI.Window {
    id: rootWindow
    width: 980
    height: 660
    minimumWidth: 780
    minimumHeight: 560
    title: qsTr("Phone and devices")
    visible: false

    background.opacity: LingmoUI.Theme.darkMode ? 0.88 : 0.97
    header.height: LingmoUI.Units.windowHeaderHeight
    contentTopMargin: 0

    readonly property var pageNames: ["devices", "phone", "transfers", "settings", "guide"]

    // "phone", or "phone:messages" for a tab inside it
    function openPage(name) {
        const parts = String(name).split(":")
        const i = pageNames.indexOf(parts[0])
        if (i >= 0)
            sideBar.currentIndex = i
        if (parts[0] === "phone")
            phonePage.subPage = parts.length > 1 ? parts[1] : ""
    }

    LingmoUI.WindowBlur {
        view: rootWindow
        geometry: Qt.rect(rootWindow.x, rootWindow.y, rootWindow.width, rootWindow.height)
        windowRadius: rootWindow.windowRadius
        enabled: true
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        LingmoUI.SettingsSideBar {
            id: sideBar
            Layout.fillHeight: true
            iconSource: "qrc:/images/lingmo-share.svg"
            model: ListModel {
                ListElement { title: qsTr("Devices"); iconSource: "qrc:/images/phone.svg"; iconColor: "#0A84FF"; section: ""; badge: "" }
                ListElement { title: qsTr("Phone"); iconSource: "qrc:/images/tablet.svg"; iconColor: "#30D158"; section: ""; badge: "" }
                ListElement { title: qsTr("Transfers"); iconSource: "qrc:/images/transfers.svg"; iconColor: "#FF9F0A"; section: ""; badge: "" }
                ListElement { title: qsTr("Settings"); iconSource: "qrc:/images/settings.svg"; iconColor: "#8E8E93"; section: ""; badge: "" }
                ListElement { title: qsTr("On your phone"); iconSource: "qrc:/images/guide.svg"; iconColor: "#BF5AF2"; section: ""; badge: "" }
            }
            Component.onCompleted: currentIndex = 0
            Connections {
                target: share.transfers
                function onCountChanged() {
                    sideBar.model.setProperty(2, "badge", share.transfers.activeCount > 0 ? String(share.transfers.activeCount) : "")
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: sideBar.currentIndex

            DevicesPage { onOpenPhone: sideBar.currentIndex = 1; onOpenGuide: sideBar.currentIndex = 4 }
            PhonePage { id: phonePage; onOpenGuide: sideBar.currentIndex = 4 }
            TransfersPage {}
            Page {
                background: null
                LingmoUI.PageHeader {
                    id: settingsHeader
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: LingmoUI.Units.largeSpacing * 2
                    anchors.rightMargin: LingmoUI.Units.largeSpacing * 2
                    title: qsTr("Settings")
                }
                ScrollablePage {
                    anchors.top: settingsHeader.bottom
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    source: "qrc:/qml/SettingsHost.qml"
                }
            }
            Page {
                background: null
                LingmoUI.PageHeader {
                    id: guideHeader
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: LingmoUI.Units.largeSpacing * 2
                    anchors.rightMargin: LingmoUI.Units.largeSpacing * 2
                    title: qsTr("On your phone")
                    subtitle: qsTr("Lingmo works with the free apps you already know: no Lingmo app needed.")
                }
                ScrollablePage {
                    anchors.top: guideHeader.bottom
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    source: "qrc:/qml/GuideHost.qml"
                }
            }
        }
    }
}
