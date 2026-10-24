import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// The paired Android phone: battery, signal, ring, media, notifications, SMS
Page {
    id: page
    background: null
    signal openGuide()

    property var phones: share.devices.phones()
    property int phoneIndex: 0
    readonly property var phone: phones.length > 0 ? phones[Math.min(phoneIndex, phones.length - 1)] : null
    readonly property string phoneId: phone ? phone.deviceId : ""
    property var notifications: []
    property string subPage
    onSubPageChanged: tabs.currentIndex = Math.max(0, ["", "notifications", "messages"].indexOf(subPage))
    property var media: ({})

    function reload() {
        if (!phone) return
        notifications = share.phoneNotifications(phoneId)
        media = share.media(phoneId)
        share.sms.device = phoneId
    }
    onPhoneIdChanged: reload()
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()

    Connections {
        target: share.devices
        function onChanged() { page.phones = share.devices.phones() }
    }
    Connections {
        target: share
        function onPhoneNotificationsChanged(id) { if ("kdc:" + id === page.phoneId) page.notifications = share.phoneNotifications(page.phoneId) }
        function onMediaChanged(id) { if ("kdc:" + id === page.phoneId) page.media = share.media(page.phoneId) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: LingmoUI.Units.largeSpacing * 2
        anchors.rightMargin: LingmoUI.Units.largeSpacing * 2
        anchors.bottomMargin: LingmoUI.Units.largeSpacing * 2
        spacing: LingmoUI.Units.largeSpacing

        LingmoUI.PageHeader {
            Layout.fillWidth: true
            title: page.phone ? page.phone.name : qsTr("Phone")
            subtitle: {
                if (!page.phone) return qsTr("Pair an Android phone to see it here.")
                if (!page.phone.reachable) return qsTr("Away: connect the phone to the same Wi-Fi.")
                let parts = []
                if (page.phone.battery >= 0) parts.push(qsTr("%1% battery").arg(page.phone.battery) + (page.phone.charging ? qsTr(", charging") : ""))
                if (page.phone.signalStrength >= 0) parts.push(qsTr("%1 signal %2/4").arg(page.phone.networkType || "").arg(page.phone.signalStrength))
                return parts.join(" · ")
            }
            ComboBox {
                visible: page.phones.length > 1
                model: page.phones.map(p => p.name)
                currentIndex: page.phoneIndex
                onActivated: (i) => page.phoneIndex = i
            }
        }

        LingmoUI.EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !page.phone
            iconName: "smartphone"
            title: qsTr("No phone paired")
            text: qsTr("Install KDE Connect on the Android phone and pair it in Devices.")
            actionText: qsTr("How to pair")
            onActionTriggered: page.openGuide()
        }

        LingmoUI.SegmentedControl {
            id: tabs
            visible: !!page.phone
            model: [qsTr("Overview"), qsTr("Notifications"), qsTr("Messages")]
            currentIndex: 0
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !!page.phone
            currentIndex: tabs.currentIndex

            // ------------------------------------------------ overview
            Flickable {
                id: overview
                clip: true
                contentHeight: overviewColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                LingmoUI.WheelHandler { target: overview }

                ColumnLayout {
                    id: overviewColumn
                    width: overview.width
                    spacing: LingmoUI.Units.largeSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: LingmoUI.Units.largeSpacing

                        LingmoUI.Card {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: LingmoUI.Units.largeSpacing
                                Label { text: qsTr("Battery"); color: LingmoUI.Theme.secondaryTextColor; font.pointSize: LingmoUI.Units.fontCaption }
                                Label {
                                    color: LingmoUI.Theme.textColor
                                    text: page.phone && page.phone.battery >= 0 ? page.phone.battery + "%" : "—"
                                    font.pointSize: LingmoUI.Units.fontDisplay
                                    font.weight: Font.DemiBold
                                }
                                ProgressBar { Layout.fillWidth: true; value: page.phone && page.phone.battery >= 0 ? page.phone.battery / 100 : 0 }
                            }
                        }
                        LingmoUI.Card {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: LingmoUI.Units.largeSpacing
                                Label { text: qsTr("Mobile signal"); color: LingmoUI.Theme.secondaryTextColor; font.pointSize: LingmoUI.Units.fontCaption }
                                Label {
                                    color: LingmoUI.Theme.textColor
                                    text: page.phone && page.phone.signalStrength >= 0 ? (page.phone.networkType || "") + " " + page.phone.signalStrength + "/4" : "—"
                                    font.pointSize: LingmoUI.Units.fontDisplay
                                    font.weight: Font.DemiBold
                                }
                                Row {
                                    spacing: 3
                                    Repeater {
                                        model: 4
                                        Rectangle {
                                            required property int index
                                            width: 8; height: 6 + index * 5
                                            anchors.bottom: parent.bottom
                                            radius: 2
                                            color: page.phone && index < page.phone.signalStrength ? LingmoUI.Theme.highlightColor : LingmoUI.Theme.alternateBackgroundColor
                                        }
                                    }
                                }
                            }
                        }
                    }

                    LingmoUI.Card {
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: LingmoUI.Units.largeSpacing
                            spacing: LingmoUI.Units.smallSpacing
                            LingmoUI.ActionButton {
                                text: qsTr("Ring the phone")
                                iconName: "audio-volume-high"
                                enabled: page.phone && page.phone.reachable
                                onClicked: share.ring(page.phoneId)
                            }
                            LingmoUI.ActionButton {
                                text: qsTr("Send clipboard")
                                iconName: "edit-paste"
                                enabled: page.phone && page.phone.reachable
                                onClicked: share.sendClipboard(page.phoneId)
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: qsTr("Remote input and presenter are used from the KDE Connect app on the phone.")
                                font.pointSize: LingmoUI.Units.fontCaption
                                color: LingmoUI.Theme.secondaryTextColor
                                wrapMode: Text.WordWrap
                                Layout.maximumWidth: 260
                            }
                        }
                    }

                    // Media playing on the phone
                    LingmoUI.Card {
                        Layout.fillWidth: true
                        visible: (page.media.title || "").length > 0 || (page.media.playerList || []).length > 0
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: LingmoUI.Units.largeSpacing
                            spacing: LingmoUI.Units.largeSpacing
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    color: LingmoUI.Theme.textColor
                                    Layout.fillWidth: true
                                    text: page.media.title || qsTr("Nothing playing")
                                    font.pointSize: LingmoUI.Units.fontSubheading
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: [page.media.artist, page.media.player].filter(s => s && s.length > 0).join(" · ")
                                    color: LingmoUI.Theme.secondaryTextColor
                                    font.pointSize: LingmoUI.Units.fontCaption
                                    elide: Text.ElideRight
                                }
                            }
                            LingmoUI.IconButton { iconName: "media-skip-backward"; tipText: qsTr("Previous"); onClicked: share.mediaAction(page.phoneId, "Previous") }
                            LingmoUI.IconButton {
                                iconName: page.media.isPlaying ? "media-playback-pause" : "media-playback-start"
                                tipText: page.media.isPlaying ? qsTr("Pause") : qsTr("Play")
                                size: 36; iconSize: 20
                                onClicked: share.mediaAction(page.phoneId, "PlayPause")
                            }
                            LingmoUI.IconButton { iconName: "media-skip-forward"; tipText: qsTr("Next"); onClicked: share.mediaAction(page.phoneId, "Next") }
                            Slider {
                                Layout.preferredWidth: 120
                                from: 0; to: 100
                                value: page.media.volume || 0
                                onMoved: share.setMediaVolume(page.phoneId, value)
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------ notifications
            ListView {
                id: notesList
                clip: true
                spacing: LingmoUI.Units.smallSpacing
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                LingmoUI.WheelHandler { target: notesList }
                model: page.notifications

                delegate: LingmoUI.Card {
                    id: note
                    required property var modelData
                    width: ListView.view.width
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: note.padding
                        anchors.rightMargin: note.padding
                        anchors.topMargin: LingmoUI.Units.largeSpacing
                        anchors.bottomMargin: LingmoUI.Units.largeSpacing
                        spacing: LingmoUI.Units.smallSpacing
                        RowLayout {
                            Label {
                                text: note.modelData.appName
                                font.pointSize: LingmoUI.Units.fontCaption
                                color: LingmoUI.Theme.secondaryTextColor
                            }
                            Item { Layout.fillWidth: true }
                            LingmoUI.IconButton {
                                visible: note.modelData.dismissable === true
                                iconName: "window-close"
                                size: 22; iconSize: 12
                                tipText: qsTr("Dismiss on the phone")
                                onClicked: share.dismissNotification(page.phoneId, note.modelData.publicId)
                            }
                        }
                        Label {
                            color: LingmoUI.Theme.textColor
                            Layout.fillWidth: true
                            text: note.modelData.title
                            font.pointSize: LingmoUI.Units.fontBody
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                            textFormat: Text.PlainText
                        }
                        Label {
                            color: LingmoUI.Theme.textColor
                            Layout.fillWidth: true
                            text: note.modelData.text
                            wrapMode: Text.WordWrap
                            textFormat: Text.PlainText
                            font.pointSize: LingmoUI.Units.fontBody
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: note.modelData.canReply === true
                            TextField {
                                id: replyField
                                Layout.fillWidth: true
                                placeholderText: qsTr("Reply…")
                                onAccepted: replyButton.clicked()
                            }
                            LingmoUI.ActionButton {
                                id: replyButton
                                text: qsTr("Send")
                                primary: true
                                enabled: replyField.text.length > 0
                                onClicked: {
                                    share.replyNotification(page.phoneId, note.modelData.publicId, replyField.text)
                                    replyField.text = ""
                                }
                            }
                        }
                    }
                }

                LingmoUI.EmptyState {
                    anchors.centerIn: parent
                    width: parent.width * 0.8
                    visible: notesList.count === 0
                    iconName: "notifications"
                    title: qsTr("No notifications")
                    text: qsTr("New notifications from the phone also show up as Lingmo notifications, where you can reply.")
                }
            }

            // ------------------------------------------------ messages
            SmsView {
                phoneId: page.phoneId
            }
        }
    }
}
