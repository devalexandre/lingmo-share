import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// SMS through KDE Connect: conversations on the left, the open one on the right
RowLayout {
    id: view
    property string phoneId
    property bool composing: false
    readonly property var sms: share.sms
    spacing: LingmoUI.Units.largeSpacing

    // Open the newest conversation when the tab shows up
    onVisibleChanged: if (visible && !composing && sms.thread < 0 && sms.threads.length > 0) sms.openThread(sms.threads[0].thread)

    function who(m) { return (m.addresses || []).join(", ") }
    function time(ms) { return ms > 0 ? Qt.formatDateTime(new Date(ms), "dd/MM hh:mm") : "" }

    ColumnLayout {
        Layout.preferredWidth: 260
        Layout.maximumWidth: 260
        Layout.fillWidth: false
        Layout.fillHeight: true
        spacing: LingmoUI.Units.smallSpacing

        LingmoUI.ActionButton {
            Layout.fillWidth: true
            text: qsTr("New message")
            iconName: "document-new"
            onClicked: { view.composing = true; view.sms.openThread(-1) }
        }

        ListView {
            id: threads
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            LingmoUI.WheelHandler { target: threads }
            model: view.sms.threads
            delegate: LingmoUI.SideBarItem {
                required property var modelData
                width: ListView.view.width
                title: view.who(modelData)
                trailing: view.time(modelData.date)
                isCurrent: !view.composing && view.sms.thread === modelData.thread
                emphasized: !modelData.read
                onClicked: { view.composing = false; view.sms.openThread(modelData.thread) }
            }

            LingmoUI.EmptyState {
                anchors.centerIn: parent
                width: parent.width
                visible: threads.count === 0
                iconName: "mail-message"
                title: qsTr("No messages")
                text: qsTr("Allow SMS in KDE Connect on the phone.")
                iconSize: 48
            }
        }
    }

    LingmoUI.Card {
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: LingmoUI.Units.largeSpacing
            spacing: LingmoUI.Units.smallSpacing

            TextField {
                id: numberField
                Layout.fillWidth: true
                visible: view.composing
                placeholderText: qsTr("Phone number")
                inputMethodHints: Qt.ImhDialableCharactersOnly
            }

            ListView {
                id: messages
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: LingmoUI.Units.smallSpacing
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                LingmoUI.WheelHandler { target: messages }
                model: view.composing ? [] : view.sms.messages
                onCountChanged: positionViewAtEnd()
                delegate: Item {
                    required property var modelData
                    width: ListView.view.width
                    height: bubble.height
                    Rectangle {
                        id: bubble
                        anchors.right: modelData.sent ? parent.right : undefined
                        anchors.left: modelData.sent ? undefined : parent.left
                        width: Math.min(parent.width * 0.75, body.implicitWidth + LingmoUI.Units.largeSpacing * 2)
                        height: body.implicitHeight + stamp.implicitHeight + LingmoUI.Units.smallSpacing * 3
                        radius: LingmoUI.Theme.mediumRadius
                        color: modelData.sent ? LingmoUI.Theme.highlightColor : LingmoUI.Theme.alternateBackgroundColor
                        Label {
                            id: body
                            x: LingmoUI.Units.largeSpacing
                            y: LingmoUI.Units.smallSpacing
                            width: Math.min(implicitWidth, view.width * 0.5)
                            text: modelData.body
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: modelData.sent ? LingmoUI.Theme.highlightedTextColor : LingmoUI.Theme.textColor
                        }
                        Label {
                            id: stamp
                            anchors.top: body.bottom
                            anchors.right: parent.right
                            anchors.rightMargin: LingmoUI.Units.largeSpacing
                            text: view.time(modelData.date)
                            font.pointSize: LingmoUI.Units.fontCaption
                            color: modelData.sent ? LingmoUI.Theme.highlightedTextColor : LingmoUI.Theme.secondaryTextColor
                        }
                    }
                }

                LingmoUI.Spinner {
                    anchors.centerIn: parent
                    running: view.sms.loading && messages.count === 0
                }
                Label {
                    anchors.centerIn: parent
                    visible: !view.composing && view.sms.thread < 0
                    text: qsTr("Choose a conversation")
                    color: LingmoUI.Theme.secondaryTextColor
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: view.composing || view.sms.thread >= 0
                TextField {
                    id: composer
                    Layout.fillWidth: true
                    placeholderText: qsTr("Text message")
                    onAccepted: sendButton.clicked()
                }
                LingmoUI.ActionButton {
                    id: sendButton
                    text: qsTr("Send")
                    primary: true
                    enabled: composer.text.length > 0 && (!view.composing || numberField.text.length > 0)
                    onClicked: {
                        if (view.composing)
                            view.sms.sendNew(numberField.text, composer.text)
                        else
                            view.sms.reply(composer.text)
                        composer.text = ""
                        view.composing = false
                    }
                }
            }
        }
    }
}
