import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

Page {
    background: null

    function sizeText(bytes) {
        if (bytes < 1024 * 1024) return qsTr("%1 KB").arg(Math.max(1, Math.round(bytes / 1024)))
        if (bytes < 1024 * 1024 * 1024) return qsTr("%1 MB").arg((bytes / 1024 / 1024).toFixed(1))
        return qsTr("%1 GB").arg((bytes / 1024 / 1024 / 1024).toFixed(2))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: LingmoUI.Units.largeSpacing * 2
        anchors.rightMargin: LingmoUI.Units.largeSpacing * 2
        anchors.bottomMargin: LingmoUI.Units.largeSpacing * 2
        spacing: LingmoUI.Units.largeSpacing

        LingmoUI.PageHeader {
            Layout.fillWidth: true
            title: qsTr("Transfers")
            subtitle: qsTr("Received files go to %1. They are never opened by themselves.").arg(share.config.receiveFolder)
            LingmoUI.ActionButton {
                text: qsTr("Clear finished")
                visible: list.count > share.transfers.activeCount
                onClicked: share.transfers.clearFinished()
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: LingmoUI.Units.smallSpacing
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            LingmoUI.WheelHandler { target: list }
            model: share.transfers

            delegate: LingmoUI.Card {
                id: card
                required property string transferId
                required property bool incoming
                required property string device
                required property string title
                required property real progress
                required property double done
                required property double total
                required property int state
                required property string error
                required property bool active
                width: ListView.view.width
                hoverEnabled: true

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: card.padding
                    anchors.rightMargin: card.padding
                    anchors.topMargin: LingmoUI.Units.largeSpacing
                    anchors.bottomMargin: LingmoUI.Units.largeSpacing
                    spacing: LingmoUI.Units.largeSpacing

                    Rectangle {
                        implicitWidth: LingmoUI.Units.iconTileSize
                        implicitHeight: LingmoUI.Units.iconTileSize
                        radius: LingmoUI.Units.iconTileRadius
                        color: card.incoming ? LingmoUI.Theme.greenColor : LingmoUI.Theme.blueColor
                        Image {
                            anchors.centerIn: parent
                            width: 16; height: 16
                            sourceSize: Qt.size(32, 32)
                            source: card.incoming ? "qrc:/images/transfers.svg" : "qrc:/images/send.svg"
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            color: LingmoUI.Theme.textColor
                            Layout.fillWidth: true
                            text: card.title
                            elide: Text.ElideMiddle
                            font.pointSize: LingmoUI.Units.fontBody
                            font.weight: Font.Medium
                        }
                        Label {
                            Layout.fillWidth: true
                            text: {
                                const who = card.incoming ? qsTr("From %1").arg(card.device) : qsTr("To %1").arg(card.device)
                                switch (card.state) {
                                case 0: return who + " · " + (card.error.length > 0 ? card.error : qsTr("Waiting"))
                                case 1: return who + " · " + qsTr("%1 of %2").arg(sizeText(card.done)).arg(sizeText(card.total))
                                case 2: return who + " · " + (card.incoming ? qsTr("Received") : qsTr("Sent"))
                                case 4: return who + " · " + qsTr("Declined")
                                case 5: return who + " · " + qsTr("Cancelled")
                                default: return who + " · " + (card.error.length > 0 ? card.error : qsTr("Failed"))
                                }
                            }
                            elide: Text.ElideRight
                            font.pointSize: LingmoUI.Units.fontCaption
                            color: card.state === 3 ? LingmoUI.Theme.errorColor : LingmoUI.Theme.secondaryTextColor
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            visible: card.active
                            value: card.progress
                            indeterminate: card.state === 0
                        }
                    }

                    LingmoUI.IconButton {
                        visible: card.incoming && card.state === 2
                        iconName: "folder-open"
                        tipText: qsTr("Show in folder")
                        onClicked: share.showInFolder(card.transferId)
                    }
                    LingmoUI.IconButton {
                        visible: card.active
                        iconName: "process-stop"
                        tipText: qsTr("Cancel")
                        onClicked: share.cancelTransfer(card.transferId)
                    }
                }
            }

            LingmoUI.EmptyState {
                anchors.centerIn: parent
                width: parent.width * 0.8
                visible: list.count === 0
                iconName: "folder-download"
                title: qsTr("No transfers yet")
                text: qsTr("Drag files to the right edge of the screen, or right-click them in Files and choose Send to nearby device.")
            }
        }
    }
}
