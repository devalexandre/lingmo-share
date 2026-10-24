import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

Page {
    id: page
    background: null
    signal openPhone()
    signal openGuide()

    property string sendTo

    FileDialog {
        id: fileDialog
        title: qsTr("Send files")
        fileMode: FileDialog.OpenFiles
        onAccepted: share.send(page.sendTo, selectedFiles)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: LingmoUI.Units.largeSpacing * 2
        anchors.rightMargin: LingmoUI.Units.largeSpacing * 2
        anchors.bottomMargin: LingmoUI.Units.largeSpacing * 2
        spacing: LingmoUI.Units.largeSpacing

        LingmoUI.PageHeader {
            Layout.fillWidth: true
            title: qsTr("Devices")
            subtitle: share.config.visibility === 2 ? qsTr("This computer is hidden. You can still send.")
                                                    : qsTr("This computer appears as %1").arg(share.config.deviceName)
            LingmoUI.IconButton {
                iconName: "view-refresh"
                tipText: qsTr("Look for devices")
                onClicked: share.refresh(true)
            }
        }

        LingmoUI.Banner {
            Layout.fillWidth: true
            type: LingmoUI.Banner.Warning
            closable: false
            title: share.firewall.length > 0 ? qsTr("The firewall (%1) may block nearby devices").arg(share.firewall) : ""
            text: share.firewall.length > 0 ? qsTr("Lingmo does not change firewall rules. To allow LocalSend (port %1) and KDE Connect, run in a terminal: %2")
                                              .arg(share.config.port).arg(share.firewallCommand) : ""
            LingmoUI.ActionButton {
                text: qsTr("Copy command")
                onClicked: share.copyText(share.firewallCommand)
            }
        }

        LingmoUI.Banner {
            Layout.fillWidth: true
            type: LingmoUI.Banner.Error
            closable: false
            text: share.localSendError
        }

        LingmoUI.Banner {
            Layout.fillWidth: true
            type: LingmoUI.Banner.Info
            closable: false
            visible: !share.androidInstalled
            title: qsTr("Android support is not installed")
            text: qsTr("iPhone, iPad, Mac and Windows work with LocalSend. For Android notifications, messages and battery, install KDE Connect support.")
            LingmoUI.ActionButton {
                text: qsTr("Install Android support")
                primary: true
                busy: share.installingAndroid
                onClicked: share.installAndroidSupport()
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: count > 0
            clip: true
            spacing: LingmoUI.Units.smallSpacing
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            LingmoUI.WheelHandler { target: list }
            model: share.devices

            delegate: LingmoUI.Card {
                id: card
                required property string deviceId
                required property string name
                required property string kind
                required property string iconName
                required property var battery
                required property bool charging
                required property var signalStrength
                required property bool reachable
                required property bool paired
                required property int pairState
                required property string verificationKey
                required property bool trusted
                required property bool sendable
                required property var lingmo
                width: ListView.view.width
                hoverEnabled: true
                highlighted: drop.containsDrag

                // Drop files on a device to send them
                DropArea {
                    id: drop
                    anchors.fill: parent
                    enabled: card.sendable
                    keys: ["text/uri-list"]
                    onDropped: (d) => { if (d.hasUrls) { d.acceptProposedAction(); share.send(card.deviceId, d.urls) } }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: card.padding
                    anchors.rightMargin: card.padding
                    anchors.topMargin: LingmoUI.Units.largeSpacing
                    anchors.bottomMargin: LingmoUI.Units.largeSpacing
                    spacing: LingmoUI.Units.largeSpacing

                    DeviceTile {
                        size: 36
                        kind: card.kind
                        iconName: card.iconName
                        opacity: card.reachable ? 1 : 0.5
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: LingmoUI.Units.smallSpacing
                            Label {
                                color: LingmoUI.Theme.textColor
                                text: card.name
                                font.pointSize: LingmoUI.Units.fontBody
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                                Layout.maximumWidth: 260
                            }
                            LingmoUI.Badge {
                                visible: card.trusted
                                text: card.kind === "android" ? qsTr("Paired") : qsTr("Trusted")
                                tint: LingmoUI.Theme.greenColor
                            }
                            LingmoUI.Badge {
                                visible: card.lingmo === true
                                text: "Lingmo"
                                tint: LingmoUI.Theme.blueColor
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: {
                                if (card.kind === "android") {
                                    if (card.pairState === 1) return qsTr("Waiting for the phone… Key: %1").arg(card.verificationKey)
                                    if (card.pairState === 2) return qsTr("Wants to pair. Key: %1").arg(card.verificationKey)
                                    if (!card.paired) return qsTr("Android · not paired")
                                    if (!card.reachable) return qsTr("Android · away")
                                    let parts = [qsTr("Android")]
                                    if (card.battery >= 0) parts.push(qsTr("%1% battery").arg(card.battery) + (card.charging ? qsTr(", charging") : ""))
                                    if (card.signalStrength >= 0) parts.push(qsTr("signal %1/4").arg(card.signalStrength))
                                    return parts.join(" · ")
                                }
                                return qsTr("LocalSend · %1").arg(card.lingmo ? qsTr("Lingmo") : qsTr("ready to receive"))
                            }
                            font.pointSize: LingmoUI.Units.fontCaption
                            color: LingmoUI.Theme.secondaryTextColor
                            elide: Text.ElideRight
                        }
                    }

                    // Pairing (Android)
                    LingmoUI.ActionButton {
                        visible: card.kind === "android" && card.pairState === 0 && card.reachable
                        text: qsTr("Pair")
                        primary: true
                        onClicked: share.pair(card.deviceId)
                    }
                    LingmoUI.ActionButton {
                        visible: card.kind === "android" && card.pairState === 1
                        text: qsTr("Cancel")
                        onClicked: share.rejectPairing(card.deviceId)
                    }
                    LingmoUI.ActionButton {
                        visible: card.kind === "android" && card.pairState === 2
                        text: qsTr("Reject")
                        onClicked: share.rejectPairing(card.deviceId)
                    }
                    LingmoUI.ActionButton {
                        visible: card.kind === "android" && card.pairState === 2
                        text: qsTr("Accept")
                        primary: true
                        onClicked: share.acceptPairing(card.deviceId)
                    }

                    LingmoUI.IconButton {
                        visible: card.kind === "android" && card.paired && card.reachable
                        iconName: "audio-volume-high"
                        tipText: qsTr("Ring the phone")
                        onClicked: share.ring(card.deviceId)
                    }
                    LingmoUI.IconButton {
                        visible: card.kind === "android" && card.paired
                        iconName: "go-next"
                        tipText: qsTr("Open phone")
                        onClicked: page.openPhone()
                    }
                    LingmoUI.ActionButton {
                        visible: card.sendable
                        text: qsTr("Send files…")
                        iconName: "document-send"
                        onClicked: { page.sendTo = card.deviceId; fileDialog.open() }
                    }
                    LingmoUI.IconButton {
                        iconName: "view-more-symbolic"
                        tipText: qsTr("More")
                        visible: (card.kind === "android" && card.paired) || card.kind === "localsend"
                        onClicked: moreMenu.popup()
                        Menu {
                            id: moreMenu
                            MenuItem {
                                visible: card.kind === "localsend"
                                height: visible ? implicitHeight : 0
                                text: card.trusted ? qsTr("Stop trusting") : qsTr("Always accept from this device")
                                onTriggered: share.trust(card.deviceId, !card.trusted)
                            }
                            MenuItem {
                                visible: card.kind === "android"
                                height: visible ? implicitHeight : 0
                                text: qsTr("Send clipboard")
                                onTriggered: share.sendClipboard(card.deviceId)
                            }
                            MenuItem {
                                visible: card.kind === "android"
                                height: visible ? implicitHeight : 0
                                text: qsTr("Unpair")
                                onTriggered: share.unpair(card.deviceId)
                            }
                        }
                    }
                }
            }
        }

        // Nothing around yet: how to get the phone in
        Flickable {
            id: emptyFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: list.count === 0
            clip: true
            contentHeight: emptyColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            LingmoUI.WheelHandler { target: emptyFlick }

            ColumnLayout {
                id: emptyColumn
                width: emptyFlick.width
                spacing: LingmoUI.Units.largeSpacing
                LingmoUI.EmptyState {
                    Layout.fillWidth: true
                    Layout.topMargin: LingmoUI.Units.largeSpacing
                    iconName: "smartphone"
                    title: qsTr("No devices nearby yet")
                    text: qsTr("Install KDE Connect (Android) or LocalSend (iPhone, iPad, Mac, Windows) on the other device, on the same Wi-Fi.")
                }
                Loader {
                    Layout.fillWidth: true
                    source: "qrc:/qml/GuideHost.qml"
                }
            }
        }
    }
}
